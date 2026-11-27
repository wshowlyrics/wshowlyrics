// SPDX-License-Identifier: GPL-3.0-or-later
#include "lrclib_response.h"
#include "../../utils/json/json_utils.h"
#include "../../constants.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A result claiming to be longer than this is not compared (it also keeps the
// arithmetic below far from overflow)
#define MAX_RESULT_DURATION_S 86400

// How far a result of result_duration_s is from a track of target_duration_ms.
// INT64_MAX if the two cannot be compared: either length is unknown, or the
// result's is absurd.
static int64_t duration_diff_ms(int64_t result_duration_s, int64_t target_duration_ms) {
    if (target_duration_ms <= 0 || result_duration_s <= 0 ||
        result_duration_s > MAX_RESULT_DURATION_S) {
        return INT64_MAX;
    }
    return llabs((result_duration_s * 1000) - target_duration_ms);
}

struct lrclib_match lrclib_find_best_match(const char *response_data,
                                           int64_t target_duration_ms) {
    struct lrclib_match best = { .duration_diff = INT64_MAX };

    // One result object after another. Each is read within its own braces
    // (json_object_end steps over braces inside the lyrics), so a field one
    // result lacks is never taken from the next.
    const char *obj_start = strchr(response_data, '{');
    const char *obj_end = json_object_end(obj_start);
    while (obj_end) {
        const int64_t result_duration_s = json_extract_int_in(obj_start, obj_end, "duration");
        const int64_t diff = duration_diff_ms(result_duration_s, target_duration_ms);
        // With the track's length known, the closest result so far; a result
        // that states no length cannot be checked against it and never wins.
        // With it unknown, the first result that has lyrics.
        const bool candidate = target_duration_ms > 0 ? diff < best.duration_diff
                                                      : !best.synced_lyrics;

        // Only results with synced lyrics count (a result with plainLyrics
        // alone is common for J-POP and instrumental tracks)
        char *synced_lyrics = candidate
            ? json_extract_string_in(obj_start, obj_end, "syncedLyrics") : NULL;
        if (synced_lyrics && synced_lyrics[0] != '\0') {
            if (target_duration_ms > 0) {
                log_info("Found result with duration %ld s (diff: %ld ms)",
                         result_duration_s, diff);
            } else {
                log_info("Found result (no duration comparison)");
            }
            free(best.synced_lyrics);
            best.synced_lyrics = synced_lyrics;
            best.obj_start = obj_start;
            best.obj_end = obj_end;
            best.duration_diff = diff;
            synced_lyrics = NULL;
        }
        free(synced_lyrics);

        obj_start = strchr(obj_end + 1, '{');
        obj_end = json_object_end(obj_start);
    }

    return best;
}
