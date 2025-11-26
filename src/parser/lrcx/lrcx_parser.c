#include "lrcx_parser.h"
#include "../utils/parser_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Parse a line with word-level timestamps
// Example: [00:05.00][00:05.20]첫 [00:05.50]번 [00:05.80]째 [00:06.00]줄
static bool parse_lrcx_line(const char *line, struct lyrics_data *data, struct lyrics_line **line_ptr) {
    if (line[0] != '[') {
        return false;
    }

    // Parse the first timestamp (line timestamp)
    int64_t line_timestamp_us;
    const char *pos = line;
    if (!parse_lrc_timestamp(pos, &line_timestamp_us, &pos)) {
        return false;
    }

    // Create new lyrics line
    struct lyrics_line *new_line = calloc(1, sizeof(struct lyrics_line));
    if (!new_line) {
        return false;
    }

    // Apply offset to line timestamp
    new_line->timestamp_us = apply_timestamp_offset(line_timestamp_us, data->metadata.offset_ms);

    // Build full text and parse word segments
    struct word_segment **next_segment = &new_line->segments;
    char *full_text = NULL;
    size_t full_text_len = 0;
    size_t full_text_capacity = 0;
    int64_t last_timestamp_us = line_timestamp_us; // Track last timestamp for end_timestamp_us

    // Check if there's text immediately after first timestamp (before next '[')
    // This handles: [00:01.00]今は[00:02.00]... where 今は should be first segment
    const char *first_text_start = pos;
    while (*first_text_start && isspace(*first_text_start)) {
        first_text_start++;
    }

    if (*first_text_start && *first_text_start != '[') {
        // Found text before next timestamp - create first segment with line timestamp
        const char *first_text_end = first_text_start;
        while (*first_text_end && *first_text_end != '[') {
            first_text_end++;
        }

        // Trim trailing whitespace
        while (first_text_end > first_text_start && isspace(*(first_text_end - 1))) {
            first_text_end--;
        }

        if (first_text_end > first_text_start) {
            // Parse text into segments (handles multiple ruby annotations)
            char *raw_text = strndup(first_text_start, first_text_end - first_text_start);

            struct word_segment *segments = NULL;
            int64_t timestamp_us = apply_timestamp_offset(line_timestamp_us, data->metadata.offset_ms);
            int seg_count = parse_karaoke_segments(raw_text, timestamp_us, &segments);
            free(raw_text);

            if (seg_count > 0 && segments) {
                // Add all segments to the line
                struct word_segment *seg = segments;
                while (seg) {
                    *next_segment = seg;
                    next_segment = &seg->next;
                    new_line->segment_count++;
                    seg = seg->next;
                }

                // Build full text from segments (base text only, without ruby notation)
                seg = segments;
                size_t estimated_len = 0;
                while (seg) {
                    if (seg->text && seg->text[0] != '\n') {
                        estimated_len += strlen(seg->text);
                    }
                    seg = seg->next;
                }

                full_text_capacity = estimated_len + 1;
                full_text = malloc(full_text_capacity);
                if (!full_text) {
                    free(new_line);
                    return false;
                }

                seg = segments;
                while (seg) {
                    if (seg->text && seg->text[0] != '\n') {
                        size_t seg_len = strlen(seg->text);
                        memcpy(full_text + full_text_len, seg->text, seg_len);
                        full_text_len += seg_len;
                    }
                    seg = seg->next;
                }
                full_text[full_text_len] = '\0';
            }

            pos = first_text_end;
        }
    }

    while (*pos) {
        // Skip whitespace
        while (*pos && isspace(*pos)) {
            pos++;
        }

        if (*pos == '\0') {
            break;
        }

        // Check for timestamp
        if (*pos == '[') {
            int64_t segment_timestamp_us;
            const char *after_timestamp = NULL;
            bool is_unfill = false;
            if (parse_lrc_timestamp_ex(pos, &segment_timestamp_us, &after_timestamp, &is_unfill)) {
                // This is a word timestamp
                pos = after_timestamp;

                // Skip whitespace after timestamp
                while (*pos && isspace(*pos)) {
                    pos++;
                }

                // Find end of word (next '[' or end of string)
                const char *word_start = pos;
                const char *word_end = pos;
                while (*word_end && *word_end != '[') {
                    word_end++;
                }

                // Trim trailing whitespace
                while (word_end > word_start && isspace(*(word_end - 1))) {
                    word_end--;
                }

                // Always create segment for every timestamp (even if empty for idle display)
                struct word_segment *segment = calloc(1, sizeof(struct word_segment));
                if (!segment) {
                    free(full_text);
                    free(new_line);
                    return false;
                }

                segment->timestamp_us = apply_timestamp_offset(segment_timestamp_us, data->metadata.offset_ms);
                segment->is_unfill = is_unfill; // Set unfill flag
                last_timestamp_us = segment_timestamp_us; // Update last seen timestamp

                if (word_end > word_start) {
                    // Has text - parse ruby annotations
                    char *raw_text = strndup(word_start, word_end - word_start);

                    // Parse ruby text into segments
                    struct word_segment *word_segments = NULL;
                    int word_seg_count = parse_karaoke_segments(raw_text, apply_timestamp_offset(segment_timestamp_us, data->metadata.offset_ms), &word_segments);

                    if (word_seg_count > 0 && word_segments) {
                        // Free raw_text and temporary segment
                        free(raw_text);
                        free(segment);

                        // Add all parsed segments to the line
                        struct word_segment *ws = word_segments;
                        while (ws) {
                            ws->is_unfill = is_unfill; // Inherit unfill flag
                            *next_segment = ws;
                            next_segment = &ws->next;
                            new_line->segment_count++;

                            // Add to full text (base text only)
                            if (ws->text && ws->text[0] != '\0') {
                                size_t word_len = strlen(ws->text);
                                if (full_text_len + word_len + 1 > full_text_capacity) {
                                    full_text_capacity = (full_text_len + word_len + 1) * 2;
                                    char *new_full_text = realloc(full_text, full_text_capacity);
                                    if (!new_full_text) {
                                        free(full_text);
                                        free(new_line);
                                        return false;
                                    }
                                    full_text = new_full_text;
                                }

                                if (full_text_len > 0) {
                                    full_text[full_text_len++] = ' ';
                                }
                                memcpy(full_text + full_text_len, ws->text, word_len);
                                full_text_len += word_len;
                                full_text[full_text_len] = '\0';
                            }

                            ws = ws->next;
                        }
                    } else {
                        // No segments - use raw text directly
                        segment->text = raw_text;  // Take ownership
                        segment->ruby = NULL;

                        *next_segment = segment;
                        next_segment = &segment->next;
                        new_line->segment_count++;

                        if (full_text_len > 0) {
                            full_text[full_text_len++] = ' ';
                        }
                        size_t word_len = strlen(segment->text);
                        memcpy(full_text + full_text_len, segment->text, word_len);
                        full_text_len += word_len;
                        full_text[full_text_len] = '\0';
                    }
                } else {
                    // Empty text for idle display (e.g., [00:01.00] or [00:01.00][00:01.20]...)
                    segment->text = strdup("");
                    *next_segment = segment;
                    next_segment = &segment->next;
                    new_line->segment_count++;
                }

                pos = word_end;
            } else {
                // Not a valid timestamp, skip this character
                pos++;
            }
        } else {
            // Text without timestamp - skip
            pos++;
        }
    }

    // Normalize all word segments first
    normalize_word_segments(new_line->segments);

    // Set the full line text (built from already-normalized segments)
    if (full_text) {
        new_line->text = full_text;
    } else {
        new_line->text = strdup("");
    }
    // Note: No need to normalize text since it's built from normalized segments

    // If no segments were parsed, create empty segment
    if (new_line->segment_count == 0) {
        struct word_segment *segment = calloc(1, sizeof(struct word_segment));
        if (!segment) {
            free(new_line->text);
            free(new_line);
            return false;
        }
        segment->timestamp_us = new_line->timestamp_us;
        segment->text = strdup("");
        new_line->segments = segment;
        new_line->segment_count = 1;
    }

    // Set line end timestamp if we saw more than the initial timestamp
    if (last_timestamp_us > line_timestamp_us) {
        new_line->end_timestamp_us = apply_timestamp_offset(last_timestamp_us, data->metadata.offset_ms);
    }

    // Calculate end_timestamp_us for each segment
    struct word_segment *seg = new_line->segments;
    while (seg) {
        if (seg->next) {
            // Use next segment's start time as this segment's end time
            seg->end_timestamp_us = seg->next->timestamp_us;
        } else {
            // Last segment - use line's end timestamp if available, otherwise 0
            seg->end_timestamp_us = new_line->end_timestamp_us;
        }
        seg = seg->next;
    }

    *line_ptr = new_line;
    return true;
}

