// SPDX-License-Identifier: GPL-3.0-or-later
#include "lyrics_manager.h"
#include "../user_experience/config/config.h"
#include "../core/state/state_helpers.h"
#include "../core/rendering/rendering_manager.h"
#include "../utils/mpris/mpris.h"
#include "../provider/lyrics/lyrics_provider.h"
#include "../provider/lrclib/lrclib_provider.h"
#include "../provider/itunes/itunes_artwork.h"
#include "../user_experience/system_tray/system_tray.h"
#include "../parser/lrc/lrc_common.h"
#include "../utils/file/file_utils.h"
#include "../parser/lrc/lrcx_parser.h"
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdatomic.h>

bool lyrics_manager_is_format(const struct lyrics_state *state, const char *extension) {
    if (!state->playback.lyrics.source_file_path) {
        return false;
    }
    const char *ext = strrchr(state->playback.lyrics.source_file_path, '.');
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
        const char *bracket_end = strchr(youtube_id, ']');
        if (bracket_end && bracket_end[1] == '\0') {
            if (youtube_id > dest && youtube_id[-1] == ' ') {
                youtube_id--;
            }
            *youtube_id = '\0';
        }
    }
}

// Cancel ongoing translation and wait for it to finish
// Prevents use-after-free errors when freeing lyrics data
void cancel_and_wait_translation(struct lyrics_data *lyrics) {
    if (!lyrics->translation_thread_active) return;

    lyrics->translation_should_cancel = true;

    int wait_count = 0;
    struct timespec wait_delay = {0, 50000000L}; // 50ms
    while (lyrics->translation_in_progress && wait_count < 100) {
        nanosleep(&wait_delay, NULL);
        wait_count++;
    }

    if (wait_count >= 100) {
        log_warn("Translation thread did not stop in time (waited 5s), force cancelling");
        pthread_cancel(lyrics->translation_thread);
    }

    pthread_join(lyrics->translation_thread, NULL);
    lyrics->translation_thread_active = false;
}

// Helper: Handle case when no player is found
static void handle_no_player_found(struct lyrics_state *state) {
    if (!state->playback.current_track.title) {
        return;  // Nothing to clear
    }

    log_info("=== No player found, clearing lyrics ===");

    // Cancel ongoing translation
    cancel_and_wait_translation(&state->playback.lyrics);

    // Free track metadata
    mpris_free_metadata(&state->playback.current_track);

    // Free lyrics
    lrc_free_data(&state->playback.lyrics);
    state->playback.current_line = NULL;
    state->playback.prev_line = NULL;
    state->playback.next_line = NULL;

    // Reset tray icon and track info
    system_tray_reset_icon();
    system_tray_update_track_info(NULL, NULL);

    // Clear the display
    rendering_manager_set_dirty(state);
}

// Helper: Detect if track changed by comparing URL or trackid
static bool detect_track_change(const struct track_metadata *new_track, const struct track_metadata *current_track) {
    // Check URL first (most reliable for local files)
    // mpv and other local players reuse trackids (e.g., /9) across different files
    if (new_track->url && current_track->url) {
        return strcmp(new_track->url, current_track->url) != 0;
    }

    // If one has URL and other doesn't, assume changed
    if (new_track->url || current_track->url) {
        return true;
    }

    // Fallback to trackid comparison (for streaming services like Spotify)
    if (new_track->trackid && current_track->trackid) {
        return strcmp(new_track->trackid, current_track->trackid) != 0;
    }

    // Neither trackid nor URL available - assume changed if we had a previous track
    return current_track->title != NULL;
}

