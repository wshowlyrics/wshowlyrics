#include "srt_parser.h"
#include "../utils/parser_utils.h"
#include "../../constants.h"
#include "../../utils/string/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Create a lyrics line with ruby text parsing
static struct lyrics_line* create_srt_line(int64_t start_us, int64_t end_us, const char *text_buffer) {
    struct lyrics_line *new_line = calloc(1, sizeof(struct lyrics_line));
    if (!new_line) {
        return NULL;
    }

    new_line->timestamp_us = start_us;
    new_line->end_timestamp_us = end_us;

    // Parse ruby text (furigana) from the subtitle
    struct ruby_segment *segments = NULL;
    int seg_count = parse_ruby_segments(text_buffer, &segments);

    if (seg_count > 0 && segments) {
        new_line->ruby_segments = segments;
        new_line->segment_count = seg_count;

        // Normalize punctuation in all ruby segments
        normalize_ruby_segments(segments);

        // Build full text without ruby notation
        size_t full_len = 0;
        struct ruby_segment *seg = segments;
        while (seg) {
            full_len += strlen(seg->text);
            seg = seg->next;
        }

        char *full_text = malloc(full_len + 1);
        if (full_text) {
            char *pos = full_text;
            seg = segments;
            while (seg) {
                size_t len = strlen(seg->text);
                memcpy(pos, seg->text, len);
                pos += len;
                seg = seg->next;
            }
            *pos = '\0';
            new_line->text = full_text;
        } else {
            new_line->text = strdup(text_buffer);
        }
    } else {
        new_line->text = strdup(text_buffer);
        // Normalize if no segments were created
        if (new_line->text) {
            normalize_fullwidth_punctuation(new_line->text);
        }
    }
    // Note: If segments exist, text is built from already-normalized segments

    return new_line;
}

// Parse SRT/WEBVTT timestamp like 00:00:12,340 --> 00:00:15,120 or 00:00:12.340 --> 00:00:15.120
static bool parse_srt_timestamp(const char *str, int64_t *start_us, int64_t *end_us) {
    int h1 = 0;
    int m1 = 0;
    int s1 = 0;
    int ms1 = 0;
    int h2 = 0;
    int m2 = 0;
    int s2 = 0;
    int ms2 = 0;

    // Try SRT format first (with comma)
    int matched = sscanf(str, "%d:%d:%d,%d --> %d:%d:%d,%d",
                         &h1, &m1, &s1, &ms1, &h2, &m2, &s2, &ms2);

    // Try WEBVTT format (with dot)
    if (matched != 8) {
        matched = sscanf(str, "%d:%d:%d.%d --> %d:%d:%d.%d",
                         &h1, &m1, &s1, &ms1, &h2, &m2, &s2, &ms2);
    }

    if (matched == 8) {
        *start_us = timestamp_to_microseconds_hms(h1, m1, s1, ms1);
        *end_us = timestamp_to_microseconds_hms(h2, m2, s2, ms2);
        return true;
    }

    return false;
}

// Append text line to buffer with bounds checking
static void append_text_to_buffer(char *text_buffer, int *text_len, const char *line) {
    size_t remaining = CONTENT_BUFFER_SIZE - *text_len - 1;

    if (*text_len > 0 && remaining > 0) {
        // Add newline between lines
        text_buffer[(*text_len)++] = '\n';
        text_buffer[*text_len] = '\0';
        remaining--;
    }

    // Append line text if there's space
    size_t line_len = strlen(line);
    if (line_len > remaining) {
        log_warn("SRT subtitle line truncated (exceeds %zu bytes buffer limit)",
                 (size_t)CONTENT_BUFFER_SIZE);
        line_len = remaining;  // Truncate if needed
    }
    if (line_len > 0) {
        memcpy(text_buffer + *text_len, line, line_len);
        *text_len += line_len;
        text_buffer[*text_len] = '\0';
    }
}

