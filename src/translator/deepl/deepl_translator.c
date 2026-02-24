#include "deepl_translator.h"
#include "../common/translator_common.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/json/json_utils.h"
#include "../../utils/string/string_utils.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/lang_detect/lang_detect.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

// HTTP status codes
#define HTTP_OK 200
#define HTTP_UNAUTHORIZED 401
#define HTTP_TOO_MANY_REQUESTS 429
#define HTTP_QUOTA_EXCEEDED 456

// DeepL API endpoints
#define DEEPL_FREE_API_URL "https://api-free.deepl.com/v2/translate"
#define DEEPL_PRO_API_URL "https://api.deepl.com/v2/translate"

// Maximum request body size (for large lyrics files)
#define MAX_REQUEST_SIZE (64 * 1024)  // 64KB

// Get DeepL API endpoint based on API key format
// Free API keys end with ":fx"
static const char* get_deepl_endpoint(const char *api_key) {
    if (!api_key) return NULL;

    size_t len = strlen(api_key);
    if (len >= 3 && strcmp(&api_key[len - 3], ":fx") == 0) {
        return DEEPL_FREE_API_URL;
    }
    return DEEPL_PRO_API_URL;
}

// Escape special characters in JSON string
static char* json_escape_string(const char *text) {
    if (!text) return NULL;

    // Calculate required size (worst case: every char needs escaping)
    size_t len = strlen(text);
    size_t escaped_len = len * 2 + 1;

    char *escaped = malloc(escaped_len);
    if (!escaped) return NULL;

    char *dst = escaped;
    for (const char *src = text; *src; src++) {
        switch (*src) {
            case '"':
                *dst++ = '\\';
                *dst++ = '"';
                break;
            case '\\':
                *dst++ = '\\';
                *dst++ = '\\';
                break;
            case '\n':
                *dst++ = '\\';
                *dst++ = 'n';
                break;
            case '\r':
                *dst++ = '\\';
                *dst++ = 'r';
                break;
            case '\t':
                *dst++ = '\\';
                *dst++ = 't';
                break;
            default:
                *dst++ = *src;
                break;
        }
    }
    *dst = '\0';

    return escaped;
}

// Setup CURL request for DeepL API
// Returns true on success, false on failure
static bool setup_deepl_curl_request(CURL **curl_out,
                                      struct curl_slist **headers_out,
                                      const char *endpoint,
                                      const char *api_key,
                                      const char *request_body,
                                      struct curl_memory_buffer *response) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    // Enforce TLS 1.2 or higher
    if (curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2) != CURLE_OK) {
        curl_easy_cleanup(curl);
        return false;
    }

    // Build headers
    struct curl_slist *headers = NULL;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: DeepL-Auth-Key %s", api_key);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    // Configure request
    if (curl_easy_setopt(curl, CURLOPT_URL, endpoint) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    *curl_out = curl;
    *headers_out = headers;
    return true;
}

// Handle DeepL API response and errors
// Returns: 0 = success, 1 = retry, -1 = fatal error (no retry)
static int handle_deepl_response(CURLcode res, long http_code,
                                  const struct curl_memory_buffer *response,
                                  char **translation_out,
                                  int attempt, int max_retries) {
    if (res == CURLE_OK && http_code == HTTP_OK && response->data) {
        // Success - parse response
        char *raw_translation = json_extract_string_from(response->data, "text", response->data);
        if (raw_translation) {
            *translation_out = translator_extract_last_line(raw_translation);
            free(raw_translation);
        }
        return 0;  // Success
    }

    if (http_code == HTTP_UNAUTHORIZED || http_code == 403) {
        // Authentication error - don't retry
        log_error("deepl_translator: Authentication error (HTTP %ld)", http_code);
        return -1;  // Fatal error
    }

    // Temporary error - retry
    int retry_delay_ms = 5000 * attempt;  // Exponential backoff
    log_warn("deepl_translator: HTTP error %ld, retrying in %dms (attempt %d/%d)",
            http_code, retry_delay_ms, attempt, max_retries);

    if (attempt < max_retries) {
        struct timespec delay = {
            .tv_sec = retry_delay_ms / 1000,
            .tv_nsec = (retry_delay_ms % 1000) * 1000000L
        };
        nanosleep(&delay, NULL);
    }

    return 1;  // Retry
}

