#include "lyrics_manager.h"
#include "../user_experience/config/config.h"
#include "../core/state/state_helpers.h"
#include "../core/rendering/rendering_manager.h"
#include "../utils/mpris/mpris.h"
#include "../provider/lyrics/lyrics_provider.h"
#include "../user_experience/system_tray/system_tray.h"
#include "../parser/lrc/lrc_common.h"
#include "../parser/lrc/lrcx_parser.h"
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

bool lyrics_manager_is_format(struct lyrics_state *state, const char *extension) {
    if (!state->lyrics.source_file_path) {
        return false;
    }
    const char *ext = strrchr(state->lyrics.source_file_path, '.');
    return ext && strcasecmp(ext, extension) == 0;
}

void lyrics_manager_clean_title(char *dest, size_t dest_size, const char *title) {
    if (!title) {
        dest[0] = '\0';
        return;
    }

    snprintf(dest, dest_size, "%s", title);

    // Remove file extension
    char *ext = strrchr(dest, '.');
    if (ext && (strcmp(ext, ".mkv") == 0 || strcmp(ext, ".mp4") == 0 ||
                strcmp(ext, ".webm") == 0 || strcmp(ext, ".mp3") == 0 ||
                strcmp(ext, ".flac") == 0 || strcmp(ext, ".opus") == 0 ||
                strcmp(ext, ".ogg") == 0 || strcmp(ext, ".m4a") == 0)) {
        *ext = '\0';
    }

    // Remove YouTube ID pattern [xxxxx]
    char *youtube_id = strrchr(dest, '[');
    if (youtube_id) {
        char *bracket_end = strchr(youtube_id, ']');
        if (bracket_end && bracket_end[1] == '\0') {
            if (youtube_id > dest && youtube_id[-1] == ' ') {
                youtube_id--;
            }
            *youtube_id = '\0';
        }
    }
}

bool lyrics_manager_update_track_info(struct lyrics_state *state) {

    struct track_metadata new_track = {0};
    if (!mpris_get_metadata(&new_track)) {
        // No player found - clear everything if we had a track before
        if (state->current_track.title) {
            log_info("=== No player found, clearing lyrics ===");

            // Cancel ongoing translation
            state->lyrics.translation_should_cancel = true;

            // Wait for translation to finish before freeing lyrics data
            // This prevents use-after-free errors
            int wait_count = 0;
            struct timespec wait_delay = {0, 50000000L}; // 50ms
            while (state->lyrics.translation_in_progress && wait_count < 100) {
                nanosleep(&wait_delay, NULL);
                wait_count++;
            }
            if (wait_count >= 100) {
                log_warn("Translation thread did not stop in time (waited 5s), force cancelling");
                pthread_cancel(state->lyrics.translation_thread);
                pthread_join(state->lyrics.translation_thread, NULL);
            } else if (state->lyrics.translation_in_progress == false && wait_count > 0) {
                // Thread finished gracefully, join it to clean up resources
                pthread_join(state->lyrics.translation_thread, NULL);
            }

            // Free track metadata
            mpris_free_metadata(&state->current_track);

            // Free lyrics
            lrc_free_data(&state->lyrics);
            state->current_line = NULL;
            state->prev_line = NULL;
            state->next_line = NULL;

            // Reset tray icon to default
            system_tray_reset_icon();

            // Clear the display
            rendering_manager_set_dirty(state);
        }
        return false;
    }

    // Check if track changed by comparing URL (unique identifier)
    bool changed = false;
    if (!state->current_track.url ||
        !new_track.url ||
        strcmp(new_track.url, state->current_track.url) != 0) {
        changed = true;
    }

    if (changed) {
        log_info("=== Track changed ===");
        log_info("Title: %s", new_track.title ? new_track.title : "Unknown");
        log_info("Artist: %s", new_track.artist ? new_track.artist : "Unknown");
        log_info("Album: %s", new_track.album ? new_track.album : "Unknown");
        log_info("URL: %s", new_track.url ? new_track.url : "None");
        log_info("Art URL: %s", new_track.art_url ? new_track.art_url : "None");

        // Cancel ongoing translation
        state->lyrics.translation_should_cancel = true;

        // Wait for translation to finish before freeing lyrics data
        // This prevents use-after-free errors
        int wait_count = 0;
        struct timespec wait_delay = {0, 50000000L}; // 50ms
        while (state->lyrics.translation_in_progress && wait_count < 100) {
            nanosleep(&wait_delay, NULL);
            wait_count++;
        }
        if (wait_count >= 100) {
            log_warn("Translation thread did not stop in time (waited 5s), force cancelling");
            pthread_cancel(state->lyrics.translation_thread);
            pthread_join(state->lyrics.translation_thread, NULL);
        } else if (state->lyrics.translation_in_progress == false && wait_count > 0) {
            // Thread finished gracefully, join it to clean up resources
            pthread_join(state->lyrics.translation_thread, NULL);
        }

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

        // Album art and notification will be sent after lyrics are loaded (to use lyrics metadata if available)
    } else {
        mpris_free_metadata(&new_track);
    }

    return changed;
}

