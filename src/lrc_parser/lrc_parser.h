#ifndef _LYRICS_LRC_PARSER_H
#define _LYRICS_LRC_PARSER_H

#include "../lyrics_types/lyrics_types.h"

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
