// SPDX-License-Identifier: GPL-3.0-or-later
#include "lrclib_response.h"
#include "../../utils/json/json_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A result claiming to be longer than this is not compared (it also keeps the
// arithmetic below far from overflow)
#define MAX_RESULT_DURATION_S 86400

struct lrclib_match lrclib_find_best_match(
    const char *response_data, int64_t target_duration_ms) {

    struct lrclib_match result = {NULL, NULL, INT64_MAX};
    const char *search_pos = response_data + 1; // Skip opening '['

    const char *obj_start = strchr(search_pos, '{');
    while (obj_start) {
        // Find object end early for loop advancement
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;
        search_pos = obj_end + 1;

        // Extract duration and syncedLyrics for this result
        int64_t result_duration = json_extract_int_from(response_data, "duration", obj_start);
        char *synced_lyrics = json_extract_string(obj_start, "syncedLyrics");

        // Skip entries without valid synced lyrics. Advance to the next object
        // first — a bare `continue` would leave obj_start pinned to this entry
        // and spin the loop forever (100% CPU) whenever a result carries only
        // plainLyrics, which is common for J-POP / instrumental tracks.
        if (!synced_lyrics || synced_lyrics[0] == '\0') {
            free(synced_lyrics);
            obj_start = strchr(search_pos, '{');
            continue;
        }

        if (target_duration_ms > 0 && result_duration > 0 &&
            result_duration <= MAX_RESULT_DURATION_S) {
            // We have both durations - compare
            int64_t duration_diff = llabs((result_duration * 1000) - target_duration_ms);
            log_info("Found result with duration %ld s (diff: %ld ms)",
                   result_duration, duration_diff);

            if (duration_diff < result.duration_diff) {
                result.duration_diff = duration_diff;
                free(result.synced_lyrics);
                result.synced_lyrics = synced_lyrics;
                result.obj_start = obj_start;
                synced_lyrics = NULL; // Prevent free below
            }
        } else if (target_duration_ms <= 0 && !result.synced_lyrics) {
            // The track's length is unknown: take the first result. (When it
            // is known, a result that states no length cannot be checked
            // against it and is passed over.)
            result.synced_lyrics = synced_lyrics;
            result.obj_start = obj_start;
            synced_lyrics = NULL; // Prevent free below
            log_info("Found result (no duration comparison)");
        }

        free(synced_lyrics);
        obj_start = strchr(search_pos, '{');
    }

    return result;
}

