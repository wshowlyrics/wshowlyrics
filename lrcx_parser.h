#ifndef _LYRICS_LRCX_PARSER_H
#define _LYRICS_LRCX_PARSER_H

#include "lrc_parser.h"
#include <stdbool.h>

// Parse LRCX file (karaoke-style with word-level timing)
// Uses the same lyrics_data structure as LRC, but populates word_segment fields
bool lrcx_parse_file(const char *filename, struct lyrics_data *data);

// Parse LRCX content from string
bool lrcx_parse_string(const char *content, struct lyrics_data *data);

// Find the word segment that should be highlighted at the given timestamp
// Returns the segment and sets *segment_index if not NULL
struct word_segment* lrcx_find_segment_at_time(struct lyrics_line *line, int64_t timestamp_us, int *segment_index);

#endif
