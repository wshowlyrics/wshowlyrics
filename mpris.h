#ifndef _LYRICS_MPRIS_H
#define _LYRICS_MPRIS_H

#include <stdbool.h>
#include <stdint.h>

struct track_metadata {
	char *title;
	char *artist;
	char *album;
	char *url; // File path or URL
	char *art_url; // Album art URL (mpris:artUrl)
	int64_t length_us; // Length in microseconds
	int64_t position_us; // Current position in microseconds
};

// Initialize MPRIS connection
bool mpris_init(void);

// Get current track metadata
bool mpris_get_metadata(struct track_metadata *metadata);

// Get current playback position in microseconds
int64_t mpris_get_position(void);

// Check if music is currently playing
bool mpris_is_playing(void);

// Free track metadata
void mpris_free_metadata(struct track_metadata *metadata);

// Cleanup MPRIS resources
void mpris_cleanup(void);

#endif
