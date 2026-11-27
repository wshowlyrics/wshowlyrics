// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LRCLIB_RESPONSE_H
#define LRCLIB_RESPONSE_H

#include <stdint.h>

// Reading what lrclib.net answers, apart from fetching it: this file has no
// libcurl in it, so the fuzz target (fuzz/fuzz_lrclib.c) can link it alone.
// The text comes from the network; treat every byte as hostile.

// The search result chosen by lrclib_find_best_match.
struct lrclib_match {
    char *synced_lyrics;    // owned (free); NULL if no result could be used
    const char *obj_start;  // the result's JSON object, inside the response:
    const char *obj_end;    // its opening and its closing brace
    int64_t duration_diff;  // |result length - track length| in ms; INT64_MAX
                            // if the lengths were not compared
};

// From a /api/search response (a JSON array, NUL-terminated, starting with
// '['), pick the result whose length is closest to the track's. A result with
// no synced lyrics is passed over, and so is one that states no length when
// the track's is known (target_duration_ms > 0). Whether the closest one is
// close enough is the caller's call.
struct lrclib_match lrclib_find_best_match(const char *response_data,
                                           int64_t target_duration_ms);

#endif // LRCLIB_RESPONSE_H
