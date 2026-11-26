// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "provider/lrclib/lrclib_response.h"
#include "utils/json/json_utils.h"

// Fuzz the reading of HTTP response bodies from lrclib.net and the iTunes
// Search API: the hand-written JSON field extraction (json_utils.c) and the
// walk over a search result array (lrclib_response.c). The bytes come from the
// network, so a hostile or broken server must not be able to crash the reader.

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;
    // The code under test reports each search result with log_info (stdout);
    // at fuzzing speed that would bury the fuzzer's own output.
    if (!freopen("/dev/null", "w", stdout)) {
        return 0;
    }
    return 0;
}

static void extract_string(const char *json, const char *key) {
    free(json_extract_string(json, key));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 65536) return 0;

    char *input = malloc(size + 1);
    if (!input) return 0;
    memcpy(input, data, size);
    input[size] = '\0';

    // The fields the providers ask for, anywhere in the body
    // (lrclib /api/get, iTunes search)
    extract_string(input, "syncedLyrics");
    extract_string(input, "plainLyrics");
    extract_string(input, "artistName");
    extract_string(input, "albumName");
    extract_string(input, "trackName");
    extract_string(input, "resultCount");
    extract_string(input, "artworkUrl100");
    (void)json_extract_int(input, "duration");

    // lrclib /api/search: the caller only passes on a body that starts with '['
    if (size >= 2 && input[0] == '[') {
        // Track length unknown, then known (200 s)
        const int64_t targets[] = { 0, 200000 };
        for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
            struct lrclib_match match = lrclib_find_best_match(input, targets[i]);
            if (match.obj_start) {
                // What extract_metadata_from_result reads from the chosen result
                extract_string(match.obj_start, "artistName");
                extract_string(match.obj_start, "albumName");
                extract_string(match.obj_start, "trackName");
            }
            free(match.synced_lyrics);
        }
    }

    free(input);
    return 0;
}
