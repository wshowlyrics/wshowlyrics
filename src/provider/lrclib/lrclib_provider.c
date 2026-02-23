#include "lrclib_provider.h"
#include "../../parser/lrc/lrc_parser.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/string/string_utils.h"
#include "../../utils/json/json_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <curl/curl.h>

// Deprecated: Use curl_url_encode from curl_utils.h instead
static char* url_encode(CURL *curl, const char *str) {
    return curl_url_encode(curl, str);
}

// Deprecated: Use json_extract_string from json_utils.h instead
static char* extract_json_string(const char *json, const char *key) {
    return json_extract_string(json, key);
}

// Deprecated: Use json_extract_int_from from json_utils.h instead
static int64_t extract_json_int(const char *json, const char *key, const char *search_start) {
    return json_extract_int_from(json, key, search_start);
}

// Build search request URL with sanitized title and optional artist
static bool build_search_request_url(CURL *curl, const char *title, const char *artist,
                                     char *url_buffer, size_t buffer_size) {
    // Sanitize title to remove YouTube IDs and file extensions
    char *clean_title = sanitize_title(title);
    if (!clean_title || clean_title[0] == '\0') {
        free(clean_title);
        return false;
    }

    log_info("Sanitized title: '%s' -> '%s'", title, clean_title);

    // Build search query - use track_name with sanitized title
    char *title_encoded = url_encode(curl, clean_title);
    free(clean_title);

    if (!title_encoded) {
        return false;
    }

    int offset = snprintf(url_buffer, buffer_size,
                         "https://lrclib.net/api/search?track_name=%s", title_encoded);
    curl_free(title_encoded);

    // Check for truncation - if URL already truncated, skip artist parameter
    if (offset < 0 || (size_t)offset >= buffer_size) {
        return true;  // URL truncated but still usable (just without artist)
    }

    // Add artist if available
    if (artist && artist[0] != '\0') {
        char *artist_encoded = url_encode(curl, artist);
        if (artist_encoded) {
            snprintf(url_buffer + offset, buffer_size - offset,
                    "&artist_name=%s", artist_encoded);
            curl_free(artist_encoded);
        }
    }

    return true;
}

// Perform CURL request and return response
static bool perform_lrclib_request(CURL *curl, const char *url, struct curl_memory_buffer *response) {
    curl_memory_buffer_init(response);

    if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK) {
        log_error("lrclib: Failed to set CURL options");
        curl_memory_buffer_free(response);
        return false;
    }

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK || http_code != HTTP_OK) {
        log_warn("lrclib search API failed (HTTP %ld)", http_code);
        curl_memory_buffer_free(response);
        return false;
    }

    return true;
}

// Structure to hold best match search results
struct best_match_result {
    char *synced_lyrics;
    char *obj_start;  // JSON object start for metadata extraction
    int64_t duration_diff;
};

// Find best match from JSON array response based on duration
static struct best_match_result find_best_match_in_results(
    const char *response_data, int64_t target_duration_ms) {

    struct best_match_result result = {NULL, NULL, INT64_MAX};
    const char *search_pos = response_data + 1; // Skip opening '['

    while (search_pos && *search_pos) {
        char *obj_start = strchr(search_pos, '{');
        if (!obj_start) break;

        // Extract duration and syncedLyrics for this result
        int64_t result_duration = extract_json_int(response_data, "duration", obj_start);
        char *synced_lyrics = extract_json_string(obj_start, "syncedLyrics");

        // Skip entries without valid synced lyrics
        if (!synced_lyrics || synced_lyrics[0] == '\0') {
            free(synced_lyrics);
            char *obj_end = strchr(obj_start, '}');
            if (!obj_end) break;
            search_pos = obj_end + 1;
            continue;
        }

        if (target_duration_ms > 0 && result_duration > 0) {
            // We have both durations - compare
            int64_t duration_diff = llabs((result_duration * 1000) - target_duration_ms);
            log_info("Found result with duration %ld s (diff: %ld ms)",
                   result_duration, duration_diff);

            if (duration_diff < result.duration_diff) {
                result.duration_diff = duration_diff;
                free(result.synced_lyrics);
                result.synced_lyrics = synced_lyrics;
                result.obj_start = obj_start;
                synced_lyrics = NULL; // Prevent free below
            }
        } else if (!result.synced_lyrics) {
            // No duration to compare, just take first result
            result.synced_lyrics = synced_lyrics;
            result.obj_start = obj_start;
            synced_lyrics = NULL; // Prevent free below
            log_info("Found result (no duration comparison)");
        }

        free(synced_lyrics);

        // Move to next object
        char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;
        search_pos = obj_end + 1;
    }

    return result;
}

