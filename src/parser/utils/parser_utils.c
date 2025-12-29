#include "parser_utils.h"
#include <stdio.h>
#include "../../constants.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Forward declarations
static void free_ruby_segments_list(struct ruby_segment *head);

// Helper: Find pointer after ']' character in timestamp string
static const char* find_end_after_bracket(const char *str) {
    const char *dot = strchr(str, '.');
    if (!dot) {
        return NULL;
    }

    const char *bracket = strchr(dot, ']');
    if (!bracket) {
        return NULL;
    }

    return bracket + 1;
}

// Helper: Parse [MM:SS.xx] format and return timestamp components
// Returns: true if parsing succeeded, false otherwise
// Output: minutes, seconds, centiseconds, and digit length (2 or 3)
static bool parse_timestamp_format(const char *str, int *minutes, int *seconds,
                                   int *centiseconds, int *digit_len) {
    int matched = sscanf(str, "[%d:%d.%d]", minutes, seconds, centiseconds);
    if (matched != 3) {
        return false;
    }

    // Determine if using centiseconds (2 digits) or milliseconds (3 digits)
    const char *dot = strchr(str, '.');
    if (!dot) {
        return false;
    }

    const char *bracket = strchr(dot, ']');
    if (!bracket) {
        return false;
    }

    *digit_len = bracket - dot - 1;
    return true;
}

bool parse_lrc_timestamp_ex(const char *str, int64_t *timestamp_us, const char **end_ptr, bool *is_unfill) {
    int minutes = 0, seconds = 0, centiseconds = 0, digit_len = 0;
    bool unfill = (str[0] == '[' && str[1] == '<');

    // Prepare parse string (remove '<' for unfill timestamps)
    char temp[64];
    const char *parse_str = str;

    if (unfill) {
        // Convert [<MM:SS.xx] to [MM:SS.xx]
        temp[0] = '[';
        snprintf(temp + 1, sizeof(temp) - 1, "%s", str + 2);
        parse_str = temp;
    }

    // Parse timestamp format
    if (!parse_timestamp_format(parse_str, &minutes, &seconds, &centiseconds, &digit_len)) {
        return false;
    }

    // Set output values
    *timestamp_us = timestamp_to_microseconds(minutes, seconds, centiseconds, digit_len == 2);

    if (end_ptr) {
        *end_ptr = find_end_after_bracket(str);
    }

    if (is_unfill) {
        *is_unfill = unfill;
    }

    return true;
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
        // This is normal during path search - don't log as error
        return false;
    }

    // Read entire file into memory
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Check for invalid file size
    if (size < 0) {
        fclose(fp);
        return false;
    }

    // Empty file is not valid lyrics
    if (size == 0) {
        fclose(fp);
        return false;
    }

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
static bool is_space_char(const char *p, const char *text_end) {
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

// Helper: Move back one UTF-8 character (skip continuation bytes)
static const char* move_back_one_utf8_char(const char *p, const char *limit) {
    // Prevent buffer underflow when p == limit
    if (p <= limit) {
        return limit;
    }
    const char *prev = p - 1;
    while (prev > limit && ((unsigned char)*prev & 0xC0) == 0x80) {
        prev--;
    }
    // Defensive check to satisfy static analyzer (prev should always be >= limit)
    if (prev < limit) {
        return limit;
    }
    return prev;
}

// Helper: Find space-based word boundary
static const char* find_space_boundary(const char *p, const char *limit, const char *text_end) {
    while (p > limit) {
        const char *prev = move_back_one_utf8_char(p, limit);
        if (is_space_char(prev, text_end)) {
            return p;
        }
        p = prev;
    }
    return limit;
}

// Helper: Find kanji-based word boundary
static const char* find_kanji_boundary(const char *p, const char *limit, const char *text_end) {
    while (p > limit) {
        const char *prev = move_back_one_utf8_char(p, limit);

        // Check if previous character is a space or not a kanji
        if (is_space_char(prev, text_end) || !is_cjk_ideograph(prev, limit, text_end)) {
            return p;  // Found word boundary
        }

        p = prev;
    }
    return limit;
}

// Helper: Find the start of the word that ends at 'end'
// For ruby text, scan backwards to find kanji sequence
// Will not go before 'limit'
static const char* find_word_start(const char *limit, const char *end, const char *text_end) {
    // Check if the character right before '{' is a kanji
    const char *check = move_back_one_utf8_char(end, limit);

    if (!is_cjk_ideograph(check, limit, text_end)) {
        // Not a kanji - use space-based boundary
        return find_space_boundary(end, limit, text_end);
    }

    // Kanji - scan backwards to find start of kanji sequence
    return find_kanji_boundary(end, limit, text_end);
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
        // Explicit cleanup to prevent potential leak (parse_ruby_segments should already cleanup on failure)
        free_ruby_segments_list(ruby_segs);
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
                free(ruby_seg->translation);
                free(ruby_seg);
                ruby_seg = next;
            }
            return 0;
        }

        // Copy data and add timestamp
        word_seg->timestamp_us = timestamp_us;
        word_seg->end_timestamp_us = 0;

        // Transfer ownership of text and ruby from ruby_seg to word_seg
        // After transfer, ruby_seg no longer owns these pointers
        word_seg->text = ruby_seg->text;
        word_seg->ruby = ruby_seg->ruby;
        word_seg->is_unfill = false;

        // Prevent double-free by nullifying transferred pointers
        ruby_seg->text = NULL;
        ruby_seg->ruby = NULL;

        *next_word = word_seg;
        next_word = &word_seg->next;

        struct ruby_segment *next_ruby = ruby_seg->next;
        free(ruby_seg);  // Free the ruby_segment shell (text/ruby already transferred)
        ruby_seg = next_ruby;
    }

    *segments = head;
    return count;
}

