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

// Safe path building functions (no variadic args, no format string vulnerabilities)
// All functions return number of characters written (excluding null), or -1 on error/truncation

// Join two path components: "dir/file"
int join_path_2(char *dest, size_t dest_size, const char *part1, const char *part2);

// Build path with extension: "dir/name.ext"
int build_path_with_ext(char *dest, size_t dest_size, const char *dir,
                        const char *name, const char *ext);

// Build path with subdirectory and extension: "dir/subdir/name.ext"
int build_path_with_subdir_ext(char *dest, size_t dest_size, const char *dir,
                               const char *subdir, const char *name, const char *ext);

// Build path for "artist - title" format: "dir/artist - title.ext"
int build_path_artist_title(char *dest, size_t dest_size, const char *dir,
                            const char *artist, const char *title, const char *ext);

// Build config path: "base/wshowlyrics/settings.ini"
int build_config_path(char *dest, size_t dest_size, const char *base);

// Get cache base directory path
// Returns $XDG_CACHE_HOME/wshowlyrics or $HOME/.cache/wshowlyrics
const char* get_cache_base_dir(void);

// Sanitize file path for logging: replaces username in $HOME with numeric UID
// e.g., /home/username/Music/song.lrc -> /home/1000/Music/song.lrc
// Also handles file:// URLs: file:///home/username/... -> file:///home/1000/...
// Uses 2 rotating internal buffers (safe for up to 2 calls in a single expression)
// If path doesn't contain $HOME prefix, returns the original pointer unchanged
const char* sanitize_path(const char *path);

// Get translated cache directory path
// Returns $XDG_CACHE_HOME/wshowlyrics/translated or $HOME/.cache/wshowlyrics/translated
const char* get_cache_translated_dir(void);

// Ensure cache directories exist using XDG Base Directory specification
// Creates: $XDG_CACHE_HOME/wshowlyrics/{album_art,lyrics,translated}/
// Returns true on success, false on error
bool ensure_cache_directories(void);

// Purge (delete) cache directories
// type: "all", "translations", "album-art", or "lyrics"
// Returns true on success, false on error
bool purge_cache(const char *type);

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

// Automatically cleanup old cache files based on policy
// max_days: Maximum age in days (-1 to disable cleanup)
// Removes files not accessed for max_days from all cache directories
// Returns true on success, false on error
bool auto_cleanup_old_cache(int max_days);

// Update access time of a cache file (touch)
// filepath: Full path to cache file
// Updates both access and modification time to current time
// Returns true on success, false if file doesn't exist or error
bool touch_cache_file(const char *filepath);

#endif
