#include "main.h"
#include "user_experience/config/config.h"
#include "constants.h"
#include "core/state/state_helpers.h"
#include "core/rendering/rendering_manager.h"
#include "lyrics/lyrics_manager.h"
#include "monitor/file_monitor.h"
#include "events/wayland_events.h"
#include "utils/wayland/wayland_init.h"
#include "utils/wayland/wayland_manager.h"
#include "utils/curl/curl_utils.h"
#include "utils/lang_detect/lang_detect.h"
#include "utils/lock/lock_file.h"
#include "translator/deepl/deepl_translator.h"
#include "translator/gemini/gemini_translator.h"
#include "translator/claude/claude_translator.h"
#include "translator/openai/openai_translator.h"
#include <ctype.h>
#include <strings.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

// Global state for signal handler
static struct lyrics_state *g_state = NULL;

// Signal handler for cleanup
static void signal_handler(int signum) {
    log_info("Received signal %d, cleaning up...", signum);

    if (g_state) {
        g_state->run = false;
    }
}

int main(int argc, char *argv[]) {
    int ret = 0;

    // Extract program name for help messages
    char *argv0_copy = strdup(argv[0]);
    if (!argv0_copy) {
        log_error("Memory allocation failed");
        return 1;
    }
    const char *program_name = basename(argv0_copy);

    // Quick check for --help before doing any initialization
    // This prevents config loading messages from appearing with help text
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            // Try to fetch detailed help from GitHub
            CURL *curl = curl_easy_init();
            if (curl) {
                struct curl_memory_buffer buffer;
                curl_memory_buffer_init(&buffer);

                const char *help_url = "https://raw.githubusercontent.com/unstable-code/lyrics/refs/heads/master/docs/help.txt";
                if (curl_easy_setopt(curl, CURLOPT_URL, help_url) != CURLE_OK ||
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) != CURLE_OK ||
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buffer) != CURLE_OK ||
                    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK ||
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L) != CURLE_OK ||  // 5 second timeout
                    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING) != CURLE_OK) {
                    // Failed to set CURL options, skip to fallback help
                    curl_memory_buffer_free(&buffer);
                    curl_easy_cleanup(curl);
                    curl = NULL;
                }

                if (curl) {
                    CURLcode res = curl_easy_perform(curl);

                    // Check HTTP response code
                    long http_code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                    curl_easy_cleanup(curl);

                    // Only use fetched content if successful (2xx status code)
                    if (res == CURLE_OK && http_code >= 200 && http_code < 300 && buffer.size > 0) {
                    // Replace %s with program name
                    char *pos = buffer.data;
                    char *end = buffer.data + buffer.size;
                    while (pos < end) {
                        char *placeholder = strstr(pos, "%s");
                        if (placeholder && placeholder < end) {
                            fwrite(pos, 1, placeholder - pos, stdout);
                            fprintf(stdout, "%s", program_name);
                            pos = placeholder + 2;
                        } else {
                            fwrite(pos, 1, end - pos, stdout);
                            break;
                        }
                    }
                    curl_memory_buffer_free(&buffer);
                    free(argv0_copy);
                    return 0;
                    }
                    curl_memory_buffer_free(&buffer);
                }
            }

            // Fallback to basic help if fetch failed
            fprintf(stdout, "Usage: %s [OPTIONS]\n\n", program_name);
            fprintf(stdout, "Wayland lyrics overlay with MPRIS integration\n\n");
            fprintf(stdout, "Options:\n");
            fprintf(stdout, "  -h, --help                   Show this help message\n");
            fprintf(stdout, "  -b, --background=COLOR       Background color (default: #00000080)\n");
            fprintf(stdout, "  -f, --foreground=COLOR       Text color (default: #FFFFFFFF)\n");
            fprintf(stdout, "  -F, --font=FONT              Font specification (default: \"Sans 20\")\n");
            fprintf(stdout, "  -a, --anchor=POSITION        Anchor: top, bottom, left, right (default: bottom)\n");
            fprintf(stdout, "  -m, --margin=PIXELS          Margin from edge (default: 32)\n\n");
            fprintf(stdout, "For full documentation, see:\n");
            fprintf(stdout, "  https://github.com/Scruel/lyrics/blob/master/README.md\n");

            free(argv0_copy);
            return 0;
        }
    }

    // Initialize configuration with defaults and load from file
    config_init_defaults(&g_config);
    char *config_loaded_path = config_load_with_fallback(&g_config);

    // Validate user config against settings.ini.example
    config_validate_user_config();

    // Parse anchor from config
    unsigned int anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    if (g_config.display.anchor) {
        if (strcmp(g_config.display.anchor, "top") == 0) {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
        } else if (strcmp(g_config.display.anchor, "left") == 0) {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
        } else if (strcmp(g_config.display.anchor, "right") == 0) {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
        } else if (strcmp(g_config.display.anchor, "bottom") == 0) {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
        }
    }

    int margin = g_config.display.margin;
    struct lyrics_state state = { 0 };
    state.current_line_index = -1; // No current line initially
    state.timing_offset_ms = 0; // No timing offset initially
    state.fifo_fd = -1; // No FIFO initially
    state.overlay_enabled = true; // Overlay enabled by default

    // Set global state for signal handler
    g_state = &state;

    // Register signal handlers for graceful cleanup
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_warn("Failed to register SIGINT handler: %s", strerror(errno));
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        log_warn("Failed to register SIGTERM handler: %s", strerror(errno));
    }
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        log_warn("Failed to register SIGHUP handler: %s", strerror(errno));
    }

    // Store config file path and checksum for hot reload
    state.config_file_path = config_loaded_path;  // Transfer ownership
    state.config_md5_checksum[0] = '\0';
    if (config_loaded_path && !calculate_file_md5(config_loaded_path, state.config_md5_checksum)) {
        log_warn("Failed to calculate MD5 for config file: %s", config_loaded_path);
    }

    // Convert hex colors to uint32 format
    state.background =
        ((uint32_t)(g_config.display.color_background[0] * 255) << 24) |
        ((uint32_t)(g_config.display.color_background[1] * 255) << 16) |
        ((uint32_t)(g_config.display.color_background[2] * 255) << 8) |
        ((uint32_t)(g_config.display.color_background[3] * 255));

    state.foreground =
        ((uint32_t)(g_config.display.color_active[0] * 255) << 24) |
        ((uint32_t)(g_config.display.color_active[1] * 255) << 16) |
        ((uint32_t)(g_config.display.color_active[2] * 255) << 8) |
        ((uint32_t)(g_config.display.color_active[3] * 255));

    // Build font string from config
    char font_str[FONT_STRING_SIZE];
    int written = snprintf(font_str, sizeof(font_str), "%s %s %d",
        g_config.display.font_family,
        g_config.display.font_weight,
        g_config.display.font_size);

    // Check for truncation (snprintf returns number of chars that would be written)
    if (written < 0 || written >= (int)sizeof(font_str)) {
        log_warn("Font string truncated (needed %d bytes, have %zu)",
                 written, sizeof(font_str));
    }

    char *font_from_config_alloc = strdup(font_str);
    state.font = font_from_config_alloc;  // Track allocated font to free later

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"background", required_argument, 0, 'b'},
        {"foreground", required_argument, 0, 'f'},
        {"font", required_argument, 0, 'F'},
        {"anchor", required_argument, 0, 'a'},
        {"margin", required_argument, 0, 'm'},
        {0, 0, 0, 0}
    };

    int c;
    int option_index = 0;
    while ((c = getopt_long(argc, argv, "hb:f:F:a:m:", long_options, &option_index)) != -1) {
        switch (c) {
        case 'b':
            state.background = state_helpers_parse_color(optarg);
            break;
        case 'f':
            state.foreground = state_helpers_parse_color(optarg);
            break;
        case 'F':
            // Free config font if overridden by command line
            if (font_from_config_alloc) {
                free(font_from_config_alloc);
                font_from_config_alloc = NULL;
            }
            state.font = optarg;
            break;
        case 'a':
            anchor = 0;
            if (strcmp(optarg, "top") == 0) {
                anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
            } else if (strcmp(optarg, "left") == 0) {
                anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
            } else if (strcmp(optarg, "right") == 0) {
                anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            } else if (strcmp(optarg, "bottom") == 0) {
                anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
            }
            break;
        case 'm':
            margin = atoi(optarg);
            break;
        default:
            // Error case - show brief error message
            fprintf(stderr, "Error: Invalid option\n");
            fprintf(stderr, "Usage: %s [OPTIONS]\n", program_name);
            fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
            free(argv0_copy);
            return 1;
        }
    }

    // Initialize translation providers
    deepl_translator_init();
    gemini_translator_init();
    claude_translator_init();
    openai_translator_init();

    // Initialize language detection for translation validation
    lang_detect_init();

    // Try to acquire lock file (prevent multiple instances)
    if (!lock_file_acquire()) {
        log_error("Another instance of wshowlyrics is already running");
        log_error("If you're sure no other instance is running, remove: %s", LOCK_FILE_PATH);
        ret = 1;
        goto exit_no_lock;
    }

    // Create FIFO for timing offset control (avoid TOCTOU by not unlinking first)
    #define FIFO_PATH "/tmp/wshowlyrics.fifo"
    mode_t old_mask = umask(0077);  // Owner-only access for privacy
    if (mkfifo(FIFO_PATH, 0600) == -1) {
        if (errno != EEXIST) {
            log_warn("Failed to create FIFO %s: %s", FIFO_PATH, strerror(errno));
        }
        // FIFO already exists, that's ok (we have exclusive lock)
    }
    umask(old_mask);

    // Open FIFO in non-blocking mode
    state.fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (state.fifo_fd < 0) {
        log_warn("Failed to open FIFO %s: %s", FIFO_PATH, strerror(errno));
    } else {
        log_info("FIFO ready at %s for timing offset control", FIFO_PATH);
    }

    // Initialize Wayland surface and connections
    if (!wayland_init_surface(&state, anchor, margin)) {
        ret = 1;
        goto exit;
    }

    struct pollfd pollfds[] = {
        { .fd = wl_display_get_fd(state.display), .events = POLLIN, },
        { .fd = state.fifo_fd, .events = POLLIN, },
    };
    int nfds = (state.fifo_fd >= 0) ? 2 : 1;

    state.run = true;
    int update_counter = 0;

    // Wayland connection manager
    struct wayland_connection wl_conn = {
        .display = state.display,
        .registry = state.registry,
        .compositor = state.compositor,
        .shm = state.shm,
        .layer_shell = state.layer_shell,
        .surface = state.surface,
        .layer_surface = state.layer_surface,
        .configured = false,
        .connected = true
    };

    // Link Wayland connection manager to state
    state.wl_conn = &wl_conn;

    while (state.run) {
        // Check if reconnection is needed (e.g., layer surface was closed)
        if (state.needs_reconnect) {
            log_info("Reconnection needed, attempting full reconnection...");
            if (wayland_events_handle_reconnection(&state, &wl_conn, pollfds)) {
                state.needs_reconnect = false;
            }
            continue;
        }

        // Flush Wayland display
        if (!wayland_manager_flush(&wl_conn)) {
            // Connection lost, attempt full reconnection
            wayland_events_handle_reconnection(&state, &wl_conn, pollfds);
            continue;
        }

        int timeout = POLL_TIMEOUT_MS;

        int poll_ret = poll(pollfds, nfds, timeout);
        if (poll_ret < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, continue
                continue;
            }
            log_error("Poll error: %s (errno=%d)", strerror(errno), errno);
            break;
        }

        // Check for errors or hangup on the Wayland fd
        if (pollfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            log_warn("Wayland connection error detected (revents=0x%x)", pollfds[0].revents);
            if (pollfds[0].revents & POLLHUP) {
                log_warn("Wayland compositor disconnected (possibly due to screen lock or tty switch)");
            }
            // Attempt full reconnection
            wayland_events_handle_reconnection(&state, &wl_conn, pollfds);
            continue;
        }

        // Read timing offset commands from FIFO
        if (state.fifo_fd >= 0 && (pollfds[1].revents & POLLIN)) {
            char buf[64];
            ssize_t n = read(state.fifo_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';

                // Remove trailing newline/whitespace
                while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' ')) {
                    buf[--n] = '\0';
                }

                if (n > 0) {
                    // Check for overlay control commands first
                    if (strcmp(buf, "show") == 0) {
                        state.overlay_enabled = true;
                        system_tray_set_overlay_state(true);
                        rendering_manager_set_dirty(&state);
                        log_info("Overlay enabled");
                    } else if (strcmp(buf, "hide") == 0) {
                        state.overlay_enabled = false;
                        system_tray_set_overlay_state(false);
                        rendering_manager_set_dirty(&state);
                        log_info("Overlay disabled");
                    } else if (strcmp(buf, "toggle") == 0) {
                        state.overlay_enabled = !state.overlay_enabled;
                        system_tray_set_overlay_state(state.overlay_enabled);
                        rendering_manager_set_dirty(&state);
                        log_info("Overlay toggled: %s", state.overlay_enabled ? "enabled" : "disabled");
                    } else {
                        // Parse as timing offset (existing logic)
                        int delta = atoi(buf);

                        if (buf[0] == '+' || buf[0] == '-') {
                            // Cumulative mode: +100, -100
                            state.timing_offset_ms += delta;
                        } else {
                            // Absolute mode: 0, 500
                            state.timing_offset_ms = delta;
                        }

                        // Clamp to reasonable range
                        if (state.timing_offset_ms < -5000) {
                            state.timing_offset_ms = -5000;
                        } else if (state.timing_offset_ms > 5000) {
                            state.timing_offset_ms = 5000;
                        }

                        log_info("Timing offset: %dms", state.timing_offset_ms);
                        rendering_manager_set_dirty(&state);
                    }
                }
            }
        }

        // Check for track changes periodically
        if (update_counter++ % TRACK_UPDATE_CHECK_INTERVAL == 0) {
            if (lyrics_manager_update_track_info(&state)) {
                // Track changed, load new lyrics
                lyrics_manager_load_lyrics(&state);
                rendering_manager_set_dirty(&state);
            } else {
                // Check if lyrics or config files have changed (every 2 seconds)
                file_monitor_check_and_reload(
                    state.lyrics.source_file_path,
                    state.lyrics.md5_checksum,
                    sizeof(state.lyrics.md5_checksum),
                    "Lyrics",
                    file_monitor_reload_lyrics,
                    &state
                );

                file_monitor_check_and_reload(
                    state.config_file_path,
                    state.config_md5_checksum,
                    sizeof(state.config_md5_checksum),
                    "Config",
                    file_monitor_reload_config,
                    &state
                );
            }
        }

        // Update current line based on playback position
        if (!state.overlay_enabled) {
            // Overlay disabled - render transparent (same as paused)
            if (state.current_line != NULL) {
                state.current_line = NULL;
                state.prev_line = NULL;
                state.next_line = NULL;
                rendering_manager_set_dirty(&state);
            }
        } else if (mpris_is_playing()) {
            lyrics_manager_update_current_line(&state);
            // Continuously update for smooth karaoke highlighting (LRCX only)
            // Frame callback limits this to vsync (60/120 fps), not unlimited
            if (lyrics_manager_is_format(&state, ".lrcx")) {
                rendering_manager_set_dirty(&state);
            }
        } else {
            // Clear lyrics when not playing (paused or stopped)
            if (state.current_line != NULL) {
                state.current_line = NULL;
                state.prev_line = NULL;
                state.next_line = NULL;
                rendering_manager_set_dirty(&state);
                log_info("Playback stopped/paused - clearing lyrics");
            }
        }

        if ((pollfds[0].revents & POLLIN) && !wayland_manager_dispatch(&wl_conn)) {
            // Connection lost, attempt full reconnection
            wayland_events_handle_reconnection(&state, &wl_conn, pollfds);
            continue;
        }

        // During instrumental break (idle time), handle lyrics file status
        // Only do one action per instrumental break to avoid doing both in same session
        if (state.in_instrumental_break && state.lyrics.source_file_path) {
            struct stat st;
            bool file_exists = (stat(state.lyrics.source_file_path, &st) == 0);

            if (!state.need_lyrics_search && !file_exists) {
                // First detection: file was deleted or moved
                log_info("Lyrics file was deleted or moved: %s", state.lyrics.source_file_path);
                log_info("Will search for lyrics during next instrumental break");
                state.need_lyrics_search = true;
                state.in_instrumental_break = false; // Reset to trigger search on next break
            } else if (state.need_lyrics_search) {
                if (file_exists) {
                    // File is back! No need to search
                    log_info("Lyrics file is back at original location: %s", state.lyrics.source_file_path);
                } else {
                    // File still missing - search for lyrics again
                    log_info("Searching for lyrics again...");
                    // Free old lyrics and search directly into state.lyrics to avoid
                    // dangling pointer issues with async translation thread
                    lrc_free_data(&state.lyrics);
                    memset(&state.lyrics, 0, sizeof(state.lyrics));
                    if (lyrics_find_for_track(&state.current_track, &state.lyrics)) {
                        log_info("Found new lyrics, replacing old ones");
                        state.current_line = NULL;
                        state.prev_line = NULL;
                        state.next_line = NULL;
                        rendering_manager_set_dirty(&state);
                    } else {
                        log_info("No lyrics found, keeping existing lyrics displayed");
                    }
                }
                // Clear flags after handling
                state.need_lyrics_search = false;
                state.in_instrumental_break = false;
            }
        }

        // Update system tray (process GTK events)
        system_tray_update();
    }

exit:
    // Clean up frame callback
    if (state.frame_callback) {
        wl_callback_destroy(state.frame_callback);
        state.frame_callback = NULL;
    }

    // Clean up FIFO
    if (state.fifo_fd >= 0) {
        close(state.fifo_fd);
        unlink(FIFO_PATH);
    }

    // Release lock file
    lock_file_release();

    system_tray_cleanup();
    lrc_free_data(&state.lyrics);
    mpris_free_metadata(&state.current_track);
    mpris_cleanup();
    lyrics_providers_cleanup();
    deepl_translator_cleanup();
    gemini_translator_cleanup();
    claude_translator_cleanup();
    openai_translator_cleanup();
    lang_detect_cleanup();

    if (state.display) {
        wl_display_disconnect(state.display);
    }
    FcFini();

exit_no_lock:
    // Free allocated font string if it was from config
    free(font_from_config_alloc);

    // Free config file path
    free(state.config_file_path);

    // Free configuration
    config_free(&g_config);

    free(argv0_copy);
    return ret;
}
