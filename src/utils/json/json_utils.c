#include "json_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Extract string value from JSON starting at specific position
char* json_extract_string_from(const char *json, const char *key, const char *search_start) {
    if (!json || !key) return NULL;

    // Build search pattern: "key":"
    char pattern[JSON_PATTERN_SIZE];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *start_pos = search_start ? search_start : json;
    const char *start = strstr(start_pos, pattern);
    if (!start) return NULL;

    start += strlen(pattern);
    const char *end = start;

    // Find the closing quote, handling escaped characters
    while (*end) {
        if (*end == '"' && (end == start || *(end - 1) != '\\')) {
            break;
        }
        end++;
    }

    if (*end != '"') return NULL;

    size_t len = end - start;
    char *escaped = malloc(len + 1);
    if (!escaped) return NULL;

    memcpy(escaped, start, len);
    escaped[len] = '\0';

    // Unescape escape sequences (\n, \t, \r, \", \\)
    char *result = malloc(len + 1);
    if (!result) {
        free(escaped);
        return NULL;
    }

    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        if (escaped[i] == '\\' && i + 1 < len) {
            if (escaped[i + 1] == 'n') {
                result[j++] = '\n';
                i += 2;
            } else if (escaped[i + 1] == 't') {
                result[j++] = '\t';
                i += 2;
            } else if (escaped[i + 1] == 'r') {
                result[j++] = '\r';
                i += 2;
            } else if (escaped[i + 1] == '"') {
                result[j++] = '"';
                i += 2;
            } else if (escaped[i + 1] == '\\') {
                result[j++] = '\\';
                i += 2;
            } else {
                result[j++] = escaped[i++];
            }
        } else {
            result[j++] = escaped[i++];
        }
    }
    result[j] = '\0';

    free(escaped);
    return result;
}

// Extract string value from JSON (looks for "key":"value")
char* json_extract_string(const char *json, const char *key) {
    return json_extract_string_from(json, key, NULL);
}

// Extract integer value from JSON starting at specific position
int64_t json_extract_int_from(const char *json, const char *key, const char *search_start) {
    if (!json || !key) return -1;

    // Build search pattern: "key":
    char pattern[JSON_PATTERN_SIZE];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char *start_pos = search_start ? search_start : json;
    const char *start = strstr(start_pos, pattern);
    if (!start) return -1;

    start += strlen(pattern);

    // Skip whitespace
    while (*start == ' ' || *start == '\t') start++;

    // Parse integer
    char *end;
    int64_t value = strtoll(start, &end, 10);
    if (end == start) return -1;

    return value;
}

// Extract integer value from JSON (looks for "key":value)
int64_t json_extract_int(const char *json, const char *key) {
    return json_extract_int_from(json, key, NULL);
}
