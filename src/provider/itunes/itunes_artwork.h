#ifndef _ITUNES_ARTWORK_H
#define _ITUNES_ARTWORK_H

#include <stdbool.h>

// Search for album artwork using iTunes Search API
// Returns artwork URL (caller must free), or NULL if not found
// Searches using artist and track name
char* itunes_search_artwork(const char *artist, const char *track);

#endif