// Extract and update metadata from matched result
static void extract_metadata_from_result(char *obj_start, struct lyrics_data *data) {
    if (!obj_start) return;

    char *artist_name = extract_json_string(obj_start, "artistName");
    char *album_name = extract_json_string(obj_start, "albumName");
    char *track_name = extract_json_string(obj_start, "trackName");

    if (artist_name && artist_name[0] != '\0') {
        free(data->metadata.artist);
        data->metadata.artist = artist_name;
        log_info("lrclib metadata: artist = %s", artist_name);
    } else {
        free(artist_name);
    }

    if (album_name && album_name[0] != '\0') {
        free(data->metadata.album);
        data->metadata.album = album_name;
        log_info("lrclib metadata: album = %s", album_name);
    } else {
        free(album_name);
    }

    if (track_name && track_name[0] != '\0') {
        free(data->metadata.title);
        data->metadata.title = track_name;
        log_info("lrclib metadata: title = %s", track_name);
    } else {
        free(track_name);
    }
}

// Try search API when we have incomplete metadata (missing artist or album)
// or when exact match fails. Select best match by duration.
// Note: Album is intentionally excluded to avoid issues with incorrect album metadata.
static bool lrclib_search_fallback(const char *title, const char *artist,
                                    int64_t duration_ms, struct lyrics_data *data) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("Failed to initialize CURL");
        return false;
    }

    // Enforce TLS 1.2 or higher for security
    if (curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2) != CURLE_OK) {
        log_error("lrclib: Failed to set SSL version");
        curl_easy_cleanup(curl);
        return false;
    }

    // Build search request URL using helper function
    char request_url[URL_BUFFER_SIZE];
    if (!build_search_request_url(curl, title, artist, request_url, sizeof(request_url))) {
        curl_easy_cleanup(curl);
        return false;
    }

    log_info("lrclib search API request: %s", request_url);

    // Perform CURL request using helper function
    struct curl_memory_buffer response;
    if (!perform_lrclib_request(curl, request_url, &response)) {
        curl_easy_cleanup(curl);
        return false;
    }
    curl_easy_cleanup(curl);

    // Validate response format
    if (!response.data || response.size < 2 || response.data[0] != '[') {
        log_info("lrclib search returned no results");
        curl_memory_buffer_free(&response);
        return false;
    }

    // Find best match using helper function
    struct best_match_result match = find_best_match_in_results(response.data, duration_ms);

    bool success = false;
    if (match.synced_lyrics && match.synced_lyrics[0] != '\0') {
        if (duration_ms > 0) {
            log_info("Selected best match with duration diff: %ld ms", match.duration_diff);
        }
        log_info("Found synced lyrics from lrclib search");
        success = lrc_parse_string(match.synced_lyrics, data);

        // Extract metadata from the matched result using helper function
        if (success) {
            extract_metadata_from_result(match.obj_start, data);
        }
    } else {
        log_info("No synced lyrics in search results");
    }

    free(match.synced_lyrics);
    curl_memory_buffer_free(&response);
    return success;
}

// Helper: Setup CURL options for lrclib request
static bool setup_lrclib_request(CURL *curl, const char *url,
                                  struct curl_memory_buffer *response) {
    if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK) {
        log_error("lrclib: Failed to set CURL options");
        return false;
    }
    return true;
}

