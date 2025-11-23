#include "parser_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool parse_lrc_timestamp(const char *str, int64_t *timestamp_us, const char **end_ptr) {
	int minutes = 0, seconds = 0, centiseconds = 0;

	// Try [MM:SS.xx] format
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
		return true;
	}

	return false;
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

// Helper: Find the start of the word that ends at 'end'
// Scans backwards from 'end' to find word boundary (space)
// Will not go before 'limit'
static const char* find_word_start(const char *limit, const char *end, const char *text_end) {
	const char *p = end;

	// Scan backwards to find the start of the word (non-space sequence)
	while (p > limit) {
		// Move back one character
		const char *prev = p - 1;

		// Move back over UTF-8 continuation bytes
		while (prev > limit && ((unsigned char)*prev & 0xC0) == 0x80) {
			prev--;
		}

		// Check if we're now at a space
		if (is_space_char(prev, limit, text_end)) {
			return p; // Found word boundary
		}

		// Move pointer to start of previous character
		p = prev;
	}

	return limit;
}

int parse_ruby_segments(const char *text, int64_t timestamp_us, struct word_segment **segments) {
	if (!text || !segments) {
		return 0;
	}

	*segments = NULL;

	// Check if text contains ruby notation or newlines
	bool has_ruby = (strchr(text, '{') != NULL);
	bool has_newline = (strchr(text, '\n') != NULL);

	if (!has_ruby && !has_newline) {
		// No ruby text and no newlines - create single segment with entire line
		struct word_segment *seg = calloc(1, sizeof(struct word_segment));
		if (!seg) {
			return 0;
		}
		seg->timestamp_us = timestamp_us;
		seg->text = strdup(text);
		seg->ruby = NULL;
		*segments = seg;
		return 1;
	}

	// Parse text with ruby annotations
	// Example: "目指{めざ}せよ　快眠{かいみん}"
	// Result: ["目指"(めざ), "せよ　", "快眠"(かいみん)]

	struct word_segment *head = NULL;
	struct word_segment **next_seg = &head;
	int count = 0;

	const char *pos = text;
	const char *seg_start = pos;
	const char *text_end = text + strlen(text);

	while (*pos) {
		if (*pos == '\n') {
			// Found newline - create segment for text before it
			if (pos > seg_start) {
				struct word_segment *seg = calloc(1, sizeof(struct word_segment));
				if (!seg) {
					goto error;
				}
				seg->timestamp_us = timestamp_us;
				seg->text = strndup(seg_start, pos - seg_start);
				seg->ruby = NULL;
				*next_seg = seg;
				next_seg = &seg->next;
				count++;
			}

			// Create a newline segment
			struct word_segment *nl_seg = calloc(1, sizeof(struct word_segment));
			if (!nl_seg) {
				goto error;
			}
			nl_seg->timestamp_us = timestamp_us;
			nl_seg->text = strdup("\n");
			nl_seg->ruby = NULL;
			*next_seg = nl_seg;
			next_seg = &nl_seg->next;
			count++;

			pos++;
			seg_start = pos;
		} else if (*pos == '{') {
			// Found ruby annotation
			// Find closing brace
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
			// It's the word immediately before '{'
			const char *word_end = pos;
			const char *word_start = find_word_start(seg_start, word_end, text_end);

			// Create segment for text before the word (if any)
			if (word_start > seg_start) {
				struct word_segment *seg = calloc(1, sizeof(struct word_segment));
				if (!seg) {
					free(ruby);
					goto error;
				}
				seg->timestamp_us = timestamp_us;
				seg->text = strndup(seg_start, word_start - seg_start);
				seg->ruby = NULL;
				*next_seg = seg;
				next_seg = &seg->next;
				count++;
			}

			// Create segment for the word with ruby annotation
			if (word_end > word_start) {
				struct word_segment *seg = calloc(1, sizeof(struct word_segment));
				if (!seg) {
					free(ruby);
					goto error;
				}
				seg->timestamp_us = timestamp_us;
				seg->text = strndup(word_start, word_end - word_start);
				seg->ruby = ruby;
				*next_seg = seg;
				next_seg = &seg->next;
				count++;
			} else {
				free(ruby); // No base text
			}

			// Move past the ruby annotation
			pos = close_brace + 1;
			seg_start = pos;
		} else {
			pos++;
		}
	}

	// Add remaining text as final segment
	if (pos > seg_start && *seg_start) {
		struct word_segment *seg = calloc(1, sizeof(struct word_segment));
		if (!seg) {
			goto error;
		}
		seg->timestamp_us = timestamp_us;
		seg->text = strdup(seg_start);
		seg->ruby = NULL;
		*next_seg = seg;
		next_seg = &seg->next;
		count++;
	}

	// If no segments created, create one with entire text
	if (count == 0) {
		struct word_segment *seg = calloc(1, sizeof(struct word_segment));
		if (!seg) {
			return 0;
		}
		seg->timestamp_us = timestamp_us;
		seg->text = strdup(text);
		seg->ruby = NULL;
		head = seg;
		count = 1;
	}

	*segments = head;
	return count;

error:
	// Cleanup on error
	while (head) {
		struct word_segment *next = head->next;
		free(head->text);
		free(head->ruby);
		free(head);
		head = next;
	}
	return 0;
}
