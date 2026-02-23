#include "lrc_parser.h"
#include "lrc_common.h"
#include "../utils/parser_utils.h"
#include "../../constants.h"
#include "../../utils/string/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Extract text after timestamp tags
static char *extract_lyrics_text(char *line) {
    char *text_start = line;
    while (*text_start == '[') {
        text_start = strchr(text_start, ']');
        if (!text_start) {
            return NULL;
        }
        text_start++;
    }
    return trim_whitespace(text_start);
}

// Build full text from ruby segments
static char *build_text_from_segments(struct ruby_segment *segments) {
    // Calculate total length
    size_t text_len = 0;
    struct ruby_segment *seg = segments;
    while (seg) {
        text_len += strlen(seg->text);
        seg = seg->next;
    }

    // Allocate and build text
    char *full_text = malloc(text_len + 1);
    if (!full_text) {
        return NULL;
    }

    char *ptr = full_text;
    seg = segments;
    while (seg) {
        size_t len = strlen(seg->text);
        memcpy(ptr, seg->text, len);
        ptr += len;
        seg = seg->next;
    }
    *ptr = '\0';

    return full_text;
}

// Parse line text into ruby segments or plain text
static char *parse_line_text(char *text_start, struct ruby_segment **segments, int *seg_count) {
    if (!text_start || !*text_start) {
        return strdup("");
    }

    // Check for URL pattern (://), filters Spotify URIs and other URLs
    bool has_url = strstr(text_start, "://") != NULL;
    if (is_text_only_whitespace(text_start) || has_url) {
        return strdup("");
    }

    // Parse ruby text (furigana) from the line into segments
    *seg_count = parse_ruby_segments(text_start, segments);

    if (*seg_count > 0 && *segments) {
        // Normalize punctuation in all ruby segments
        normalize_ruby_segments(*segments);

        // Build full text without ruby notation for display
        char *full_text = build_text_from_segments(*segments);
        return full_text ? full_text : strdup("");
    }

    // No segments created (error case) - use original text, trimmed
    char *trimmed = trim_whitespace(text_start);
    return strdup(trimmed ? trimmed : "");
}

// Process a timed line (line starting with timestamp)
static bool process_timed_line(char *line, struct lyrics_data *data,
                              struct lyrics_line ***next_line, int64_t *last_timestamp_us) {
    int64_t timestamp_us;
    if (!parse_lrc_timestamp(line, &timestamp_us, NULL)) {
        return false;
    }

    // Extract text after timestamp tags
    char *text_start = extract_lyrics_text(line);

    // Create new line even if text is empty (for instrumental breaks)
    struct lyrics_line *new_line = calloc(1, sizeof(struct lyrics_line));
    if (!new_line) {
        return false;
    }

    // Apply offset and validate timestamp order
    new_line->timestamp_us = apply_timestamp_offset(timestamp_us, data->metadata.offset_ms);
    validate_timestamp_order(new_line->timestamp_us, last_timestamp_us, "LRC");
    *last_timestamp_us = new_line->timestamp_us;

    // Parse text and ruby segments
    struct ruby_segment *segments = NULL;
    int seg_count = 0;
    new_line->text = parse_line_text(text_start, &segments, &seg_count);

    if (seg_count > 0 && segments) {
        new_line->ruby_segments = segments;
        new_line->segment_count = seg_count;
    } else if (new_line->text && !new_line->ruby_segments) {
        // Only normalize if we didn't parse segments (fallback case)
        normalize_fullwidth_punctuation(new_line->text);
    }

    // Link new line to list
    **next_line = new_line;
    *next_line = &new_line->next;
    data->line_count++;

    return true;
}

bool lrc_parse_string(const char *content, struct lyrics_data *data) {
    char *content_copy = NULL;
    if (!parse_init(content, data, &content_copy)) {
        return false;
    }

    struct lyrics_line **next_line = &data->lines;
    char *saveptr;
    char *line = strtok_r(content_copy, "\n", &saveptr);
    int64_t last_timestamp_us = -1;

    while (line) {
        // Trim whitespace
        line = trim_whitespace(line);

        // Skip empty lines
        if (*line == '\0') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        // Try to parse as metadata
        if (parse_lrc_metadata_tag(line, &data->metadata)) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        // Try to parse as timed line
        if (line[0] == '[') {
            if (!process_timed_line(line, data, &next_line, &last_timestamp_us)) {
                free(content_copy);
                lrc_free_data(data);
                return false;
            }
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(content_copy);
    return data->line_count > 0;
}

bool lrc_parse_file(const char *filename, struct lyrics_data *data) {
    bool success = parse_file_generic(filename, data, lrc_parse_string);

    if (success) {
        warn_missing_metadata(data, "LRC");
    }

    return success;
}
