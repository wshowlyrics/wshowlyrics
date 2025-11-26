#include "parser_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

bool parse_lrc_timestamp_ex(const char *str, int64_t *timestamp_us, const char **end_ptr, bool *is_unfill) {
    int minutes = 0, seconds = 0, centiseconds = 0;
    const char *parse_str = str;
    bool unfill = false;

    // Check for unfill marker [<MM:SS.xx]
    if (str[0] == '[' && str[1] == '<') {
        unfill = true;
        parse_str = str + 1; // Skip '[', leaving '<MM:SS.xx]' which we'll adjust
        // Create temporary string without '<': [MM:SS.xx]
        char temp[64];
        temp[0] = '[';
        strncpy(temp + 1, parse_str + 1, sizeof(temp) - 2);
        temp[sizeof(temp) - 1] = '\0';
        parse_str = temp;

        // Try [MM:SS.xx] format
        int matched = sscanf(parse_str, "[%d:%d.%d]", &minutes, &seconds, &centiseconds);
        if (matched == 3) {
            // Handle both centiseconds (2 digits) and milliseconds (3 digits)
            int len = 0;
            const char *dot = strchr(parse_str, '.');
            if (dot) {
                const char *bracket = strchr(dot, ']');
                if (bracket) {
                    len = bracket - dot - 1;
                    if (end_ptr) {
                        // Point to character after ']' in original string
                        const char *orig_dot = strchr(str, '.');
                        if (orig_dot) {
                            const char *orig_bracket = strchr(orig_dot, ']');
                            if (orig_bracket) {
                                *end_ptr = orig_bracket + 1;
                            }
                        }
                    }
                }
            }

            if (len == 2) {
                // Centiseconds
                *timestamp_us = (int64_t)minutes * 60 * 1000000 +
                                (int64_t)seconds * 1000000 +
                                (int64_t)centiseconds * 10000;
            } else {
                // Milliseconds
                *timestamp_us = (int64_t)minutes * 60 * 1000000 +
                                (int64_t)seconds * 1000000 +
                                (int64_t)centiseconds * 1000;
            }

            if (is_unfill) {
                *is_unfill = unfill;
            }
            return true;
        }
    } else {
        // Normal timestamp [MM:SS.xx]
        int matched = sscanf(str, "[%d:%d.%d]", &minutes, &seconds, &centiseconds);
        if (matched == 3) {
            // Handle both centiseconds (2 digits) and milliseconds (3 digits)
            int len = 0;
            const char *dot = strchr(str, '.');
            if (dot) {
                const char *bracket = strchr(dot, ']');
                if (bracket) {
                    len = bracket - dot - 1;
                    if (end_ptr) {
                        *end_ptr = bracket + 1; // Point to character after ']'
                    }
                }
            }

            if (len == 2) {
                // Centiseconds
                *timestamp_us = (int64_t)minutes * 60 * 1000000 +
                                (int64_t)seconds * 1000000 +
                                (int64_t)centiseconds * 10000;
            } else {
                // Milliseconds
                *timestamp_us = (int64_t)minutes * 60 * 1000000 +
                                (int64_t)seconds * 1000000 +
                                (int64_t)centiseconds * 1000;
            }

            if (is_unfill) {
                *is_unfill = unfill;
            }
            return true;
        }
    }

    return false;
}

bool parse_lrc_timestamp(const char *str, int64_t *timestamp_us, const char **end_ptr) {
    return parse_lrc_timestamp_ex(str, timestamp_us, end_ptr, NULL);
}

// Helper to parse string metadata tag
static bool parse_string_tag(const char *line, const char *tag, size_t tag_len,
                             char **target_field) {
    if (strncmp(line, tag, tag_len) != 0) {
        return false;
    }

    const char *end = strchr(line + tag_len, ']');
    if (!end) {
        return false;
    }

    size_t len = end - (line + tag_len);
    free(*target_field);
    *target_field = strndup(line + tag_len, len);
    return true;
}

bool parse_lrc_metadata_tag(const char *line, struct lyrics_metadata *metadata) {
    // Table-driven metadata parsing
    struct {
        const char *tag;
        size_t tag_len;
        char **field;
    } string_tags[] = {
        {"[ti:", 4, &metadata->title},
        {"[ar:", 4, &metadata->artist},
        {"[al:", 4, &metadata->album},
        {NULL, 0, NULL}
    };

    // Try string tags
    for (int i = 0; string_tags[i].tag != NULL; i++) {
        if (parse_string_tag(line, string_tags[i].tag, string_tags[i].tag_len,
                             string_tags[i].field)) {
            return true;
        }
    }

    // Special case: offset (integer value)
    if (strncmp(line, "[offset:", 8) == 0) {
        const char *end = strchr(line + 8, ']');
        if (end) {
            metadata->offset_ms = atoi(line + 8);
            return true;
        }
    }

    return false;
}

