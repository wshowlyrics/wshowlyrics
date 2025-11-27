#ifndef _STRING_UTILS_H
#define _STRING_UTILS_H

// Sanitize title by removing YouTube ID and file extensions
// Example: "[4K60FPS] Song Title [AbCdEfG1234].mkv" -> "Song Title"
// Caller must free the returned string
char* sanitize_title(const char *title);

#endif
