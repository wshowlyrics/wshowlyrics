#ifndef _LYRICS_FILE_UTILS_H
#define _LYRICS_FILE_UTILS_H

#include <stdbool.h>
#include <stddef.h>

// Calculate MD5 checksum of a file
// Returns true on success, false on error
// checksum_out must be at least 33 bytes (32 hex chars + null terminator)
bool calculate_file_md5(const char *filepath, char *checksum_out);

// Check if a file's MD5 checksum has changed
// Returns true if the file exists and checksum differs from expected
bool file_has_changed(const char *filepath, const char *expected_checksum);

// Build a file path safely with bounds checking
// Returns number of characters written (excluding null), or -1 on error/truncation
// Pattern examples: "%s/%s.%s" for "dir/file.ext", "%s/.lyrics" for "home/.lyrics"
int build_path(char *dest, size_t dest_size, const char *fmt, ...);

// Ensure /tmp/wshowlyrics cache directories exist
// Creates: /tmp/wshowlyrics/album_art/ and /tmp/wshowlyrics/lyrics/
// Returns true on success, false on error
bool ensure_cache_directories(void);

// Build cache file path for album art
// md5_hash: 32-char hex string
// Returns number of characters written, or -1 on error
int build_album_art_cache_path(char *dest, size_t dest_size, const char *md5_hash);

// Build cache file path for lyrics
// md5_hash: 32-char hex string
// Returns number of characters written, or -1 on error
int build_lyrics_cache_path(char *dest, size_t dest_size, const char *md5_hash);

// Calculate MD5 hash of metadata (artist-title-album)
// Returns true on success, false on error
// md5_out must be at least 33 bytes (32 hex chars + null terminator)
bool calculate_metadata_md5(const char *artist, const char *title, const char *album, char *md5_out);

// Build cache file path for translated lyrics
// original_md5: MD5 hash of the original lyrics file
// target_lang: Target language code (e.g., "EN", "KO", "JA")
// Returns number of characters written, or -1 on error
int build_translation_cache_path(char *dest, size_t dest_size,
                                  const char *original_md5,
                                  const char *target_lang);

#endif
