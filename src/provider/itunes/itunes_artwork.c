#include "itunes_artwork.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/string/string_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <curl/curl.h>

// Simple JSON string extractor (looks for "key":"value")
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
    char *result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';

    return result;
}

// URL encode a string for use in query parameters
static char* url_encode(CURL *curl, const char *str) {
    if (!str) return NULL;
    return curl_easy_escape(curl, str, 0);
}

char* itunes_search_artwork(const char *artist, const char *track) {
    if (!track) {
        return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("Failed to initialize CURL for iTunes search");
        return NULL;
    }

    // Sanitize track title to remove YouTube IDs and file extensions
    char *clean_track = sanitize_title(track);
    if (!clean_track || strlen(clean_track) == 0) {
        free(clean_track);
        curl_easy_cleanup(curl);
        return NULL;
    }

    if (strcmp(track, clean_track) != 0) {
        log_info("iTunes search: sanitized '%s' -> '%s'", track, clean_track);
    }

    // Build search term
    char search_term[2048];
    if (artist && strlen(artist) > 0 && strcasecmp(artist, "Unknown") != 0) {
        // Include artist in search
        snprintf(search_term, sizeof(search_term), "%s %s", artist, clean_track);
    } else {
        // Artist unknown - search by track only
        snprintf(search_term, sizeof(search_term), "%s", clean_track);
    }

    free(clean_track);

    // URL encode search term
    char *term_encoded = url_encode(curl, search_term);
    if (!term_encoded) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    // Build iTunes Search API URL
    char request_url[2048];
    snprintf(request_url, sizeof(request_url),
             "https://itunes.apple.com/search?term=%s&entity=song&limit=1",
             term_encoded);

    curl_free(term_encoded);

    log_info("iTunes API request: %s", request_url);

    // Setup CURL request
    struct curl_memory_buffer response;
    curl_memory_buffer_init(&response);
    curl_easy_setopt(curl, CURLOPT_URL, request_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wshowlyrics/0.1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        log_warn("iTunes API request failed: %s", curl_easy_strerror(res));
        curl_memory_buffer_free(&response);
        return NULL;
    }

    if (http_code != HTTP_OK) {
        log_warn("iTunes API returned HTTP %ld", http_code);
        curl_memory_buffer_free(&response);
        return NULL;
    }

    if (!response.data || response.size < 2) {
        log_warn("iTunes API: empty response");
        curl_memory_buffer_free(&response);
        return NULL;
    }

    // Check result count
    char *result_count_str = extract_json_string(response.data, "resultCount");
    if (result_count_str) {
        int result_count = atoi(result_count_str);
        free(result_count_str);

        if (result_count == 0) {
            log_info("iTunes API: no tracks found");
            curl_memory_buffer_free(&response);
            return NULL;
        }
    }

    // Extract artworkUrl100 from first result
    // iTunes API returns JSON like: {"resultCount":1,"results":[{"artworkUrl60":"...","artworkUrl100":"..."}]}
    char *artwork_url = extract_json_string(response.data, "artworkUrl100");

    if (!artwork_url) {
        // Try artworkUrl60 as fallback
        artwork_url = extract_json_string(response.data, "artworkUrl60");
    }

    if (artwork_url && strlen(artwork_url) > 0) {
        log_info("iTunes API: found artwork: %s", artwork_url);
    } else {
        log_info("iTunes API: no artwork found");
        free(artwork_url);
        artwork_url = NULL;
    }

    curl_memory_buffer_free(&response);
    return artwork_url;
}
