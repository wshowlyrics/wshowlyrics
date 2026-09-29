// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _ITUNES_ARTWORK_H
#define _ITUNES_ARTWORK_H

#include <stdbool.h>
#include <stdatomic.h>

// Search for album artwork using iTunes Search API
// Returns artwork URL (caller must free), or NULL if not found
// Searches using artist, track, and album name (album is optional but improves accuracy)
char* itunes_search_artwork(const char *artist, const char *album, const char *track);

// Point the in-flight iTunes search request at a cancel flag (or NULL to clear).
// When *flag becomes true the request aborts promptly instead of waiting out the
// timeout. Set by the async artwork worker; a single search runs at a time.
void itunes_set_cancel_flag(_Atomic bool *flag);

#endif
