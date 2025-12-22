#include "lrcx_parser.h"
#include "lrc_common.h"
#include "../utils/parser_utils.h"
#include "../../constants.h"
#include "../../utils/string/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Parse first text segment (text immediately after line timestamp, before first word timestamp)
// Returns updated position and whether full_text was allocated
static const char* parse_first_text_segment(const char *pos, int64_t line_timestamp_us,
                                             struct lyrics_data *data, struct lyrics_line *new_line,
                                             struct word_segment ***next_segment_ptr,
                                             char **full_text, size_t *full_text_len,
                                             size_t *full_text_capacity) {
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
                    **next_segment_ptr = seg;
                    *next_segment_ptr = &seg->next;
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

                *full_text_capacity = estimated_len + 1;
                *full_text = malloc(*full_text_capacity);
                if (!*full_text) {
                    return NULL;  // Signal error
                }

                seg = segments;
                while (seg) {
                    if (seg->text && seg->text[0] != '\n') {
                        size_t seg_len = strlen(seg->text);
                        memcpy(*full_text + *full_text_len, seg->text, seg_len);
                        *full_text_len += seg_len;
                    }
                    seg = seg->next;
                }
                (*full_text)[*full_text_len] = '\0';
            }

            return first_text_end;
        }
    }

    return pos;
}

// Ensure full_text buffer has enough capacity and append text with optional space
// Returns false on allocation failure
static bool ensure_and_append_to_full_text(char **full_text, size_t *full_text_len,
                                            size_t *full_text_capacity, const char *text,
                                            bool prepend_space) {
    if (!text || text[0] == '\0') {
        return true;
    }

    size_t text_len = strlen(text);
    size_t space_len = (prepend_space && *full_text_len > 0) ? 1 : 0;
    size_t required_capacity = *full_text_len + text_len + space_len + 1;

    // Expand buffer if needed
    if (required_capacity > *full_text_capacity) {
        *full_text_capacity = required_capacity * 2;
        char *new_full_text = realloc(*full_text, *full_text_capacity);
        if (!new_full_text) {
            return false;
        }
        *full_text = new_full_text;
    }

    // Add space if needed
    if (space_len > 0) {
        (*full_text)[(*full_text_len)++] = ' ';
    }

    // Copy text
    memcpy(*full_text + *full_text_len, text, text_len);
    *full_text_len += text_len;
    (*full_text)[*full_text_len] = '\0';

    return true;
}

// Add parsed word segments to line and update full_text
// Returns false on error
static bool add_parsed_word_segments(struct word_segment *word_segments, bool is_unfill,
                                      struct lyrics_line *new_line,
                                      struct word_segment ***next_segment_ptr,
                                      char **full_text, size_t *full_text_len,
                                      size_t *full_text_capacity) {
    struct word_segment *ws = word_segments;
    while (ws) {
        ws->is_unfill = is_unfill;
        **next_segment_ptr = ws;
        *next_segment_ptr = &ws->next;
        new_line->segment_count++;

        // Add to full text (base text only)
        if (ws->text && ws->text[0] != '\0' &&
            !ensure_and_append_to_full_text(full_text, full_text_len, full_text_capacity,
                                             ws->text, true)) {
            return false;
        }

        ws = ws->next;
    }

    return true;
}

// Add raw text segment to line and update full_text
// Returns false on error
static bool add_raw_text_segment(struct word_segment *segment,
                                  struct lyrics_line *new_line,
                                  struct word_segment ***next_segment_ptr,
                                  char **full_text, size_t *full_text_len,
                                  size_t *full_text_capacity) {
    **next_segment_ptr = segment;
    *next_segment_ptr = &segment->next;
    new_line->segment_count++;

    // Add to full text
    if (!ensure_and_append_to_full_text(full_text, full_text_len, full_text_capacity,
                                         segment->text, true)) {
        return false;
    }

    return true;
}

// Parse a single word segment with timestamp
// Returns updated position, or NULL on error
static const char* parse_word_timestamp_segment(const char *pos, struct lyrics_data *data,
                                                 struct lyrics_line *new_line,
                                                 struct word_segment ***next_segment_ptr,
                                                 char **full_text, size_t *full_text_len,
                                                 size_t *full_text_capacity,
                                                 int64_t *last_timestamp_us) {
    int64_t segment_timestamp_us;
    const char *after_timestamp = NULL;
    bool is_unfill = false;

    if (!parse_lrc_timestamp_ex(pos, &segment_timestamp_us, &after_timestamp, &is_unfill)) {
        return pos + 1;  // Not a valid timestamp, skip this character
    }

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
        return NULL;  // Signal error
    }

    segment->timestamp_us = apply_timestamp_offset(segment_timestamp_us, data->metadata.offset_ms);
    segment->is_unfill = is_unfill;
    *last_timestamp_us = segment_timestamp_us;

    if (word_end > word_start) {
        // Has text - parse ruby annotations
        char *raw_text = strndup(word_start, word_end - word_start);
        struct word_segment *word_segments = NULL;
        int word_seg_count = parse_karaoke_segments(raw_text,
                                                     apply_timestamp_offset(segment_timestamp_us, data->metadata.offset_ms),
                                                     &word_segments);

        if (word_seg_count > 0 && word_segments) {
            // Free raw_text and temporary segment
            free(raw_text);
            free(segment);

            // Add all parsed segments using helper
            if (!add_parsed_word_segments(word_segments, is_unfill, new_line, next_segment_ptr,
                                           full_text, full_text_len, full_text_capacity)) {
                return NULL;  // Signal error
            }
        } else {
            // No segments - use raw text directly
            segment->text = raw_text;  // Take ownership
            segment->ruby = NULL;

            // Add raw text segment using helper
            if (!add_raw_text_segment(segment, new_line, next_segment_ptr,
                                       full_text, full_text_len, full_text_capacity)) {
                return NULL;  // Signal error
            }
        }
    } else {
        // Empty text for idle display
        segment->text = strdup("");
        **next_segment_ptr = segment;
        *next_segment_ptr = &segment->next;
        new_line->segment_count++;
    }

    return word_end;
}

