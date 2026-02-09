#include "file_utils.h"
#include "../runtime/runtime_dir.h"
#include "../../constants.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <fcntl.h>

// Static cache for directory paths (initialized on first use)
static char g_cache_base_dir[512] = {0};
static char g_cache_album_art_dir[600] = {0};
static char g_cache_lyrics_dir[600] = {0};
static char g_cache_translated_dir[600] = {0};
static bool g_cache_dirs_initialized = false;
static enum cache_mode g_cache_mode = CACHE_MODE_PERSISTENT;

void set_cache_mode(enum cache_mode mode) {
    g_cache_mode = mode;
}

bool is_cache_enabled(void) {
    return g_cache_mode != CACHE_MODE_OFF;
}

// Initialize cache directory paths using XDG Base Directory specification
static void init_cache_directories(void) {
    if (g_cache_dirs_initialized) {
        return;
    }

    if (g_cache_mode == CACHE_MODE_OFF) {
        // Empty paths — all path builders will return -1
        g_cache_base_dir[0] = '\0';
        g_cache_album_art_dir[0] = '\0';
        g_cache_lyrics_dir[0] = '\0';
        g_cache_translated_dir[0] = '\0';
        g_cache_dirs_initialized = true;
        return;
    }

    if (g_cache_mode == CACHE_MODE_SESSION) {
        // Use XDG_RUNTIME_DIR (RAM-based, cleared on reboot)
        const char *runtime = get_runtime_dir();
        if (runtime) {
            snprintf(g_cache_base_dir, sizeof(g_cache_base_dir), "%s/cache", runtime);
        } else {
            // XDG_RUNTIME_DIR not available, fall back to persistent
            log_warn("Session cache requires XDG_RUNTIME_DIR, falling back to persistent cache");
            g_cache_mode = CACHE_MODE_PERSISTENT;
            // fall through to persistent logic below
        }
    }

    if (g_cache_mode == CACHE_MODE_PERSISTENT) {
        const char *xdg_cache = getenv("XDG_CACHE_HOME");
        const char *home = getenv("HOME");

        if (xdg_cache && xdg_cache[0] != '\0') {
            snprintf(g_cache_base_dir, sizeof(g_cache_base_dir), "%s/wshowlyrics", xdg_cache);
        } else if (home && home[0] != '\0') {
            snprintf(g_cache_base_dir, sizeof(g_cache_base_dir), "%s/.cache/wshowlyrics", home);
        } else {
            // Last resort: use runtime dir
            const char *runtime = get_runtime_dir();
            if (runtime) {
                snprintf(g_cache_base_dir, sizeof(g_cache_base_dir), "%s/cache", runtime);
            }
            // If runtime is NULL, main() will have already exited
        }
    }

    // Build subdirectory paths
    snprintf(g_cache_album_art_dir, sizeof(g_cache_album_art_dir), "%s/album_art", g_cache_base_dir);
    snprintf(g_cache_lyrics_dir, sizeof(g_cache_lyrics_dir), "%s/lyrics", g_cache_base_dir);
    snprintf(g_cache_translated_dir, sizeof(g_cache_translated_dir), "%s/translated", g_cache_base_dir);

    g_cache_dirs_initialized = true;
}

// Get cache base directory path
const char* get_cache_base_dir(void) {
    init_cache_directories();
    return g_cache_base_dir;
}

// Get translated cache directory path
const char* get_cache_translated_dir(void) {
    init_cache_directories();
    return g_cache_translated_dir;
}

