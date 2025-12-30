#include "itunes_artwork.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/string/string_utils.h"
#include "../../utils/json/json_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <curl/curl.h>

// Deprecated: Use json_extract_string from json_utils.h instead
static char* extract_json_string(const char *json, const char *key) {
    return json_extract_string(json, key);
}

// Deprecated: Use curl_url_encode from curl_utils.h instead
static char* url_encode(CURL *curl, const char *str) {
    return curl_url_encode(curl, str);
}

// Helper: Build search term with available metadata
static void build_search_term_with_metadata(char *buffer, size_t buffer_size,
                                            const char *clean_track,
                                            const char *artist, const char *album) {
    bool has_artist = (artist && artist[0] != '\0' && strcasecmp(artist, "Unknown") != 0);
    bool has_album = (album && album[0] != '\0' && strcasecmp(album, "Unknown") != 0);

    int offset = snprintf(buffer, buffer_size, "%s", clean_track);

    if (has_artist) {
        offset += snprintf(buffer + offset, buffer_size - offset, " %s", artist);
    }

    if (has_album) {
        snprintf(buffer + offset, buffer_size - offset, " %s", album);
    }

    log_info("iTunes search metadata (artist: %s, album: %s)",
             has_artist ? artist : "none", has_album ? album : "none");
}

// Helper: Setup CURL options for iTunes request
static bool setup_itunes_request(CURL *curl, const char *url,
                                 struct curl_memory_buffer *response) {
    if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK) {
        log_error("iTunes: Failed to set CURL options");
        return false;
    }
    return true;
}

// Helper: Extract artwork URL from iTunes API response
static char* extract_artwork_from_response(const char *response_data) {
    // Check result count
    char *result_count_str = extract_json_string(response_data, "resultCount");
    if (result_count_str) {
        int result_count = atoi(result_count_str);
        free(result_count_str);

        if (result_count == 0) {
            log_info("iTunes API: no tracks found");
            return NULL;
        }
    }

    // Extract artworkUrl100 from first result
    char *artwork_url = extract_json_string(response_data, "artworkUrl100");

    if (!artwork_url) {
        // Try artworkUrl60 as fallback
        artwork_url = extract_json_string(response_data, "artworkUrl60");
    }

    if (artwork_url && artwork_url[0] != '\0') {
        log_info("iTunes API: found artwork: %s", artwork_url);
    } else {
        log_info("iTunes API: no artwork found");
        free(artwork_url);
        artwork_url = NULL;
    }

    return artwork_url;
}

char* itunes_search_artwork(const char *artist, const char *album, const char *track) {
    // Require at least a track title with non-empty content
    if (!track || track[0] == '\0') {
        log_info("Missing track title, cannot search iTunes");
        return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("Failed to initialize CURL for iTunes search");
        return NULL;
    }

    // Enforce TLS 1.2 or higher for security
    if (curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2) != CURLE_OK) {
        log_error("iTunes: Failed to set SSL version");
        curl_easy_cleanup(curl);
        return NULL;
    }

    // Sanitize track title to remove YouTube IDs and file extensions
    char *clean_track = sanitize_title(track);
    if (!clean_track || clean_track[0] == '\0') {
        free(clean_track);
        curl_easy_cleanup(curl);
        return NULL;
    }

    if (strcmp(track, clean_track) != 0) {
        log_info("iTunes search: sanitized '%s' -> '%s'", track, clean_track);
    }

    // Build search term with available metadata
    char search_term[URL_BUFFER_SIZE];
    build_search_term_with_metadata(search_term, sizeof(search_term),
                                    clean_track, artist, album);

    free(clean_track);

    // URL encode search term
    char *term_encoded = url_encode(curl, search_term);
    if (!term_encoded) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    // Build iTunes Search API URL
    char request_url[URL_BUFFER_SIZE];
    snprintf(request_url, sizeof(request_url),
             "https://itunes.apple.com/search?term=%s&entity=song&limit=1",
             term_encoded);

    curl_free(term_encoded);

    log_info("iTunes API request: %s", request_url);

    // Setup CURL request
    struct curl_memory_buffer response;
    curl_memory_buffer_init(&response);

    if (!setup_itunes_request(curl, request_url, &response)) {
        curl_memory_buffer_free(&response);
        curl_easy_cleanup(curl);
        return NULL;
    }

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

    // Extract artwork URL from response
    char *artwork_url = extract_artwork_from_response(response.data);

    curl_memory_buffer_free(&response);
    return artwork_url;
}
