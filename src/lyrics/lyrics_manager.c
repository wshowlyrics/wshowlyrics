// SPDX-License-Identifier: GPL-3.0-or-later
#include "lyrics_manager.h"
#include "../user_experience/config/config.h"
#include "../core/state/state_helpers.h"
#include "../core/rendering/rendering_manager.h"
#include "../utils/mpris/mpris.h"
#include "../provider/lyrics/lyrics_provider.h"
#include "../translator/common/translator_common.h"
#include "../user_experience/system_tray/system_tray.h"
#include "../parser/lrc/lrc_common.h"
#include "../utils/file/file_utils.h"
#include "../utils/thread/thread_cancel.h"
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

// A join slower than this is logged: the translation thread is expected to
// notice the cancel flag within a sleep slice or one curl progress callback
#define TRANSLATION_JOIN_WARN_MS 1000

// Cancel ongoing translation and wait for it to finish
// Prevents use-after-free errors when freeing lyrics data
static void cancel_and_wait_translation(struct lyrics_data *lyrics) {
    if (!lyrics->translation_thread_active) return;

    lyrics->translation_should_cancel = true;

    // Plain join, no pthread_cancel fallback. The thread spends its time inside
    // libcurl/OpenSSL, which do not support being cancelled: killing it there
    // can leave their internal locks held and stall the next request, and it
    // skips the thread's own cleanup and cache save. Its waits and requests all
    // watch the cancel flag and are bounded by timeouts (see
    // translator_sleep_cancellable / curl_install_thread_cancel), so it exits
    // on its own; a slow exit is reported instead of papered over.
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pthread_join(lyrics->translation_thread, NULL);
    clock_gettime(CLOCK_MONOTONIC, &end);

    const long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000L +
                            (end.tv_nsec - start.tv_nsec) / 1000000L;
    if (elapsed_ms > TRANSLATION_JOIN_WARN_MS) {
        log_warn("Translation thread took %ld ms to stop after cancel", elapsed_ms);
    }

    lyrics->translation_thread_active = false;
}

// Stop everything still running in the background for the current track: the
// lyrics search, the artwork fetch and the translation. One call so that a path
// which ends a track (track change, player gone, shutdown) cannot stop some of
// them and forget another.
void lyrics_manager_cancel_track_work(struct lyrics_state *state) {
    lyrics_manager_cancel_fetch(state);
    lyrics_manager_cancel_art_fetch(state);
    cancel_and_wait_translation(&state->playback.lyrics);
}

