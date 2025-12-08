#include "lyrics_provider.h"
#include "../lrclib/lrclib_provider.h"
#include "../../parser/lrc/lrc_parser.h"
#include "../../parser/srt/srt_parser.h"
#include "../../parser/lrcx/lrcx_parser.h"
#include "../../user_experience/config/config.h"
#include "../../constants.h"
#include "../../utils/file/file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

// ============================================================================
// Local file provider
// ============================================================================

static char* sanitize_filename(const char *str) {
    if (!str) return NULL;

    char *result = strdup(str);
    if (!result) return NULL;

    // Replace invalid filename characters
    for (char *p = result; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }

    return result;
}

static char* remove_extension(const char *str) {
    if (!str) return NULL;

    char *result = strdup(str);
    if (!result) return NULL;

    // Find the last dot
    char *dot = strrchr(result, '.');
    if (dot && dot != result) {
        // Common audio/video extensions to remove
        const char *exts[] = {
            ".mp3", ".flac", ".ogg", ".opus", ".m4a", ".aac", ".wav",
            ".mp4", ".mkv", ".avi", ".webm", ".mov",
            NULL
        };

        for (int i = 0; exts[i]; i++) {
            if (strcasecmp(dot, exts[i]) == 0) {
                *dot = '\0';
                break;
            }
        }
    }

    return result;
}

static bool try_load_lyrics_file(const char *path, struct lyrics_data *data) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }

    // Check file extension to determine parser
    const char *ext = strrchr(path, '.');
    if (!ext) return false;

    // Check if this extension is enabled in config
    if (!config_is_extension_enabled(ext)) {
        return false;
    }

    bool success = false;

    // Try LRCX only for .lrcx files
    if (strcasecmp(ext, ".lrcx") == 0) {
        if (lrcx_parse_file(path, data)) {
            log_success("Loaded LRCX file: %s", path);
            success = true;
        }
    }

    // Try LRC for .lrc files
    if (strcasecmp(ext, ".lrc") == 0) {
        if (lrc_parse_file(path, data)) {
            log_success("Loaded LRC file: %s", path);
            success = true;
        }
    }

    // Try SRT for .srt and .vtt files
    if (strcasecmp(ext, ".srt") == 0 || strcasecmp(ext, ".vtt") == 0) {
        if (srt_parse_file(path, data)) {
            log_success("Loaded %s file: %s", strcasecmp(ext, ".vtt") == 0 ? "VTT" : "SRT", path);
            success = true;
        }
    }

    if (success) {
        // Store the file path and calculate checksum
        data->source_file_path = strdup(path);
        if (!calculate_file_md5(path, data->md5_checksum)) {
            log_warn("Failed to calculate MD5 checksum for %s", path);
            data->md5_checksum[0] = '\0';
        }
    }

    return success;
}

// URL decode a string (handles %XX encoding)
static char* url_decode_string(const char *str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    char *decoded = malloc(len + 1);
    if (!decoded) return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        if (str[i] == '%' && i + 2 < len) {
            // Decode %XX
            char hex[3] = { str[i+1], str[i+2], '\0' };
            char *endptr;
            long val = strtol(hex, &endptr, 16);
            if (*endptr == '\0') {
                decoded[j++] = (char)val;
                i += 3;
                continue;
            }
        }
        decoded[j++] = str[i++];
    }
    decoded[j] = '\0';

    return decoded;
}

// Cleanup partially allocated extension array (helper for error paths)
static void cleanup_partial_extensions(char **exts, int count) {
    if (!exts) return;
    for (int i = 0; i < count; i++) {
        free(exts[i]);
    }
    free(exts);
}

