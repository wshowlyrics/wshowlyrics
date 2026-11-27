// SPDX-License-Identifier: GPL-3.0-or-later
#include "json_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_whitespace(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

// Given the first character after a string's opening quote, the closing quote;
// NULL if the string is not closed. A backslash takes the next character with
// it, so \" does not end the string and \\" does.
static const char *string_end(const char *p) {
    while (*p && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        p++;
    }
    return *p == '"' ? p : NULL;
}

// Where the value of key starts, looking from start up to end (NULL: to the
// end of the text). NULL if the key is not there.
static const char *find_value(const char *start, const char *end, const char *key) {
    if (!start || !key) {
        return NULL;
    }

    char pattern[JSON_PATTERN_SIZE];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const size_t pattern_len = strlen(pattern);

    // "key" also matches a string *value* that happens to read like the key;
    // only an occurrence followed by a colon is the key
    const char *value = NULL;
    const char *pos = strstr(start, pattern);
    while (pos && !value && (!end || pos < end)) {
        const char *after = skip_whitespace(pos + pattern_len);
        if (*after == ':') {
            value = skip_whitespace(after + 1);
        } else {
            pos = strstr(pos + 1, pattern);
        }
    }
    return (value && (!end || value < end)) ? value : NULL;
}

// What a backslash followed by c stands for, or 0 if the pair is kept as is
// (\uXXXX and anything unknown)
static char unescaped(char c) {
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '"': return '"';
        case '\\': return '\\';
        case '/': return '/';
        default: return 0;
    }
}

char* json_extract_string_in(const char *start, const char *end, const char *key) {
    const char *value = find_value(start, end, key);
    if (!value || *value != '"') {
        return NULL;
    }
    value++;

    const char *close = string_end(value);
    if (!close || (end && close >= end)) {
        return NULL;
    }

    const size_t len = (size_t)(close - value);
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }

    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        const char plain = (value[i] == '\\' && i + 1 < len) ? unescaped(value[i + 1]) : 0;
        if (plain) {
            result[out++] = plain;
            i++;
        } else {
            result[out++] = value[i];
        }
    }
    result[out] = '\0';
    return result;
}

char* json_extract_string(const char *json, const char *key) {
    return json_extract_string_in(json, NULL, key);
}

int64_t json_extract_int_in(const char *start, const char *end, const char *key) {
    const char *value = find_value(start, end, key);
    if (!value) {
        return -1;
    }

    char *number_end;
    const int64_t number = strtoll(value, &number_end, 10);
    return number_end == value ? -1 : number;
}

int64_t json_extract_int(const char *json, const char *key) {
    return json_extract_int_in(json, NULL, key);
}

const char* json_object_end(const char *obj_start) {
    if (!obj_start || *obj_start != '{') {
        return NULL;
    }

    int depth = 0;
    const char *end = NULL;
    const char *p = obj_start;
    while (p && *p && !end) {
        if (*p == '"') {
            p = string_end(p + 1);   // NULL (an unclosed string) ends the walk
        } else if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                end = p;
            }
        }
        if (p) {
            p++;
        }
    }
    return end;
}
