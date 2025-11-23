#include "lrc_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Parse LRC timestamp like [00:12.34] or [00:12.340]
static bool parse_timestamp(const char *str, int64_t *timestamp_us) {
	int minutes = 0, seconds = 0, centiseconds = 0;

	// Try [MM:SS.xx] format
	int matched = sscanf(str, "[%d:%d.%d]", &minutes, &seconds, &centiseconds);
	if (matched == 3) {
		// Handle both centiseconds (2 digits) and milliseconds (3 digits)
		int len = 0;
		const char *dot = strchr(str, '.');
		if (dot) {
			const char *bracket = strchr(dot, ']');
			if (bracket) {
				len = bracket - dot - 1;
			}
		}

		if (len == 2) {
			// Centiseconds
			*timestamp_us = (int64_t)minutes * 60 * 1000000 +
			                (int64_t)seconds * 1000000 +
			                (int64_t)centiseconds * 10000;
		} else {
			// Milliseconds
			*timestamp_us = (int64_t)minutes * 60 * 1000000 +
			                (int64_t)seconds * 1000000 +
			                (int64_t)centiseconds * 1000;
		}
		return true;
	}

	return false;
}

// Parse metadata tag like [ti:Title]
static bool parse_metadata_tag(const char *line, struct lyrics_metadata *metadata) {
	if (strncmp(line, "[ti:", 4) == 0) {
		const char *end = strchr(line + 4, ']');
		if (end) {
			size_t len = end - (line + 4);
			free(metadata->title);
			metadata->title = strndup(line + 4, len);
			return true;
		}
	} else if (strncmp(line, "[ar:", 4) == 0) {
		const char *end = strchr(line + 4, ']');
		if (end) {
			size_t len = end - (line + 4);
			free(metadata->artist);
			metadata->artist = strndup(line + 4, len);
			return true;
		}
	} else if (strncmp(line, "[al:", 4) == 0) {
		const char *end = strchr(line + 4, ']');
		if (end) {
			size_t len = end - (line + 4);
			free(metadata->album);
			metadata->album = strndup(line + 4, len);
			return true;
		}
	} else if (strncmp(line, "[offset:", 8) == 0) {
		const char *end = strchr(line + 8, ']');
		if (end) {
			metadata->offset_ms = atoi(line + 8);
			return true;
		}
	}

	return false;
}

bool lrc_parse_string(const char *content, struct lyrics_data *data) {
	if (!content || !data) {
		return false;
	}

	memset(data, 0, sizeof(struct lyrics_data));

	char *content_copy = strdup(content);
	if (!content_copy) {
		return false;
	}

	struct lyrics_line **next_line = &data->lines;
	char *line = strtok(content_copy, "\n");

	while (line) {
		// Skip empty lines
		while (*line && isspace(*line)) {
			line++;
		}

		if (*line == '\0') {
			line = strtok(NULL, "\n");
			continue;
		}

		// Try to parse as metadata
		if (parse_metadata_tag(line, &data->metadata)) {
			line = strtok(NULL, "\n");
			continue;
		}

		// Try to parse as timed line
		if (line[0] == '[') {
			int64_t timestamp_us;
			if (parse_timestamp(line, &timestamp_us)) {
				// Find the end of timestamp(s)
				const char *text_start = line;
				while (*text_start == '[') {
					text_start = strchr(text_start, ']');
					if (!text_start) {
						break;
					}
					text_start++;
				}

				// Create new line even if text is empty or whitespace-only
				// This allows instrumental breaks to be represented
				struct lyrics_line *new_line = calloc(1, sizeof(struct lyrics_line));
				if (!new_line) {
					free(content_copy);
					lrc_free_data(data);
					return false;
				}

				// Apply offset
				new_line->timestamp_us = timestamp_us + (int64_t)data->metadata.offset_ms * 1000;

				// If text is empty or only whitespace, use empty string
				if (text_start && *text_start) {
					// Check if text is only whitespace
					const char *check = text_start;
					bool only_whitespace = true;
					while (*check) {
						if (!isspace(*check)) {
							only_whitespace = false;
							break;
						}
						check++;
					}

					// Check if text contains URL pattern (://)
					// This filters out Spotify URIs and other URLs from being displayed
					bool has_url = strstr(text_start, "://") != NULL;

					if (only_whitespace || has_url) {
						new_line->text = strdup("");
					} else {
						new_line->text = strdup(text_start);
					}
				} else {
					new_line->text = strdup("");
				}

				*next_line = new_line;
				next_line = &new_line->next;
				data->line_count++;
			}
		}

		line = strtok(NULL, "\n");
	}

	free(content_copy);
	return data->line_count > 0;
}

bool lrc_parse_file(const char *filename, struct lyrics_data *data) {
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		fprintf(stderr, "Failed to open LRC file: %s\n", filename);
		return false;
	}

	// Read entire file into memory
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *content = malloc(size + 1);
	if (!content) {
		fclose(fp);
		return false;
	}

	size_t read = fread(content, 1, size, fp);
	content[read] = '\0';
	fclose(fp);

	bool result = lrc_parse_string(content, data);
	free(content);

	return result;
}

void lrc_free_data(struct lyrics_data *data) {
	if (!data) {
		return;
	}

	free(data->metadata.title);
	free(data->metadata.artist);
	free(data->metadata.album);

	struct lyrics_line *line = data->lines;
	while (line) {
		struct lyrics_line *next = line->next;
		free(line->text);

		// Free word segments if present (LRCX format)
		struct word_segment *segment = line->segments;
		while (segment) {
			struct word_segment *next_segment = segment->next;
			free(segment->text);
			free(segment);
			segment = next_segment;
		}

		free(line);
		line = next;
	}

	memset(data, 0, sizeof(struct lyrics_data));
}

struct lyrics_line* lrc_find_line_at_time(struct lyrics_data *data, int64_t timestamp_us) {
	if (!data || !data->lines) {
		return NULL;
	}

	struct lyrics_line *current = NULL;
	struct lyrics_line *line = data->lines;

	while (line) {
		if (line->timestamp_us > timestamp_us) {
			break;
		}
		current = line;
		line = line->next;
	}

	return current;
}

int lrc_get_line_index(struct lyrics_data *data, struct lyrics_line *target) {
	if (!data || !target) {
		return -1;
	}

	int index = 0;
	struct lyrics_line *line = data->lines;

	while (line) {
		if (line == target) {
			return index;
		}
		line = line->next;
		index++;
	}

	return -1;
}
