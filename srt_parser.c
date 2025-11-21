#include "srt_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Parse SRT timestamp like 00:00:12,340 --> 00:00:15,120
static bool parse_srt_timestamp(const char *str, int64_t *start_us, int64_t *end_us) {
	int h1, m1, s1, ms1, h2, m2, s2, ms2;

	int matched = sscanf(str, "%d:%d:%d,%d --> %d:%d:%d,%d",
	                     &h1, &m1, &s1, &ms1, &h2, &m2, &s2, &ms2);

	if (matched == 8) {
		*start_us = (int64_t)h1 * 3600 * 1000000 +
		            (int64_t)m1 * 60 * 1000000 +
		            (int64_t)s1 * 1000000 +
		            (int64_t)ms1 * 1000;

		*end_us = (int64_t)h2 * 3600 * 1000000 +
		          (int64_t)m2 * 60 * 1000000 +
		          (int64_t)s2 * 1000000 +
		          (int64_t)ms2 * 1000;

		return true;
	}

	return false;
}

bool srt_parse_string(const char *content, struct lyrics_data *data) {
	if (!content || !data) {
		return false;
	}

	memset(data, 0, sizeof(struct lyrics_data));

	char *content_copy = strdup(content);
	if (!content_copy) {
		return false;
	}

	struct lyrics_line **next_line = &data->lines;
	char *line = content_copy;
	char *next = NULL;

	enum {
		STATE_INDEX,
		STATE_TIMESTAMP,
		STATE_TEXT
	} state = STATE_INDEX;

	int64_t current_start_us = 0;
	int64_t current_end_us = 0;
	char text_buffer[4096] = {0};
	int text_len = 0;

	while (line) {
		// Find next line
		next = strchr(line, '\n');
		if (next) {
			*next = '\0';
			next++;
		}

		// Trim whitespace
		while (*line && isspace(*line)) {
			line++;
		}

		// Remove trailing whitespace
		char *end = line + strlen(line) - 1;
		while (end > line && isspace(*end)) {
			*end = '\0';
			end--;
		}

		switch (state) {
		case STATE_INDEX:
			// Expecting a number
			if (*line && isdigit(*line)) {
				state = STATE_TIMESTAMP;
			}
			break;

		case STATE_TIMESTAMP:
			// Expecting timestamp
			if (strstr(line, "-->")) {
				if (parse_srt_timestamp(line, &current_start_us, &current_end_us)) {
					state = STATE_TEXT;
					text_len = 0;
					text_buffer[0] = '\0';
				}
			}
			break;

		case STATE_TEXT:
			// Collecting text until empty line
			if (*line == '\0') {
				// End of subtitle, create line
				if (text_len > 0) {
					struct lyrics_line *new_line = calloc(1, sizeof(struct lyrics_line));
					if (new_line) {
						new_line->timestamp_us = current_start_us;
						new_line->end_timestamp_us = current_end_us;
						new_line->text = strdup(text_buffer);

						*next_line = new_line;
						next_line = &new_line->next;
						data->line_count++;
					}
				}
				state = STATE_INDEX;
			} else {
				// Append text
				if (text_len > 0) {
					// Add space between lines
					strncat(text_buffer, " ", sizeof(text_buffer) - text_len - 1);
					text_len++;
				}
				strncat(text_buffer, line, sizeof(text_buffer) - text_len - 1);
				text_len = strlen(text_buffer);
			}
			break;
		}

		line = next;
	}

	// Handle last subtitle if file doesn't end with empty line
	if (state == STATE_TEXT && text_len > 0) {
		struct lyrics_line *new_line = calloc(1, sizeof(struct lyrics_line));
		if (new_line) {
			new_line->timestamp_us = current_start_us;
			new_line->end_timestamp_us = current_end_us;
			new_line->text = strdup(text_buffer);

			*next_line = new_line;
			next_line = &new_line->next;
			data->line_count++;
		}
	}

	free(content_copy);
	return data->line_count > 0;
}

bool srt_parse_file(const char *filename, struct lyrics_data *data) {
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		fprintf(stderr, "Failed to open SRT file: %s\n", filename);
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

	bool result = srt_parse_string(content, data);
	free(content);

	return result;
}
