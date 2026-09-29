// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LYRICS_MANAGER_H
#define LYRICS_MANAGER_H

#include "../main.h"
#include "../lyrics_types.h"
#include <stdbool.h>

/**
 * Check if current lyrics file matches a specific format
 *
 * @param state Lyrics state
 * @param extension File extension to check (e.g., ".lrcx", ".lrc", ".srt")
 * @return true if current lyrics matches the format, false otherwise
 */
bool lyrics_manager_is_format(const struct lyrics_state *state, const char *extension);

/**
 * Clean track title (remove file extensions, YouTube IDs)
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param title Original title (may be NULL)
 */
void lyrics_manager_clean_title(char *dest, size_t dest_size, const char *title);

/**
 * Update track information from MPRIS
 * Checks if the currently playing track has changed
 *
 * @param state Lyrics state
 * @return true if track changed, false otherwise
 */
bool lyrics_manager_update_track_info(struct lyrics_state *state);

/**
 * Load lyrics for current track synchronously
 * Searches local files and online sources, blocking until done. Used by the
 * local-file hot-reload path; track changes use the async begin/poll pair below.
 *
 * @param state Lyrics state
 * @return true if lyrics loaded, false if not found
 */
bool lyrics_manager_load_lyrics(struct lyrics_state *state);

/**
 * Begin an asynchronous lyrics load for the current track.
 * Clears the current lyrics immediately and runs the (blocking, network) search
 * on a worker thread. Non-blocking; call lyrics_manager_poll_load each tick to
 * install the result once ready.
 *
 * @param state Lyrics state
 */
void lyrics_manager_begin_load(struct lyrics_state *state);

/**
 * Consume a completed asynchronous lyrics load, if any.
 * Swaps the fetched lyrics into the live state and runs album-art/notification/
 * translation finalization on the main thread. No-op while the fetch is running.
 *
 * @param state Lyrics state
 */
void lyrics_manager_poll_load(struct lyrics_state *state);

/**
 * Cancel and discard any in-flight or unconsumed async lyrics fetch.
 * Call on track change (implicitly done by begin_load) and at shutdown.
 *
 * @param state Lyrics state
 */
void lyrics_manager_cancel_fetch(struct lyrics_state *state);

/**
 * Consume a completed asynchronous artwork (iTunes) fetch, if any.
 * Applies the resolved tray icon and sends the deferred track notification on
 * the main thread. No-op while the artwork worker is running.
 *
 * @param state Lyrics state
 */
void lyrics_manager_poll_art(struct lyrics_state *state);

/**
 * Cancel and discard any in-flight or unconsumed async artwork fetch.
 * Call on track change (implicitly done by begin_load) and at shutdown.
 *
 * @param state Lyrics state
 */
void lyrics_manager_cancel_art_fetch(struct lyrics_state *state);

/**
 * Update current line based on playback position
 * Updates current_line, prev_line, next_line pointers
 *
 * @param state Lyrics state
 */
void lyrics_manager_update_current_line(struct lyrics_state *state);

/**
 * Cancel ongoing translation and wait for thread to finish
 * Prevents use-after-free errors when freeing lyrics data
 *
 * @param lyrics Lyrics data containing translation state
 */
void cancel_and_wait_translation(struct lyrics_data *lyrics);

#endif // LYRICS_MANAGER_H
