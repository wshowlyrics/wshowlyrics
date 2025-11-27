#include "main.h"
#include "user_experience/config/config.h"
#include "constants.h"
#include "utils/render/render_helpers.h"
#include "utils/wayland/wayland_manager.h"
#include "utils/curl/curl_utils.h"
#include <ctype.h>
#include <strings.h>
#include <curl/curl.h>

// Helper function to check if current lyrics file is a specific format
static bool is_lyrics_format(struct lyrics_state *state, const char *extension) {
    if (!state->lyrics.source_file_path) {
        return false;
    }
    const char *ext = strrchr(state->lyrics.source_file_path, '.');
    return ext && strcasecmp(ext, extension) == 0;
}

static cairo_subpixel_order_t to_cairo_subpixel_order(
        enum wl_output_subpixel subpixel) {
    switch (subpixel) {
    case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
        return CAIRO_SUBPIXEL_ORDER_RGB;
    case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
        return CAIRO_SUBPIXEL_ORDER_BGR;
    case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
        return CAIRO_SUBPIXEL_ORDER_VRGB;
    case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
        return CAIRO_SUBPIXEL_ORDER_VBGR;
    default:
        return CAIRO_SUBPIXEL_ORDER_DEFAULT;
    }
    return CAIRO_SUBPIXEL_ORDER_DEFAULT;
}

static void render_to_cairo(cairo_t *cairo, struct lyrics_state *state,
        int scale, uint32_t *width, uint32_t *height) {
    const char *text_to_display = " "; // Default to single space
    bool has_lyrics = (state->current_line && state->current_line->text);
    bool is_empty_line = false; // Track if current line is empty (instrumental break)
    bool is_karaoke = false; // Track if this is karaoke-style LRCX

    if (has_lyrics) {
        // Check if the text is empty or only whitespace
        const char *text = state->current_line->text;
        if (text[0] == '\0') {
            // Empty string - treat as idle/instrumental break
            is_empty_line = true;
            text_to_display = " "; // Display single space to keep surface visible
        } else {
            text_to_display = text;
            // Karaoke mode is only enabled for LRCX format
            is_karaoke = is_lyrics_format(state, ".lrcx");
        }
    }

    // Use transparent background when no lyrics or during instrumental breaks
    uint32_t background_color = (has_lyrics && !is_empty_line) ? state->background : 0x00000000;

    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_u32(cairo, background_color);
    cairo_paint(cairo);

    if (is_karaoke) {
        // Karaoke-style rendering with progressive word fill (wipe effect)
        int64_t position_us = mpris_get_position();
        int w, h;
        render_karaoke_segments(cairo, state->font, scale,
                               state->current_line->segments,
                               state->foreground, position_us, &w, &h);
        *width = w;
        *height = h;
    } else {
        // Normal rendering (non-karaoke)
        cairo_set_source_u32(cairo, state->foreground);

        // Check if this line has segments (for ruby text support)
        // Note: LRCX uses word_segment (with karaoke), LRC/SRT use ruby_segment (furigana only)
        bool has_word_segments = (has_lyrics && state->current_line->segments && state->current_line->segment_count > 0);
        bool has_ruby_segments = (has_lyrics && state->current_line->ruby_segments && state->current_line->segment_count > 0);

        if (has_ruby_segments && !has_word_segments) {
            // Render LRC/SRT with ruby_segment (furigana only, no karaoke)
            int w, h;
            render_ruby_segments(cairo, state->font, scale,
                                state->current_line->ruby_segments,
                                state->foreground, &w, &h);
            *width = w;
            *height = h;
        } else if (has_lyrics && has_word_segments) {
            // Render LRCX with word_segment but without karaoke fill effect
            // This shouldn't happen normally (LRCX uses karaoke mode above)
            // But we handle it for completeness
            int w, h;
            render_word_segments_static(cairo, state->font, scale,
                                       state->current_line->segments,
                                       state->foreground, &w, &h);
            *width = w;
            *height = h;
        } else {
            // No segments - simple text rendering
            int w, h;
            render_plain_text(cairo, state->font, scale,
                            text_to_display, state->foreground, &w, &h);
            *width = w;
            *height = h;
        }
    }
}