bool lyrics_manager_load_lyrics(struct lyrics_state *state) {
    // Cancel ongoing translation and wait for it to finish
    // This prevents race condition where old and new translation threads
    // write to the same cache file simultaneously
    state->lyrics.translation_should_cancel = true;

    int wait_count = 0;
    struct timespec wait_delay = {0, 50000000L}; // 50ms
    while (state->lyrics.translation_in_progress && wait_count < 100) {
        nanosleep(&wait_delay, NULL);
        wait_count++;
    }
    if (wait_count >= 100) {
        log_warn("Translation thread did not stop in time (waited 5s), force cancelling");
        pthread_cancel(state->lyrics.translation_thread);
        pthread_join(state->lyrics.translation_thread, NULL);
    } else if (state->lyrics.translation_in_progress == false && wait_count > 0) {
        // Thread finished gracefully, join it to clean up resources
        pthread_join(state->lyrics.translation_thread, NULL);
    }

    // Free previous lyrics
    lrc_free_data(&state->lyrics);
    state->current_line = NULL;
    state->prev_line = NULL;
    state->next_line = NULL;

    // Reset timing offset for new track
    state->timing_offset_ms = 0;

    // Reset overlay visibility for new track
    if (!state->overlay_enabled) {
        state->overlay_enabled = true;
        system_tray_set_overlay_state(true);
        log_info("Overlay auto-enabled for new track");
    }

    // Try to find lyrics
    if (!lyrics_find_for_track(&state->current_track, &state->lyrics)) {
        log_info("No lyrics found for current track");

        // Even without lyrics, try to update album art with MPRIS metadata
        if (g_config.lyrics.enable_itunes) {
            system_tray_update_icon_with_fallback(
                state->current_track.art_url,
                state->current_track.artist,
                state->current_track.album,
                state->current_track.title
            );
        }

        // Send notification even without lyrics
        if (g_config.lyrics.enable_notifications) {
            char cleaned_title[TITLE_BUFFER_SIZE];
            lyrics_manager_clean_title(cleaned_title, sizeof(cleaned_title), state->current_track.title);

            struct notification_info notif_info = {
                .title = cleaned_title,
                .artist = state->current_track.artist,
                .album = state->current_track.album,
                .player_name = state->current_track.player_name
            };
            system_tray_send_notification(&notif_info);
        }

        return false;
    }

    // Reset translation cancel flag for new lyrics
    state->lyrics.translation_should_cancel = false;

    log_info("Loaded %d lines of lyrics", state->lyrics.line_count);

    // Set initial line to NULL so first update will trigger line_changed
    // This ensures prev/next lines are set for multiline display
    state->current_line = NULL;
    state->track_changed = false;

    // Update album art with best available metadata
    // Prefer lyrics metadata (more accurate) over MPRIS metadata
    const char *artist = state->lyrics.metadata.artist;
    const char *album = state->lyrics.metadata.album;
    const char *title = state->lyrics.metadata.title;

    // Fall back to MPRIS metadata if lyrics metadata is not available
    if (!artist || strlen(artist) == 0) {
        artist = state->current_track.artist;
    }
    if (!album || strlen(album) == 0) {
        album = state->current_track.album;
    }
    if (!title || strlen(title) == 0) {
        title = state->current_track.title;
    }

    // Update album art (try MPRIS URL first, then iTunes API)
    log_info("Updating album art with metadata (artist: %s, album: %s, title: %s)",
             artist ? artist : "Unknown", album ? album : "Unknown", title ? title : "Unknown");
    system_tray_update_icon_with_fallback(state->current_track.art_url, artist, album, title);

    // Send desktop notification after album art is updated
    if (g_config.lyrics.enable_notifications) {
        char cleaned_title[TITLE_BUFFER_SIZE];
        lyrics_manager_clean_title(cleaned_title, sizeof(cleaned_title), title);

        struct notification_info notif_info = {
            .title = cleaned_title,
            .artist = artist,
            .album = album,
            .player_name = state->current_track.player_name
        };
        system_tray_send_notification(&notif_info);
    }

    return true;
}

