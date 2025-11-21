#ifndef _LYRICS_SRT_PARSER_H
#define _LYRICS_SRT_PARSER_H

#include "lrc_parser.h"

// Parse SRT file (reuses lyrics_data structure from LRC parser)
bool srt_parse_file(const char *filename, struct lyrics_data *data);

// Parse SRT content from string
bool srt_parse_string(const char *content, struct lyrics_data *data);

#endif
