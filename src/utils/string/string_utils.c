#include "string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
    char *start = result;
    while (*start == ' ' || *start == '\t') start++;

    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t')) {
        *end = '\0';
        end--;
    }

    // If start != result, we need to move the string
    if (start != result) {
        memmove(result, start, strlen(start) + 1);
    }

    return result;
}