void lyrics_manager_update_current_line(struct lyrics_state *state) {
    if (!state->lyrics.lines) {
        // No lyrics loaded - clear instrumental break flag
        state->in_instrumental_break = false;
        return;
    }

    // Get current playback position with timing offset applied
    // Positive offset = advance lyrics (faster), Negative offset = delay lyrics (slower)
    int64_t position_us = mpris_get_position();
    position_us += (int64_t)state->timing_offset_ms * 1000LL;

    // Find the appropriate line for current position
    struct lyrics_line *new_line = lrc_find_line_at_time(&state->lyrics, position_us);

    // Check if we should clear the lyrics for SRT/WEBVTT formats
    // SRT/WEBVTT have explicit end timestamps, unlike LRC/LRCX
    if (new_line && new_line->end_timestamp_us > 0 &&
        (lyrics_manager_is_format(state, ".srt") || lyrics_manager_is_format(state, ".vtt")) &&
        position_us > new_line->end_timestamp_us) {
        // SRT/WEBVTT: Clear line when end timestamp is reached
        new_line = NULL;
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

        // Calculate current line index
        if (display_line) {
            int index = 0;
            struct lyrics_line *line = state->lyrics.lines;
            while (line && line != display_line) {
                index++;
                line = line->next;
            }
            state->current_line_index = line ? index : -1;
        } else {
            state->current_line_index = -1;
        }

        // Update prev/next lines for multi-line display (LRCX only)
        if (lyrics_manager_is_format(state, ".lrcx") && g_config.display.enable_multiline_lrcx) {
            // For instrumental breaks (empty lines), use new_line instead of display_line
            // to maintain context display
            struct lyrics_line *context_line = display_line ? display_line : new_line;
            lrcx_find_context_lines(&state->lyrics, context_line,
                                   &state->prev_line, &state->next_line);
        } else {
            state->prev_line = NULL;
            state->next_line = NULL;
        }

        if (display_line && display_line->text) {
            int index = lrc_get_line_index(&state->lyrics, display_line);

            // Escape newlines for cleaner log output
            char *escaped_text = state_helpers_escape_newlines(display_line->text);
            if (escaped_text) {
                log_info("Line %d/%d: %s", index + 1, state->lyrics.line_count, escaped_text);
                free(escaped_text);
            } else {
                // Fallback if allocation failed
                log_info("Line %d/%d: %s", index + 1, state->lyrics.line_count, display_line->text);
            }

            // For karaoke (LRCX), set initial segment
            if (lyrics_manager_is_format(state, ".lrcx") && display_line->segments) {
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

        rendering_manager_set_dirty(state);
    }

    // Clear instrumental break flag when lyrics are showing
    if (state->current_line) {
        state->in_instrumental_break = false;
    }

    // Update word segment for karaoke highlighting (LRCX only)
    if (lyrics_manager_is_format(state, ".lrcx") && new_line && new_line->segments) {
        struct word_segment *new_segment = lrcx_find_segment_at_time(new_line, position_us, NULL);
        if (new_segment != state->current_segment) {
            state->current_segment = new_segment;
            rendering_manager_set_dirty(state);
        }
    }
}
