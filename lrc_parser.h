#ifndef _LYRICS_LRC_PARSER_H
#define _LYRICS_LRC_PARSER_H

#include <stdint.h>
#include <stdbool.h>

// Word segment with timestamp for karaoke highlighting (LRCX format)
struct word_segment {
	int64_t timestamp_us; // When this word should start highlighting
	char *text; // The word/text segment
	struct word_segment *next;
};

struct lyrics_line {
	int64_t timestamp_us; // Timestamp in microseconds
	int64_t end_timestamp_us; // End timestamp in microseconds (0 if not specified, e.g., LRC format)
	char *text; // Full line text (for non-karaoke display)
	struct word_segment *segments; // Word-level timing for karaoke (NULL if not LRCX)
	int segment_count; // Number of word segments (0 if not LRCX)
	struct lyrics_line *next;
};

struct lyrics_metadata {
	char *title;
	char *artist;
	char *album;
	int offset_ms; // Offset in milliseconds
};

struct lyrics_data {
	struct lyrics_metadata metadata;
	struct lyrics_line *lines;
	int line_count;
};

// Parse LRC file
bool lrc_parse_file(const char *filename, struct lyrics_data *data);

// Parse LRC content from string
bool lrc_parse_string(const char *content, struct lyrics_data *data);

// Free lyrics data
void lrc_free_data(struct lyrics_data *data);

// Find the line that should be displayed at the given timestamp
struct lyrics_line* lrc_find_line_at_time(struct lyrics_data *data, int64_t timestamp_us);

// Get the index of a line
int lrc_get_line_index(struct lyrics_data *data, struct lyrics_line *line);

#endif
