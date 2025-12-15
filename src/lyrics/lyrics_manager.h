#ifndef LYRICS_MANAGER_H
#define LYRICS_MANAGER_H

#include "../main.h"
#include <stdbool.h>

/**
 * Check if current lyrics file matches a specific format
 *
 * @param state Lyrics state
 * @param extension File extension to check (e.g., ".lrcx", ".lrc", ".srt")
 * @return true if current lyrics matches the format, false otherwise
 */
bool lyrics_manager_is_format(struct lyrics_state *state, const char *extension);

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
 * Load lyrics for current track
 * Searches local files and online sources
 *
 * @param state Lyrics state
 * @return true if lyrics loaded, false if not found
 */
bool lyrics_manager_load_lyrics(struct lyrics_state *state);

/**
 * Update current line based on playback position
 * Updates current_line, prev_line, next_line pointers
 *
 * @param state Lyrics state
 */
void lyrics_manager_update_current_line(struct lyrics_state *state);

#endif // LYRICS_MANAGER_H