// Get ordered list of extensions from config
// Returns NULL-terminated array of extension strings
// Caller must free the array and all strings
static char** get_extension_priority(void) {
    if (!g_config.lyrics.extensions || g_config.lyrics.extensions[0] == '\0') {
        // Default: all extensions in default order
        char **exts = malloc(5 * sizeof(char*));
        if (!exts) return NULL;

        // Initialize all pointers to NULL for safe cleanup
        for (int i = 0; i < 5; i++) {
            exts[i] = NULL;
        }

        // Allocate strings with NULL checks
        exts[0] = strdup("lrcx");
        if (!exts[0]) goto cleanup_default;
        exts[1] = strdup("lrc");
        if (!exts[1]) goto cleanup_default;
        exts[2] = strdup("srt");
        if (!exts[2]) goto cleanup_default;
        exts[3] = strdup("vtt");
        if (!exts[3]) goto cleanup_default;
        exts[4] = NULL;
        return exts;

cleanup_default:
        cleanup_partial_extensions(exts, 5);
        return NULL;
    }

    // Parse comma-separated list
    char *exts_copy = strdup(g_config.lyrics.extensions);
    if (!exts_copy) return NULL;

    // Count extensions
    int count = 1;
    for (char *p = exts_copy; *p; p++) {
        if (*p == ',') count++;
    }

    char **result = malloc((count + 1) * sizeof(char*));
    if (!result) {
        free(exts_copy);
        return NULL;
    }

    // Initialize all pointers to NULL for safe cleanup
    for (int i = 0; i <= count; i++) {
        result[i] = NULL;
    }

    // Split and trim
    int idx = 0;
    char *token = strtok(exts_copy, ",");
    while (token && idx < count) {
        char *trimmed = config_trim_whitespace(token);
        result[idx] = strdup(trimmed);
        if (!result[idx]) {
            // Cleanup on allocation failure
            cleanup_partial_extensions(result, idx);
            free(exts_copy);
            return NULL;
        }
        idx++;
        token = strtok(NULL, ",");
    }
    result[idx] = NULL;

    free(exts_copy);
    return result;
}

// Free extension priority array
static void free_extension_priority(char **exts) {
    if (!exts) return;
    for (int i = 0; exts[i]; i++) {
        free(exts[i]);
    }
    free(exts);
}

static char* get_directory_from_url(const char *url) {
    if (!url) return NULL;

    // Skip non-file URLs (http://, https://, spotify:, etc.)
    // These should not be used for local file searching
    if (strstr(url, "://") != NULL && strncmp(url, "file://", 7) != 0) {
        return NULL;
    }

    // Handle file:// URLs
    const char *path = url;
    if (strncmp(url, "file://", 7) == 0) {
        path = url + 7;
    }

    // URL decode the path to handle Unicode and special characters
    char *decoded_path = url_decode_string(path);
    if (!decoded_path) return NULL;

    // Get directory part
    char *last_slash = strrchr(decoded_path, '/');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        free(decoded_path);
        return NULL;
    }

    log_info("Extracted directory from URL: %s", decoded_path);
    return decoded_path;
}

// Extract filename from URL (without extension)
static char* get_filename_from_url(const char *url) {
    if (!url) return NULL;

    // Skip non-file URLs (http://, https://, spotify:, etc.)
    // These should not be used for local file searching
    if (strstr(url, "://") != NULL && strncmp(url, "file://", 7) != 0) {
        return NULL;
    }

    const char *path = url;
    if (strncmp(url, "file://", 7) == 0) {
        path = url + 7;
    }

    // URL decode
    char *decoded = url_decode_string(path);
    if (!decoded) return NULL;

    // Get filename part
    char *last_slash = strrchr(decoded, '/');
    char *filename = last_slash ? (last_slash + 1) : decoded;

    // Remove extension
    char *result = remove_extension(filename);

    free(decoded);
    return result;
}

// Check if running from local build directory
static bool is_local_build_executable(void) {
    char exe_path[PATH_BUFFER_SIZE];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return false;
    }

    exe_path[len] = '\0';
    // Check if executable path contains "build/" or ends with "/lyrics" in local directory
    return (strstr(exe_path, "/build/") != NULL ||
            (strstr(exe_path, "/lyrics") != NULL && exe_path[0] != '/' &&
             strncmp(exe_path, "/usr/", 5) != 0));
}

// Build list of directories to search for lyrics files
// Returns number of directories added
static int build_search_directories(const char **search_dirs, int max_dirs,
                                    const char *current_dir, char *lyrics_dir_buf, size_t buf_size) {
    int dir_count = 0;

    // Priority 1: Directory of currently playing file
    if (current_dir && dir_count < max_dirs) {
        search_dirs[dir_count++] = current_dir;
    }

    // Priority 2: Current directory (only for local builds)
    if (is_local_build_executable() && dir_count < max_dirs) {
        search_dirs[dir_count++] = ".";
    }

    // Priority 3: XDG_MUSIC_DIR
    const char *xdg_music = getenv("XDG_MUSIC_DIR");
    if (xdg_music && dir_count < max_dirs) {
        search_dirs[dir_count++] = xdg_music;
    }

    // Priority 4: ~/.lyrics directory
    const char *home = getenv("HOME");
    if (home && dir_count < max_dirs) {
        if (build_path(lyrics_dir_buf, buf_size, "%s/.lyrics", home) >= 0) {
            search_dirs[dir_count++] = lyrics_dir_buf;
        }
    }

    // Priority 5: Home directory
    if (home && dir_count < max_dirs) {
        search_dirs[dir_count++] = home;
    }

    return dir_count;
}

