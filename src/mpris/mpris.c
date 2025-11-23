#include "mpris.h"
#include "../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Simple implementation using playerctl command
// For a full implementation, we would use D-Bus directly

bool mpris_init(void) {
	// Check if playerctl is available
	int ret = system("which playerctl > /dev/null 2>&1");
	return ret == 0;
}

static char* execute_command(const char *cmd) {
	FILE *fp = popen(cmd, "r");
	if (!fp) {
		return NULL;
	}

	char *result = NULL;
	char buffer[PATH_BUFFER_SIZE];
	size_t total_size = 0;

	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		size_t len = strlen(buffer);
		// Remove trailing newline
		if (len > 0 && buffer[len - 1] == '\n') {
			buffer[len - 1] = '\0';
			len--;
		}

		char *new_result = realloc(result, total_size + len + 1);
		if (!new_result) {
			free(result);
			pclose(fp);
			return NULL;
		}
		result = new_result;

		if (total_size == 0) {
			result[0] = '\0';
		}
		strcat(result, buffer);
		total_size += len;
	}

	pclose(fp);
	return result;
}

bool mpris_get_metadata(struct track_metadata *metadata) {
	if (!metadata) {
		return false;
	}

	memset(metadata, 0, sizeof(struct track_metadata));

	// Get all metadata in a single command to ensure consistency
	// Format: title|artist|album|url|artUrl|length
	char *result = execute_command(
		"playerctl --player=%any metadata --format "
		"'{{title}}|||{{artist}}|||{{album}}|||{{xesam:url}}|||{{mpris:artUrl}}|||{{mpris:length}}' 2>/dev/null"
	);

	if (!result) {
		return false;
	}

	// Parse the delimited result: title|||artist|||album|||url|||artUrl|||length
	// Note: fields can be empty, so we need to carefully split by delimiter
	char *title_start = result;

	char *artist_start = strstr(title_start, "|||");
	if (!artist_start) {
		free(result);
		return false;
	}
	*artist_start = '\0';
	artist_start += 3;

	char *album_start = strstr(artist_start, "|||");
	if (!album_start) {
		free(result);
		return false;
	}
	*album_start = '\0';
	album_start += 3;

	char *url_start = strstr(album_start, "|||");
	if (!url_start) {
		free(result);
		return false;
	}
	*url_start = '\0';
	url_start += 3;

	char *art_url_start = strstr(url_start, "|||");
	if (!art_url_start) {
		free(result);
		return false;
	}
	*art_url_start = '\0';
	art_url_start += 3;

	char *length_start = strstr(art_url_start, "|||");
	if (!length_start) {
		free(result);
		return false;
	}
	*length_start = '\0';
	length_start += 3;

	// Copy the values
	metadata->title = strdup(title_start);
	if (strlen(artist_start) > 0) {
		metadata->artist = strdup(artist_start);
	}
	if (strlen(album_start) > 0) {
		metadata->album = strdup(album_start);
	}
	if (strlen(url_start) > 0) {
		metadata->url = strdup(url_start);
	}
	if (strlen(art_url_start) > 0) {
		metadata->art_url = strdup(art_url_start);
	}
	if (strlen(length_start) > 0) {
		metadata->length_us = atoll(length_start);
	}

	// Get position separately (not available in metadata format)
	char *position_str = execute_command("playerctl --player=%any position 2>/dev/null");
	if (position_str) {
		double position_sec = atof(position_str);
		metadata->position_us = (int64_t)(position_sec * 1000000);
		free(position_str);
	}

	free(result);
	return metadata->title != NULL;
}

int64_t mpris_get_position(void) {
	// Use metadata format to get position in microseconds
	char *position_str = execute_command("playerctl --player=%any metadata -f '{{position}}' 2>/dev/null");
	if (!position_str) {
		return 0;
	}

	// Position from metadata is in microseconds
	int64_t position_us = atoll(position_str);
	free(position_str);
	return position_us;
}

bool mpris_is_playing(void) {
	char *status = execute_command("playerctl --player=%any status 2>/dev/null");
	if (!status) {
		return false;
	}

	bool playing = (strcmp(status, "Playing") == 0);
	free(status);
	return playing;
}

void mpris_free_metadata(struct track_metadata *metadata) {
	if (!metadata) {
		return;
	}

	free(metadata->title);
	free(metadata->artist);
	free(metadata->album);
	free(metadata->url);
	free(metadata->art_url);
	memset(metadata, 0, sizeof(struct track_metadata));
}

void mpris_cleanup(void) {
	// Nothing to cleanup in this simple implementation
}
