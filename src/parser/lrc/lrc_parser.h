#ifndef _LYRICS_LRC_PARSER_H
#define _LYRICS_LRC_PARSER_H

#include "../../lyrics_types.h"
#include "lrc_common.h"

// Parse LRC file
bool lrc_parse_file(const char *filename, struct lyrics_data *data);

// Parse LRC content from string
bool lrc_parse_string(const char *content, struct lyrics_data *data);

#endif