static void render_transparent_frame(struct lyrics_state *state) {
    // Skip rendering if Wayland connection is not available
    if (!state->wl_conn || !state->wl_conn->connected) {
        return;
    }

    const int scale = state->output ? state->output->scale : 1;
    state->current_buffer = get_next_buffer(state->shm,
            state->buffers, state->width * scale, state->height * scale);
    if (state->current_buffer) {
        cairo_t *shm = state->current_buffer->cairo;
        cairo_save(shm);
        cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
        cairo_paint(shm);
        cairo_restore(shm);

        wl_surface_set_buffer_scale(state->surface, scale);
        wl_surface_attach(state->surface, state->current_buffer->buffer, 0, 0);
        wl_surface_damage_buffer(state->surface, 0, 0, state->width, state->height);
        wl_surface_commit(state->surface);
    }
}

static void render_frame(struct lyrics_state *state) {
    // Skip rendering if Wayland connection is not available
    if (!state->wl_conn || !state->wl_conn->connected) {
        return;
    }

    cairo_surface_t *recorder = cairo_recording_surface_create(
            CAIRO_CONTENT_COLOR_ALPHA, NULL);
    cairo_t *cairo = cairo_create(recorder);
    cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
    if (state->output) {
        cairo_font_options_set_subpixel_order(
                fo, to_cairo_subpixel_order(state->output->subpixel));
    }
    cairo_set_font_options(cairo, fo);
    cairo_font_options_destroy(fo);
    cairo_save(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cairo);
    cairo_restore(cairo);

    const int scale = state->output ? state->output->scale : 1;
    uint32_t width = 0, height = 0;
    render_to_cairo(cairo, state, scale, &width, &height);

    if (height / scale != state->height
            || width / scale != state->width
            || state->width == 0) {
        // Size change detected - make overlay transparent during resize
        render_transparent_frame(state);

        // Reconfigure surface size
        if (width == 0 || height == 0) {
            // Keep a minimal 1x1 surface instead of detaching
            zwlr_layer_surface_v1_set_size(state->layer_surface, 1, 1);
        } else {
            zwlr_layer_surface_v1_set_size(
                    state->layer_surface, width / scale, height / scale);
        }

        wl_surface_commit(state->surface);
    } else if (height > 0) {
        // Replay recording into shm and send it off
        state->current_buffer = get_next_buffer(state->shm,
                state->buffers, state->width * scale, state->height * scale);
        if (!state->current_buffer) {
            cairo_surface_destroy(recorder);
            cairo_destroy(cairo);
            return;
        }
        cairo_t *shm = state->current_buffer->cairo;

        cairo_save(shm);
        cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
        cairo_paint(shm);
        cairo_restore(shm);

        cairo_set_source_surface(shm, recorder, 0.0, 0.0);
        cairo_paint(shm);

        wl_surface_set_buffer_scale(state->surface, scale);
        wl_surface_attach(state->surface,
                state->current_buffer->buffer, 0, 0);
        wl_surface_damage_buffer(state->surface, 0, 0,
                state->width, state->height);
        wl_surface_commit(state->surface);
    }

    cairo_surface_destroy(recorder);
    cairo_destroy(cairo);
}

static void set_dirty(struct lyrics_state *state) {
    // Skip if Wayland connection is not available
    if (!state->wl_conn || !state->wl_conn->connected) {
        return;
    }

    if (state->frame_scheduled) {
        state->dirty = true;
    } else if (state->surface) {
        render_frame(state);
    }
}

static void layer_surface_configure(void *data,
        struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1,
        uint32_t serial, uint32_t width, uint32_t height) {
    struct lyrics_state *state = data;
    state->width = width;
    state->height = height;
    zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);
    set_dirty(state);
}