// Sanitize file path for logging: replaces username in $HOME with numeric UID
const char* sanitize_path(const char *path) {
    static char bufs[2][PATH_BUFFER_SIZE];
    static int idx = 0;

    if (!path) return path;

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') return path;

    size_t home_len = strlen(home);

    // Determine where $HOME starts in the path
    const char *home_pos = NULL;
    size_t prefix_len = 0;

    if (strncmp(path, home, home_len) == 0) {
        home_pos = path;
    } else if (strncmp(path, "file://", 7) == 0 &&
               strncmp(path + 7, home, home_len) == 0) {
        home_pos = path + 7;
        prefix_len = 7;
    }

    if (!home_pos) return path;

    // Find parent directory of HOME (e.g., /home/)
    const char *last_slash = strrchr(home, '/');
    if (!last_slash) return path;

    size_t parent_len = (size_t)(last_slash - home + 1);
    uid_t uid = getuid();

    char *buf = bufs[idx];
    idx = (idx + 1) % 2;

    snprintf(buf, PATH_BUFFER_SIZE, "%.*s%.*s%u%s",
             (int)prefix_len, path,
             (int)parent_len, home,
             uid,
             home_pos + home_len);

    return buf;
}

bool calculate_file_md5(const char *filepath, char *checksum_out) {
    if (!filepath || !checksum_out) {
        return false;
    }

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return false;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fclose(file);
        return false;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1) {
        EVP_MD_CTX_free(mdctx);
        fclose(file);
        return false;
    }

    unsigned char buffer[MD5_BUFFER_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, bytes_read) != 1) {
            EVP_MD_CTX_free(mdctx);
            fclose(file);
            return false;
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        fclose(file);
        return false;
    }

    EVP_MD_CTX_free(mdctx);
    fclose(file);

    // Convert to hex string
    for (unsigned int i = 0; i < hash_len; i++) {
        snprintf(checksum_out + (i * 2), 3, "%02x", hash[i]);
    }
    checksum_out[hash_len * 2] = '\0';

    return true;
}

bool file_has_changed(const char *filepath, const char *expected_checksum) {
    if (!filepath || !expected_checksum) {
        return false;
    }

    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return false; // File doesn't exist
    }

    char current_checksum[MD5_DIGEST_STRING_LENGTH];
    if (!calculate_file_md5(filepath, current_checksum)) {
        return false;
    }

    return strcmp(current_checksum, expected_checksum) != 0;
}

// Safe path building functions (no variadic args, no format string vulnerabilities)

// Join two path components: "dir/file"
int join_path_2(char *dest, size_t dest_size, const char *part1, const char *part2) {
    if (!dest || !part1 || !part2 || dest_size == 0) {
        return -1;
    }

    int written = snprintf(dest, dest_size, "%s/%s", part1, part2);
    if (written < 0 || written >= (int)dest_size) {
        return -1;
    }
    return written;
}

// Build path with extension: "dir/name.ext"
int build_path_with_ext(char *dest, size_t dest_size, const char *dir,
                        const char *name, const char *ext) {
    if (!dest || !dir || !name || !ext || dest_size == 0) {
        return -1;
    }

    int written = snprintf(dest, dest_size, "%s/%s.%s", dir, name, ext);
    if (written < 0 || written >= (int)dest_size) {
        return -1;
    }
    return written;
}

// Build path with subdirectory and extension: "dir/subdir/name.ext"
int build_path_with_subdir_ext(char *dest, size_t dest_size, const char *dir,
                               const char *subdir, const char *name, const char *ext) {
    if (!dest || !dir || !subdir || !name || !ext || dest_size == 0) {
        return -1;
    }

    int written = snprintf(dest, dest_size, "%s/%s/%s.%s", dir, subdir, name, ext);
    if (written < 0 || written >= (int)dest_size) {
        return -1;
    }
    return written;
}

// Build path for "artist - title" format: "dir/artist - title.ext"
int build_path_artist_title(char *dest, size_t dest_size, const char *dir,
                            const char *artist, const char *title, const char *ext) {
    if (!dest || !dir || !artist || !title || !ext || dest_size == 0) {
        return -1;
    }

    int written = snprintf(dest, dest_size, "%s/%s - %s.%s", dir, artist, title, ext);
    if (written < 0 || written >= (int)dest_size) {
        return -1;
    }
    return written;
}