// Helper: Free all ruby segments in a linked list
static void free_ruby_segments_list(struct ruby_segment *head) {
    while (head) {
        struct ruby_segment *next = head->next;
        free(head->text);
        free(head->ruby);
        free(head->translation);
        free(head);
        head = next;
    }
}

// Helper: Create and append a ruby segment to the list
// Returns: true on success, false on allocation failure
static bool create_and_append_segment(const char *text, size_t text_len,
                                      const char *ruby, const char *translation,
                                      struct ruby_segment ***next_seg, int *count) {
    struct ruby_segment *seg = calloc(1, sizeof(struct ruby_segment));
    if (!seg) {
        return false;
    }

    seg->text = text_len > 0 ? strndup(text, text_len) : strdup(text);
    seg->ruby = ruby ? strdup(ruby) : NULL;
    seg->translation = translation ? strdup(translation) : NULL;

    if (!seg->text || (ruby && !seg->ruby) || (translation && !seg->translation)) {
        free(seg->text);
        free(seg->ruby);
        free(seg->translation);
        free(seg);
        return false;
    }

    **next_seg = seg;
    *next_seg = &seg->next;
    (*count)++;
    return true;
}

// Helper: Handle translation segment at line start: {translation text}
// Returns: closing brace position on success, NULL on malformed or allocation failure
// Note: Caller is responsible for cleanup on failure
static const char* handle_translation(const char *pos, struct ruby_segment ***next_seg,
                                     int *count, struct ruby_segment *head) {
    const char *close_brace = strchr(pos, '}');
    if (!close_brace) {
        return NULL;  // Malformed
    }

    const char *trans_start = pos + 1;
    size_t trans_len = close_brace - trans_start;

    // Allocate translation string separately to prevent leak on failure
    char *translation = strndup(trans_start, trans_len);
    if (!translation) {
        return NULL;
    }

    if (!create_and_append_segment("", 0, NULL, translation, next_seg, count)) {
        free(translation);  // Free on failure to prevent leak
        return NULL;
    }

    // Free original pointer since create_and_append_segment() makes a copy via strdup()
    free(translation);
    return close_brace + 1;
}

// Helper: Handle newline - creates segment for text before it and newline segment
// Returns: true on success, false on allocation failure
// Note: Caller is responsible for cleanup on failure
static bool handle_newline(const char *seg_start, const char *pos,
                          struct ruby_segment ***next_seg, int *count,
                          struct ruby_segment *head) {
    // Create segment for text before newline (if any)
    if (pos > seg_start) {
        if (!create_and_append_segment(seg_start, pos - seg_start, NULL, NULL,
                                       next_seg, count)) {
            return false;
        }
    }

    // Create newline segment
    if (!create_and_append_segment("\n", 1, NULL, NULL, next_seg, count)) {
        return false;
    }

    return true;
}

// Helper: Handle ruby annotation - creates segments for text before word and word with ruby
// Returns: closing brace position on success, NULL on allocation failure
// Note: Caller is responsible for cleanup on failure
static const char* handle_ruby_annotation(const char *pos, const char *seg_start,
                                         const char *text_end, struct ruby_segment ***next_seg,
                                         int *count, struct ruby_segment *head) {
    const char *close_brace = strchr(pos, '}');
    if (!close_brace) {
        return NULL;  // Malformed
    }

    // Extract ruby text
    size_t ruby_len = close_brace - pos - 1;
    char *ruby = ruby_len > 0 ? strndup(pos + 1, ruby_len) : NULL;

    // Find the word that this ruby annotation applies to
    const char *word_end = pos;
    const char *word_start = find_word_start(seg_start, word_end, text_end);

    // Create segment for text before the word (if any)
    if (word_start > seg_start) {
        if (!create_and_append_segment(seg_start, word_start - seg_start, NULL, NULL,
                                       next_seg, count)) {
            free(ruby);
            return NULL;
        }
    }

    // Create segment for the word with ruby
    if (!create_and_append_segment(word_start, word_end - word_start, ruby, NULL,
                                   next_seg, count)) {
        free(ruby);
        return NULL;
    }

    free(ruby);
    return close_brace + 1;
}

