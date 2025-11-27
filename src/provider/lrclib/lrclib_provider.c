#include "lrclib_provider.h"
#include "../../parser/lrc/lrc_parser.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/string/string_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <curl/curl.h>

// URL encode a string for use in query parameters
static char* url_encode(CURL *curl, const char *str) {
    if (!str) return NULL;
    return curl_easy_escape(curl, str, 0);
}

// Simple JSON string extractor (looks for "key":"value")
// Also unescapes \n to actual newlines
static char* extract_json_string(const char *json, const char *key) {
    if (!json || !key) return NULL;

    // Build search pattern: "key":"
    char pattern[JSON_PATTERN_SIZE];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    char *start = strstr(json, pattern);
    if (!start) return NULL;

    start += strlen(pattern);
    char *end = start;

    // Find the closing quote, handling escaped characters
    while (*end) {
        if (*end == '"' && (end == start || *(end - 1) != '\\')) {
            break;
        }
        end++;
    }

    if (*end != '"') return NULL;

    size_t len = end - start;
    char *escaped = malloc(len + 1);
    if (!escaped) return NULL;

    memcpy(escaped, start, len);
    escaped[len] = '\0';

    // Unescape \n to actual newlines
    char *result = malloc(len + 1);
    if (!result) {
        free(escaped);
        return NULL;
    }

    size_t i = 0, j = 0;
    while (i < len) {
        if (escaped[i] == '\\' && i + 1 < len) {
            if (escaped[i + 1] == 'n') {
                result[j++] = '\n';
                i += 2;
            } else if (escaped[i + 1] == 't') {
                result[j++] = '\t';
                i += 2;
            } else if (escaped[i + 1] == 'r') {
                result[j++] = '\r';
                i += 2;
            } else if (escaped[i + 1] == '"') {
                result[j++] = '"';
                i += 2;
            } else if (escaped[i + 1] == '\\') {
                result[j++] = '\\';
                i += 2;
            } else {
                result[j++] = escaped[i++];
            }
        } else {
            result[j++] = escaped[i++];
        }
    }
    result[j] = '\0';

    free(escaped);
    return result;
}

// Helper to extract integer from JSON (looks for "key":value or "key": value)
static int64_t extract_json_int(const char *json, const char *key, const char *search_start) {
    if (!json || !key) return -1;

    // Build search pattern: "key":
    char pattern[JSON_PATTERN_SIZE];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    char *start = search_start ? strstr(search_start, pattern) : strstr(json, pattern);
    if (!start) return -1;

    start += strlen(pattern);

    // Skip whitespace
    while (*start == ' ' || *start == '\t') start++;

    // Parse integer
    char *end;
    int64_t value = strtoll(start, &end, 10);
    if (end == start) return -1;

    return value;
}

// Try search API when we have incomplete metadata (missing artist or album)
// or when exact match fails. Select best match by duration.
static bool lrclib_search_fallback(const char *title, const char *artist, const char *album,
                                    int64_t duration_ms, struct lyrics_data *data) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize CURL\n");
        return false;
    }

    // Sanitize title to remove YouTube IDs and file extensions
    char *clean_title = sanitize_title(title);
    if (!clean_title || strlen(clean_title) == 0) {
        free(clean_title);
        curl_easy_cleanup(curl);
        return false;
    }

    printf("Sanitized title: '%s' -> '%s'\n", title, clean_title);

    // Build search query - use track_name with sanitized title
    char request_url[URL_BUFFER_SIZE];
    char *title_encoded = url_encode(curl, clean_title);
    free(clean_title);

    if (!title_encoded) {
        curl_easy_cleanup(curl);
        return false;
    }

    int offset = snprintf(request_url, sizeof(request_url),
                         "https://lrclib.net/api/search?track_name=%s", title_encoded);
    curl_free(title_encoded);

    // Add artist if available
    if (artist && strlen(artist) > 0) {
        char *artist_encoded = url_encode(curl, artist);
        if (artist_encoded) {
            offset += snprintf(request_url + offset, sizeof(request_url) - offset,
                             "&artist_name=%s", artist_encoded);
            curl_free(artist_encoded);
        }
    }

    // Add album if available
    if (album && strlen(album) > 0) {
        char *album_encoded = url_encode(curl, album);
        if (album_encoded) {
            snprintf(request_url + offset, sizeof(request_url) - offset,
                    "&album_name=%s", album_encoded);
            curl_free(album_encoded);
        }
    }

    printf("lrclib search API request: %s\n", request_url);

    // Setup CURL request
    struct curl_memory_buffer response;
    curl_memory_buffer_init(&response);
    curl_easy_setopt(curl, CURLOPT_URL, request_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != HTTP_OK) {
        printf("lrclib search API failed (HTTP %ld)\n", http_code);
        curl_memory_buffer_free(&response);
        return false;
    }

    // Parse JSON array response - find best match by duration
    if (!response.data || response.size < 2) {
        curl_memory_buffer_free(&response);
        return false;
    }

    // Check if we got an array (starts with '[')
    if (response.data[0] != '[') {
        printf("lrclib search returned no results\n");
        curl_memory_buffer_free(&response);
        return false;
    }

    // Find the best match by duration (if we have duration)
    // Otherwise just take the first result with syncedLyrics
    char *best_synced_lyrics = NULL;
    char *best_obj_start = NULL;  // Track the JSON object for metadata extraction
    int64_t best_duration_diff = INT64_MAX;
    char *search_pos = response.data + 1; // Skip opening '['

    while (search_pos && *search_pos) {
        // Find next object in array
        char *obj_start = strchr(search_pos, '{');
        if (!obj_start) break;

        // Extract duration and syncedLyrics for this result
        int64_t result_duration = extract_json_int(response.data, "duration", obj_start);
        char *synced_lyrics = extract_json_string(obj_start, "syncedLyrics");

        // Only consider results with synced lyrics
        if (synced_lyrics && strlen(synced_lyrics) > 0) {
            if (duration_ms > 0 && result_duration > 0) {
                // We have both durations - compare
                int64_t duration_diff = llabs((result_duration * 1000) - duration_ms);
                printf("Found result with duration %ld s (diff: %ld ms)\n",
                       result_duration, duration_diff);

                if (duration_diff < best_duration_diff) {
                    best_duration_diff = duration_diff;
                    free(best_synced_lyrics);
                    best_synced_lyrics = synced_lyrics;
                    best_obj_start = obj_start;  // Remember this object for metadata
                    synced_lyrics = NULL; // Prevent free below
                }
            } else if (!best_synced_lyrics) {
                // No duration to compare, just take first result
                best_synced_lyrics = synced_lyrics;
                best_obj_start = obj_start;  // Remember this object for metadata
                synced_lyrics = NULL; // Prevent free below
                printf("Found result (no duration comparison)\n");
            }
        }

        free(synced_lyrics);

        // Move to next object
        char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;
        search_pos = obj_end + 1;
    }

    bool success = false;
    if (best_synced_lyrics && strlen(best_synced_lyrics) > 0) {
        if (duration_ms > 0) {
            printf("Selected best match with duration diff: %ld ms\n", best_duration_diff);
        }
        printf("Found synced lyrics from lrclib search\n");
        success = lrc_parse_string(best_synced_lyrics, data);

        // Extract metadata from the matched result
        if (success && best_obj_start) {
            char *artist_name = extract_json_string(best_obj_start, "artistName");
            char *album_name = extract_json_string(best_obj_start, "albumName");
            char *track_name = extract_json_string(best_obj_start, "trackName");

            if (artist_name && strlen(artist_name) > 0) {
                free(data->metadata.artist);
                data->metadata.artist = artist_name;
                printf("lrclib metadata: artist = %s\n", artist_name);
            } else {
                free(artist_name);
            }

            if (album_name && strlen(album_name) > 0) {
                free(data->metadata.album);
                data->metadata.album = album_name;
                printf("lrclib metadata: album = %s\n", album_name);
            } else {
                free(album_name);
            }

            if (track_name && strlen(track_name) > 0) {
                free(data->metadata.title);
                data->metadata.title = track_name;
                printf("lrclib metadata: title = %s\n", track_name);
            } else {
                free(track_name);
            }
        }
    } else {
        printf("No synced lyrics in search results\n");
    }

    free(best_synced_lyrics);
    curl_memory_buffer_free(&response);
    return success;
}

