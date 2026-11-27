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
#include "../utils/thread/worker_handoff.h"
#include "../parser/lrc/lrcx_parser.h"
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

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
    // thread_cancel_sleep_ms / curl_install_thread_cancel), so it exits
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
// which ends a track (track change, player gone) cannot stop some of them and
// forget another. The lyrics search and the artwork lookup are dropped at once;
// only the translation is waited for.
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

// One artwork lookup. Run by a detached worker the main thread never joins:
// the lookup resolves the music file and runs ffmpeg on it, and that file may
// be on a mount that stopped answering (#66). See worker_handoff.h for who
// frees the job.
struct art_fetch_job {
    struct worker_handoff handoff;
    struct tray_art_job *art;     // what to resolve, and the outcome; owned
};

static void art_fetch_job_free(struct art_fetch_job *job) {
    system_tray_art_free(job->art);
    free(job);
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

// Send the track notification that was held back for an artwork lookup.
static void send_art_notification(const struct lyrics_state *state,
                                  const struct tray_art_job *art) {
    const struct tray_art_request *request = system_tray_art_request(art);
    send_track_notification(state, request->artist, request->album, request->track);
}

// Artwork worker: resolve the job's art into the cache PNG. Touches only the
// job, the network and ffmpeg — never GTK/AppIndicator or live state.
static void *art_fetch_worker(void *arg) {
    struct art_fetch_job *job = arg;

    worker_handoff_enter(&job->handoff, "art-fetch");
    system_tray_art_run(job->art);

    if (!worker_handoff_finish(&job->handoff)) {
        log_info("An artwork lookup that was dropped has ended");
        art_fetch_job_free(job);
    }
    return NULL;
}

// Start the artwork worker on a prepared lookup (owned from here on). The
// track notification waits until the art lands (or fails) — see poll_art. A
// track change drops the lookup first, so current_track is still this
// lookup's track whenever the notification is sent.
static void begin_art_fetch(struct lyrics_state *state, struct tray_art_job *art) {
    struct art_fetch_job *job = calloc(1, sizeof(*job));
    if (job) {
        job->art = art;
        if (worker_handoff_start(&job->handoff, art_fetch_worker, job)) {
            state->playback.async_art.job = job;
            return;
        }
    }

    log_warn("Failed to start artwork worker; using default icon");
    system_tray_reset_icon();
    send_art_notification(state, art);
    system_tray_art_free(art);
    free(job);
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
    struct tray_art_job *art = system_tray_art_prepare(&request);
    if (art) {
        begin_art_fetch(state, art);
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

// --- Async lyrics fetch ---------------------------------------------------

// One lyrics search. Run by a detached worker the main thread never joins:
// the search reads the music file's directory, which may be on a mount that
// stopped answering (#68). See worker_handoff.h for who frees the job.
struct lyrics_fetch_job {
    struct worker_handoff handoff;
    bool found;                   // worker outcome, valid once the worker is done
    bool same_track;              // search repeated for the track already playing:
                                  // install the lyrics, but no artwork or notification
    struct lyrics_data result;    // filled by the worker
    struct track_metadata track;  // deep-copied track snapshot the worker fetches for
    struct lyrics_search_options options;  // the settings the worker searches with
};

static void fetch_job_free(struct lyrics_fetch_job *job) {
    lrc_free_data(&job->result);
    mpris_free_metadata(&job->track);
    lyrics_search_options_free(&job->options);
    free(job);
}

// Drop the fetch the main thread is waiting for, if any: ask it to stop and
// leave it behind. Never blocks. A worker that is still running frees the job
// itself when it returns; its result is never looked at.
void lyrics_manager_cancel_fetch(struct lyrics_state *state) {
    struct lyrics_fetch_job *job = state->playback.async_fetch.job;
    if (!job) return;
    state->playback.async_fetch.job = NULL;

    if (!worker_handoff_abandon(&job->handoff)) {
        fetch_job_free(job);   // the worker had already finished
    }
}

// Fetch worker: runs the blocking provider search into its job. Touches only
// the job - never the live lyrics, GTK, the renderer or the config.
static void *lyrics_fetch_worker(void *arg) {
    struct lyrics_fetch_job *job = arg;

    worker_handoff_enter(&job->handoff, "lyrics-fetch");
    job->found = lyrics_find_for_track(&job->track, &job->result, &job->options);

    if (!worker_handoff_finish(&job->handoff)) {
        log_info("A lyrics search that was dropped has ended");
        fetch_job_free(job);
    }
    return NULL;
}

// Snapshot the track and kick off a fetch worker for it. The live lyrics must
// already be empty and the previous fetch dropped. Non-blocking; the result is
// consumed later by lyrics_manager_poll_load, which treats it as same_track says.
static void start_fetch(struct lyrics_state *state, bool same_track) {
    struct lyrics_fetch_job *job = calloc(1, sizeof(*job));
    if (!job) {
        log_error("Out of memory; lyrics search skipped");
        finalize_not_found(state, same_track);
        return;
    }
    copy_track_metadata(&job->track, &state->playback.current_track);
    // Copied here, on the main thread: the worker never reads the config
    lyrics_search_options_capture(&job->options);
    job->same_track = same_track;

    if (worker_handoff_start(&job->handoff, lyrics_fetch_worker, job)) {
        state->playback.async_fetch.job = job;
        return;
    }

    // No search on the main thread instead: it could block there for as long
    // as the track's file system takes, which is what the worker is for
    log_warn("Failed to start the lyrics search thread; no lyrics for this track");
    fetch_job_free(job);
    finalize_not_found(state, same_track);
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

// Consume a completed async fetch (call every main-loop tick). Moves the fetched
// result into the live lyrics and runs the main-thread finalize steps. No-op
// while the worker is still running.
void lyrics_manager_poll_load(struct lyrics_state *state) {
    struct lyrics_fetch_job *job = state->playback.async_fetch.job;
    if (!job || !worker_handoff_done(&job->handoff)) {
        return;
    }
    state->playback.async_fetch.job = NULL;

    const bool found = job->found;
    const bool same_track = job->same_track;
    if (found) {
        // The live lyrics were freed when the fetch began, so this move-assign
        // does not leak; zero the source so the job does not free it too.
        state->playback.lyrics = job->result;
        memset(&job->result, 0, sizeof(job->result));
    }
    fetch_job_free(job);

    if (found) {
        finalize_found(state, same_track);
    } else {
        finalize_not_found(state, same_track);
    }

    rendering_manager_set_dirty(state);
}

// How long shutdown waits for dropped workers to return. A cancelled request
// aborts within a second; a worker that takes longer is blocked in a file
// system call and will not come back until its mount does.
#define WORKER_EXIT_WAIT_MS 2000

// Stop the track's background work and give the detached workers - dropped
// ones too - a moment to return, so library teardown does not run under a
// request that is about to end anyway. Does not wait for one stuck on a mount.
void lyrics_manager_shutdown(struct lyrics_state *state) {
    lyrics_manager_cancel_track_work(state);

    if (!worker_handoff_wait_all(WORKER_EXIT_WAIT_MS)) {
        log_warn("A worker is still waiting on a file system that does not answer; exiting without it");
    }
}

// Drop the artwork lookup the main thread is waiting for, if any: ask it to
// stop and leave it behind. Never blocks. A worker that is still running frees
// the job itself when it returns; its icon and notification never appear.
void lyrics_manager_cancel_art_fetch(struct lyrics_state *state) {
    struct art_fetch_job *job = state->playback.async_art.job;
    if (!job) return;
    state->playback.async_art.job = NULL;

    if (!worker_handoff_abandon(&job->handoff)) {
        art_fetch_job_free(job);   // the worker had already finished
    }
}

// Consume a completed artwork lookup (call every main-loop tick). Applies the
// resolved icon on the main thread and sends the deferred track notification.
void lyrics_manager_poll_art(struct lyrics_state *state) {
    struct art_fetch_job *job = state->playback.async_art.job;
    if (!job || !worker_handoff_done(&job->handoff)) {
        return;
    }
    state->playback.async_art.job = NULL;

    system_tray_art_apply(job->art);
    send_art_notification(state, job->art);
    art_fetch_job_free(job);
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