// Helper: Handle track changed - log metadata, cancel translation, update state
static void handle_track_changed(struct lyrics_state *state, const struct track_metadata *new_track) {
    log_info("=== Track changed ===");
    log_info("Title: %s", new_track->title ? new_track->title : "Unknown");
    log_info("Artist: %s", new_track->artist ? new_track->artist : "Unknown");
    log_info("Album: %s", new_track->album ? new_track->album : "Unknown");
    log_info("URL: %s", new_track->url ? sanitize_path(new_track->url) : "None");
    log_info("Track ID: %s", new_track->trackid ? new_track->trackid : "None");
    log_info("Art URL: %s", new_track->art_url ? new_track->art_url : "None");

    // Cancel ongoing translation
    cancel_and_wait_translation(&state->playback.lyrics);

    // Update track metadata
    mpris_free_metadata(&state->playback.current_track);
    state->playback.current_track = *new_track;
    state->playback.track_changed = true;

    // Record when the track started
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    state->playback.track_start_time_us = now.tv_sec * 1000000 + now.tv_nsec / 1000;
    state->playback.track_start_time_us -= state->playback.current_track.position_us;

    // Reset tray icon and update track info
    system_tray_reset_icon();
    system_tray_update_track_info(new_track->artist, new_track->title);
}

bool lyrics_manager_update_track_info(struct lyrics_state *state) {
    struct track_metadata new_track = {0};

    if (!mpris_get_metadata(&new_track)) {
        // No player found - clear everything if we had a track before
        handle_no_player_found(state);
        mpris_free_metadata(&new_track);
        return false;
    }

    // Check if track changed
    bool changed = detect_track_change(&new_track, &state->playback.current_track);

    if (changed) {
        handle_track_changed(state, &new_track);
        // Album art and notification will be sent after lyrics are loaded
    } else {
        mpris_free_metadata(&new_track);
    }

    return changed;
}

// Reset per-track state before loading new lyrics: stop any running translation,
// free the currently displayed lyrics, and clear the line cursors. After this
// the live lyrics is empty, so the render loop draws nothing until new lyrics
// are installed (synchronously below, or by lyrics_manager_poll_load).
static void reset_for_new_lyrics(struct lyrics_state *state) {
    // Cancel ongoing translation and wait for it to finish. This prevents a race
    // where old and new translation threads write to the same cache file.
    cancel_and_wait_translation(&state->playback.lyrics);

    lrc_free_data(&state->playback.lyrics);
    state->playback.current_line = NULL;
    state->playback.prev_line = NULL;
    state->playback.next_line = NULL;

    // Reset timing offset to global offset for new track
    state->playback.timing_offset_ms = g_config.lyrics.global_offset_ms;

    // Reset overlay visibility for new track
    if (!state->runtime.overlay_enabled) {
        state->runtime.overlay_enabled = true;
        system_tray_set_overlay_state(true);
        log_info("Overlay auto-enabled for new track");
    }
}

// --- Async album-art (iTunes) offload ------------------------------------

// Free the artwork worker's owned input / deferred-notification strings.
static void free_art_fetch_inputs(struct async_art_fetch *af) {
    free(af->artist); free(af->album); free(af->track);
    af->artist = af->album = af->track = NULL;
    free(af->notif_title); free(af->notif_artist);
    free(af->notif_album); free(af->notif_player);
    af->notif_title = af->notif_artist = af->notif_album = af->notif_player = NULL;
    af->notify_pending = false;
}

// Send the track-change desktop notification (main thread).
static void send_track_notification(struct lyrics_state *state, const char *artist,
                                    const char *album, const char *title) {
    if (!g_config.lyrics.enable_notifications) {
        return;
    }
    char cleaned_title[TITLE_BUFFER_SIZE];
    lyrics_manager_clean_title(cleaned_title, sizeof(cleaned_title), title);
    struct notification_info notif_info = {
        .title = cleaned_title,
        .artist = artist,
        .album = album,
        .player_name = state->playback.current_track.player_name
    };
    system_tray_send_notification(&notif_info);
}

// Artwork worker: resolve iTunes art into the cache PNG. Touches only the
// async_art buffers and the network — never GTK/AppIndicator or live state.
static void *art_fetch_worker(void *arg) {
    struct lyrics_state *state = arg;
    struct async_art_fetch *af = &state->playback.async_art;

    itunes_set_cancel_flag(&af->should_cancel);
    system_tray_set_art_cancel_flag(&af->should_cancel);
    af->found = system_tray_resolve_itunes_art(af->artist, af->album, af->track, af->cache_path);
    itunes_set_cancel_flag(NULL);
    system_tray_set_art_cancel_flag(NULL);

    atomic_store(&af->ready, true);
    return NULL;
}

