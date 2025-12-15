#include "state_helpers.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* state_helpers_escape_newlines(const char *text) {
    if (!text) {
        return NULL;
    }

    // Count newlines to calculate needed buffer size
    // CRLF needs 4 chars (^M^J), LF needs 2 chars (^J), CR needs 2 chars (^M)
    size_t extra_chars = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\r' && *(p + 1) == '\n') {
            extra_chars += 3; // ^M^J (4 chars) - 2 original = 2 extra
            p++; // Skip the \n
        } else if (*p == '\n') {
            extra_chars += 1; // ^J (2 chars) - 1 original = 1 extra
        } else if (*p == '\r') {
            extra_chars += 1; // ^M (2 chars) - 1 original = 1 extra
        }
    }

    // Allocate buffer
    size_t len = strlen(text);
    char *escaped = malloc(len + extra_chars + 1);
    if (!escaped) {
        return NULL;
    }

    // Copy and escape newlines
    char *dst = escaped;
    for (const char *src = text; *src; src++) {
        if (*src == '\r' && *(src + 1) == '\n') {
            // CRLF -> ^M^J
            *dst++ = '^';
            *dst++ = 'M';
            *dst++ = '^';
            *dst++ = 'J';
            src++; // Skip the \n
        } else if (*src == '\n') {
            // LF -> ^J
            *dst++ = '^';
            *dst++ = 'J';
        } else if (*src == '\r') {
            // CR -> ^M
            *dst++ = '^';
            *dst++ = 'M';
        } else {
            *dst++ = *src;
        }
    }
    *dst = '\0';

    return escaped;
}

uint32_t state_helpers_parse_color(const char *color) {
    if (color[0] == '#') {
        ++color;
    }

    const int len = strlen(color);
    if (len != 6 && len != 8) {
        log_warn("Invalid color %s, defaulting to color 0xFFFFFFFF", color);
        return 0xFFFFFFFF;
    }
    uint32_t res = (uint32_t)strtoul(color, NULL, 16);
    if (len == 6) {
        res = (res << 8) | 0xFF;
    }
    return res;
}
