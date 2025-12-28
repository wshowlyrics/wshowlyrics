#include "lrc_parser.h"
#include "lrc_common.h"
#include "../utils/parser_utils.h"
#include "../../constants.h"
#include "../../utils/string/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

bool lrc_parse_string(const char *content, struct lyrics_data *data) {
    char *content_copy = NULL;
    if (!parse_init(content, data, &content_copy)) {
        return false;
    }

    struct lyrics_line **next_line = &data->lines;
    char *saveptr;
    char *line = strtok_r(content_copy, "\n", &saveptr);
    int64_t last_timestamp_us = -1; // Track last timestamp for validation

    while (line) {
        // Trim whitespace
        line = trim_whitespace(line);

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
            int64_t timestamp_us;
            if (parse_lrc_timestamp(line, &timestamp_us, NULL)) {
                // Find the end of timestamp(s)
                char *text_start = line;
                while (*text_start == '[') {
                    text_start = strchr(text_start, ']');
                    if (!text_start) {
                        break;
                    }
                    text_start++;
                }

                // Trim whitespace after timestamp tags
                if (text_start) {
                    text_start = trim_whitespace(text_start);
                }

                // Create new line even if text is empty or whitespace-only
                // This allows instrumental breaks to be represented
                struct lyrics_line *new_line = calloc(1, sizeof(struct lyrics_line));
                if (!new_line) {
                    free(content_copy);
                    lrc_free_data(data);
                    return false;
                }

                // Apply offset
                new_line->timestamp_us = apply_timestamp_offset(timestamp_us, data->metadata.offset_ms);

                // Validate timestamp order (warn if going backwards)
                validate_timestamp_order(new_line->timestamp_us, &last_timestamp_us, "LRC");
                last_timestamp_us = new_line->timestamp_us;

                // If text is empty or only whitespace, use empty string
                if (text_start && *text_start) {
                    // Check if text contains URL pattern (://)
                    // This filters out Spotify URIs and other URLs from being displayed
                    bool has_url = strstr(text_start, "://") != NULL;

                    if (is_text_only_whitespace(text_start) || has_url) {
                        new_line->text = strdup("");
                    } else {
                        // Parse ruby text (furigana) from the line into segments
                        struct ruby_segment *segments = NULL;
                        int seg_count = parse_ruby_segments(text_start, &segments);

                        if (seg_count > 0 && segments) {
                            new_line->ruby_segments = segments;
                            new_line->segment_count = seg_count;

                            // Normalize punctuation in all ruby segments
                            normalize_ruby_segments(segments);

                            // Build full text without ruby notation for display
                            size_t text_len = 0;
                            struct ruby_segment *seg = segments;
                            while (seg) {
                                text_len += strlen(seg->text);
                                seg = seg->next;
                            }

                            char *full_text = malloc(text_len + 1);
                            if (full_text) {
                                // Use pointer-based concatenation instead of strcat for O(n) performance
                                char *ptr = full_text;
                                seg = segments;
                                while (seg) {
                                    size_t len = strlen(seg->text);
                                    memcpy(ptr, seg->text, len);
                                    ptr += len;
                                    seg = seg->next;
                                }
                                *ptr = '\0';
                                new_line->text = full_text;
                            } else {
                                new_line->text = strdup("");
                            }
                        } else {
                            // No segments created (error case) - use original text, trimmed
                            // text_start is already in a mutable buffer (from strtok), safe to trim in-place
                            char *trimmed = trim_whitespace(text_start);
                            new_line->text = strdup(trimmed ? trimmed : "");
                        }
                    }
                } else {
                    new_line->text = strdup("");
                }

                // Note: No need to normalize new_line->text here since it's built
                // from already-normalized segments, or for non-segment text,
                // we normalize it directly below
                if (new_line->text && !new_line->ruby_segments) {
                    // Only normalize if we didn't parse segments (fallback case)
                    normalize_fullwidth_punctuation(new_line->text);
                }

                *next_line = new_line;
                next_line = &new_line->next;
                data->line_count++;
            }
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(content_copy);
    return data->line_count > 0;
}

bool lrc_parse_file(const char *filename, struct lyrics_data *data) {
    bool success = parse_file_generic(filename, "LRC", data, lrc_parse_string);

    if (success) {
        warn_missing_metadata(data, "LRC");
    }

    return success;
}