// Start the iTunes artwork worker for the current track. The track notification
// is captured and deferred until the art lands (or fails) — see poll_art.
static void begin_art_fetch(struct lyrics_state *state, const char *artist,
                            const char *album, const char *title,
                            const char *md5, const char *cache_path) {
    struct async_art_fetch *af = &state->playback.async_art;
    snprintf(af->md5, sizeof(af->md5), "%s", md5);
    snprintf(af->cache_path, sizeof(af->cache_path), "%s", cache_path);
    af->artist = artist ? strdup(artist) : NULL;
    af->album  = album  ? strdup(album)  : NULL;
    af->track  = title  ? strdup(title)  : NULL;

    af->notify_pending = g_config.lyrics.enable_notifications;
    if (af->notify_pending) {
        char cleaned_title[TITLE_BUFFER_SIZE];
        lyrics_manager_clean_title(cleaned_title, sizeof(cleaned_title), title);
        af->notif_title  = strdup(cleaned_title);
        af->notif_artist = artist ? strdup(artist) : NULL;
        af->notif_album  = album  ? strdup(album)  : NULL;
        af->notif_player = state->playback.current_track.player_name
                               ? strdup(state->playback.current_track.player_name) : NULL;
    }

    af->should_cancel = false;
    af->found = false;
    af->ready = false;
    if (pthread_create(&af->thread, NULL, art_fetch_worker, state) != 0) {
        log_warn("Failed to start artwork worker; skipping iTunes art");
        free_art_fetch_inputs(af);
        send_track_notification(state, artist, album, title);  // deferred notif fires now
        return;
    }
    af->thread_active = true;
}

// Resolve album art via the fast (non-network) sources on the main thread, then
// either send the track notification now, or — when only iTunes can supply it —
// start the artwork worker and defer the notification until the art resolves.
static void finalize_common(struct lyrics_state *state, const char *art_url,
                            const char *file_url, const char *artist,
                            const char *album, const char *title) {
    lyrics_manager_cancel_art_fetch(state);   // supersede any prior art fetch

    char md5[MD5_DIGEST_STRING_LENGTH];
    char cache_path[512];
    enum tray_art_status st = system_tray_update_icon_fast(
        art_url, file_url, artist, album, title,
        md5, sizeof(md5), cache_path, sizeof(cache_path));

    if (st == TRAY_ART_NEED_ITUNES) {
        begin_art_fetch(state, artist, album, title, md5, cache_path);
        return;   // notification is sent once the art lands (poll_art)
    }
    send_track_notification(state, artist, album, title);
}

// Post-processing when no lyrics were found: album art + notification.
// Runs on the main thread (touches GTK), never on the fetch worker.
static void finalize_not_found(struct lyrics_state *state) {
    log_info("No lyrics found for current track");
    finalize_common(state,
                    state->playback.current_track.art_url,
                    state->playback.current_track.url,
                    state->playback.current_track.artist,
                    state->playback.current_track.album,
                    state->playback.current_track.title);
}

// Post-processing when lyrics are installed in the live state: album art,
// notification, and starting translation. Runs on the main thread against the
// LIVE lyrics — starting translation here (not inside the provider chain) is
// what makes the fetch safe to run on a worker thread.
static void finalize_found(struct lyrics_state *state) {
    state->playback.lyrics.translation_should_cancel = false;

    log_info("Loaded %d lines of lyrics", state->playback.lyrics.line_count);

    // Set initial line to NULL so first update will trigger line_changed
    // This ensures prev/next lines are set for multiline display
    state->playback.current_line = NULL;
    state->playback.track_changed = false;

    // Update album art with best available metadata
    // Prefer lyrics metadata (more accurate) over MPRIS metadata
    const char *artist = state->playback.lyrics.metadata.artist;
    const char *album = state->playback.lyrics.metadata.album;
    const char *title = state->playback.lyrics.metadata.title;

    if (!artist || artist[0] == '\0') {
        artist = state->playback.current_track.artist;
    }
    if (!album || album[0] == '\0') {
        album = state->playback.current_track.album;
    }
    if (!title || title[0] == '\0') {
        title = state->playback.current_track.title;
    }

    log_info("Updating album art with metadata (artist: %s, album: %s, title: %s)",
             artist ? artist : "Unknown", album ? album : "Unknown", title ? title : "Unknown");
    finalize_common(state, state->playback.current_track.art_url,
                    state->playback.current_track.url, artist, album, title);

    // Start translation on the live lyrics (was previously started inside the
    // provider chain; moved out so the fetch can run off the main thread).
    lyrics_provider_translate(&state->playback.lyrics,
                              state->playback.current_track.length_us);
}