// Build config path: "base/wshowlyrics/settings.ini"
int build_config_path(char *dest, size_t dest_size, const char *base) {
    if (!dest || !base || dest_size == 0) {
        return -1;
    }

    int written = snprintf(dest, dest_size, "%s/wshowlyrics/settings.ini", base);
    if (written < 0 || written >= (int)dest_size) {
        return -1;
    }
    return written;
}

// Build translation cache path helper: "dir/md5_lang.json"
static int build_translation_cache_path_internal(char *dest, size_t dest_size, const char *dir,
                                                  const char *md5, const char *lang) {
    if (!dest || !dir || !md5 || !lang || dest_size == 0) {
        return -1;
    }

    int written = snprintf(dest, dest_size, "%s/%s_%s.json", dir, md5, lang);
    if (written < 0 || written >= (int)dest_size) {
        return -1;
    }
    return written;
}

static bool mkdir_p(const char *path) {
    struct stat st;

    // Try to create directory first to avoid TOCTOU race condition
    // Use 0700 for privacy (owner-only access)
    if (mkdir(path, 0700) == 0) {
        return true;  // Successfully created
    }

    // If mkdir failed with EEXIST, verify it's actually a directory
    if (errno == EEXIST) {
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return true;  // Already exists and is a directory
        }
        // Exists but is not a directory
        return false;
    }

    // Other error (permission denied, parent doesn't exist, etc.)
    return false;
}

bool ensure_cache_directories(void) {
    init_cache_directories();

    if (g_cache_mode == CACHE_MODE_OFF) {
        return false;  // Cache disabled
    }

    // Create base directory
    if (!mkdir_p(g_cache_base_dir)) {
        return false;
    }

    // Create subdirectories
    if (!mkdir_p(g_cache_album_art_dir)) {
        return false;
    }

    if (!mkdir_p(g_cache_lyrics_dir)) {
        return false;
    }

    if (!mkdir_p(g_cache_translated_dir)) {
        return false;
    }

    return true;
}

int build_album_art_cache_path(char *dest, size_t dest_size, const char *md5_hash) {
    if (!dest || !md5_hash || dest_size == 0) {
        return -1;
    }

    // Validate MD5 is not empty
    if (md5_hash[0] == '\0') {
        log_error("build_album_art_cache_path: MD5 hash is empty");
        return -1;
    }

    init_cache_directories();
    return build_path_with_ext(dest, dest_size, g_cache_album_art_dir, md5_hash, "png");
}

int build_lyrics_cache_path(char *dest, size_t dest_size, const char *md5_hash) {
    if (!dest || !md5_hash || dest_size == 0) {
        return -1;
    }

    // Validate MD5 is not empty
    if (md5_hash[0] == '\0') {
        log_error("build_lyrics_cache_path: MD5 hash is empty");
        return -1;
    }

    init_cache_directories();
    return build_path_with_ext(dest, dest_size, g_cache_lyrics_dir, md5_hash, "lrc");
}

bool calculate_metadata_md5(const char *artist, const char *title, const char *album, char *md5_out) {
    if (!md5_out) {
        return false;
    }

    // Combine metadata into single string: "artist|title|album"
    // Use | as separator to avoid conflicts with common metadata characters
    char metadata[PATH_BUFFER_SIZE];
    snprintf(metadata, sizeof(metadata), "%s|%s|%s",
             artist ? artist : "",
             title ? title : "",
             album ? album : "");

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        return false;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1) {
        EVP_MD_CTX_free(mdctx);
        return false;
    }

    if (EVP_DigestUpdate(mdctx, metadata, strlen(metadata)) != 1) {
        EVP_MD_CTX_free(mdctx);
        return false;
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        return false;
    }

    EVP_MD_CTX_free(mdctx);

    // Convert to hex string
    for (unsigned int i = 0; i < hash_len; i++) {
        snprintf(md5_out + (i * 2), 3, "%02x", hash[i]);
    }
    md5_out[hash_len * 2] = '\0';

    return true;
}

