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
#include "utils/dbus_control/dbus_control.h"
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

// Handle --purge option
static int handle_purge_option(const char *arg) {
    const char *type = "all";
    if (strncmp(arg, "--purge=", 8) == 0) {
        type = arg + 8;
    }

    fprintf(stdout, "Purging cache type: %s\n", type);
    if (purge_cache(type)) {
        fprintf(stdout, "Cache purged successfully\n");
        return 0;
    } else {
        fprintf(stderr, "Failed to purge cache\n");
        return 1;
    }
}

// Fetch and display detailed help from GitHub
static bool display_detailed_help(const char *program_name) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    struct curl_memory_buffer buffer;
    curl_memory_buffer_init(&buffer);

    const char *help_url = "https://raw.githubusercontent.com/unstable-code/lyrics/refs/heads/master/docs/help.txt";

    if (curl_easy_setopt(curl, CURLOPT_URL, help_url) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buffer) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING) != CURLE_OK) {
        curl_memory_buffer_free(&buffer);
        curl_easy_cleanup(curl);
        return false;
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code < 200 || http_code >= 300 || buffer.size == 0) {
        curl_memory_buffer_free(&buffer);
        return false;
    }

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
    return true;
}

// Handle --help option
static int handle_help_option(const char *program_name) {
    if (display_detailed_help(program_name)) {
        return 0;
    }

    // Fallback to basic help if fetch failed
    fprintf(stdout, "Wayland lyrics overlay with MPRIS integration\n\n");
    fprintf(stdout, "For detailed help and usage information, see:\n");
    fprintf(stdout, "  https://raw.githubusercontent.com/unstable-code/lyrics/refs/heads/master/docs/help.txt\n\n");
    fprintf(stdout, "Documentation:\n");
    fprintf(stdout, "  https://github.com/unstable-code/lyrics/blob/master/README.md\n");
    return 0;
}

// Parse anchor string to Wayland anchor flags
static uint32_t parse_anchor_string(const char *anchor_str) {
    if (!anchor_str) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    }

    if (strcmp(anchor_str, "top") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    } else if (strcmp(anchor_str, "left") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    } else if (strcmp(anchor_str, "right") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    } else if (strcmp(anchor_str, "bottom") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    }

    return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
}

// Register signal handlers for graceful shutdown
static void setup_signal_handlers(struct lyrics_state *state) {
    g_state = state;

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
}

// Initialize state colors from config
static void initialize_state_colors(struct lyrics_state *state) {
    state->background =
        ((uint32_t)(g_config.display.color_background[0] * 255) << 24) |
        ((uint32_t)(g_config.display.color_background[1] * 255) << 16) |
        ((uint32_t)(g_config.display.color_background[2] * 255) << 8) |
        ((uint32_t)(g_config.display.color_background[3] * 255));

    state->foreground =
        ((uint32_t)(g_config.display.color_active[0] * 255) << 24) |
        ((uint32_t)(g_config.display.color_active[1] * 255) << 16) |
        ((uint32_t)(g_config.display.color_active[2] * 255) << 8) |
        ((uint32_t)(g_config.display.color_active[3] * 255));
}

// Build font string from config
static char *build_font_string(void) {
    char font_str[FONT_STRING_SIZE];
    int written = snprintf(font_str, sizeof(font_str), "%s %s %d",
        g_config.display.font_family,
        g_config.display.font_weight,
        g_config.display.font_size);

    if (written < 0 || written >= (int)sizeof(font_str)) {
        log_warn("Font string truncated (needed %d bytes, have %zu)",
                 written, sizeof(font_str));
    }

    return strdup(font_str);
}

// Initialize subsystems (translation, language detection, D-Bus)
static bool initialize_subsystems(struct lyrics_state *state) {
    deepl_translator_init();
    gemini_translator_init();
    claude_translator_init();
    openai_translator_init();
    lang_detect_init();

    int cleanup_days = config_get_cleanup_days(g_config.cache.cleanup_policy);
    auto_cleanup_old_cache(cleanup_days);

    if (!dbus_control_init(state)) {
        log_warn("Failed to initialize D-Bus control interface (overlay/offset control disabled)");
    }

    return true;
}