// Deep-copy a track snapshot so the fetch worker reads a stable copy that is
// independent of the live current_track (which the main thread may replace).
static void copy_track_metadata(struct track_metadata *dst, const struct track_metadata *src) {
    memset(dst, 0, sizeof(*dst));
    if (!src) return;
    dst->title       = src->title       ? strdup(src->title)       : NULL;
    dst->artist      = src->artist      ? strdup(src->artist)      : NULL;
    dst->album       = src->album       ? strdup(src->album)       : NULL;
    dst->url         = src->url         ? strdup(src->url)         : NULL;
    dst->trackid     = src->trackid     ? strdup(src->trackid)     : NULL;
    dst->art_url     = src->art_url     ? strdup(src->art_url)     : NULL;
    dst->player_name = src->player_name ? strdup(src->player_name) : NULL;
    dst->length_us   = src->length_us;
    dst->position_us = src->position_us;
}

// Discard an in-flight (or completed-but-unconsumed) async fetch: ask it to
// abort, join it, and free its private result and track snapshot.
void lyrics_manager_cancel_fetch(struct lyrics_state *state) {
    struct async_lyrics_fetch *fetch = &state->playback.async_fetch;
    if (!fetch->thread_active) return;

    fetch->should_cancel = true;
    pthread_join(fetch->thread, NULL);
    fetch->thread_active = false;
    fetch->in_progress = false;
    fetch->ready = false;
    lrclib_set_cancel_flag(NULL);

    lrc_free_data(&fetch->result);
    memset(&fetch->result, 0, sizeof(fetch->result));
    mpris_free_metadata(&fetch->track);
    memset(&fetch->track, 0, sizeof(fetch->track));
}

// Fetch worker: runs the (blocking, network) provider search into the fetch's
// private result buffer. Touches only fetch->result / fetch->track and atomics —
// never the live lyrics, GTK, or the renderer.
static void *lyrics_fetch_worker(void *arg) {
    struct lyrics_state *state = arg;
    struct async_lyrics_fetch *fetch = &state->playback.async_fetch;

    lrclib_set_cancel_flag(&fetch->should_cancel);
    fetch->found = lyrics_find_for_track(&fetch->track, &fetch->result);
    lrclib_set_cancel_flag(NULL);

    fetch->in_progress = false;
    atomic_store(&fetch->ready, true);
    return NULL;
}

// Synchronous load — used by the local-file hot-reload path, where the search
// hits the fast local provider. Frees old lyrics, searches, and finalizes inline.
bool lyrics_manager_load_lyrics(struct lyrics_state *state) {
    lyrics_manager_cancel_fetch(state);   // drop any pending async fetch first
    reset_for_new_lyrics(state);

    if (!lyrics_find_for_track(&state->playback.current_track, &state->playback.lyrics)) {
        finalize_not_found(state);
        return false;
    }
    finalize_found(state);
    return true;
}

