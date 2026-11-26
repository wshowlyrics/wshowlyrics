// SPDX-License-Identifier: GPL-3.0-or-later
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
    int length_ms; // [length:mm:ss] - length of the track the lyrics were fetched for
                   // (written into cached lyrics files); 0 if not given
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

// The settings a lyrics search reads, copied out of the config before the
// search starts. The search runs on a worker thread, and the main thread frees
// and reloads the config whenever settings.ini is saved - so the worker must
// never read the config itself (#67).
struct lyrics_search_options {
    char *extensions;    // [lyrics] extensions (empty: local search off)
    char *search_dirs;   // [lyrics] search_dirs (empty: the default directories)
    char *ignore_dirs;   // [lyrics] ignore_dirs
    bool enable_lrclib;  // [lyrics] enable_lrclib
};

// Async lyrics fetch — the (blocking, network, file system) provider search
// runs on a worker thread so it never stalls the main poll() loop. Each search
// is a job the worker fills in private; the main thread holds only the job it
// is waiting for, and drops it - without waiting for the worker - when the
// track changes (see lyrics_manager.c).
struct lyrics_fetch_job;
struct async_lyrics_fetch {
    struct lyrics_fetch_job *job;  // the search whose result is awaited, or NULL
};

// Async album-art fetch — every step of the tray artwork fallback that can
// block (art URL download, ffmpeg extraction, iTunes) runs on a worker thread
// that resolves the art into {md5}.png. Only the cache lookup stays on the main
// thread. The main thread applies the resulting icon (GTK is not thread-safe)
// and sends the track notification once the art lands (or fails), and drops
// the lookup - without waiting for the worker - when the track changes.
struct art_fetch_job;  // lyrics_manager.c

struct async_art_fetch {
    struct art_fetch_job *job;   // the lookup whose result is awaited, or NULL
};

// Playback state - currently playing track, current/prev/next lines, timing
struct playback_state {
    struct lyrics_data lyrics;
    struct track_metadata current_track;
    struct async_lyrics_fetch async_fetch;  // off-main-thread lyrics search
    struct async_art_fetch async_art;        // off-main-thread tray artwork
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
