#include "parser_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool parse_lrc_timestamp(const char *str, int64_t *timestamp_us, const char **end_ptr) {
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
				if (end_ptr) {
					*end_ptr = bracket + 1; // Point to character after ']'
				}
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

// Helper to parse string metadata tag
static bool parse_string_tag(const char *line, const char *tag, size_t tag_len,
                             char **target_field) {
	if (strncmp(line, tag, tag_len) != 0) {
		return false;
	}

	const char *end = strchr(line + tag_len, ']');
	if (!end) {
		return false;
	}

	size_t len = end - (line + tag_len);
	free(*target_field);
	*target_field = strndup(line + tag_len, len);
	return true;
}

bool parse_lrc_metadata_tag(const char *line, struct lyrics_metadata *metadata) {
	// Table-driven metadata parsing
	struct {
		const char *tag;
		size_t tag_len;
		char **field;
	} string_tags[] = {
		{"[ti:", 4, &metadata->title},
		{"[ar:", 4, &metadata->artist},
		{"[al:", 4, &metadata->album},
		{NULL, 0, NULL}
	};

	// Try string tags
	for (int i = 0; string_tags[i].tag != NULL; i++) {
		if (parse_string_tag(line, string_tags[i].tag, string_tags[i].tag_len,
		                     string_tags[i].field)) {
			return true;
		}
	}

	// Special case: offset (integer value)
	if (strncmp(line, "[offset:", 8) == 0) {
		const char *end = strchr(line + 8, ']');
		if (end) {
			metadata->offset_ms = atoi(line + 8);
			return true;
		}
	}

	return false;
}

bool parse_file_generic(const char *filename, const char *format_name,
                        struct lyrics_data *data,
                        bool (*parser_func)(const char *, struct lyrics_data *)) {
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		fprintf(stderr, "Failed to open %s file: %s\n", format_name, filename);
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

	bool result = parser_func(content, data);
	free(content);

	return result;
}
