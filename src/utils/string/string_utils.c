// SPDX-License-Identifier: GPL-3.0-or-later
#include "string_utils.h"
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static bool is_media_extension(const char *ext) {
    static const char *const media_exts[] = {
        // audio
        ".mp3", ".flac", ".ogg", ".oga", ".opus", ".m4a", ".m4b", ".aac", ".wav",
        ".wma", ".ape", ".wv", ".mpc", ".tta", ".aiff", ".mka",
        // video
        ".mp4", ".mkv", ".avi", ".webm", ".mov", ".flv", ".wmv", ".ts", ".m2ts", ".3gp",
        NULL
    };

    for (int i = 0; media_exts[i]; i++) {
        if (strcasecmp(ext, media_exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

void strip_media_extension(char *name) {
    if (!name) {
        return;
    }
    char *dot = strrchr(name, '.');
    if (dot && dot != name && is_media_extension(dot)) {
        *dot = '\0';
    }
}

// Longest tail after the last dot that is still taken for a file extension
#define MAX_FILE_EXTENSION_CHARS 5

void strip_file_extension(char *filename) {
    if (!filename) {
        return;
    }
    char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return;
    }

    const char *end = dot + 1;
    while (isalnum((unsigned char)*end)) {
        end++;
    }
    size_t length = (size_t)(end - dot - 1);
    if (*end == '\0' && length > 0 && length <= MAX_FILE_EXTENSION_CHARS) {
        *dot = '\0';
    }
}

// Sanitize title by removing bracketed parts such as a YouTube ID or quality tag.
// The media file extension is already gone: mpris.c cuts it when it reads the title.
// Example: "[4K60FPS] Song Title [AbCdEfG1234]" -> "Song Title"
char* sanitize_title(const char *title) {
    if (!title) return NULL;

    char *result = strdup(title);
    if (!result) return NULL;

    // Remove YouTube ID pattern: [alphanumeric 11 chars] or (alphanumeric 11 chars)
    // Also remove quality tags like [4K], [60FPS], etc.
    char *write_pos = result;
    const char *read_pos = result;

    while (*read_pos) {
        if (*read_pos == '[' || *read_pos == '(') {
            char open = *read_pos;
            char close = (open == '[') ? ']' : ')';
            const char *bracket_end = strchr(read_pos + 1, close);

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
    const char *trimmed = trim_whitespace(result);

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