bool parse_file_generic(const char *filename, const char *format_name,
                        struct lyrics_data *data,
                        bool (*parser_func)(const char *, struct lyrics_data *)) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open %s file: %s\n", format_name, filename);
        return false;
    }

    // Read entire file into memory
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return false;
    }

    size_t read = fread(content, 1, size, fp);
    content[read] = '\0';
    fclose(fp);

    bool result = parser_func(content, data);
    free(content);

    return result;
}

char* parse_ruby_text(const char *text, char **ruby_text) {
    if (!text || !ruby_text) {
        return NULL;
    }

    *ruby_text = NULL;

    // Check if text contains ruby notation
    const char *open_brace = strchr(text, '{');
    if (!open_brace) {
        // No ruby text - return copy of original text
        return strdup(text);
    }

    const char *close_brace = strchr(open_brace, '}');
    if (!close_brace) {
        // Malformed - return copy of original text
        return strdup(text);
    }

    // Extract base text (before {)
    size_t base_len = open_brace - text;

    // Extract ruby text (between { and })
    size_t ruby_len = close_brace - open_brace - 1;
    if (ruby_len > 0) {
        *ruby_text = strndup(open_brace + 1, ruby_len);
    }

    // Calculate total result length (base + text after })
    const char *after_ruby = close_brace + 1;
    size_t after_len = strlen(after_ruby);
    size_t total_len = base_len + after_len;

    // Build result string (base text + text after })
    char *result = malloc(total_len + 1);
    if (!result) {
        free(*ruby_text);
        *ruby_text = NULL;
        return strdup(text);
    }

    memcpy(result, text, base_len);
    memcpy(result + base_len, after_ruby, after_len);
    result[total_len] = '\0';

    return result;
}

// Helper: Check if position p starts with a space character (ASCII or Japanese)
static bool is_space_char(const char *p, const char *text_start, const char *text_end) {
    if (p >= text_end) {
        return false;
    }

    unsigned char c = *p;

    // ASCII space or tab
    if (c == ' ' || c == '\t') {
        return true;
    }

    // Japanese space (　= E3 80 80) - need 3 bytes
    if (p + 2 < text_end &&
        (unsigned char)p[0] == 0xE3 &&
        (unsigned char)p[1] == 0x80 &&
        (unsigned char)p[2] == 0x80) {
        return true;
    }

    return false;
}

