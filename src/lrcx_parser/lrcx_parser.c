#include "lrcx_parser.h"
#include "../parser_utils/parser_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Parse a line with word-level timestamps
// Example: [00:05.00][00:05.20]첫 [00:05.50]번 [00:05.80]째 [00:06.00]줄
static bool parse_lrcx_line(const char *line, struct lyrics_data *data, struct lyrics_line **line_ptr) {
	if (line[0] != '[') {
		return false;
	}

	// Parse the first timestamp (line timestamp)
	int64_t line_timestamp_us;
	const char *pos = line;
	if (!parse_lrc_timestamp(pos, &line_timestamp_us, &pos)) {
		return false;
	}

	// Create new lyrics line
	struct lyrics_line *new_line = calloc(1, sizeof(struct lyrics_line));
	if (!new_line) {
		return false;
	}

	// Apply offset to line timestamp
	new_line->timestamp_us = line_timestamp_us + (int64_t)data->metadata.offset_ms * 1000;

	// Build full text and parse word segments
	struct word_segment **next_segment = &new_line->segments;
	char *full_text = NULL;
	size_t full_text_len = 0;
	size_t full_text_capacity = 0;

	// Check if there's text immediately after first timestamp (before next '[')
	// This handles: [00:01.00]今は[00:02.00]... where 今は should be first segment
	const char *first_text_start = pos;
	while (*first_text_start && isspace(*first_text_start)) {
		first_text_start++;
	}

	if (*first_text_start && *first_text_start != '[') {
		// Found text before next timestamp - create first segment with line timestamp
		const char *first_text_end = first_text_start;
		while (*first_text_end && *first_text_end != '[') {
			first_text_end++;
		}

		// Trim trailing whitespace
		while (first_text_end > first_text_start && isspace(*(first_text_end - 1))) {
			first_text_end--;
		}

		if (first_text_end > first_text_start) {
			struct word_segment *segment = calloc(1, sizeof(struct word_segment));
			if (!segment) {
				free(new_line);
				return false;
			}

			segment->timestamp_us = line_timestamp_us + (int64_t)data->metadata.offset_ms * 1000;
			segment->text = strndup(first_text_start, first_text_end - first_text_start);

			// Initialize full text with first segment
			size_t word_len = first_text_end - first_text_start;
			full_text_capacity = word_len * 2 + 1;
			full_text = malloc(full_text_capacity);
			if (!full_text) {
				free(segment->text);
				free(segment);
				free(new_line);
				return false;
			}
			memcpy(full_text, first_text_start, word_len);
			full_text_len = word_len;
			full_text[full_text_len] = '\0';

			*next_segment = segment;
			next_segment = &segment->next;
			new_line->segment_count++;

			pos = first_text_end;
		}
	}

	while (*pos) {
		// Skip whitespace
		while (*pos && isspace(*pos)) {
			pos++;
		}

		if (*pos == '\0') {
			break;
		}

		// Check for timestamp
		if (*pos == '[') {
			int64_t segment_timestamp_us;
			const char *after_timestamp = NULL;
			if (parse_lrc_timestamp(pos, &segment_timestamp_us, &after_timestamp)) {
				// This is a word timestamp
				pos = after_timestamp;

				// Skip whitespace after timestamp
				while (*pos && isspace(*pos)) {
					pos++;
				}

				// Find end of word (next '[' or end of string)
				const char *word_start = pos;
				const char *word_end = pos;
				while (*word_end && *word_end != '[') {
					word_end++;
				}

				// Trim trailing whitespace
				while (word_end > word_start && isspace(*(word_end - 1))) {
					word_end--;
				}

				// Always create segment for every timestamp (even if empty for idle display)
				struct word_segment *segment = calloc(1, sizeof(struct word_segment));
				if (!segment) {
					free(full_text);
					free(new_line);
					return false;
				}

				segment->timestamp_us = segment_timestamp_us + (int64_t)data->metadata.offset_ms * 1000;

				if (word_end > word_start) {
					// Has text
					segment->text = strndup(word_start, word_end - word_start);

					// Add to full text
					size_t word_len = word_end - word_start;
					if (full_text_len + word_len + 1 > full_text_capacity) {
						full_text_capacity = (full_text_len + word_len + 1) * 2;
						char *new_full_text = realloc(full_text, full_text_capacity);
						if (!new_full_text) {
							free(segment->text);
							free(segment);
							free(full_text);
							free(new_line);
							return false;
						}
						full_text = new_full_text;
					}

					if (full_text_len > 0) {
						full_text[full_text_len++] = ' '; // Add space between words
					}
					memcpy(full_text + full_text_len, word_start, word_len);
					full_text_len += word_len;
					full_text[full_text_len] = '\0';
				} else {
					// Empty text for idle display (e.g., [00:01.00] or [00:01.00][00:01.20]...)
					segment->text = strdup("");
				}

				*next_segment = segment;
				next_segment = &segment->next;
				new_line->segment_count++;

				pos = word_end;
			} else {
				// Not a valid timestamp, skip this character
				pos++;
			}
		} else {
			// Text without timestamp - skip
			pos++;
		}
	}

	// Set the full line text
	if (full_text) {
		new_line->text = full_text;
	} else {
		new_line->text = strdup("");
	}

	// If no segments were parsed, create empty segment
	if (new_line->segment_count == 0) {
		struct word_segment *segment = calloc(1, sizeof(struct word_segment));
		if (!segment) {
			free(new_line->text);
			free(new_line);
			return false;
		}
		segment->timestamp_us = new_line->timestamp_us;
		segment->text = strdup("");
		new_line->segments = segment;
		new_line->segment_count = 1;
	}

	*line_ptr = new_line;
	return true;
}

bool lrcx_parse_string(const char *content, struct lyrics_data *data) {
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
		if (parse_lrc_metadata_tag(line, &data->metadata)) {
			line = strtok(NULL, "\n");
			continue;
		}

		// Try to parse as LRCX line
		struct lyrics_line *new_line = NULL;
		if (parse_lrcx_line(line, data, &new_line)) {
			*next_line = new_line;
			next_line = &new_line->next;
			data->line_count++;
		}

		line = strtok(NULL, "\n");
	}

	free(content_copy);
	return data->line_count > 0;
}

bool lrcx_parse_file(const char *filename, struct lyrics_data *data) {
	return parse_file_generic(filename, "LRCX", data, lrcx_parse_string);
}

struct word_segment* lrcx_find_segment_at_time(struct lyrics_line *line, int64_t timestamp_us, int *segment_index) {
	if (!line || !line->segments) {
		return NULL;
	}

	struct word_segment *current = NULL;
	struct word_segment *segment = line->segments;
	int index = 0;

	while (segment) {
		if (segment->timestamp_us > timestamp_us) {
			break;
		}
		current = segment;
		if (segment_index && segment == current) {
			*segment_index = index;
		}
		segment = segment->next;
		index++;
	}

	return current;
}
