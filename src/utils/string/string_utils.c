#include "string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

// Sanitize title by removing YouTube ID and file extensions
// Example: "[4K60FPS] Song Title [AbCdEfG1234].mkv" -> "Song Title"
char* sanitize_title(const char *title) {
    if (!title) return NULL;

    char *result = strdup(title);
    if (!result) return NULL;

    // Remove file extensions (.mkv, .mp4, .mp3, .flac, .m4a, .webm, etc.)
    char *ext = strrchr(result, '.');
    if (ext) {
        const char *common_exts[] = {
            ".mkv", ".mp4", ".avi", ".webm", ".mov", ".flv",
            ".mp3", ".flac", ".m4a", ".aac", ".ogg", ".opus", ".wav",
            NULL
        };
        for (int i = 0; common_exts[i]; i++) {
            if (strcasecmp(ext, common_exts[i]) == 0) {
                *ext = '\0';
                break;
            }
        }
    }

    // Remove YouTube ID pattern: [alphanumeric 11 chars] or (alphanumeric 11 chars)
    // Also remove quality tags like [4K], [60FPS], etc.
    char *write_pos = result;
    char *read_pos = result;

    while (*read_pos) {
        if (*read_pos == '[' || *read_pos == '(') {
            char open = *read_pos;
            char close = (open == '[') ? ']' : ')';
            char *bracket_end = strchr(read_pos + 1, close);

            if (bracket_end) {
                // Skip this bracketed content
                read_pos = bracket_end + 1;
                continue;
            }
        }
        *write_pos++ = *read_pos++;
    }
    *write_pos = '\0';

    // Trim leading/trailing whitespace
    char *trimmed = trim_whitespace(result);

    // If trimmed pointer moved, we need to shift the string within the buffer
    if (trimmed != result) {
        memmove(result, trimmed, strlen(trimmed) + 1);
    }

    return result;
}

// Trim leading and trailing whitespace from string (in-place)
char* trim_whitespace(char *str) {
    if (!str) return NULL;

    // Trim leading whitespace
    while (isspace((unsigned char)*str)) {
        str++;
    }

    // Empty string after trimming leading whitespace
    if (*str == '\0') {
        return str;
    }

    // Trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    return str;
}

// Escape special shell characters in a string for safe use in shell commands
void escape_shell_string(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        if (output && output_size > 0) {
            output[0] = '\0';
        }
        return;
    }

    const char *src = input;
    char *dst = output;
    size_t remaining = output_size - 1;

    while (*src && remaining > 1) {
        if ((*src == '"' || *src == '\\' || *src == '$' || *src == '`') && remaining > 2) {
            *dst++ = '\\';
            remaining--;
        }
        *dst++ = *src++;
        remaining--;
    }
    *dst = '\0';
}
