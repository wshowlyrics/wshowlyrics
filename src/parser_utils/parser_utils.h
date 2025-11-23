#ifndef PARSER_UTILS_H
#define PARSER_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include "../lyrics_provider/lyrics_provider.h"

// Parse timestamp in [MM:SS.xx] format
// Supports both centiseconds (2 digits) and milliseconds (3 digits)
// If end_ptr is provided, it will point to the character after ']'
bool parse_lrc_timestamp(const char *str, int64_t *timestamp_us, const char **end_ptr);

// Parse LRC metadata tags ([ti:], [ar:], [al:], [offset:])
bool parse_lrc_metadata_tag(const char *line, struct lyrics_metadata *metadata);

// Read entire file into string, then call parser function
// parser_func should parse the string and populate data
bool parse_file_generic(const char *filename, const char *format_name,
                        struct lyrics_data *data,
                        bool (*parser_func)(const char *, struct lyrics_data *));

#endif // PARSER_UTILS_H