// Parse command-line options
static int parse_command_line_options(int argc, char *argv[], struct lyrics_state *state,
                                      uint32_t *anchor, int *margin, char **font_from_config,
                                      const char *program_name) {
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
            state->background = state_helpers_parse_color(optarg);
            break;
        case 'f':
            state->foreground = state_helpers_parse_color(optarg);
            break;
        case 'F':
            free(*font_from_config);
            *font_from_config = NULL;
            state->font = optarg;
            break;
        case 'a':
            *anchor = parse_anchor_string(optarg);
            break;
        case 'm':
            *margin = atoi(optarg);
            break;
        default:
            fprintf(stderr, "Error: Invalid option\n");
            fprintf(stderr, "Usage: %s [OPTIONS]\n", program_name);
            fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
            return 1;
        }
    }
    return 0;
}

// Monitor and update system state in main loop
static void monitor_track_and_files(struct lyrics_state *state, int *update_counter) {
    // Check for track changes
    if (mpris_check_metadata_changed()) {
        if (lyrics_manager_update_track_info(state)) {
            lyrics_manager_load_lyrics(state);
            mpris_apply_position_fix_if_needed();
            rendering_manager_set_dirty(state);
        }
    }

    // Check if lyrics or config files have changed (every 2 seconds)
    if ((*update_counter)++ % TRACK_UPDATE_CHECK_INTERVAL == 0) {
        if (state->lyrics.source_file_path && strncmp(state->lyrics.source_file_path, "/tmp/", 5) != 0) {
            file_monitor_check_and_reload(
                state->lyrics.source_file_path,
                state->lyrics.md5_checksum,
                sizeof(state->lyrics.md5_checksum),
                "Lyrics",
                file_monitor_reload_lyrics,
                state
            );
        }

        file_monitor_check_and_reload(
            state->config_file_path,
            state->config_md5_checksum,
            sizeof(state->config_md5_checksum),
            "Config",
            file_monitor_reload_config,
            state
        );
    }
}

// Update current line based on playback state
static void update_playback_state(struct lyrics_state *state) {
    if (!state->overlay_enabled) {
        if (state->current_line != NULL) {
            state->current_line = NULL;
            state->prev_line = NULL;
            state->next_line = NULL;
            rendering_manager_set_dirty(state);
        }
    } else if (mpris_is_playing()) {
        lyrics_manager_update_current_line(state);
        if (lyrics_manager_is_format(state, ".lrcx")) {
            rendering_manager_set_dirty(state);
        }
    } else {
        if (state->current_line != NULL) {
            state->current_line = NULL;
            state->prev_line = NULL;
            state->next_line = NULL;
            rendering_manager_set_dirty(state);
            log_info("Playback stopped/paused - clearing lyrics");
        }
    }
}

// Handle lyrics file status during instrumental breaks
static void handle_instrumental_break(struct lyrics_state *state) {
    if (!state->in_instrumental_break || !state->lyrics.source_file_path) {
        return;
    }

    struct stat st;
    bool file_exists = (stat(state->lyrics.source_file_path, &st) == 0);

    if (!state->need_lyrics_search && !file_exists) {
        log_info("Lyrics file was deleted or moved: %s", state->lyrics.source_file_path);
        log_info("Will search for lyrics during next instrumental break");
        state->need_lyrics_search = true;
        state->in_instrumental_break = false;
    } else if (state->need_lyrics_search) {
        if (file_exists) {
            log_info("Lyrics file is back at original location: %s", state->lyrics.source_file_path);
        } else {
            log_info("Searching for lyrics again...");
            lrc_free_data(&state->lyrics);
            memset(&state->lyrics, 0, sizeof(state->lyrics));
            if (lyrics_find_for_track(&state->current_track, &state->lyrics)) {
                log_info("Found new lyrics, replacing old ones");
                state->current_line = NULL;
                state->prev_line = NULL;
                state->next_line = NULL;
                rendering_manager_set_dirty(state);
            } else {
                log_info("No lyrics found, keeping existing lyrics displayed");
            }
        }
        state->need_lyrics_search = false;
        state->in_instrumental_break = false;
    }
}

