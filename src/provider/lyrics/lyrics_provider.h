// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _LYRICS_PROVIDER_H
#define _LYRICS_PROVIDER_H

#include "../../parser/lrc/lrc_parser.h"
#include "../../utils/mpris/mpris.h"
#include <stdbool.h>

// Main thread. Copy the current settings into options (free with _free).
void lyrics_search_options_capture(struct lyrics_search_options *options);

// Free what _capture copied and zero the struct.
void lyrics_search_options_free(struct lyrics_search_options *options);

// Abstract interface for lyrics providers
struct lyrics_provider {
    const char *name;

    // Search for lyrics
    // url: optional file path/URL for local file search optimization
    // duration_ms: track duration in milliseconds (0 if unknown)
    bool (*search)(const char *title, const char *artist, const char *album,
                   const char *url, int64_t duration_ms, struct lyrics_data *data);

    // Initialize provider
    bool (*init)(void);

    // Cleanup provider
    void (*cleanup)(void);

    // Check if provider is enabled for this search (NULL means always enabled)
    bool (*is_enabled)(const struct lyrics_search_options *options);
};

// Concrete implementations
extern struct lyrics_provider local_provider;

// High-level API: Find lyrics for a track, with the settings in options.
// Blocking (local files, then the network); safe to run on a worker thread -
// it reads nothing of the config.
bool lyrics_find_for_track(struct track_metadata *track, struct lyrics_data *data,
                           const struct lyrics_search_options *options);

// Start translation of the given lyrics with the configured provider.
// Call on the LIVE lyrics from the main thread after a fetch is swapped in;
// lyrics_find_for_track no longer starts translation itself.
void lyrics_provider_translate(struct lyrics_data *data, int64_t track_length_us);

// Initialize all providers
void lyrics_providers_init(void);

// Cleanup all providers
void lyrics_providers_cleanup(void);

#endif