static void layer_surface_closed(void *data,
        struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1) {
    (void)zwlr_layer_surface_v1;
    struct lyrics_state *state = data;

    // Ignore close events during reconnection (expected behavior)
    if (state->reconnecting) {
        return;
    }

    fprintf(stderr, LOG_WARN "  Layer surface closed by compositor\n");
    // Signal that we need to reconnect
    state->needs_reconnect = true;
    state->wl_conn->connected = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void surface_enter(void *data,
        struct wl_surface *wl_surface, struct wl_output *output) {
    struct lyrics_state *state = data;
    struct lyrics_output *lyrics_output = state->outputs;
    while (lyrics_output && lyrics_output->output != output) {
        lyrics_output = lyrics_output->next;
    }
    if (lyrics_output) {
        state->output = lyrics_output;
    }
}

static void surface_leave(void *data,
        struct wl_surface *wl_surface, struct wl_output *output) {
    // Not needed for this application
}

static const struct wl_surface_listener wl_surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
};

static void output_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
        int32_t subpixel, const char *make, const char *model,
        int32_t transform) {
    struct lyrics_output *output = data;
    output->subpixel = subpixel;
}

static void output_mode(void *data, struct wl_output *wl_output,
        uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    struct lyrics_output *output = data;
    output->width = width;
    output->height = height;
    log_info("Screen resolution: %dx%d", width, height);
}

static void output_done(void *data, struct wl_output *wl_output) {
    // Not needed
}

static void output_scale(void *data,
        struct wl_output *wl_output, int32_t factor) {
    struct lyrics_output *output = data;
    output->scale = factor;
}

static const struct wl_output_listener wl_output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