// Helper: Handle case when no player is found
static void handle_no_player_found(struct lyrics_state *state) {
    if (!state->playback.current_track.title) {
        return;  // Nothing to clear
    }

    log_info("=== No player found, clearing lyrics ===");

    // Drop a lyrics search or artwork fetch still running for the track that
    // just went away. Left alone, its result would be installed once it
    // finishes: lyrics for a track nobody is playing, a notification with an
    // empty title, an iTunes lookup and - with translation on - API calls.
    lyrics_manager_cancel_track_work(state);

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

// Stop any running translation, free the currently displayed lyrics, and clear
// the line cursors. After this the live lyrics is empty, so the render loop
// draws nothing until lyrics_manager_poll_load installs new ones.
static void clear_live_lyrics(struct lyrics_state *state) {
    // Cancel ongoing translation and wait for it to finish. This prevents a race
    // where old and new translation threads write to the same cache file.
    cancel_and_wait_translation(&state->playback.lyrics);

    lrc_free_data(&state->playback.lyrics);
    state->playback.current_line = NULL;
    state->playback.prev_line = NULL;
    state->playback.next_line = NULL;
}

// Reset per-track state before loading new lyrics: the lyrics themselves, and
// what the user adjusted for the previous track.
static void reset_for_new_track(struct lyrics_state *state) {
    clear_live_lyrics(state);

    // Reset timing offset to global offset for new track
    state->playback.timing_offset_ms = g_config.lyrics.global_offset_ms;

    // Reset overlay visibility for new track
    if (!state->runtime.overlay_enabled) {
        state->runtime.overlay_enabled = true;
        system_tray_set_overlay_state(true);
        log_info("Overlay auto-enabled for new track");
    }
}

// --- Async album-art offload ---------------------------------------------

// Free the artwork fetch's job.
static void clear_art_fetch(struct async_art_fetch *af) {
    system_tray_art_free(af->job);
    af->job = NULL;
}

// Send the track-change desktop notification (main thread).
static void send_track_notification(const struct lyrics_state *state, const char *artist,
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

// Send the track notification that was held back for the artwork job.
static void send_art_job_notification(const struct lyrics_state *state) {
    const struct tray_art_request *request =
        system_tray_art_request(state->playback.async_art.job);
    send_track_notification(state, request->artist, request->album, request->track);
}

// Artwork worker: resolve the job's art into the cache PNG. Touches only the
// job, the network and ffmpeg — never GTK/AppIndicator or live state.
static void *art_fetch_worker(void *arg) {
    struct lyrics_state *state = arg;
    struct async_art_fetch *af = &state->playback.async_art;

    thread_cancel_bind(&af->should_cancel);
    system_tray_art_run(af->job);

    atomic_store(&af->ready, true);
    return NULL;
}

// Start the artwork worker on a prepared job (owned from here on). The track
// notification waits until the art lands (or fails) — see poll_art. A track
// change cancels the fetch first, so current_track is still this job's track
// whenever the notification is sent.
static void begin_art_fetch(struct lyrics_state *state, struct tray_art_job *job) {
    struct async_art_fetch *af = &state->playback.async_art;
    af->job = job;
    af->should_cancel = false;
    af->ready = false;
    if (pthread_create(&af->thread, NULL, art_fetch_worker, state) != 0) {
        log_warn("Failed to start artwork worker; using default icon");
        system_tray_reset_icon();
        send_art_job_notification(state);
        clear_art_fetch(af);
        return;
    }
    af->thread_active = true;
}

// Resolve album art from the cache on the main thread, then either send the
// track notification now, or — when the art still has to be downloaded or
// extracted — start the artwork worker and defer the notification until it lands.
static void finalize_common(struct lyrics_state *state, const char *art_url,
                            const char *file_url, const char *artist,
                            const char *album, const char *title) {
    lyrics_manager_cancel_art_fetch(state);   // supersede any prior art fetch

    const struct tray_art_request request = {
        .art_url = art_url,
        .file_url = file_url,
        .artist = artist,
        .album = album,
        .track = title,
        .length_us = state->playback.current_track.length_us,
    };
    struct tray_art_job *job = system_tray_art_prepare(&request);
    if (job) {
        begin_art_fetch(state, job);
        return;   // notification is sent once the art lands (poll_art)
    }
    send_track_notification(state, artist, album, title);
}

// Post-processing when no lyrics were found: album art + notification, unless
// same_track. Runs on the main thread (touches GTK), never on the fetch worker.
static void finalize_not_found(struct lyrics_state *state, bool same_track) {
    log_info("No lyrics found for current track");
    if (same_track) {
        return;
    }
    finalize_common(state,
                    state->playback.current_track.art_url,
                    state->playback.current_track.url,
                    state->playback.current_track.artist,
                    state->playback.current_track.album,
                    state->playback.current_track.title);
}

// Album art and notification for a track whose lyrics were just installed.
static void announce_track(struct lyrics_state *state) {
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
}

// Post-processing when lyrics are installed in the live state: album art,
// notification, and starting translation. Runs on the main thread against the
// LIVE lyrics — starting translation here (not inside the provider chain) is
// what makes the fetch safe to run on a worker thread. same_track skips the art
// and the notification (see lyrics_manager_begin_reload).
static void finalize_found(struct lyrics_state *state, bool same_track) {
    state->playback.lyrics.translation_should_cancel = false;

    log_info("Loaded %d lines of lyrics", state->playback.lyrics.line_count);

    // Set initial line to NULL so first update will trigger line_changed
    // This ensures prev/next lines are set for multiline display
    state->playback.current_line = NULL;
    state->playback.track_changed = false;

    if (!same_track) {
        announce_track(state);
    }

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

    thread_cancel_bind(&fetch->should_cancel);
    fetch->found = lyrics_find_for_track(&fetch->track, &fetch->result);

    fetch->in_progress = false;
    atomic_store(&fetch->ready, true);
    return NULL;
}

// Snapshot the track and kick off the fetch worker for it. The live lyrics must
// already be empty. Non-blocking; the result is consumed later by
// lyrics_manager_poll_load, which treats it as same_track says.
static void start_fetch(struct lyrics_state *state, bool same_track) {
    struct async_lyrics_fetch *fetch = &state->playback.async_fetch;
    memset(&fetch->result, 0, sizeof(fetch->result));
    copy_track_metadata(&fetch->track, &state->playback.current_track);
    fetch->same_track = same_track;
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
            finalize_found(state, same_track);
        } else {
            finalize_not_found(state, same_track);
        }
        return;
    }
    fetch->thread_active = true;
}

// Begin an asynchronous load for a new track: reset per-track state (so the
// overlay clears immediately) and search off the main thread.
void lyrics_manager_begin_load(struct lyrics_state *state) {
    lyrics_manager_cancel_track_work(state);  // supersede the previous track's work
    reset_for_new_track(state);
    start_fetch(state, false);
}

// Search again for the track already playing, off the main thread: its lyrics
// file changed or disappeared, or the translation settings changed. What
// belongs to the track rather than to its lyrics is left alone: the timing
// offset and overlay state the user set, the artwork (and its fetch, if still
// running), and the notification, which is not sent a second time.
void lyrics_manager_begin_reload(struct lyrics_state *state) {
    lyrics_manager_cancel_fetch(state);
    clear_live_lyrics(state);
    start_fetch(state, true);
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

    bool found = fetch->found;
    mpris_free_metadata(&fetch->track);
    memset(&fetch->track, 0, sizeof(fetch->track));

    if (found) {
        // The live lyrics were freed when the fetch began, so this move-assign
        // does not leak; zero the source so we never double-free the buffer.
        state->playback.lyrics = fetch->result;
        memset(&fetch->result, 0, sizeof(fetch->result));
        finalize_found(state, fetch->same_track);
    } else {
        lrc_free_data(&fetch->result);
        memset(&fetch->result, 0, sizeof(fetch->result));
        finalize_not_found(state, fetch->same_track);
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
    clear_art_fetch(af);
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

    system_tray_art_apply(af->job);
    send_art_job_notification(state);

    clear_art_fetch(af);
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
