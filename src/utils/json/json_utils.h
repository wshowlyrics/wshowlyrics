#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdint.h>

// Extract string value from JSON (looks for "key":"value")
// Returns allocated string (caller must free) or NULL on error
// Handles escape sequences (\n, \t, \r, \", \\)
char* json_extract_string(const char *json, const char *key);

// Extract string value from JSON starting at specific position
// Useful for parsing JSON arrays or multiple occurrences
char* json_extract_string_from(const char *json, const char *key, const char *search_start);

// Extract integer value from JSON (looks for "key":value or "key": value)
// Returns -1 on error or if key not found
int64_t json_extract_int(const char *json, const char *key);

// Extract integer value starting at specific position
int64_t json_extract_int_from(const char *json, const char *key, const char *search_start);

#endif // JSON_UTILS_H
