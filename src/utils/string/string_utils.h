#ifndef _STRING_UTILS_H
#define _STRING_UTILS_H

#include <stddef.h>

// Sanitize title by removing YouTube ID and file extensions
// Example: "[4K60FPS] Song Title [AbCdEfG1234].mkv" -> "Song Title"
// Caller must free the returned string
char* sanitize_title(const char *title);

// Trim leading and trailing whitespace from string (in-place)
// Returns pointer to trimmed string (within original buffer)
// NULL-safe: returns NULL if input is NULL
char* trim_whitespace(char *str);

// Escape special shell characters in a string for safe use in shell commands
// Escapes: " \ $ `
// output_size must be at least 2x input length to handle worst case
void escape_shell_string(const char *input, char *output, size_t output_size);

#endif
