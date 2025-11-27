#include "lrc_parser.h"
#include "../utils/parser_utils.h"
#include "../../constants.h"
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
    char *line = strtok(content_copy, "\n");
    int64_t last_timestamp_us = -1; // Track last timestamp for validation

    while (line) {
        // Skip empty lines
        while (*line && isspace(*line)) {
            line++;
        }

        if (*line == '\0') {
            line = strtok(NULL, "\n");
            continue;
        }

        // Try to parse as metadata
        if (parse_lrc_metadata_tag(line, &data->metadata)) {
            line = strtok(NULL, "\n");
            continue;
        }

        // Try to parse as timed line
        if (line[0] == '[') {
            int64_t timestamp_us;
            if (parse_lrc_timestamp(line, &timestamp_us, NULL)) {
                // Find the end of timestamp(s)
                const char *text_start = line;
                while (*text_start == '[') {
                    text_start = strchr(text_start, ']');
                    if (!text_start) {
                        break;
                    }
                    text_start++;
                }

                // Skip leading whitespace after timestamp tags
                while (text_start && *text_start && isspace((unsigned char)*text_start)) {
                    text_start++;
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
                            // Trim trailing whitespace
                            const char *end = text_start + strlen(text_start) - 1;
                            while (end > text_start && isspace((unsigned char)*end)) {
                                end--;
                            }
                            size_t len = end - text_start + 1;
                            char *trimmed = malloc(len + 1);
                            if (trimmed) {
                                memcpy(trimmed, text_start, len);
                                trimmed[len] = '\0';
                                new_line->text = trimmed;
                            } else {
                                new_line->text = strdup("");
                            }
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

        line = strtok(NULL, "\n");
    }

    free(content_copy);
    return data->line_count > 0;
}

bool lrc_parse_file(const char *filename, struct lyrics_data *data) {
    bool success = parse_file_generic(filename, "LRC", data, lrc_parse_string);

    // Warn if critical metadata is missing (only for local files)
    if (success) {
        if (!data->metadata.artist || strlen(data->metadata.artist) == 0) {
            fprintf(stderr, LOG_WARN " LRC file missing artist metadata [ar:Artist Name]\n");
        }
        if (!data->metadata.album || strlen(data->metadata.album) == 0) {
            fprintf(stderr, LOG_WARN " LRC file missing album metadata [al:Album Name]\n");
        }
    }

    return success;
}

void lrc_free_data(struct lyrics_data *data) {
    if (!data) {
        return;
    }

    free(data->metadata.title);
    free(data->metadata.artist);
    free(data->metadata.album);
    free(data->source_file_path);

    struct lyrics_line *line = data->lines;
    while (line) {
        struct lyrics_line *next = line->next;
        free(line->text);

        // Free ruby segments if present (LRC format)
        free_ruby_segments(line->ruby_segments);

        // Free word segments if present (LRCX format)
        free_word_segments(line->segments);

        free(line);
        line = next;
    }

    memset(data, 0, sizeof(struct lyrics_data));
}

struct lyrics_line* lrc_find_line_at_time(struct lyrics_data *data, int64_t timestamp_us) {
    if (!data || !data->lines) {
        return NULL;
    }

    struct lyrics_line *current = NULL;
    struct lyrics_line *line = data->lines;

    while (line) {
        if (line->timestamp_us > timestamp_us) {
            break;
        }
        current = line;
        line = line->next;
    }

    return current;
}

int lrc_get_line_index(struct lyrics_data *data, struct lyrics_line *target) {
    if (!data || !target) {
        return -1;
    }

    int index = 0;
    struct lyrics_line *line = data->lines;

    while (line) {
        if (line == target) {
            return index;
        }
        line = line->next;
        index++;
    }

    return -1;
}