// Begin an asynchronous load: reset per-track state (so the overlay clears
// immediately), snapshot the track, and kick off the fetch worker. Non-blocking;
// the result is consumed later by lyrics_manager_poll_load.
void lyrics_manager_begin_load(struct lyrics_state *state) {
    lyrics_manager_cancel_fetch(state);       // supersede any prior in-flight fetch
    lyrics_manager_cancel_art_fetch(state);   // and the previous track's artwork fetch
    reset_for_new_lyrics(state);

    struct async_lyrics_fetch *fetch = &state->playback.async_fetch;
    memset(&fetch->result, 0, sizeof(fetch->result));
    copy_track_metadata(&fetch->track, &state->playback.current_track);
    fetch->should_cancel = false;
    fetch->found = false;
    fetch->ready = false;
    fetch->in_progress = true;

    if (pthread_create(&fetch->thread, NULL, lyrics_fetch_worker, state) != 0) {
        log_warn("Failed to start async lyrics fetch thread; loading synchronously");
        fetch->in_progress = false;
        mpris_free_metadata(&fetch->track);
        memset(&fetch->track, 0, sizeof(fetch->track));
        if (lyrics_find_for_track(&state->playback.current_track, &state->playback.lyrics)) {
            finalize_found(state);
        } else {
            finalize_not_found(state);
        }
        return;
    }
    fetch->thread_active = true;
}

// Consume a completed async fetch (call every main-loop tick). Swaps the fetched
// result into the live lyrics and runs the main-thread finalize steps. No-op
// while the worker is still running.
void lyrics_manager_poll_load(struct lyrics_state *state) {
    struct async_lyrics_fetch *fetch = &state->playback.async_fetch;
    if (!fetch->thread_active || !atomic_load(&fetch->ready)) {
        return;
    }

    pthread_join(fetch->thread, NULL);
    fetch->thread_active = false;
    fetch->ready = false;
    lrclib_set_cancel_flag(NULL);

    bool found = fetch->found;
    mpris_free_metadata(&fetch->track);
    memset(&fetch->track, 0, sizeof(fetch->track));

    if (found) {
        // reset_for_new_lyrics already freed the live lyrics, so this move-assign
        // does not leak; zero the source so we never double-free the buffer.
        state->playback.lyrics = fetch->result;
        memset(&fetch->result, 0, sizeof(fetch->result));
        finalize_found(state);
    } else {
        lrc_free_data(&fetch->result);
        memset(&fetch->result, 0, sizeof(fetch->result));
        finalize_not_found(state);
    }

    rendering_manager_set_dirty(state);
}

// Discard an in-flight (or unconsumed) artwork fetch: abort, join, free inputs.
void lyrics_manager_cancel_art_fetch(struct lyrics_state *state) {
    struct async_art_fetch *af = &state->playback.async_art;
    if (!af->thread_active) return;

    af->should_cancel = true;
    pthread_join(af->thread, NULL);
    af->thread_active = false;
    af->ready = false;
    itunes_set_cancel_flag(NULL);
    system_tray_set_art_cancel_flag(NULL);
    free_art_fetch_inputs(af);
}

// Consume a completed artwork fetch (call every main-loop tick). Applies the
// resolved icon on the main thread and sends the deferred track notification.
void lyrics_manager_poll_art(struct lyrics_state *state) {
    struct async_art_fetch *af = &state->playback.async_art;
    if (!af->thread_active || !atomic_load(&af->ready)) {
        return;
    }

    pthread_join(af->thread, NULL);
    af->thread_active = false;
    af->ready = false;
    itunes_set_cancel_flag(NULL);
    system_tray_set_art_cancel_flag(NULL);

    if (af->found) {
        system_tray_apply_cached_icon(af->md5);
    } else {
        // iTunes had nothing either — fall back to the default icon.
        system_tray_reset_icon();
    }

    if (af->notify_pending) {
        struct notification_info notif_info = {
            .title = af->notif_title,
            .artist = af->notif_artist,
            .album = af->notif_album,
            .player_name = af->notif_player,
        };
        system_tray_send_notification(&notif_info);
    }

    free_art_fetch_inputs(af);
}

// Helper: Check if text is empty or whitespace-only
static bool is_whitespace_only(const char *text) {
    if (!text) {
        return true;
    }

    const char *p = text;
    while (*p) {
        if (!isspace(*p)) {
            return false;
        }
        p++;
    }
    return true;
}

