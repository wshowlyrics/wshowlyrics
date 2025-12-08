#ifndef _ITUNES_ARTWORK_H
#define _ITUNES_ARTWORK_H

#include <stdbool.h>

// Search for album artwork using iTunes Search API
// Returns artwork URL (caller must free), or NULL if not found
// Searches using artist, track, and album name (album is optional but improves accuracy)
char* itunes_search_artwork(const char *artist, const char *album, const char *track);

#endif
