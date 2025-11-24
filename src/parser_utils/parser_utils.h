#ifndef PARSER_UTILS_H
#define PARSER_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include "../lyrics_types/lyrics_types.h"

// Parse timestamp in [MM:SS.xx] or [<MM:SS.xx] format
// Supports both centiseconds (2 digits) and milliseconds (3 digits)
// If end_ptr is provided, it will point to the character after ']'
// If is_unfill is provided, it will be set to true for [<...] timestamps
bool parse_lrc_timestamp(const char *str, int64_t *timestamp_us, const char **end_ptr);
bool parse_lrc_timestamp_ex(const char *str, int64_t *timestamp_us, const char **end_ptr, bool *is_unfill);

// Parse LRC metadata tags ([ti:], [ar:], [al:], [offset:])
bool parse_lrc_metadata_tag(const char *line, struct lyrics_metadata *metadata);

// Read entire file into string, then call parser function
// parser_func should parse the string and populate data
bool parse_file_generic(const char *filename, const char *format_name,
                        struct lyrics_data *data,
                        bool (*parser_func)(const char *, struct lyrics_data *));

// Parse ruby text (furigana) from text
// Syntax: base{ruby} - e.g., 心{こころ}
// Returns: base text (without ruby markers), sets *ruby_text to ruby content (or NULL)
// Caller must free returned string and *ruby_text
// Example: parse_ruby_text("心{こころ}", &ruby) returns "心" and sets ruby to "こころ"
char* parse_ruby_text(const char *text, char **ruby_text);

// Split text with ruby annotations into ruby segments (for LRC/SRT)
// Example: "目指せよ　快眠{かいみん}" becomes 2 segments:
//   1. text="目指せよ　", ruby=NULL
//   2. text="快眠", ruby="かいみん"
// Returns number of segments created, or 0 on error
// No timestamp information - used for furigana display only
int parse_ruby_segments(const char *text, struct ruby_segment **segments);

// Split text with ruby annotations into karaoke segments (for LRCX)
// Same as parse_ruby_segments but includes timestamp information for karaoke features
// Returns number of segments created, or 0 on error
int parse_karaoke_segments(const char *text, int64_t timestamp_us, struct word_segment **segments);

// ============================================================================
// Common parser utilities
// ============================================================================

// Initialize parsing - validates inputs and prepares lyrics_data structure
// Returns duplicated content string in content_copy_out (caller must free)
// Returns false if validation fails
bool parse_init(const char *content, struct lyrics_data *data, char **content_copy_out);

// Free all ruby segments in a linked list
void free_ruby_segments(struct ruby_segment *segments);

// Free all word segments in a linked list
void free_word_segments(struct word_segment *segments);

// Check if text contains only whitespace characters
bool is_text_only_whitespace(const char *text);

// Apply offset to timestamp (inline for performance)
static inline int64_t apply_timestamp_offset(int64_t timestamp_us, int offset_ms) {
    return timestamp_us + (int64_t)offset_ms * 1000;
}

#endif // PARSER_UTILS_H