// Helper: Calculate line index in linked list
static int calculate_line_index(const struct lyrics_data *lyrics, const struct lyrics_line *target) {
    if (!target) {
        return -1;
    }

    int index = 0;
    const struct lyrics_line *line = lyrics->lines;
    while (line && line != target) {
        index++;
        line = line->next;
    }

    return line ? index : -1;
}

// Helper: Handle line changed - update state, log, and set context lines
static void handle_line_changed(struct lyrics_state *state, struct lyrics_line *display_line,
                                struct lyrics_line *new_line, bool is_empty_text) {
    state->playback.current_line = display_line;
    state->playback.current_segment = NULL;

    // Calculate and store line index
    state->playback.current_line_index = calculate_line_index(&state->playback.lyrics, display_line);

    // Update prev/next lines for multi-line display (LRCX only)
    if (lyrics_manager_is_format(state, ".lrcx") && g_config.display.enable_multiline_lrcx) {
        struct lyrics_line *context_line = display_line ? display_line : new_line;
        lrcx_find_context_lines(&state->playback.lyrics, context_line,
                               &state->playback.prev_line, &state->playback.next_line);
    } else {
        state->playback.prev_line = NULL;
        state->playback.next_line = NULL;
    }

    // Log line change
    if (display_line && display_line->text) {
        int index = lrc_get_line_index(&state->playback.lyrics, display_line);
        char *escaped_text = state_helpers_escape_newlines(display_line->text);
        if (escaped_text) {
            log_info("Line %d/%d: %s", index + 1, state->playback.lyrics.line_count, escaped_text);
            free(escaped_text);
        } else {
            log_info("Line %d/%d: %s", index + 1, state->playback.lyrics.line_count, display_line->text);
        }

        // For karaoke (LRCX), set initial segment
        if (lyrics_manager_is_format(state, ".lrcx") && display_line->segments) {
            state->playback.current_segment = display_line->segments;
        }
    } else {
        log_info("Instrumental break - clearing lyrics (new_line=%p, is_empty_text=%d, display_line=%p)",
            (void*)new_line, is_empty_text, (void*)display_line);

        if (!state->playback.in_instrumental_break) {
            state->playback.in_instrumental_break = true;
        }
    }

    rendering_manager_set_dirty(state);
}

void lyrics_manager_update_current_line(struct lyrics_state *state) {
    if (!state->playback.lyrics.lines) {
        state->playback.in_instrumental_break = false;
        return;
    }

    // Get current playback position with timing offset applied
    int64_t position_us = mpris_get_position();
    position_us += (int64_t)state->playback.timing_offset_ms * 1000LL;

    // Find the appropriate line for current position
    struct lyrics_line *new_line = lrc_find_line_at_time(&state->playback.lyrics, position_us);

    // Check if we should clear the lyrics for SRT/WEBVTT formats
    if (new_line && new_line->end_timestamp_us > 0 &&
        (lyrics_manager_is_format(state, ".srt") || lyrics_manager_is_format(state, ".vtt")) &&
        position_us > new_line->end_timestamp_us) {
        new_line = NULL;
    }

    // Treat empty/whitespace-only text lines as NULL (no lyrics to display)
    bool is_empty_text = is_whitespace_only(new_line ? new_line->text : NULL);
    struct lyrics_line *display_line = is_empty_text ? NULL : new_line;

    // Handle line change
    if (display_line != state->playback.current_line) {
        handle_line_changed(state, display_line, new_line, is_empty_text);
    }

    // Clear instrumental break flag when lyrics are showing
    if (state->playback.current_line) {
        state->playback.in_instrumental_break = false;
    }

    // Update word segment for karaoke highlighting (LRCX only)
    if (lyrics_manager_is_format(state, ".lrcx") && new_line && new_line->segments) {
        struct word_segment *new_segment = lrcx_find_segment_at_time(new_line, position_us, NULL);
        if (new_segment != state->playback.current_segment) {
            state->playback.current_segment = new_segment;
            rendering_manager_set_dirty(state);
        }
    }
}