static bool lrclib_search(const char *title, const char *artist, const char *album,
                          const char *url, int64_t duration_ms, struct lyrics_data *data) {
    (void)url; // Unused for online search

    // Require at least a title
    if (!title) {
        return false;
    }

    // If we're missing artist or album, use search API instead
    if (!artist || !album || strlen(artist) == 0 || strlen(album) == 0) {
        printf("Missing metadata (artist: %s, album: %s), using search API\n",
               artist ? artist : "none", album ? album : "none");
        return lrclib_search_fallback(title, artist, album, duration_ms, data);
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize CURL\n");
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

    printf("lrclib API request: %s\n", request_url);

    curl_free(title_encoded);
    curl_free(artist_encoded);
    curl_free(album_encoded);

    // Setup CURL request
    struct curl_memory_buffer response;
    curl_memory_buffer_init(&response);
    curl_easy_setopt(curl, CURLOPT_URL, request_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "lrclib API request failed: %s\n", curl_easy_strerror(res));
        curl_memory_buffer_free(&response);
        // Try search API as fallback
        printf("Trying search API as fallback\n");
        return lrclib_search_fallback(title, artist, album, duration_ms, data);
    }

    if (http_code != HTTP_OK) {
        printf("lrclib API returned HTTP %ld (no exact match found)\n", http_code);
        curl_memory_buffer_free(&response);
        // Try search API as fallback
        printf("Trying search API as fallback\n");
        return lrclib_search_fallback(title, artist, album, duration_ms, data);
    }

    // Parse JSON response
    if (!response.data) {
        printf("Empty response, trying search API as fallback\n");
        curl_memory_buffer_free(&response);
        return lrclib_search_fallback(title, artist, album, duration_ms, data);
    }

    // Extract syncedLyrics or plainLyrics
    char *synced_lyrics = extract_json_string(response.data, "syncedLyrics");
    char *plain_lyrics = extract_json_string(response.data, "plainLyrics");

    bool success = false;

    // Only use synced lyrics (LRC format) - skip plainLyrics
    if (synced_lyrics && strlen(synced_lyrics) > 0) {
        printf("Found synced lyrics from lrclib exact match\n");
        success = lrc_parse_string(synced_lyrics, data);
    } else if (plain_lyrics && strlen(plain_lyrics) > 0) {
        printf("Plain lyrics available but ignored (synced lyrics only)\n");
    }

    free(synced_lyrics);
    free(plain_lyrics);
    curl_memory_buffer_free(&response);

    // If exact match didn't return synced lyrics, try search API
    if (!success) {
        printf("No synced lyrics in exact match, trying search API\n");
        return lrclib_search_fallback(title, artist, album, duration_ms, data);
    }

    return success;
}

static bool lrclib_init(void) {
    // Initialize CURL globally
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to initialize CURL: %s\n", curl_easy_strerror(res));
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
