#ifndef _LYRICS_PROVIDER_H
#define _LYRICS_PROVIDER_H

#include "lrc_parser.h"
#include "mpris.h"
#include <stdbool.h>

// Abstract interface for lyrics providers
struct lyrics_provider {
	const char *name;

	// Search for lyrics
	// url: optional file path/URL for local file search optimization
	bool (*search)(const char *title, const char *artist, const char *album,
	               const char *url, struct lyrics_data *data);

	// Initialize provider
	bool (*init)(void);

	// Cleanup provider
	void (*cleanup)(void);
};

// Concrete implementations
extern struct lyrics_provider lrclib_provider;
extern struct lyrics_provider local_provider;

// High-level API: Find lyrics for current track
// Tries local files first, then falls back to online providers
bool lyrics_find_for_track(struct track_metadata *track, struct lyrics_data *data);

// Initialize all providers
void lyrics_providers_init(void);

// Cleanup all providers
void lyrics_providers_cleanup(void);

#endif
