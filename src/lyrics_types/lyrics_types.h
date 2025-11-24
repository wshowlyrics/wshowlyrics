#ifndef _LYRICS_TYPES_H
#define _LYRICS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// Ruby segment for furigana (ruby text) support in LRC/SRT formats
// No timing information - used only for displaying ruby text above base text
struct ruby_segment {
    char *text; // The word/text segment (main text, base text for ruby)
    char *ruby; // Ruby text (furigana) to display above main text (NULL if none)
    struct ruby_segment *next;
};

// Word segment with timing for karaoke features in LRCX format
// Extends ruby_segment with timing information for progressive fill effects
struct word_segment {
    int64_t timestamp_us; // When this word should start highlighting
    int64_t end_timestamp_us; // When this word should finish highlighting (0 if calculated from next segment)
    char *text; // The word/text segment (main text, base text for ruby)
    char *ruby; // Ruby text (furigana) to display above main text (NULL if none)
    bool is_unfill; // True if this segment should unfill (reverse fill from 100% to 0%)
    struct word_segment *next;
};

struct lyrics_line {
    int64_t timestamp_us; // Timestamp in microseconds
    int64_t end_timestamp_us; // End timestamp in microseconds (0 if not specified, e.g., LRC format)
    char *text; // Full line text (for display)

    // Format-specific segment data (only one will be set)
    struct ruby_segment *ruby_segments; // Ruby text segments for LRC/SRT (NULL if not used)
    struct word_segment *segments; // Word-level timing for LRCX karaoke (NULL if not used)

    int segment_count; // Number of segments (ruby or word)
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
    char *source_file_path; // Path to the loaded lyrics file (for reload detection)
    char md5_checksum[33]; // MD5 checksum of the file (32 hex chars + null terminator)
};

#endif // _LYRICS_TYPES_H