// Translate a single line (for individual requests)
static char* translate_single_line(const char *text, const char *target_lang,
                                   const char *api_key, const char *model_name) {
    (void)model_name;  // DeepL doesn't use model names

    if (!text || !target_lang || !api_key) {
        return NULL;
    }

    const struct config *cfg = config_get();
    const int max_retries = cfg->translation.max_retries;

    char *translation = NULL;
    int attempt = 0;

    while (attempt < max_retries && !translation) {
        attempt++;

        // Build JSON request
        char request_body[4096];
        char *escaped = json_escape_string(text);
        if (!escaped) {
            return NULL;
        }

        snprintf(request_body, sizeof(request_body),
                 "{\"text\":[\"%s\"],\"target_lang\":\"%s\"}",
                 escaped, target_lang);
        free(escaped);

        const char *endpoint = get_deepl_endpoint(api_key);

        // Setup CURL request using helper
        struct curl_memory_buffer response;
        curl_memory_buffer_init(&response);

        CURL *curl = NULL;
        struct curl_slist *headers = NULL;
        if (!setup_deepl_curl_request(&curl, &headers, endpoint, api_key,
                                       request_body, &response)) {
            return NULL;
        }

        // Perform request
        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        // Handle response using helper
        int result = handle_deepl_response(res, http_code, &response,
                                            &translation, attempt, max_retries);
        curl_memory_buffer_free(&response);

        if (result == 0) {
            break;  // Success
        } else if (result == -1) {
            return NULL;  // Fatal error
        }
        // result == 1: retry
    }

    return translation;
}

// Initialize translator
bool deepl_translator_init(void) {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        log_error("Failed to initialize CURL for DeepL translator");
        return false;
    }
    return true;
}

// Cleanup translator
void deepl_translator_cleanup(void) {
    curl_global_cleanup();
}

// Main translation function
bool deepl_translate_lyrics(struct lyrics_data *data, int64_t track_length_us) {
    const struct config *cfg = config_get();

    // Check if API key is configured
    if (!cfg->translation.api_key || cfg->translation.api_key[0] == '\0') {
        log_warn("DeepL translation: API key not configured");
        return false;
    }

    // Check if lyrics data is valid
    if (!data || !data->lines) {
        return false;
    }

    // Check if lyrics should be translated (LRC only)
    if (!translator_should_translate(data)) {
        return false;
    }

    // Build cache path
    char cache_path[PATH_BUFFER_SIZE];
    if (build_translation_cache_path(cache_path, sizeof(cache_path),
                                      data->md5_checksum,
                                      cfg->translation.target_language) <= 0) {
        log_warn("deepl_translator: Failed to build cache path");
        cache_path[0] = '\0';  // Mark as invalid
    }

    // Prepare thread args
    struct translator_thread_args *args = malloc(sizeof(*args));
    if (!args) {
        log_error("Failed to allocate memory for translation thread args");
        return false;
    }

    args->data = data;
    args->target_lang = strdup(cfg->translation.target_language);
    args->api_key = strdup(cfg->translation.api_key);
    args->model_name = NULL;  // DeepL doesn't use model names
    args->cache_path = cache_path[0] != '\0' ? strdup(cache_path) : strdup("");
    args->track_length_us = track_length_us;
    args->provider_name = "deepl_translator";
    args->translate_line_fn = translate_single_line;

    if (!args->target_lang || !args->api_key || !args->cache_path) {
        log_error("Failed to allocate memory for translation thread args");
        free(args->target_lang);
        free(args->api_key);
        free(args->cache_path);
        free(args);
        return false;
    }

    // Start translation thread
    if (pthread_create(&data->translation_thread, NULL, translator_async_worker, args) != 0) {
        log_error("Failed to create translation thread");
        free(args->target_lang);
        free(args->api_key);
        free(args->cache_path);
        free(args);
        return false;
    }

    // Don't detach - we need the handle for cancellation
    return true;
}