// Try to find lyrics using exact filename from URL
static bool try_exact_filename(const char *dir, const char *filename,
                                char **extensions, struct lyrics_data *data) {
    if (!filename) {
        return false;
    }

    char path[PATH_BUFFER_SIZE];
    for (int ext_idx = 0; extensions[ext_idx]; ext_idx++) {
        if (build_path(path, sizeof(path), "%s/%s.%s", dir, filename, extensions[ext_idx]) < 0) {
            continue;  // Path too long, skip
        }
        log_info("Trying: %s", path);
        if (try_load_lyrics_file(path, data)) {
            return true;
        }
    }

    return false;
}

// Try to find lyrics using title-based patterns
static bool try_title_patterns(const char *dir, const char *title_safe,
                                const char *artist_safe, char **extensions,
                                struct lyrics_data *data) {
    char path[PATH_BUFFER_SIZE];

    for (int ext_idx = 0; extensions[ext_idx]; ext_idx++) {
        const char *ext = extensions[ext_idx];

        // Pattern 1: Title.ext
        if (build_path(path, sizeof(path), "%s/%s.%s", dir, title_safe, ext) >= 0) {
            log_info("Trying: %s", path);
            if (try_load_lyrics_file(path, data)) {
                return true;
            }
        }

        if (artist_safe) {
            // Pattern 2: Artist - Title.ext
            if (build_path(path, sizeof(path), "%s/%s - %s.%s", dir, artist_safe, title_safe, ext) >= 0) {
                if (try_load_lyrics_file(path, data)) {
                    return true;
                }
            }

            // Pattern 3: Artist/Title.ext
            if (build_path(path, sizeof(path), "%s/%s/%s.%s", dir, artist_safe, title_safe, ext) >= 0) {
                if (try_load_lyrics_file(path, data)) {
                    return true;
                }
            }
        }
    }

    return false;
}

// Cleanup all allocated resources used in search
static void cleanup_search_resources(char **extensions, char *title_safe,
                                     char *artist_safe, char *current_dir,
                                     char *filename_from_url) {
    free_extension_priority(extensions);
    free(title_safe);
    free(artist_safe);
    free(current_dir);
    free(filename_from_url);
}

static bool local_search(const char *title, const char *artist, const char *album,
                         const char *url, int64_t duration_ms, struct lyrics_data *data) {
    (void)duration_ms; // Unused for local search
    (void)album;       // Unused for local search

    // Require title with non-empty content
    if (!title || strlen(title) == 0) {
        log_info("Missing title, cannot search local files");
        return false;
    }

    // Prepare sanitized filenames
    char *title_no_ext = remove_extension(title);
    char *title_safe = sanitize_filename(title_no_ext ? title_no_ext : title);
    char *artist_safe = artist ? sanitize_filename(artist) : NULL;
    free(title_no_ext);

    // Extract paths from URL
    char *current_dir = get_directory_from_url(url);
    char *filename_from_url = get_filename_from_url(url);

    // Get extension priority list
    char **extensions = get_extension_priority();
    if (!extensions) {
        cleanup_search_resources(extensions, title_safe, artist_safe, current_dir, filename_from_url);
        return false;
    }

    // Build directory search list
    const char *search_dirs[10] = {0};
    char lyrics_dir[FILENAME_BUFFER_SIZE] = {0};
    int dir_count = build_search_directories(search_dirs, 10, current_dir, lyrics_dir, sizeof(lyrics_dir));

    // Search all directories
    for (int i = 0; i < dir_count; i++) {
        if (!search_dirs[i]) continue;

        // Try exact filename first (only in first directory - file's own directory)
        if (i == 0 && try_exact_filename(search_dirs[i], filename_from_url, extensions, data)) {
            cleanup_search_resources(extensions, title_safe, artist_safe, current_dir, filename_from_url);
            return true;
        }

        // Try title-based patterns
        if (try_title_patterns(search_dirs[i], title_safe, artist_safe, extensions, data)) {
            cleanup_search_resources(extensions, title_safe, artist_safe, current_dir, filename_from_url);
            return true;
        }
    }

    // No lyrics found
    cleanup_search_resources(extensions, title_safe, artist_safe, current_dir, filename_from_url);
    return false;
}