// Main event loop
static void run_main_event_loop(struct lyrics_state *state, struct wayland_connection *wl_conn) {
    struct pollfd pollfds[] = {
        { .fd = wl_display_get_fd(state->display), .events = POLLIN, },
    };
    int nfds = 1;
    int update_counter = 0;

    while (state->run) {
        if (state->needs_reconnect) {
            log_info("Reconnection needed, attempting full reconnection...");
            if (wayland_events_handle_reconnection(state, wl_conn, pollfds)) {
                state->needs_reconnect = false;
            }
            continue;
        }

        if (!wayland_manager_flush(wl_conn)) {
            wayland_events_handle_reconnection(state, wl_conn, pollfds);
            continue;
        }

        int poll_ret = poll(pollfds, nfds, POLL_TIMEOUT_MS);
        if (poll_ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error("Poll error: %s (errno=%d)", strerror(errno), errno);
            break;
        }

        if (pollfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            log_warn("Wayland connection error detected (revents=0x%x)", pollfds[0].revents);
            if (pollfds[0].revents & POLLHUP) {
                log_warn("Wayland compositor disconnected (possibly due to screen lock or tty switch)");
            }
            wayland_events_handle_reconnection(state, wl_conn, pollfds);
            continue;
        }

        monitor_track_and_files(state, &update_counter);
        update_playback_state(state);

        if ((pollfds[0].revents & POLLIN) && !wayland_manager_dispatch(wl_conn)) {
            wayland_events_handle_reconnection(state, wl_conn, pollfds);
            continue;
        }

        handle_instrumental_break(state);
        system_tray_update();
    }
}

// Cleanup all resources
static void cleanup_resources(struct lyrics_state *state, char *font_from_config) {
    if (state->frame_callback) {
        wl_callback_destroy(state->frame_callback);
        state->frame_callback = NULL;
    }

    dbus_control_cleanup();
    system_tray_cleanup();
    lrc_free_data(&state->lyrics);
    mpris_free_metadata(&state->current_track);
    mpris_cleanup();
    lyrics_providers_cleanup();
    deepl_translator_cleanup();
    gemini_translator_cleanup();
    claude_translator_cleanup();
    openai_translator_cleanup();
    lang_detect_cleanup();

    if (state->display) {
        wl_display_disconnect(state->display);
    }
    FcFini();

    free(font_from_config);
    free(state->config_file_path);
    config_free(&g_config);
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

    // Quick check for --help and --purge before initialization
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--purge") == 0 || strncmp(argv[i], "--purge=", 8) == 0) {
            int purge_ret = handle_purge_option(argv[i]);
            free(argv0_copy);
            return purge_ret;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            int help_ret = handle_help_option(program_name);
            free(argv0_copy);
            return help_ret;
        }
    }

    // Initialize configuration with defaults and load from file
    config_init_defaults(&g_config);
    char *config_loaded_path = config_load_with_fallback(&g_config);
    config_validate_user_config();

    // Parse anchor from config
    uint32_t anchor = parse_anchor_string(g_config.display.anchor);
    int margin = g_config.display.margin;

    // Initialize state
    struct lyrics_state state = { 0 };
    state.current_line_index = -1;
    state.timing_offset_ms = 0;
    state.overlay_enabled = true;
    state.config_file_path = config_loaded_path;
    state.config_md5_checksum[0] = '\0';

    if (config_loaded_path && !calculate_file_md5(config_loaded_path, state.config_md5_checksum)) {
        log_warn("Failed to calculate MD5 for config file: %s", config_loaded_path);
    }

    // Setup signal handlers
    setup_signal_handlers(&state);

    // Initialize state colors
    initialize_state_colors(&state);

    // Build font string from config
    char *font_from_config_alloc = build_font_string();
    state.font = font_from_config_alloc;

    // Parse command-line options
    if (parse_command_line_options(argc, argv, &state, &anchor, &margin,
                                   &font_from_config_alloc, program_name) != 0) {
        free(argv0_copy);
        free(font_from_config_alloc);
        config_free(&g_config);
        return 1;
    }

    // Initialize subsystems
    initialize_subsystems(&state);

    // Initialize Wayland surface and connections
    if (!wayland_init_surface(&state, anchor, margin)) {
        ret = 1;
        goto exit;
    }

    state.run = true;

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

    state.wl_conn = &wl_conn;
    state.anchor = anchor;
    state.margin = margin;

    // Run main event loop
    run_main_event_loop(&state, &wl_conn);

exit:
    cleanup_resources(&state, font_from_config_alloc);
    free(argv0_copy);
    return ret;
}