static void registry_global(void *data, struct wl_registry *wl_registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct lyrics_state *state = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(wl_registry,
                name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layer_shell = wl_registry_bind(wl_registry,
                name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct lyrics_output *output = calloc(1, sizeof(struct lyrics_output));
        output->output = wl_registry_bind(wl_registry,
                name, &wl_output_interface, 3);
        output->scale = 1;
        output->height = 0;
        output->width = 0;
        struct lyrics_output **link = &state->outputs;
        while (*link) {
            link = &(*link)->next;
        }
        *link = output;
        wl_output_add_listener(output->output, &wl_output_listener, output);

        // Set first output as default
        if (!state->output) {
            state->output = output;
            log_info("Set primary output");
        }
    }
}

static void registry_global_remove(void *data,
        struct wl_registry *wl_registry, uint32_t name) {
    /* This space deliberately left blank */
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// Helper function to handle full Wayland reconnection
// Returns true if reconnection successful and state updated, false otherwise
static bool handle_wayland_reconnection(struct lyrics_state *state,
        struct wayland_connection *wl_conn, struct pollfd *pollfd) {
    // Mark that we're reconnecting (to ignore layer_surface_closed events)
    state->reconnecting = true;

    // Clean up old buffers before reconnecting
    // (they were created with the old wl_shm)
    for (int i = 0; i < 2; i++) {
        if (state->buffers[i].buffer) {
            destroy_buffer(&state->buffers[i]);
        }
    }
    state->current_buffer = NULL;

    if (!wayland_manager_reconnect_full(wl_conn, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
            "lyrics", state->anchor, state->margin)) {
        fprintf(stderr, "Full reconnection failed, will retry...\n");
        state->reconnecting = false;
        return false;
    }

    // Update state pointers
    state->display = wl_conn->display;
    state->registry = wl_conn->registry;
    state->compositor = wl_conn->compositor;
    state->shm = wl_conn->shm;
    state->layer_shell = wl_conn->layer_shell;
    state->surface = wl_conn->surface;
    state->layer_surface = wl_conn->layer_surface;

    // Add listeners to new surfaces
    wl_surface_add_listener(state->surface, &wl_surface_listener, state);
    zwlr_layer_surface_v1_add_listener(state->layer_surface,
            &layer_surface_listener, state);

    // Commit the surface
    wl_surface_commit(state->surface);

    // Wait for configure event
    int retry = 0;
    state->width = state->height = 0;
    while ((state->width == 0 || state->height == 0) && retry < 10) {
        if (wl_display_roundtrip(state->display) == -1) {
            fprintf(stderr, LOG_WARN "  Roundtrip failed, compositor may not be available yet\n");
            state->reconnecting = false;
            return false;
        }
        retry++;
    }

    if (state->width == 0 || state->height == 0) {
        fprintf(stderr, LOG_WARN "  Layer surface configuration failed after reconnection (compositor not ready)\n");
        state->reconnecting = false;
        return false;
    }

    // Update pollfd with new display fd
    pollfd->fd = wl_display_get_fd(wl_conn->display);

    state->reconnecting = false;
    printf(LOG_SUCCESS "Successfully reconnected - overlay should be visible again" LOG_COLOR_RESET "\n");
    set_dirty(state);
    return true;
}

static uint32_t parse_color(const char *color) {
    if (color[0] == '#') {
        ++color;
    }

    const int len = strlen(color);
    if (len != 6 && len != 8) {
        fprintf(stderr, "Invalid color %s, defaulting to color "
                "0xFFFFFFFF\n", color);
        return 0xFFFFFFFF;
    }
    uint32_t res = (uint32_t)strtoul(color, NULL, 16);
    if (len == 6) {
        res = (res << 8) | 0xFF;
    }
    return res;
}

static bool update_track_info(struct lyrics_state *state) {

    struct track_metadata new_track = {0};
    if (!mpris_get_metadata(&new_track)) {
        // No player found - clear everything if we had a track before
        if (state->current_track.title) {
            printf("\n=== No player found, clearing lyrics ===\n");

            // Free track metadata
            mpris_free_metadata(&state->current_track);

            // Free lyrics
            lrc_free_data(&state->lyrics);
            state->current_line = NULL;

            // Reset tray icon to default
            system_tray_reset_icon();
            system_tray_update_tooltip("wshowlyrics");

            // Clear the display
            set_dirty(state);
        }
        return false;
    }

    // Check if track changed
    bool changed = false;
    if (!state->current_track.title ||
        strcmp(new_track.title, state->current_track.title) != 0) {
        changed = true;
    }

    if (changed) {
        printf("\n=== Track changed ===\n");
        printf("Title: %s\n", new_track.title);
        printf("Artist: %s\n", new_track.artist ? new_track.artist : "Unknown");
        printf("Album: %s\n", new_track.album ? new_track.album : "Unknown");
        printf("Art URL: %s\n", new_track.art_url ? new_track.art_url : "None");

        mpris_free_metadata(&state->current_track);
        state->current_track = new_track;
        state->track_changed = true;

        // Record when the track started
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        state->track_start_time_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
        state->track_start_time_us -= state->current_track.position_us;

        // Reset tray icon to default before updating
        system_tray_reset_icon();

        // Update tooltip
        char tooltip[TOOLTIP_BUFFER_SIZE];
        char cleaned_title[TITLE_BUFFER_SIZE];

        // Clean up title: remove YouTube ID and file extension
        if (new_track.title) {
            strncpy(cleaned_title, new_track.title, sizeof(cleaned_title) - 1);
            cleaned_title[sizeof(cleaned_title) - 1] = '\0';

            // Remove file extension first
            char *ext = strrchr(cleaned_title, '.');
            if (ext) {
                // Common video/audio extensions
                if (strcmp(ext, ".mkv") == 0 || strcmp(ext, ".mp4") == 0 ||
                    strcmp(ext, ".webm") == 0 || strcmp(ext, ".mp3") == 0 ||
                    strcmp(ext, ".flac") == 0 || strcmp(ext, ".opus") == 0 ||
                    strcmp(ext, ".ogg") == 0 || strcmp(ext, ".m4a") == 0) {
                    *ext = '\0';
                }
            }

            // Remove YouTube ID pattern [xxxxx]
            char *youtube_id = strrchr(cleaned_title, '[');
            if (youtube_id) {
                char *bracket_end = strchr(youtube_id, ']');
                if (bracket_end && bracket_end[1] == '\0') {
                    // Remove trailing space before bracket if exists
                    if (youtube_id > cleaned_title && youtube_id[-1] == ' ') {
                        youtube_id--;
                    }
                    *youtube_id = '\0';
                }
            }
        } else {
            cleaned_title[0] = '\0';
        }

        if (new_track.artist && strlen(new_track.artist) > 0) {
            snprintf(tooltip, sizeof(tooltip), "%s - %s", new_track.artist, cleaned_title);
        } else {
            snprintf(tooltip, sizeof(tooltip), "%s", cleaned_title);
        }
        system_tray_update_tooltip(tooltip);

        // Update system tray icon with album art
        // Try MPRIS art_url first, fallback to iTunes Search API if unavailable
        system_tray_update_icon_with_fallback(new_track.art_url, new_track.artist, new_track.title);
    } else {
        mpris_free_metadata(&new_track);
    }

    return changed;
}

static bool load_lyrics_for_track(struct lyrics_state *state) {
    // Free previous lyrics
    lrc_free_data(&state->lyrics);
    state->current_line = NULL;

    // Try to find lyrics
    if (!lyrics_find_for_track(&state->current_track, &state->lyrics)) {
        printf("No lyrics found for current track\n");
        return false;
    }

    printf("Loaded %d lines of lyrics\n", state->lyrics.line_count);

    // Set initial line
    state->current_line = state->lyrics.lines;
    state->track_changed = false;

    // If MPRIS didn't provide album art and iTunes is enabled, try iTunes API with metadata from lyrics
    // (lrclib may have populated artist/album information)
    if (g_config.lyrics.enable_itunes && (!state->current_track.art_url || strlen(state->current_track.art_url) == 0)) {
        const char *artist = state->lyrics.metadata.artist;
        const char *title = state->lyrics.metadata.title;

        // Use lyrics metadata if available, otherwise fall back to MPRIS metadata
        if (!artist || strlen(artist) == 0) {
            artist = state->current_track.artist;
        }
        if (!title || strlen(title) == 0) {
            title = state->current_track.title;
        }

        if (artist && title && strlen(artist) > 0 && strlen(title) > 0) {
            printf("Trying iTunes API with lyrics metadata (artist: %s, title: %s)\n", artist, title);
            system_tray_update_icon_with_fallback(NULL, artist, title);
        }
    }

    return true;
}

static void update_current_line(struct lyrics_state *state) {
    if (!state->lyrics.lines) {
        // No lyrics loaded - clear instrumental break flag
        state->in_instrumental_break = false;
        return;
    }

    // Get current playback position
    int64_t position_us = mpris_get_position();

    // Find the appropriate line for current position
    struct lyrics_line *new_line = lrc_find_line_at_time(&state->lyrics, position_us);

    // Check if we should clear the lyrics for SRT/WEBVTT formats
    // SRT/WEBVTT have explicit end timestamps, unlike LRC/LRCX
    if (new_line && new_line->end_timestamp_us > 0) {
        if (is_lyrics_format(state, ".srt") || is_lyrics_format(state, ".vtt")) {
            // SRT/WEBVTT: Clear line when end timestamp is reached
            if (position_us > new_line->end_timestamp_us) {
                new_line = NULL;
            }
        }
    }

    // Check if line text is empty or whitespace-only (instrumental break in LRC/LRCX)
    bool is_empty_text = false;
    if (new_line && new_line->text) {
        const char *p = new_line->text;
        is_empty_text = true;
        while (*p) {
            if (!isspace(*p)) {
                is_empty_text = false;
                break;
            }
            p++;
        }
    }

    // Treat empty/whitespace-only text lines as NULL (no lyrics to display)
    struct lyrics_line *display_line = is_empty_text ? NULL : new_line;

    bool line_changed = (display_line != state->current_line);
    if (line_changed) {
        state->current_line = display_line;
        state->current_segment = NULL; // Reset word segment when line changes

        if (display_line && display_line->text) {
            int index = lrc_get_line_index(&state->lyrics, display_line);
            log_info("Line %d/%d: %s", index + 1, state->lyrics.line_count, display_line->text);

            // For karaoke (LRCX), set initial segment
            if (is_lyrics_format(state, ".lrcx") && display_line->segments) {
                state->current_segment = display_line->segments;
            }
        } else {
            // Debug: why is this being printed?
            log_info("Instrumental break - clearing lyrics (new_line=%p, is_empty_text=%d, display_line=%p)",
                (void*)new_line, is_empty_text, (void*)display_line);

            // Mark that we're in instrumental break (for file check during idle time)
            if (!state->in_instrumental_break) {
                state->in_instrumental_break = true;
            }
        }

        set_dirty(state);
    }

    // Clear instrumental break flag when lyrics are showing
    if (state->current_line) {
        state->in_instrumental_break = false;
    }

    // Update word segment for karaoke highlighting (LRCX only)
    if (is_lyrics_format(state, ".lrcx") && new_line && new_line->segments) {
        struct word_segment *new_segment = lrcx_find_segment_at_time(new_line, position_us, NULL);
        if (new_segment != state->current_segment) {
            state->current_segment = new_segment;
            set_dirty(state);
        }
    }
}

int main(int argc, char *argv[]) {
    int ret = 0;

    // Extract program name for help messages
    char *argv0_copy = strdup(argv[0]);
    if (!argv0_copy) {
        fprintf(stderr, "Memory allocation failed\n");
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
                curl_easy_setopt(curl, CURLOPT_URL, help_url);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buffer);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);  // 5 second timeout
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "wshowlyrics/1.0");

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

    // Initialize configuration with defaults
    config_init_defaults(&g_config);

    // Try to load configuration in priority order:
    // 1. ~/.config/wshowlyrics/settings.ini (user config)
    // 2. /etc/wshowlyrics/settings.ini (system-wide config)

    bool config_loaded = false;

    // Try user config first
    char *user_config_path = config_get_path();
    if (user_config_path) {
        // Create config directory if it doesn't exist
        char *dir_path = strdup(user_config_path);
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdir(dir_path, 0755);  // Create ~/.config/wshowlyrics/
        }
        free(dir_path);

        // Check if user config exists
        struct stat st;
        if (stat(user_config_path, &st) != 0) {
            // User config doesn't exist - try to copy from system config
            const char *system_config = "/etc/wshowlyrics/settings.ini";
            if (stat(system_config, &st) == 0) {
                // System config exists - copy it
                FILE *src = fopen(system_config, "r");
                if (src) {
                    FILE *dst = fopen(user_config_path, "w");
                    if (dst) {
                        char buf[CONTENT_BUFFER_SIZE];
                        size_t n;
                        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                            fwrite(buf, 1, n, dst);
                        }
                        fclose(dst);
                        printf("Copied system config to user config: %s\n", user_config_path);
                    }
                    fclose(src);
                }
            }
        }

        // Try to load user config
        if (config_load(&g_config, user_config_path)) {
            config_loaded = true;
        }
        free(user_config_path);
    }

    // If user config not found, try system-wide config
    if (!config_loaded) {
        config_load(&g_config, "/etc/wshowlyrics/settings.ini");
        // Note: config_load returns false if file doesn't exist, which is fine
        // We'll just use the defaults initialized above
    }

    unsigned int anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    int margin = g_config.display.margin_bottom;
    struct lyrics_state state = { 0 };

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
    snprintf(font_str, sizeof(font_str), "%s %s %d",
        g_config.display.font_family,
        g_config.display.font_weight,
        g_config.display.font_size);
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
            state.background = parse_color(optarg);
            break;
        case 'f':
            state.foreground = parse_color(optarg);
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

    // Fontconfig initializations
    if(!FcInit()) {
        log_error("Failed to initialize fontconfig");
        return 1;
    }

    log_info("Compositor: %s", getenv("WAYLAND_DISPLAY") ?: "wayland-0");
    log_info("Using compositor interfaces...");

    // Initialize lyrics providers
    lyrics_providers_init();

    // Initialize MPRIS for automatic lyrics detection
    if (!mpris_init()) {
        log_error("Failed to initialize MPRIS (playerctl not found?)");
        ret = 1;
        goto exit;
    }
    log_info("MPRIS mode enabled - will track currently playing music");

    // Initialize system tray
    if (system_tray_init()) {
        log_info("System tray initialized (album art display)");
    } else {
        log_warn("Failed to initialize system tray");
    }

    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "wl_display_connect: %s\n", strerror(errno));
        ret = 1;
        goto exit;
    }

    state.registry = wl_display_get_registry(state.display);
    assert(state.registry);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    const struct {
        const char *name;
        void *ptr;
    } need_globals[] = {
        {"wl_compositor", &state.compositor},
        {"wl_shm", &state.shm},
        {"wlr_layer_shell", &state.layer_shell},
    };
    for (size_t i = 0; i < sizeof(need_globals) / sizeof(need_globals[0]); ++i) {
        if (!need_globals[i].ptr) {
            fprintf(stderr, "Error: required Wayland interface '%s' "
                    "is not present\n", need_globals[i].name);
            ret = 1;
            goto exit;
        }
    }

    state.surface = wl_compositor_create_surface(state.compositor);
    assert(state.surface);

    state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            state.layer_shell, state.surface, NULL,
            ZWLR_LAYER_SHELL_V1_LAYER_TOP, "lyrics");
    assert(state.layer_surface);

    wl_surface_add_listener(state.surface, &wl_surface_listener, &state);
    zwlr_layer_surface_v1_add_listener(
            state.layer_surface, &layer_surface_listener, &state);
    zwlr_layer_surface_v1_set_size(state.layer_surface, 1, 1);
    zwlr_layer_surface_v1_set_anchor(state.layer_surface, anchor);
    zwlr_layer_surface_v1_set_margin(state.layer_surface,
            margin, margin, margin, margin);
    zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, 0);

    // Set empty input region to allow clicks to pass through
    struct wl_region *region = wl_compositor_create_region(state.compositor);
    wl_surface_set_input_region(state.surface, region);
    wl_region_destroy(region);

    wl_surface_commit(state.surface);

    // Wait for configure event
    int retry_count = 0;
    while ((state.width == 0 || state.height == 0) && retry_count < WAYLAND_CONFIGURE_RETRY_LIMIT) {
        wl_display_roundtrip(state.display);
        retry_count++;
    }

    retry_count = 0;
    while ((state.width == 0 || state.height == 0) && retry_count < WAYLAND_CONFIGURE_RETRY_LIMIT) {
        wl_display_dispatch(state.display);
        retry_count++;
    }

    if (state.width == 0 || state.height == 0) {
        fprintf(stderr, "Layer surface configuration failed\n");
        ret = 1;
        goto exit;
    }

    struct pollfd pollfds[] = {
        { .fd = wl_display_get_fd(state.display), .events = POLLIN, },
    };

    // Store surface configuration for reinitialization
    state.anchor = anchor;
    state.margin = margin;

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
            fprintf(stderr, "Reconnection needed, attempting full reconnection...\n");
            if (handle_wayland_reconnection(&state, &wl_conn, pollfds)) {
                state.needs_reconnect = false;
            }
            continue;
        }

        // Flush Wayland display
        if (!wayland_manager_flush(&wl_conn)) {
            // Connection lost, attempt full reconnection
            handle_wayland_reconnection(&state, &wl_conn, pollfds);
            continue;
        }

        int timeout = POLL_TIMEOUT_MS;

        int poll_ret = poll(pollfds, sizeof(pollfds) / sizeof(pollfds[0]), timeout);
        if (poll_ret < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, continue
                continue;
            }
            fprintf(stderr, LOG_ERROR "  Poll error: %s (errno=%d)\n", strerror(errno), errno);
            break;
        }

        // Check for errors or hangup on the Wayland fd
        if (pollfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, LOG_WARN "  Wayland connection error detected (revents=0x%x)\n", pollfds[0].revents);
            if (pollfds[0].revents & POLLHUP) {
                fprintf(stderr, LOG_WARN "  Wayland compositor disconnected (possibly due to screen lock or tty switch)\n");
            }
            // Attempt full reconnection
            handle_wayland_reconnection(&state, &wl_conn, pollfds);
            continue;
        }

        // Check for track changes periodically
        if (update_counter++ % TRACK_UPDATE_CHECK_INTERVAL == 0) {
            if (update_track_info(&state)) {
                // Track changed, load new lyrics
                load_lyrics_for_track(&state);
                set_dirty(&state);
            } else {
                // Check if current lyrics file has changed (every 2 seconds)
                if (state.lyrics.source_file_path && state.lyrics.md5_checksum[0] != '\0') {
                    char current_checksum[MD5_DIGEST_STRING_LENGTH];
                    if (calculate_file_md5(state.lyrics.source_file_path, current_checksum)) {
                        if (strcmp(current_checksum, state.lyrics.md5_checksum) != 0) {
                            printf("Lyrics file changed, reloading: %s\n", state.lyrics.source_file_path);
                            // Hide overlay during reload to prevent flickering
                            render_transparent_frame(&state);
                            load_lyrics_for_track(&state);
                            set_dirty(&state);
                        }
                    }
                }
            }
        }

        // Update current line based on playback position
        if (mpris_is_playing()) {
            update_current_line(&state);
            // Continuously update for smooth karaoke highlighting (LRCX only)
            if (is_lyrics_format(&state, ".lrcx")) {
                set_dirty(&state);
            }
        } else {
            // Clear lyrics when not playing (paused or stopped)
            if (state.current_line != NULL) {
                state.current_line = NULL;
                set_dirty(&state);
                log_info("Playback stopped/paused - clearing lyrics");
            }
        }

        if (pollfds[0].revents & POLLIN) {
            if (!wayland_manager_dispatch(&wl_conn)) {
                // Connection lost, attempt full reconnection
                handle_wayland_reconnection(&state, &wl_conn, pollfds);
                continue;
            }
        }

        // During instrumental break (idle time), handle lyrics file status
        // Only do one action per instrumental break to avoid doing both in same session
        if (state.in_instrumental_break && state.lyrics.source_file_path) {
            struct stat st;
            bool file_exists = (stat(state.lyrics.source_file_path, &st) == 0);

            if (!state.need_lyrics_search && !file_exists) {
                // First detection: file was deleted or moved
                printf("Lyrics file was deleted or moved: %s\n", state.lyrics.source_file_path);
                printf("Will search for lyrics during next instrumental break\n");
                state.need_lyrics_search = true;
                state.in_instrumental_break = false; // Reset to trigger search on next break
            } else if (state.need_lyrics_search) {
                if (file_exists) {
                    // File is back! No need to search
                    printf("Lyrics file is back at original location: %s\n", state.lyrics.source_file_path);
                } else {
                    // File still missing - search for lyrics again
                    printf("Searching for lyrics again...\n");
                    struct lyrics_data new_lyrics = {0};
                    if (lyrics_find_for_track(&state.current_track, &new_lyrics)) {
                        printf("Found new lyrics, replacing old ones\n");
                        lrc_free_data(&state.lyrics);
                        state.lyrics = new_lyrics;
                        state.current_line = NULL;
                        set_dirty(&state);
                    } else {
                        printf("No lyrics found, keeping existing lyrics displayed\n");
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
    system_tray_cleanup();
    lrc_free_data(&state.lyrics);
    mpris_free_metadata(&state.current_track);
    mpris_cleanup();
    lyrics_providers_cleanup();

    if (state.display) {
        wl_display_disconnect(state.display);
    }
    FcFini();

    // Free allocated font string if it was from config
    if (font_from_config_alloc) {
        free(font_from_config_alloc);
    }

    free(argv0_copy);
    return ret;
}