static bool local_init(void) {
    return true; // Nothing to initialize
}

static void local_cleanup(void) {
    // Nothing to cleanup
}

struct lyrics_provider local_provider = {
    .name = "local",
    .search = local_search,
    .init = local_init,
    .cleanup = local_cleanup,
};

// ============================================================================
// High-level API
// ============================================================================

static struct lyrics_provider *providers[] = {
    &local_provider,
    &lrclib_provider,  // Try online if local not found
    NULL
};

void lyrics_providers_init(void) {
    for (int i = 0; providers[i]; i++) {
        if (providers[i]->init) {
            providers[i]->init();
        }
    }
}

void lyrics_providers_cleanup(void) {
    for (int i = 0; providers[i]; i++) {
        if (providers[i]->cleanup) {
            providers[i]->cleanup();
        }
    }
}

bool lyrics_find_for_track(struct track_metadata *track, struct lyrics_data *data) {
    if (!track || !track->title) {
        return false;
    }

    log_info("Searching lyrics for: %s - %s",
             track->artist ? track->artist : "Unknown", track->title);

    if (track->url) {
        log_info("File location: %s", track->url);
    }

    // Convert duration from microseconds to milliseconds
    int64_t duration_ms = track->length_us / 1000;

    // Calculate metadata hash for caching
    char metadata_hash[MD5_DIGEST_STRING_LENGTH];
    bool has_hash = calculate_metadata_md5(track->artist, track->title, track->album, metadata_hash);

    // Ensure cache directories exist
    ensure_cache_directories();

    // Try each provider in order
    for (int i = 0; providers[i]; i++) {
        // Skip lrclib if disabled in config
        if (strcmp(providers[i]->name, "lrclib") == 0 && !g_config.lyrics.enable_lrclib) {
            log_info("Skipped provider: %s (disabled in config)", providers[i]->name);
            continue;
        }

        log_info("Trying provider: %s", providers[i]->name);
        if (providers[i]->search(track->title, track->artist, track->album,
                                 track->url, duration_ms, data)) {
            log_success("Found lyrics via %s provider", providers[i]->name);

            // If lyrics came from lrclib, cache them
            if (strcmp(providers[i]->name, "lrclib") == 0 && has_hash) {
                char cache_path[512];
                if (build_lyrics_cache_path(cache_path, sizeof(cache_path), metadata_hash) > 0) {
                    FILE *f = fopen(cache_path, "w");
                    if (f) {
                        // Write LRC metadata tags
                        fprintf(f, "[ar:%s]\n", track->artist ? track->artist : "Unknown");
                        fprintf(f, "[ti:%s]\n", track->title ? track->title : "Unknown");
                        fprintf(f, "[al:%s]\n", track->album ? track->album : "Unknown");

                        // Write lyrics lines with timestamps
                        struct lyrics_line *line = data->lines;
                        while (line) {
                            fprintf(f, "[%02ld:%02ld.%02ld]%s\n",
                                   (line->timestamp_us / 1000000) / 60,
                                   (line->timestamp_us / 1000000) % 60,
                                   (line->timestamp_us / 10000) % 100,
                                   line->text);
                            line = line->next;
                        }
                        fclose(f);
                        log_info("Cached lyrics: %s", cache_path);
                    } else {
                        log_warn("Failed to cache lyrics");
                    }
                }
            }

            return true;
        }

        // If local provider failed and we're about to try lrclib, check cache first
        if (strcmp(providers[i]->name, "local") == 0 && has_hash) {
            char cache_path[512];
            if (build_lyrics_cache_path(cache_path, sizeof(cache_path), metadata_hash) > 0) {
                struct stat st;
                if (stat(cache_path, &st) == 0) {
                    log_info("Found cached lyrics: %s", cache_path);

                    // Load from cache
                    if (lrc_parse_file(cache_path, data)) {
                        log_success("Found lyrics via cache");
                        return true;
                    } else {
                        log_warn("Failed to parse cached lyrics, will continue to online provider");
                        // Delete corrupted cache file
                        unlink(cache_path);
                    }
                }
            }
        }
    }

    log_warn("No lyrics found");
    return false;
}