// Note: This is a wrapper function - the actual implementation is the helper above
int build_translation_cache_path(char *dest, size_t dest_size,
                                  const char *original_md5,
                                  const char *target_lang) {
    if (!dest || !original_md5 || !target_lang || dest_size == 0) {
        return -1;
    }

    // Validate MD5 is not empty
    if (original_md5[0] == '\0') {
        log_error("build_translation_cache_path: MD5 checksum is empty");
        return -1;
    }

    init_cache_directories();

    return build_translation_cache_path_internal(dest, dest_size, g_cache_translated_dir,
                                                   original_md5, target_lang);
}

// Helper: Handle unlinkat failure (ignore ENOENT, log other errors)
// Returns: true if error should be ignored (ENOENT), false if real error
static bool handle_unlinkat_failure(const char *path, const char *filename, bool is_warning) {
    if (errno == ENOENT) {
        // File was already deleted - not an error
        return true;
    }

    if (is_warning) {
        log_warn("Failed to remove file %s: %s", filename, strerror(errno));
    } else {
        log_error("Failed to remove file %s/%s: %s", sanitize_path(path), filename, strerror(errno));
    }

    return false;
}

// Recursively remove directory and its contents
// Uses fstatat() and unlinkat() to avoid TOCTOU race conditions
static bool remove_directory_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        // Directory doesn't exist or can't be opened
        if (errno == ENOENT) {
            return true; // Already doesn't exist - success
        }
        log_error("Failed to open directory %s: %s", sanitize_path(path), strerror(errno));
        return false;
    }

    int dir_fd = dirfd(dir);
    if (dir_fd == -1) {
        log_error("Failed to get directory fd for %s: %s", sanitize_path(path), strerror(errno));
        closedir(dir);
        return false;
    }

    struct dirent *entry;
    bool success = true;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        struct stat st;
        // Use fstatat() with dirfd to avoid TOCTOU race condition
        // AT_SYMLINK_NOFOLLOW: don't follow symlinks (security)
        if (fstatat(dir_fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) == -1) {
            log_error("Failed to stat %s/%s: %s", sanitize_path(path), entry->d_name, strerror(errno));
            success = false;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Recursively remove subdirectory
            char full_path[PATH_BUFFER_SIZE];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            if (!remove_directory_recursive(full_path)) {
                success = false;
            }
        } else {
            // Remove file using unlinkat() with dirfd (atomicity)
            // File might have been deleted by another process (race condition)
            if (unlinkat(dir_fd, entry->d_name, 0) == -1 &&
                !handle_unlinkat_failure(path, entry->d_name, false)) {
                success = false;
            }
        }
    }

    closedir(dir);

    // Remove the directory itself
    if (rmdir(path) == -1) {
        log_error("Failed to remove directory %s: %s", sanitize_path(path), strerror(errno));
        return false;
    }

    return success;
}

