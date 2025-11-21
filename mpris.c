#include "mpris.h"
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
	char buffer[1024];
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

	// Get title
	metadata->title = execute_command("playerctl metadata title 2>/dev/null");
	if (!metadata->title) {
		return false;
	}

	// Get artist
	metadata->artist = execute_command("playerctl metadata artist 2>/dev/null");

	// Get album
	metadata->album = execute_command("playerctl metadata album 2>/dev/null");

	// Get URL/file path
	metadata->url = execute_command("playerctl metadata xesam:url 2>/dev/null");

	// Get length in microseconds
	char *length_str = execute_command("playerctl metadata mpris:length 2>/dev/null");
	if (length_str) {
		metadata->length_us = atoll(length_str);
		free(length_str);
	}

	// Get position in microseconds
	char *position_str = execute_command("playerctl position 2>/dev/null");
	if (position_str) {
		// Position is in seconds (float), convert to microseconds
		double position_sec = atof(position_str);
		metadata->position_us = (int64_t)(position_sec * 1000000);
		free(position_str);
	}

	return true;
}

int64_t mpris_get_position(void) {
	// Use metadata format to get position in microseconds
	char *position_str = execute_command("playerctl metadata -f '{{position}}' 2>/dev/null");
	if (!position_str) {
		return 0;
	}

	// Position from metadata is in microseconds
	int64_t position_us = atoll(position_str);
	free(position_str);
	return position_us;
}

bool mpris_is_playing(void) {
	char *status = execute_command("playerctl status 2>/dev/null");
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
	memset(metadata, 0, sizeof(struct track_metadata));
}

void mpris_cleanup(void) {
	// Nothing to cleanup in this simple implementation
}