// Helper: Check if line should be skipped (WEBVTT headers, empty lines, etc.)
static bool should_skip_line(const char *line) {
    if (*line == '\0') {
        return true;
    }

    // Skip WEBVTT headers
    if (strncmp(line, "WEBVTT", 6) == 0 ||
        strncmp(line, "Kind:", 5) == 0 ||
        strncmp(line, "NOTE", 4) == 0) {
        return true;
    }

    return false;
}

// Helper: Try to parse timestamp and transition state if successful
// Returns: true if timestamp was parsed and state should transition to TEXT
static bool try_parse_timestamp(const char *line, int64_t *start_us, int64_t *end_us) {
    if (!strstr(line, "-->")) {
        return false;
    }

    return parse_srt_timestamp(line, start_us, end_us);
}

// Helper: Create subtitle line and add to list
// Returns: true if line was created successfully
static bool create_and_add_subtitle(const char *text_buffer, int text_len,
                                    int64_t start_us, int64_t end_us,
                                    int64_t *last_timestamp_us,
                                    struct lyrics_line ***next_line_ptr,
                                    struct lyrics_data *data) {
    if (text_len <= 0) {
        return false;
    }

    // Validate timestamp order (warn if going backwards)
    validate_timestamp_order(start_us, last_timestamp_us, "SRT");
    *last_timestamp_us = start_us;

    // Create and append line
    struct lyrics_line *new_line = create_srt_line(start_us, end_us, text_buffer);
    if (!new_line) {
        return false;
    }

    **next_line_ptr = new_line;
    *next_line_ptr = &new_line->next;
    data->line_count++;
    return true;
}

bool srt_parse_string(const char *content, struct lyrics_data *data) {
    char *content_copy = NULL;
    if (!parse_init(content, data, &content_copy)) {
        return false;
    }

    struct lyrics_line **next_line = &data->lines;
    char *line = content_copy;
    char *next = NULL;

    enum {
        STATE_INDEX,
        STATE_TIMESTAMP,
        STATE_TEXT
    } state = STATE_INDEX;

    int64_t current_start_us = 0;
    int64_t current_end_us = 0;
    char text_buffer[CONTENT_BUFFER_SIZE] = {0};
    int text_len = 0;
    int64_t last_timestamp_us = -1; // Track last timestamp for validation

    while (line) {
        // Find next line
        next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        // Trim whitespace
        line = trim_whitespace(line);

        switch (state) {
        case STATE_INDEX:
            // Skip empty lines and WEBVTT headers
            if (should_skip_line(line)) {
                break;
            }

            // Check for timestamp directly (WEBVTT may not have index numbers)
            if (try_parse_timestamp(line, &current_start_us, &current_end_us)) {
                state = STATE_TEXT;
                text_len = 0;
                text_buffer[0] = '\0';
            }
            // Or expecting a number (SRT format)
            else if (*line && isdigit(*line)) {
                state = STATE_TIMESTAMP;
            }
            break;

        case STATE_TIMESTAMP:
            // Expecting timestamp
            if (try_parse_timestamp(line, &current_start_us, &current_end_us)) {
                state = STATE_TEXT;
                text_len = 0;
                text_buffer[0] = '\0';
            }
            break;

        case STATE_TEXT:
            // Collecting text until empty line
            if (*line == '\0') {
                // End of subtitle, create line
                create_and_add_subtitle(text_buffer, text_len,
                                       current_start_us, current_end_us,
                                       &last_timestamp_us, &next_line, data);
                state = STATE_INDEX;
            } else {
                // Append text with proper bounds checking
                append_text_to_buffer(text_buffer, &text_len, line);
            }
            break;
        }

        line = next;
    }

    // Handle last subtitle if file doesn't end with empty line
    if (state == STATE_TEXT) {
        create_and_add_subtitle(text_buffer, text_len,
                               current_start_us, current_end_us,
                               &last_timestamp_us, &next_line, data);
    }

    free(content_copy);
    return data->line_count > 0;
}

bool srt_parse_file(const char *filename, struct lyrics_data *data) {
    return parse_file_generic(filename, data, srt_parse_string);
}