// Parse text into ruby segments with ruby annotations, translations, and newlines
// Precondition: text must be a valid NULL-terminated string
// Returns: number of segments created, or 0 on failure
int parse_ruby_segments(const char *text, struct ruby_segment **segments) {
    if (!text || !segments) {
        return 0;
    }

    *segments = NULL;

    // Check if text contains ruby notation, translation tags, or newlines
    bool has_ruby = (strchr(text, '{') != NULL);
    bool has_translation = (strstr(text, "<sub>") != NULL);
    bool has_newline = (strchr(text, '\n') != NULL);

    if (!has_ruby && !has_translation && !has_newline) {
        // No ruby text, translation, or newlines - create single segment
        int count = 0;
        struct ruby_segment **next_seg = segments;
        // strlen() is safe here: text is guaranteed to be NULL-terminated by precondition
        if (!create_and_append_segment(text, strlen(text), NULL, NULL, &next_seg, &count)) {
            return 0;
        }
        return count;
    }

    // Parse text with ruby annotations and translation tags
    struct ruby_segment *head = NULL;
    struct ruby_segment **next_seg = &head;
    int count = 0;

    const char *pos = text;
    const char *seg_start = pos;
    // strlen() is safe here: text is guaranteed to be NULL-terminated by precondition
    const char *text_end = text + strlen(text);

    while (*pos) {
        if (*pos == '{' && pos == seg_start) {
            // Translation at line start
            const char *new_pos = handle_translation(pos, &next_seg, &count, head);
            if (!new_pos) {
                if (strchr(pos, '}')) {
                    // Failed with valid closing brace - allocation error
                    free_ruby_segments_list(head);
                    return 0;
                }
                // Malformed - treat as regular text
                pos++;
                continue;
            }
            pos = new_pos;
            seg_start = pos;
        } else if (*pos == '\n') {
            // Newline
            if (!handle_newline(seg_start, pos, &next_seg, &count, head)) {
                free_ruby_segments_list(head);
                return 0;
            }
            pos++;
            seg_start = pos;
        } else if (*pos == '{') {
            // Ruby annotation
            const char *new_pos = handle_ruby_annotation(pos, seg_start, text_end,
                                                         &next_seg, &count, head);
            if (!new_pos) {
                if (strchr(pos, '}')) {
                    // Failed with valid closing brace - allocation error
                    free_ruby_segments_list(head);
                    return 0;
                }
                // Malformed - treat as regular text
                pos++;
                continue;
            }
            pos = new_pos;
            seg_start = pos;
        } else {
            pos++;
        }
    }

    // Add remaining text as final segment
    if (seg_start < text_end) {
        if (!create_and_append_segment(seg_start, text_end - seg_start, NULL, NULL,
                                       &next_seg, &count)) {
            free_ruby_segments_list(head);
            return 0;
        }
    }

    // If no segments created, create one with entire text
    if (count == 0) {
        // strlen() is safe here: text is guaranteed to be NULL-terminated by precondition
        if (!create_and_append_segment(text, strlen(text), NULL, NULL, &next_seg, &count)) {
            free_ruby_segments_list(head);
            return 0;
        }
        // No need to assign head = *segments; next_seg already updated head via &head
    }

    *segments = head;
    return count;
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
        free(segments->translation);
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


bool validate_timestamp_order(int64_t current_us, int64_t *last_us, const char *format_name) {
    if (!last_us || *last_us < 0) {
        return true;  // No previous timestamp to compare
    }

    if (current_us < *last_us) {
        // Warn about backwards timestamp
        log_warn("%s timestamp goes backwards: %02ld:%02ld:%02ld,%03ld -> %02ld:%02ld:%02ld,%03ld",
                 format_name,
                 *last_us / 3600000000,
                 (*last_us / 60000000) % 60,
                 (*last_us / 1000000) % 60,
                 (*last_us / 1000) % 1000,
                 current_us / 3600000000,
                 (current_us / 60000000) % 60,
                 (current_us / 1000000) % 60,
                 (current_us / 1000) % 1000);
        return false;
    }

    return true;
}

void warn_missing_metadata(struct lyrics_data *data, const char *format_name) {
    if (!data) {
        return;
    }

    // Warn if critical metadata is missing (only for local files)
    if (!data->metadata.artist || data->metadata.artist[0] == '\0') {
        log_warn("%s file missing artist metadata [ar:Artist Name]", format_name);
    }
    if (!data->metadata.album || data->metadata.album[0] == '\0') {
        log_warn("%s file missing album metadata [al:Album Name]", format_name);
    }
}
