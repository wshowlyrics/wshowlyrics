#ifndef _LYRICS_TYPES_H
#define _LYRICS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include "constants.h"
#include "utils/mpris/mpris.h"

// Ruby segment for furigana (ruby text) support in LRC/SRT formats
// No timing information - used only for displaying ruby text above base text
struct ruby_segment {
    char *text; // The word/text segment (main text, base text for ruby)
    char *ruby; // Ruby text (furigana) to display above main text (NULL if none)
    char *translation; // Translated text to display below main text (NULL if none)
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
    char *translation; // Translated line text (NULL if not translated)
    int translation_retry_count; // R15: cross-session retry count for failed translations.
                                 // Persisted via the cache "retry_counts" sparse dict.
                                 // When >= cfg.translation.max_retries, this line is treated
                                 // as permanently un-translatable and not re-attempted.

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
    char md5_checksum[MD5_DIGEST_STRING_LENGTH]; // MD5 checksum of the file

    // Translation progress (for async translation)
    _Atomic bool translation_in_progress;
    _Atomic bool translation_should_cancel;  // Set to true to cancel ongoing translation
    _Atomic bool translation_will_discard;   // True if translation will be discarded (below threshold)
    _Atomic bool translation_thread_active;  // True after pthread_create, false after join
    pthread_t translation_thread;    // Thread handle for cancellation
    _Atomic int translation_current;
    _Atomic int translation_total;
};

// Playback state - currently playing track, current/prev/next lines, timing
struct playback_state {
    struct lyrics_data lyrics;
    struct track_metadata current_track;
    struct lyrics_line *current_line;
    int current_line_index; // 0-based index of current_line in lyrics.lines (-1 if no current line)
    struct word_segment *current_segment; // For karaoke highlighting (LRCX)
    struct lyrics_line *prev_line;  // Previous line for multi-line display
    struct lyrics_line *next_line;  // Next line for multi-line display
    int64_t track_start_time_us;    // When the track started (monotonic clock)
    bool track_changed;
    bool in_instrumental_break;     // True when in instrumental break (no lyrics)
    int timing_offset_ms;           // Runtime timing offset (-1000 to +1000 ms)
};

#endif // _LYRICS_TYPES_H
