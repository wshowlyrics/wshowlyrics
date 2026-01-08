#include "state_helpers.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* state_helpers_escape_newlines(const char *text) {
    if (!text) {
        return NULL;
    }

    // Allocate buffer (same size, we'll only remove chars)
    size_t len = strlen(text);
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }

    // Copy and strip newlines (CR, LF, CRLF)
    char *dst = result;
    const char *src = text;
    while (*src) {
        if (*src == '\r' || *src == '\n') {
            // Skip newline characters
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    return result;
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