bool purge_cache(const char *type) {
    if (!type) {
        log_error("purge_cache: type is NULL");
        return false;
    }

    init_cache_directories();

    bool success = true;
    int removed_count = 0;

    if (strcmp(type, "all") == 0) {
        log_info("Purging all cache directories...");
        if (remove_directory_recursive(g_cache_base_dir)) {
            log_info("Successfully purged all cache");
            removed_count++;
        } else {
            log_error("Failed to purge all cache");
            success = false;
        }
    } else if (strcmp(type, "translations") == 0) {
        log_info("Purging translation cache...");
        if (remove_directory_recursive(g_cache_translated_dir)) {
            log_info("Successfully purged translation cache");
            removed_count++;
        } else {
            log_error("Failed to purge translation cache");
            success = false;
        }
    } else if (strcmp(type, "album-art") == 0) {
        log_info("Purging album art cache...");
        if (remove_directory_recursive(g_cache_album_art_dir)) {
            log_info("Successfully purged album art cache");
            removed_count++;
        } else {
            log_error("Failed to purge album art cache");
            success = false;
        }
    } else if (strcmp(type, "lyrics") == 0) {
        log_info("Purging lyrics cache...");
        if (remove_directory_recursive(g_cache_lyrics_dir)) {
            log_info("Successfully purged lyrics cache");
            removed_count++;
        } else {
            log_error("Failed to purge lyrics cache");
            success = false;
        }
    } else {
        log_error("purge_cache: invalid type '%s' (must be 'all', 'translations', 'album-art', or 'lyrics')", type);
        return false;
    }

    if (removed_count > 0) {
        log_info("Cache purge completed");
    }

    return success;
}

// Clean up old cache files in a directory
// Uses fstatat() and unlinkat() to avoid TOCTOU race conditions
static int cleanup_directory(const char *dir_path, int max_days) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        if (errno == ENOENT) {
            return 0;  // Directory doesn't exist, nothing to clean
        }
        log_warn("Failed to open directory %s: %s", sanitize_path(dir_path), strerror(errno));
        return 0;
    }

    int dir_fd = dirfd(dir);
    if (dir_fd == -1) {
        log_warn("Failed to get directory fd for %s: %s", sanitize_path(dir_path), strerror(errno));
        closedir(dir);
        return 0;
    }

    int removed_count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        struct stat st;
        // Use fstatat() with dirfd to avoid TOCTOU race condition
        // AT_SYMLINK_NOFOLLOW: don't follow symlinks (security)
        if (fstatat(dir_fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) == -1) {
            continue;
        }

        // Only process regular files
        if (!S_ISREG(st.st_mode)) {
            continue;
        }

        // Check if file is old enough to delete (based on access time)
        time_t now = time(NULL);
        if (now == (time_t)-1) {
            continue;
        }

        time_t file_atime = st.st_atime;  // Last access time
        double age_seconds = difftime(now, file_atime);
        double max_seconds = max_days * 24 * 60 * 60;

        if (age_seconds > max_seconds) {
            // Remove file using unlinkat() with dirfd (atomicity)
            if (unlinkat(dir_fd, entry->d_name, 0) == 0) {
                log_info("Removed old cache file: %s (not accessed for %d+ days)", entry->d_name, max_days);
                removed_count++;
            } else {
                // File might have been deleted by another process (race condition)
                handle_unlinkat_failure(NULL, entry->d_name, true);
            }
        }
    }

    closedir(dir);
    return removed_count;
}

// Auto cleanup old cache files based on config policy
bool auto_cleanup_old_cache(int max_days) {
    if (max_days < 0) {
        // Cleanup disabled
        return true;
    }

    init_cache_directories();

    log_info("Starting automatic cache cleanup (removing files older than %d days)...", max_days);

    int total_removed = 0;

    // Clean up each cache directory
    total_removed += cleanup_directory(g_cache_translated_dir, max_days);
    total_removed += cleanup_directory(g_cache_album_art_dir, max_days);
    total_removed += cleanup_directory(g_cache_lyrics_dir, max_days);

    if (total_removed > 0) {
        log_info("Cache cleanup complete: removed %d old file(s)", total_removed);
    } else {
        log_info("Cache cleanup complete: no old files found");
    }

    return true;
}

// Update access time of a cache file (touch)
bool touch_cache_file(const char *filepath) {
    if (!filepath) {
        return false;
    }

    // Update access and modification time to current time
    if (utime(filepath, NULL) == 0) {
        return true;
    }

    // If utime fails, it might not exist yet, which is fine
    if (errno != ENOENT) {
        log_warn("Failed to update access time for %s: %s", sanitize_path(filepath), strerror(errno));
    }

    return false;
}
