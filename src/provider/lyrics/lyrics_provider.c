#define _GNU_SOURCE
#include "lyrics_provider.h"
#include "../lrclib/lrclib_provider.h"
#include "../../parser/lrc/lrc_parser.h"
#include "../../parser/lrc/lrcx_parser.h"
#include "../../parser/srt/srt_parser.h"
#include "../../user_experience/config/config.h"
#include "../../translator/deepl/deepl_translator.h"
#include "../../translator/gemini/gemini_translator.h"
#include "../../translator/claude/claude_translator.h"
#include "../../translator/openai/openai_translator.h"
#include "../../constants.h"
#include "../../utils/file/file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>

// ============================================================================
// Translation helper
// ============================================================================

// Translate lyrics using the configured translation provider
static void translate_lyrics_with_provider(struct lyrics_data *data, int64_t track_length_us) {
    if (!data || !g_config.translation.provider) {
        return;
    }

    const char *provider = g_config.translation.provider;

    // Skip if translation is disabled
    if (strcmp(provider, "false") == 0) {
        log_info("Translation disabled (provider=false)");
        return;
    }

    // Only translate LRC files
    // SRT/VTT/LRCX already have ruby text {} support and don't need translation
    if (data->source_file_path) {
        const char *ext = strrchr(data->source_file_path, '.');
        if (ext && (strcasecmp(ext, ".srt") == 0 ||
                    strcasecmp(ext, ".vtt") == 0 ||
                    strcasecmp(ext, ".lrcx") == 0)) {
            log_info("Translation skipped for format: %s", ext);
            return;  // Skip translation silently
        }
    }

    log_info("Starting translation with provider: %s", provider);

    // Call appropriate translator based on provider
    if (strcmp(provider, "deepl") == 0) {
        deepl_translate_lyrics(data, track_length_us);
    } else if (strncmp(provider, "gemini-", 7) == 0 || strncmp(provider, "gemini", 6) == 0) {
        gemini_translate_lyrics(data, track_length_us);
    } else if (strncmp(provider, "claude-", 7) == 0 || strncmp(provider, "claude", 6) == 0) {
        claude_translate_lyrics(data, track_length_us);
    } else if (strncmp(provider, "gpt-", 4) == 0 || strncmp(provider, "openai", 6) == 0) {
        openai_translate_lyrics(data, track_length_us);
    } else {
        log_warn("Unknown translation provider: %s", provider);
    }
}

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
    // Check file extension to determine parser
    // Note: No stat() call here to avoid TOCTOU race condition
    // Parser functions will handle missing/inaccessible files
    const char *ext = strrchr(path, '.');
    if (!ext) return false;

    // Check if this extension is enabled in config
    if (!config_is_extension_enabled(ext)) {
        return false;
    }

    bool success = false;

    // Try LRCX only for .lrcx files
    if (strcasecmp(ext, ".lrcx") == 0 && lrcx_parse_file(path, data)) {
        log_success("Loaded LRCX file: %s", sanitize_path(path));
        success = true;
    }

    // Try LRC for .lrc files
    if (strcasecmp(ext, ".lrc") == 0 && lrc_parse_file(path, data)) {
        log_success("Loaded LRC file: %s", sanitize_path(path));
        success = true;
    }

    // Try SRT for .srt and .vtt files
    if ((strcasecmp(ext, ".srt") == 0 || strcasecmp(ext, ".vtt") == 0) && srt_parse_file(path, data)) {
        log_success("Loaded %s file: %s", strcasecmp(ext, ".vtt") == 0 ? "VTT" : "SRT", sanitize_path(path));
        success = true;
    }

    if (success) {
        // Store the file path and calculate checksum
        data->source_file_path = strdup(path);
        if (!calculate_file_md5(path, data->md5_checksum)) {
            log_warn("Failed to calculate MD5 checksum for %s", sanitize_path(path));
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

    size_t i = 0;
    size_t j = 0;
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

// Helper: Create default extension priority list
static char** create_default_extensions(void) {
    const char *defaults[] = {"lrcx", "lrc", "srt", "vtt", NULL};
    const int count = 5; // 4 extensions + NULL terminator

    char **exts = malloc(count * sizeof(char*));
    if (!exts) return NULL;

    // Initialize all pointers to NULL for safe cleanup
    for (int i = 0; i < count; i++) {
        exts[i] = NULL;
    }

    // Allocate strings with error checking
    for (int i = 0; defaults[i] != NULL; i++) {
        exts[i] = strdup(defaults[i]);
        if (!exts[i]) {
            cleanup_partial_extensions(exts, count);
            return NULL;
        }
    }

    return exts;
}

// Helper: Parse comma-separated custom extension list
static char** parse_custom_extensions(const char *extensions) {
    char *exts_copy = strdup(extensions);
    if (!exts_copy) return NULL;

    // Count extensions
    int count = 1;
    for (const char *p = exts_copy; *p; p++) {
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
    char *saveptr;
    char *token = strtok_r(exts_copy, ",", &saveptr);
    while (token && idx < count) {
        const char *trimmed = config_trim_whitespace(token);
        result[idx] = strdup(trimmed);
        if (!result[idx]) {
            cleanup_partial_extensions(result, idx);
            free(exts_copy);
            return NULL;
        }
        idx++;
        token = strtok_r(NULL, ",", &saveptr);
    }
    result[idx] = NULL;

    free(exts_copy);
    return result;
}

// Get ordered list of extensions from config
// Returns NULL-terminated array of extension strings
// Caller must free the array and all strings
static char** get_extension_priority(void) {
    if (!g_config.lyrics.extensions || g_config.lyrics.extensions[0] == '\0') {
        return create_default_extensions();
    }

    return parse_custom_extensions(g_config.lyrics.extensions);
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

    log_info("Extracted directory from URL: %s", sanitize_path(decoded_path));
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
    const char *last_slash = strrchr(decoded, '/');
    const char *filename = last_slash ? (last_slash + 1) : decoded;

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

// Expand path token: ~ → $HOME, {music_dir} → music file directory
// Caller must free the returned string
static char* expand_path_token(const char *token, const char *music_file_dir) {
    if (strcmp(token, "{music_dir}") == 0) {
        return music_file_dir ? strdup(music_file_dir) : strdup("");
    }

    if (token[0] == '~' && (token[1] == '/' || token[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            size_t len = strlen(home) + strlen(token + 1) + 1;
            char *result = malloc(len);
            if (result) {
                snprintf(result, len, "%s%s", home, token + 1);
            }
            return result;
        }
    }

    return strdup(token);
}

// Check if the music file's directory matches any entry in ignore_dirs
// Returns true if the track should be skipped (not searched for lyrics)
static bool is_track_ignored(const char *music_file_dir) {
    if (!music_file_dir || !g_config.lyrics.ignore_dirs ||
        g_config.lyrics.ignore_dirs[0] == '\0') {
        return false;
    }

    char *ignore_copy = strdup(g_config.lyrics.ignore_dirs);
    if (!ignore_copy) return false;

    char *saveptr;
    char *token = strtok_r(ignore_copy, ":", &saveptr);

    while (token) {
        const char *trimmed = config_trim_whitespace(token);
        char *expanded = expand_path_token(trimmed, music_file_dir);
        if (!expanded) {
            token = strtok_r(NULL, ":", &saveptr);
            continue;
        }

        // Compare using realpath to handle symlinks/relative paths
        char resolved_dir[PATH_MAX];
        char resolved_ignore[PATH_MAX];
        bool match = false;

        if (realpath(music_file_dir, resolved_dir) && realpath(expanded, resolved_ignore)) {
            match = (strcmp(resolved_dir, resolved_ignore) == 0);
        } else {
            // realpath failed, fall back to direct string comparison
            match = (strcmp(music_file_dir, expanded) == 0);
        }

        free(expanded);
        if (match) {
            free(ignore_copy);
            return true;
        }
        token = strtok_r(NULL, ":", &saveptr);
    }

    free(ignore_copy);
    return false;
}

// Parse user-defined search directories from config
// Returns number of directories added
static int build_custom_search_dirs(const char **search_dirs, int max_dirs,
                                    const char *current_dir,
                                    char **allocated_dirs, int *alloc_count) {
    int dir_count = 0;
    char *dirs_copy = strdup(g_config.lyrics.search_dirs);
    if (!dirs_copy) return 0;

    char *saveptr;
    char *token = strtok_r(dirs_copy, ":", &saveptr);

    while (token && dir_count < max_dirs) {
        const char *trimmed = config_trim_whitespace(token);
        char *expanded = expand_path_token(trimmed, current_dir);

        if (expanded && expanded[0] != '\0') {
            allocated_dirs[*alloc_count] = expanded;
            search_dirs[dir_count++] = expanded;
            (*alloc_count)++;
        } else {
            free(expanded);
        }
        token = strtok_r(NULL, ":", &saveptr);
    }
    free(dirs_copy);
    return dir_count;
}

// Build default search directory list (hardcoded priority order)
// Returns number of directories added
static int build_default_search_dirs(const char **search_dirs, int max_dirs,
                                     const char *current_dir,
                                     char *lyrics_dir_buf, size_t buf_size) {
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
    if (home && dir_count < max_dirs &&
        join_path_2(lyrics_dir_buf, buf_size, home, ".lyrics") >= 0) {
        search_dirs[dir_count++] = lyrics_dir_buf;
    }

    // Priority 5: Home directory
    if (home && dir_count < max_dirs) {
        search_dirs[dir_count++] = home;
    }

    return dir_count;
}

// Build list of directories to search for lyrics files
// allocated_dirs/alloc_count track dynamically allocated paths (from search_dirs config)
// Caller must free allocated_dirs[0..alloc_count-1] after use
// Returns number of directories added
static int build_search_directories(const char **search_dirs, int max_dirs,
                                    const char *current_dir, char *lyrics_dir_buf, size_t buf_size,
                                    char **allocated_dirs, int *alloc_count) {
    *alloc_count = 0;

    if (g_config.lyrics.search_dirs && g_config.lyrics.search_dirs[0] != '\0') {
        return build_custom_search_dirs(search_dirs, max_dirs, current_dir,
                                        allocated_dirs, alloc_count);
    }

    return build_default_search_dirs(search_dirs, max_dirs, current_dir,
                                     lyrics_dir_buf, buf_size);
}

// Try to find lyrics using exact filename from URL
static bool try_exact_filename(const char *dir, const char *filename,
                                char **extensions, struct lyrics_data *data) {
    if (!filename) {
        return false;
    }

    char path[PATH_BUFFER_SIZE];
    for (int ext_idx = 0; extensions[ext_idx]; ext_idx++) {
        if (build_path_with_ext(path, sizeof(path), dir, filename, extensions[ext_idx]) < 0) {
            continue;  // Path too long, skip
        }
        log_info("Trying: %s", sanitize_path(path));
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

        // Search: <title>.<extension>
        if (build_path_with_ext(path, sizeof(path), dir, title_safe, ext) >= 0) {
            log_info("Trying: %s", sanitize_path(path));
            if (try_load_lyrics_file(path, data)) {
                return true;
            }
        }

        if (artist_safe) {
            // Search: <artist> - <title>.<extension>
            if (build_path_artist_title(path, sizeof(path), dir, artist_safe, title_safe, ext) >= 0 &&
                try_load_lyrics_file(path, data)) {
                return true;
            }

            // Search: <artist>/<title>.<extension>
            if (build_path_with_subdir_ext(path, sizeof(path), dir, artist_safe, title_safe, ext) >= 0 &&
                try_load_lyrics_file(path, data)) {
                return true;
            }
        }
    }

    return false;
}

// Free dynamically allocated search directories
static void free_allocated_dirs(char **allocated_dirs, int alloc_count) {
    for (int i = 0; i < alloc_count; i++) {
        free(allocated_dirs[i]);
    }
}

// Cleanup all allocated resources used in search
static void cleanup_search_resources(char **extensions, char *title_safe,
                                     char *artist_safe, char *current_dir,
                                     char *filename_from_url,
                                     char **allocated_dirs, int alloc_count) {
    free_extension_priority(extensions);
    free(title_safe);
    free(artist_safe);
    free(current_dir);
    free(filename_from_url);
    free_allocated_dirs(allocated_dirs, alloc_count);
}

static bool local_search(const char *title, const char *artist, const char *album,
                         const char *url, int64_t duration_ms, struct lyrics_data *data) {
    (void)duration_ms; // Unused for local search
    (void)album;       // Unused for local search

    // Require title with non-empty content
    if (!title || title[0] == '\0') {
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
        cleanup_search_resources(extensions, title_safe, artist_safe, current_dir, filename_from_url, NULL, 0);
        return false;
    }

    // Build directory search list
    const char *search_dirs[10] = {0};
    char lyrics_dir[FILENAME_BUFFER_SIZE] = {0};
    char *allocated_dirs[10] = {0};
    int alloc_count = 0;
    int dir_count = build_search_directories(search_dirs, 10, current_dir, lyrics_dir, sizeof(lyrics_dir),
                                             allocated_dirs, &alloc_count);

    // Search all directories
    bool found = false;
    for (int i = 0; i < dir_count && !found; i++) {
        if (!search_dirs[i]) continue;

        // Try exact filename first (only in first directory - file's own directory)
        found = (i == 0 && try_exact_filename(search_dirs[i], filename_from_url, extensions, data)) ||
                try_title_patterns(search_dirs[i], title_safe, artist_safe, extensions, data);
    }

    cleanup_search_resources(extensions, title_safe, artist_safe, current_dir, filename_from_url,
                             allocated_dirs, alloc_count);
    return found;
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

// Try to load lyrics from cache
static bool try_load_from_cache(const char *metadata_hash, struct lyrics_data *data,
                               int64_t track_length_us) {
    char cache_path[512];
    if (build_lyrics_cache_path(cache_path, sizeof(cache_path), metadata_hash) <= 0) {
        return false;
    }

    if (!lrc_parse_file(cache_path, data)) {
        return false;
    }

    log_info("Found cached lyrics: %s", sanitize_path(cache_path));
    log_success("Found lyrics via cache");

    // Update access time to prevent automatic cleanup
    touch_cache_file(cache_path);

    // Store the file path and calculate checksum
    data->source_file_path = strdup(cache_path);
    if (!calculate_file_md5(cache_path, data->md5_checksum)) {
        log_warn("Failed to calculate MD5 checksum for %s", sanitize_path(cache_path));
        data->md5_checksum[0] = '\0';
    }

    // Try to translate lyrics with configured provider
    translate_lyrics_with_provider(data, track_length_us);

    return true;
}

// Check if provider should be skipped
static bool should_skip_provider(const struct lyrics_provider *provider) {
    if (strcmp(provider->name, "local") == 0 &&
        (!g_config.lyrics.extensions || g_config.lyrics.extensions[0] == '\0')) {
        log_info("Skipped provider: %s (no extensions configured)", provider->name);
        return true;
    }

    if (strcmp(provider->name, "lrclib") == 0 && !g_config.lyrics.enable_lrclib) {
        log_info("Skipped provider: %s (disabled in config)", provider->name);
        return true;
    }

    return false;
}

// Cache lrclib lyrics to file
static void cache_lrclib_lyrics(const struct track_metadata *track, struct lyrics_data *data,
                                const char *metadata_hash) {
    char cache_path[512];
    if (build_lyrics_cache_path(cache_path, sizeof(cache_path), metadata_hash) <= 0) {
        return;
    }

    mode_t old_mask = umask(0077);
    FILE *f = fopen(cache_path, "w");
    umask(old_mask);

    if (!f) {
        log_warn("Failed to cache lyrics");
        return;
    }

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
    log_info("Cached lyrics: %s", sanitize_path(cache_path));

    // Calculate MD5 checksum of cached file
    data->source_file_path = strdup(cache_path);
    if (!calculate_file_md5(cache_path, data->md5_checksum)) {
        log_warn("Failed to calculate MD5 checksum for %s", sanitize_path(cache_path));
        data->md5_checksum[0] = '\0';
    }
}

// Try all providers to find lyrics
static bool try_providers(struct track_metadata *track, struct lyrics_data *data,
                         int64_t duration_ms, const char *metadata_hash,
                         bool *tried_local, bool *tried_lrclib) {
    for (int i = 0; providers[i]; i++) {
        if (should_skip_provider(providers[i])) {
            continue;
        }

        // Track which providers were actually tried
        if (strcmp(providers[i]->name, "local") == 0) {
            *tried_local = true;
        } else if (strcmp(providers[i]->name, "lrclib") == 0) {
            *tried_lrclib = true;
        }

        log_info("Trying provider: %s", providers[i]->name);
        if (!providers[i]->search(track->title, track->artist, track->album,
                                 track->url, duration_ms, data)) {
            continue;
        }

        log_success("Found lyrics via %s provider", providers[i]->name);

        // If lyrics came from lrclib, cache them first
        if (strcmp(providers[i]->name, "lrclib") == 0 && metadata_hash) {
            cache_lrclib_lyrics(track, data, metadata_hash);
        }

        // Try to translate lyrics with configured provider
        translate_lyrics_with_provider(data, track->length_us);

        return true;
    }

    return false;
}

// Log search feedback based on which providers were tried
static void log_search_feedback(bool tried_local, bool tried_lrclib) {
    if (!tried_local && !tried_lrclib) {
        log_warn("No lyrics providers enabled. Check 'extensions' and 'enable_lrclib' settings in ~/.config/wshowlyrics/settings.ini");
    } else if (tried_local && tried_lrclib) {
        log_warn("Tried local files and online provider (lrclib), but no lyrics found");
    } else if (tried_local) {
        log_warn("Tried local files, but no lyrics found");
    } else if (tried_lrclib) {
        log_warn("Tried online provider (lrclib), but no lyrics found");
    }
}

bool lyrics_find_for_track(struct track_metadata *track, struct lyrics_data *data) {
    if (!track || !track->title) {
        return false;
    }

    log_info("Searching lyrics for: %s - %s",
             track->artist ? track->artist : "Unknown", track->title);

    if (track->url) {
        log_info("File location: %s", sanitize_path(track->url));
    }

    // Check if music file's directory is in ignore_dirs (skip lyrics search entirely)
    if (track->url) {
        char *music_dir = get_directory_from_url(track->url);
        if (music_dir) {
            if (is_track_ignored(music_dir)) {
                log_info("Skipping lyrics search: music directory is in ignore_dirs (%s)",
                         sanitize_path(music_dir));
                free(music_dir);
                return false;
            }
            free(music_dir);
        }
    }

    // Convert duration from microseconds to milliseconds
    int64_t duration_ms = track->length_us / 1000;

    // Calculate metadata hash for caching
    char metadata_hash[MD5_DIGEST_STRING_LENGTH];
    bool has_hash = calculate_metadata_md5(track->artist, track->title, track->album, metadata_hash);

    // Ensure cache directories exist
    ensure_cache_directories();

    // Check cache first
    if (has_hash && try_load_from_cache(metadata_hash, data, track->length_us)) {
        return true;
    }

    // Try each provider in order
    bool tried_local = false;
    bool tried_lrclib = false;

    if (try_providers(track, data, duration_ms, has_hash ? metadata_hash : NULL,
                     &tried_local, &tried_lrclib)) {
        return true;
    }

    // Provide helpful feedback based on what happened
    log_search_feedback(tried_local, tried_lrclib);
    return false;
}