// Helper: Check if a UTF-8 character is a CJK ideograph (kanji/hanzi)
static bool is_cjk_ideograph(const char *p, const char *limit, const char *text_end) {
    if (p >= text_end || p < limit) {
        return false;
    }

    // Decode UTF-8 to get Unicode code point
    unsigned char c1 = (unsigned char)p[0];

    // 3-byte UTF-8 (most common for CJK)
    if ((c1 & 0xF0) == 0xE0 && p + 2 < text_end) {
        unsigned char c2 = (unsigned char)p[1];
        unsigned char c3 = (unsigned char)p[2];
        uint32_t codepoint = ((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);

        // CJK Unified Ideographs: U+4E00 - U+9FFF
        // CJK Extension A: U+3400 - U+4DBF
        return (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
               (codepoint >= 0x3400 && codepoint <= 0x4DBF);
    }

    // 4-byte UTF-8 (for extension B and beyond)
    if ((c1 & 0xF8) == 0xF0 && p + 3 < text_end) {
        unsigned char c2 = (unsigned char)p[1];
        unsigned char c3 = (unsigned char)p[2];
        unsigned char c4 = (unsigned char)p[3];
        uint32_t codepoint = ((c1 & 0x07) << 18) | ((c2 & 0x3F) << 12) |
                             ((c3 & 0x3F) << 6) | (c4 & 0x3F);

        // CJK Extension B-F: U+20000 - U+2CEAF
        return (codepoint >= 0x20000 && codepoint <= 0x2CEAF);
    }

    return false;
}

// Helper: Find the start of the word that ends at 'end'
// For ruby text, scan backwards to find kanji sequence
// Will not go before 'limit'
static const char* find_word_start(const char *limit, const char *end, const char *text_end) {
    const char *p = end;

    // First, check if the character right before '{' is a kanji
    // If not, use space-based word boundary (old behavior)
    const char *check = p - 1;
    while (check > limit && ((unsigned char)*check & 0xC0) == 0x80) {
        check--;
    }

    if (!is_cjk_ideograph(check, limit, text_end)) {
        // Not a kanji - use space-based boundary
        while (p > limit) {
            const char *prev = p - 1;
            while (prev > limit && ((unsigned char)*prev & 0xC0) == 0x80) {
                prev--;
            }
            if (is_space_char(prev, limit, text_end)) {
                return p;
            }
            p = prev;
        }
        return limit;
    }

    // Scan backwards to find start of kanji sequence
    while (p > limit) {
        // Move back one character
        const char *prev = p - 1;

        // Move back over UTF-8 continuation bytes
        while (prev > limit && ((unsigned char)*prev & 0xC0) == 0x80) {
            prev--;
        }

        // Check if previous character is a space or not a kanji
        if (is_space_char(prev, limit, text_end) || !is_cjk_ideograph(prev, limit, text_end)) {
            return p; // Found word boundary
        }

        // Move pointer to start of previous character
        p = prev;
    }

    return limit;
}

int parse_karaoke_segments(const char *text, int64_t timestamp_us, struct word_segment **segments) {
    if (!text || !segments) {
        return 0;
    }

    *segments = NULL;

    // First parse as ruby segments (no timestamps)
    struct ruby_segment *ruby_segs = NULL;
    int count = parse_ruby_segments(text, &ruby_segs);
    if (count == 0) {
        return 0;
    }

    // Convert ruby_segment to word_segment by adding timestamp information
    struct word_segment *head = NULL;
    struct word_segment **next_word = &head;

    struct ruby_segment *ruby_seg = ruby_segs;
    while (ruby_seg) {
        struct word_segment *word_seg = calloc(1, sizeof(struct word_segment));
        if (!word_seg) {
            // Cleanup on error
            while (head) {
                struct word_segment *next = head->next;
                free(head->text);
                free(head->ruby);
                free(head);
                head = next;
            }
            // Cleanup remaining ruby segments
            while (ruby_seg) {
                struct ruby_segment *next = ruby_seg->next;
                free(ruby_seg->text);
                free(ruby_seg->ruby);
                free(ruby_seg);
                ruby_seg = next;
            }
            return 0;
        }

        // Copy data and add timestamp
        word_seg->timestamp_us = timestamp_us;
        word_seg->end_timestamp_us = 0;
        word_seg->text = ruby_seg->text; // Transfer ownership
        word_seg->ruby = ruby_seg->ruby; // Transfer ownership
        word_seg->is_unfill = false;

        *next_word = word_seg;
        next_word = &word_seg->next;

        struct ruby_segment *next_ruby = ruby_seg->next;
        free(ruby_seg); // Free the ruby_segment shell (but not text/ruby which were transferred)
        ruby_seg = next_ruby;
    }

    *segments = head;
    return count;
}

int parse_ruby_segments(const char *text, struct ruby_segment **segments) {
    if (!text || !segments) {
        return 0;
    }

    *segments = NULL;

    // Check if text contains ruby notation or newlines
    bool has_ruby = (strchr(text, '{') != NULL);
    bool has_newline = (strchr(text, '\n') != NULL);

    if (!has_ruby && !has_newline) {
        // No ruby text and no newlines - create single segment with entire line
        struct ruby_segment *seg = calloc(1, sizeof(struct ruby_segment));
        if (!seg) {
            return 0;
        }
        seg->text = strdup(text);
        seg->ruby = NULL;
        *segments = seg;
        return 1;
    }

    // Parse text with ruby annotations
    struct ruby_segment *head = NULL;
    struct ruby_segment **next_seg = &head;
    int count = 0;

    const char *pos = text;
    const char *seg_start = pos;
    const char *text_end = text + strlen(text);

    while (*pos) {
        if (*pos == '\n') {
            // Found newline - create segment for text before it
            if (pos > seg_start) {
                struct ruby_segment *seg = calloc(1, sizeof(struct ruby_segment));
                if (!seg) {
                    goto ruby_error;
                }
                seg->text = strndup(seg_start, pos - seg_start);
                seg->ruby = NULL;
                *next_seg = seg;
                next_seg = &seg->next;
                count++;
            }

            // Create a newline segment
            struct ruby_segment *nl_seg = calloc(1, sizeof(struct ruby_segment));
            if (!nl_seg) {
                goto ruby_error;
            }
            nl_seg->text = strdup("\n");
            nl_seg->ruby = NULL;
            *next_seg = nl_seg;
            next_seg = &nl_seg->next;
            count++;

            pos++;
            seg_start = pos;
        } else if (*pos == '{') {
            // Found ruby annotation
            const char *close_brace = strchr(pos, '}');
            if (!close_brace) {
                // Malformed - treat as regular text
                pos++;
                continue;
            }

            // Extract ruby text
            size_t ruby_len = close_brace - pos - 1;
            char *ruby = ruby_len > 0 ? strndup(pos + 1, ruby_len) : NULL;

            // Find the word that this ruby annotation applies to
            const char *word_end = pos;
            const char *word_start = find_word_start(seg_start, word_end, text_end);

            // Create segment for text before the word (if any)
            if (word_start > seg_start) {
                struct ruby_segment *seg = calloc(1, sizeof(struct ruby_segment));
                if (!seg) {
                    free(ruby);
                    goto ruby_error;
                }
                seg->text = strndup(seg_start, word_start - seg_start);
                seg->ruby = NULL;
                *next_seg = seg;
                next_seg = &seg->next;
                count++;
            }

            // Create segment for the word with ruby
            struct ruby_segment *seg = calloc(1, sizeof(struct ruby_segment));
            if (!seg) {
                free(ruby);
                goto ruby_error;
            }
            seg->text = strndup(word_start, word_end - word_start);
            seg->ruby = ruby;
            *next_seg = seg;
            next_seg = &seg->next;
            count++;

            // Move past the ruby annotation
            pos = close_brace + 1;
            seg_start = pos;
        } else {
            pos++;
        }
    }

    // Add remaining text as final segment
    if (seg_start < text_end) {
        struct ruby_segment *seg = calloc(1, sizeof(struct ruby_segment));
        if (!seg) {
            goto ruby_error;
        }
        seg->text = strdup(seg_start);
        seg->ruby = NULL;
        *next_seg = seg;
        next_seg = &seg->next;
        count++;
    }

    // If no segments created, create one with entire text
    if (count == 0) {
        struct ruby_segment *seg = calloc(1, sizeof(struct ruby_segment));
        if (!seg) {
            return 0;
        }
        seg->text = strdup(text);
        seg->ruby = NULL;
        head = seg;
        count = 1;
    }

    *segments = head;
    return count;

ruby_error:
    // Cleanup on error
    while (head) {
        struct ruby_segment *next = head->next;
        free(head->text);
        free(head->ruby);
        free(head);
        head = next;
    }
    return 0;
}

// ============================================================================
// Common parser utilities
// ============================================================================

bool parse_init(const char *content, struct lyrics_data *data, char **content_copy_out) {
    if (!content || !data) {
        return false;
    }

    memset(data, 0, sizeof(struct lyrics_data));

    char *content_copy = strdup(content);
    if (!content_copy) {
        return false;
    }

    *content_copy_out = content_copy;
    return true;
}

void free_ruby_segments(struct ruby_segment *segments) {
    while (segments) {
        struct ruby_segment *next = segments->next;
        free(segments->text);
        free(segments->ruby);
        free(segments);
        segments = next;
    }
}

void free_word_segments(struct word_segment *segments) {
    while (segments) {
        struct word_segment *next = segments->next;
        free(segments->text);
        free(segments->ruby);
        free(segments);
        segments = next;
    }
}

bool is_text_only_whitespace(const char *text) {
    if (!text) {
        return true;
    }

    while (*text) {
        if (!isspace(*text)) {
            return false;
        }
        text++;
    }

    return true;
}

void normalize_fullwidth_punctuation(char *text) {
    if (!text) return;

    char *read = text;
    char *write = text;

    while (*read) {
        // Check for fullwidth punctuation (U+FF00 - U+FF5E)
        if ((unsigned char)read[0] == 0xEF && (unsigned char)read[1] == 0xBC) {
            unsigned char third = (unsigned char)read[2];
            // U+FF01-FF5E maps to 0x21-0x7E
            if (third >= 0x81 && third <= 0xBF) {
                // Convert to halfwidth: U+FF01 (ef bc 81) -> ! (0x21)
                *write++ = third - 0x60;
                read += 3;
                continue;
            }
        } else if ((unsigned char)read[0] == 0xEF && (unsigned char)read[1] == 0xBD) {
            unsigned char third = (unsigned char)read[2];
            // U+FF61-FF9F (halfwidth katakana range, but check for punct)
            if (third >= 0x80 && third <= 0x9F) {
                *write++ = third - 0x20;
                read += 3;
                continue;
            }
        }

        // Copy as-is
        *write++ = *read++;
    }
    *write = '\0';
}

void normalize_ruby_segments(struct ruby_segment *segments) {
    while (segments) {
        if (segments->text) {
            normalize_fullwidth_punctuation(segments->text);
        }
        if (segments->ruby) {
            normalize_fullwidth_punctuation(segments->ruby);
        }
        segments = segments->next;
    }
}

void normalize_word_segments(struct word_segment *segments) {
    while (segments) {
        if (segments->text) {
            normalize_fullwidth_punctuation(segments->text);
        }
        if (segments->ruby) {
            normalize_fullwidth_punctuation(segments->ruby);
        }
        segments = segments->next;
    }
}
