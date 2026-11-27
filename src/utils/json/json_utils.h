// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdint.h>

// Reading single values out of JSON text without building a tree. Enough for
// the flat answers of lrclib.net and the iTunes Search API; the text comes
// from the network, so nothing here trusts it to be well formed. A key is
// found as "key" followed by a colon, with or without white space around it.
// (The translators' replies are parsed with json-c instead.)

// The string value of key. Returns an allocated string (caller frees) with the
// escapes \n \t \r \" \\ \/ resolved (\uXXXX, \b and \f are left as written),
// or NULL if the key is not there or its value is not a string (e.g. null).
char* json_extract_string(const char *json, const char *key);

// The integer value of key (a fraction is cut off). Returns -1 if the key is
// not there or its value is not a number.
int64_t json_extract_int(const char *json, const char *key);

// The same, looking only at the text from start up to (not including) end -
// e.g. one object of an array, so that a key missing from it is not taken from
// the next object. end == NULL means up to the end of the text.
char* json_extract_string_in(const char *start, const char *end, const char *key);
int64_t json_extract_int_in(const char *start, const char *end, const char *key);

// Given the '{' that opens an object, the '}' that closes it - braces inside
// strings and nested objects are stepped over. NULL if obj_start is NULL, does
// not point at '{', or the object is not closed.
const char* json_object_end(const char *obj_start);

#endif // JSON_UTILS_H
