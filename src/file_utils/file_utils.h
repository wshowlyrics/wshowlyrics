#ifndef _LYRICS_FILE_UTILS_H
#define _LYRICS_FILE_UTILS_H

#include <stdbool.h>

// Calculate MD5 checksum of a file
// Returns true on success, false on error
// checksum_out must be at least 33 bytes (32 hex chars + null terminator)
bool calculate_file_md5(const char *filepath, char *checksum_out);

// Check if a file's MD5 checksum has changed
// Returns true if the file exists and checksum differs from expected
bool file_has_changed(const char *filepath, const char *expected_checksum);

#endif