bool lrcx_parse_string(const char *content, struct lyrics_data *data) {
    char *content_copy = NULL;
    if (!parse_init(content, data, &content_copy)) {
        return false;
    }

    struct lyrics_line **next_line = &data->lines;
    char *line = strtok(content_copy, "\n");
    int64_t last_line_timestamp_us = -1; // Track last line timestamp for validation

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

        // Try to parse as LRCX line
        struct lyrics_line *new_line = NULL;
        if (parse_lrcx_line(line, data, &new_line)) {
            // Validate line timestamp order (warn if going backwards)
            if (last_line_timestamp_us >= 0 && new_line->timestamp_us < last_line_timestamp_us) {
                fprintf(stderr, "\033[1;33mWARN:\033[0m LRCX line timestamp goes backwards: [%02ld:%02ld.%02ld] -> [%02ld:%02ld.%02ld]\n",
                        last_line_timestamp_us / 60000000,
                        (last_line_timestamp_us / 1000000) % 60,
                        (last_line_timestamp_us / 10000) % 100,
                        new_line->timestamp_us / 60000000,
                        (new_line->timestamp_us / 1000000) % 60,
                        (new_line->timestamp_us / 10000) % 100);
            }

            // Validate word segment timestamps within the line
            int64_t last_word_timestamp_us = new_line->timestamp_us;
            struct word_segment *seg = new_line->segments;
            while (seg) {
                if (seg->timestamp_us < last_word_timestamp_us) {
                    fprintf(stderr, "\033[1;33mWARN:\033[0m LRCX word timestamp goes backwards within line: [%02ld:%02ld.%02ld] -> [%02ld:%02ld.%02ld]\n",
                            last_word_timestamp_us / 60000000,
                            (last_word_timestamp_us / 1000000) % 60,
                            (last_word_timestamp_us / 10000) % 100,
                            seg->timestamp_us / 60000000,
                            (seg->timestamp_us / 1000000) % 60,
                            (seg->timestamp_us / 10000) % 100);
                }
                last_word_timestamp_us = seg->timestamp_us;
                seg = seg->next;
            }

            last_line_timestamp_us = new_line->timestamp_us;

            *next_line = new_line;
            next_line = &new_line->next;
            data->line_count++;
        }

        line = strtok(NULL, "\n");
    }

    free(content_copy);
    return data->line_count > 0;
}

bool lrcx_parse_file(const char *filename, struct lyrics_data *data) {
    return parse_file_generic(filename, "LRCX", data, lrcx_parse_string);
}

struct word_segment* lrcx_find_segment_at_time(struct lyrics_line *line, int64_t timestamp_us, int *segment_index) {
    if (!line || !line->segments) {
        return NULL;
    }

    struct word_segment *current = NULL;
    struct word_segment *segment = line->segments;
    int index = 0;

    while (segment) {
        if (segment->timestamp_us > timestamp_us) {
            break;
        }
        current = segment;
        if (segment_index && segment == current) {
            *segment_index = index;
        }
        segment = segment->next;
        index++;
    }

    return current;
}
