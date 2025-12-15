#ifndef _LYRICS_LRC_COMMON_H
#define _LYRICS_LRC_COMMON_H

#include "../../lyrics_types.h"
#include <stdbool.h>
#include <stdint.h>

// Free lyrics data (shared by LRC and LRCX)
void lrc_free_data(struct lyrics_data *data);

// Find the line that should be displayed at the given timestamp
struct lyrics_line* lrc_find_line_at_time(struct lyrics_data *data, int64_t timestamp_us);

// Get the index of a line
int lrc_get_line_index(struct lyrics_data *data, struct lyrics_line *line);

#endif // _LYRICS_LRC_COMMON_H