static bool lrclib_search(const char *title, const char *artist, const char *album,
                          const char *url, int64_t duration_ms, struct lyrics_data *data) {
    (void)url; // Unused for online search

    // Require at least a title with non-empty content
    if (!title || title[0] == '\0') {
        log_info("Missing title, cannot search lrclib");
        return false;
    }

    // If we're missing artist or album, use search API instead
    // Check for NULL or empty string for both artist and album
    bool has_artist = (artist && artist[0] != '\0');
    bool has_album = (album && album[0] != '\0');

    if (!has_artist || !has_album) {
        log_info("Missing metadata (artist: %s, album: %s), using search API",
               artist ? artist : "none", album ? album : "none");
        return lrclib_search_fallback(title, artist, duration_ms, data);
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("Failed to initialize CURL");
        return false;
    }

    // Enforce TLS 1.2 or higher for security
    if (curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2) != CURLE_OK) {
        log_error("lrclib: Failed to set SSL version");
        curl_easy_cleanup(curl);
        return false;
    }

    // URL encode parameters
    char *title_encoded = url_encode(curl, title);
    char *artist_encoded = url_encode(curl, artist);
    char *album_encoded = url_encode(curl, album);

    if (!title_encoded || !artist_encoded || !album_encoded) {
        curl_free(title_encoded);
        curl_free(artist_encoded);
        curl_free(album_encoded);
        curl_easy_cleanup(curl);
        return false;
    }

    // Build request URL for exact match API (include duration if available)
    char request_url[URL_BUFFER_SIZE];
    if (duration_ms > 0) {
        int64_t duration_sec = duration_ms / 1000;
        snprintf(request_url, sizeof(request_url),
                 "https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s&duration=%ld",
                 title_encoded, artist_encoded, album_encoded, duration_sec);
    } else {
        snprintf(request_url, sizeof(request_url),
                 "https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s",
                 title_encoded, artist_encoded, album_encoded);
    }

    log_info("lrclib API request: %s", request_url);

    curl_free(title_encoded);
    curl_free(artist_encoded);
    curl_free(album_encoded);

    // Setup CURL request
    struct curl_memory_buffer response;
    curl_memory_buffer_init(&response);

    if (!setup_lrclib_request(curl, request_url, &response)) {
        curl_memory_buffer_free(&response);
        curl_easy_cleanup(curl);
        return false;
    }

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        log_error("lrclib API request failed: %s", curl_easy_strerror(res));
        curl_memory_buffer_free(&response);
        // Try search API as fallback
        log_info("Trying search API as fallback");
        return lrclib_search_fallback(title, artist, duration_ms, data);
    }

    if (http_code != HTTP_OK) {
        log_info("lrclib API returned HTTP %ld (no exact match found)", http_code);
        curl_memory_buffer_free(&response);
        // Try search API as fallback
        log_info("Trying search API as fallback");
        return lrclib_search_fallback(title, artist, duration_ms, data);
    }

    // Parse JSON response
    if (!response.data) {
        log_info("Empty response, trying search API as fallback");
        curl_memory_buffer_free(&response);
        return lrclib_search_fallback(title, artist, duration_ms, data);
    }

    // Extract syncedLyrics or plainLyrics
    char *synced_lyrics = extract_json_string(response.data, "syncedLyrics");
    char *plain_lyrics = extract_json_string(response.data, "plainLyrics");

    bool success = false;

    // Only use synced lyrics (LRC format) - skip plainLyrics
    if (synced_lyrics && synced_lyrics[0] != '\0') {
        log_info("Found synced lyrics from lrclib exact match");
        success = lrc_parse_string(synced_lyrics, data);
    } else if (plain_lyrics && plain_lyrics[0] != '\0') {
        log_info("Plain lyrics available but ignored (synced lyrics only)");
    }

    free(synced_lyrics);
    free(plain_lyrics);
    curl_memory_buffer_free(&response);

    // If exact match didn't return synced lyrics, try search API
    if (!success) {
        log_info("No synced lyrics in exact match, trying search API");
        return lrclib_search_fallback(title, artist, duration_ms, data);
    }

    return success;
}

static bool lrclib_init(void) {
    // Initialize CURL globally
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        log_error("Failed to initialize CURL: %s", curl_easy_strerror(res));
        return false;
    }
    return true;
}

static void lrclib_cleanup(void) {
    curl_global_cleanup();
}

struct lyrics_provider lrclib_provider = {
    .name = "lrclib",
    .search = lrclib_search,
    .init = lrclib_init,
    .cleanup = lrclib_cleanup,
};