// Finalize line segments: normalize, set full text, calculate end timestamps
// Returns false on error
static bool finalize_line_segments(struct lyrics_line *new_line, char *full_text,
                                   int64_t line_timestamp_us, int64_t last_timestamp_us) {
    // Normalize all word segments first
    normalize_word_segments(new_line->segments);

    // Set the full line text (built from already-normalized segments)
    if (full_text) {
        new_line->text = full_text;
    } else {
        new_line->text = strdup("");
    }

    // If no segments were parsed, create empty segment
    if (new_line->segment_count == 0) {
        struct word_segment *segment = calloc(1, sizeof(struct word_segment));
        if (!segment) {
            free(new_line->text);
            return false;
        }
        segment->timestamp_us = new_line->timestamp_us;
        segment->text = strdup("");
        new_line->segments = segment;
        new_line->segment_count = 1;
    }

    // Set line end timestamp if we saw more than the initial timestamp
    if (last_timestamp_us > line_timestamp_us) {
        new_line->end_timestamp_us = last_timestamp_us;
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

    return true;
}

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

    // Parse first text segment (text immediately after line timestamp)
    pos = parse_first_text_segment(pos, line_timestamp_us, data, new_line,
                                     &next_segment, &full_text, &full_text_len, &full_text_capacity);
    if (!pos) {
        // Error during first segment parsing
        free(new_line);
        return false;
    }

    // Parse remaining word segments with timestamps
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
            pos = parse_word_timestamp_segment(pos, data, new_line, &next_segment,
                                               &full_text, &full_text_len, &full_text_capacity,
                                               &last_timestamp_us);
            if (!pos) {
                // Error during word segment parsing
                free(full_text);
                free(new_line);
                return false;
            }
        } else {
            // Text without timestamp - skip
            pos++;
        }
    }

    // Finalize line: normalize segments, set text, calculate end timestamps
    if (!finalize_line_segments(new_line, full_text, line_timestamp_us,
                                 apply_timestamp_offset(last_timestamp_us, data->metadata.offset_ms))) {
        free(new_line);
        return false;
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
    char *saveptr;
    char *line = strtok_r(content_copy, "\n", &saveptr);
    int64_t last_line_timestamp_us = -1; // Track last line timestamp for validation

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

        // Try to parse as LRCX line
        struct lyrics_line *new_line = NULL;
        if (parse_lrcx_line(line, data, &new_line)) {
            // Validate line timestamp order (warn if going backwards)
            validate_timestamp_order(new_line->timestamp_us, &last_line_timestamp_us, "LRCX line");

            // Validate word segment timestamps within the line
            int64_t last_word_timestamp_us = new_line->timestamp_us;
            struct word_segment *seg = new_line->segments;
            while (seg) {
                validate_timestamp_order(seg->timestamp_us, &last_word_timestamp_us, "LRCX word");
                last_word_timestamp_us = seg->timestamp_us;
                seg = seg->next;
            }

            last_line_timestamp_us = new_line->timestamp_us;

            *next_line = new_line;
            next_line = &new_line->next;
            data->line_count++;
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(content_copy);
    return data->line_count > 0;
}

bool lrcx_parse_file(const char *filename, struct lyrics_data *data) {
    bool success = parse_file_generic(filename, "LRCX", data, lrcx_parse_string);

    // Warn if critical metadata is missing (only for local files)
    if (success) {
        if (!data->metadata.artist || data->metadata.artist[0] == '\0') {
            log_warn("LRCX file missing artist metadata [ar:Artist Name]");
        }
        if (!data->metadata.album || data->metadata.album[0] == '\0') {
            log_warn("LRCX file missing album metadata [al:Album Name]");
        }
    }

    return success;
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

void lrcx_find_context_lines(struct lyrics_data *data,
                             struct lyrics_line *current,
                             struct lyrics_line **prev_out,
                             struct lyrics_line **next_out) {
    *prev_out = NULL;
    *next_out = NULL;

    if (!data || !data->lines || !current) {
        return;
    }

    // Find previous line by traversing from start
    // Skip empty lines (instrumental breaks)
    struct lyrics_line *prev = NULL;
    struct lyrics_line *iter = data->lines;
    while (iter && iter != current) {
        // Only consider non-empty lines as context
        if (iter->text && iter->text[0] != '\0') {
            prev = iter;
        }
        iter = iter->next;
    }
    *prev_out = prev;

    // Find next non-empty line
    struct lyrics_line *next = current->next;
    while (next && next->text && next->text[0] == '\0') {
        next = next->next;
    }
    *next_out = next;
}
