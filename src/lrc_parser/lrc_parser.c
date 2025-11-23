#include "lrc_parser.h"
#include "../parser_utils/parser_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
		if (parse_lrc_metadata_tag(line, &data->metadata)) {
			line = strtok(NULL, "\n");
			continue;
		}

		// Try to parse as timed line
		if (line[0] == '[') {
			int64_t timestamp_us;
			if (parse_lrc_timestamp(line, &timestamp_us, NULL)) {
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
						// Parse ruby text from the line into segments
						// This allows ruby text to be rendered even in non-karaoke mode
						struct word_segment *segments = NULL;
						int seg_count = parse_ruby_segments(text_start, new_line->timestamp_us, &segments);

						if (seg_count > 0 && segments) {
							new_line->segments = segments;
							new_line->segment_count = seg_count;

							// Build full text without ruby notation for display
							size_t text_len = 0;
							struct word_segment *seg = segments;
							while (seg) {
								text_len += strlen(seg->text);
								seg = seg->next;
							}

							char *full_text = malloc(text_len + 1);
							if (full_text) {
								full_text[0] = '\0';
								seg = segments;
								while (seg) {
									strcat(full_text, seg->text);
									seg = seg->next;
								}
								new_line->text = full_text;
							} else {
								new_line->text = strdup("");
							}
						} else {
							// No segments created (error case) - use original text
							new_line->text = strdup(text_start);
						}
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
	return parse_file_generic(filename, "LRC", data, lrc_parse_string);
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
			free(segment->ruby);
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
