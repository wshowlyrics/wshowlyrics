#include "deepl_translator.h"
#include "../common/translator_common.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/json/json_utils.h"
#include "../../utils/string/string_utils.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/render/render_common.h"
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

// Logging macros are defined in constants.h

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

// Rate limit delay between individual requests (milliseconds)
#define RATE_LIMIT_DELAY_MS 200

// Thread argument for async translation
struct translation_thread_args {
    struct lyrics_data *data;
    char *target_lang;
    char *api_key;
    char *cache_path;
};


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

// Build JSON request body with all lyrics lines (DEPRECATED - now using individual line translation)
#if 0
static char* build_json_request(struct lyrics_data *data, const char *target_lang) {
    if (!data || !data->lines || !target_lang) return NULL;

    // Allocate buffer for request
    char *buffer = malloc(MAX_REQUEST_SIZE);
    if (!buffer) return NULL;

    int offset = snprintf(buffer, MAX_REQUEST_SIZE,
                         "{\"text\":[");

    // Add each line's full text as a separate array element
    struct lyrics_line *line = data->lines;
    bool first = true;

    while (line && offset < MAX_REQUEST_SIZE - 1024) {
        // Only translate lines with ruby_segments (LRC format)
        if (line->ruby_segments && line->text && line->text[0] != '\0') {
            if (!first) {
                offset += snprintf(buffer + offset, MAX_REQUEST_SIZE - offset, ",");
            }

            // Strip ruby notation before translation
            // Example: "心{こころ}音{ね}" -> "心音"
            char *stripped = strip_ruby_notation(line->text);
            if (!stripped) {
                log_warn("Failed to strip ruby from: %s", line->text);
                line = line->next;
                continue;
            }

            // Debug: Check if stripping resulted in empty text
            if (stripped[0] == '\0') {
                log_warn("Stripped text is empty, skipping: %s", line->text);
                free(stripped);
                line = line->next;
                continue;
            }

            // Escape quotes and special characters
            char *escaped = json_escape_string(stripped);
            free(stripped);

            if (escaped) {
                offset += snprintf(buffer + offset, MAX_REQUEST_SIZE - offset,
                                 "\"%s\"", escaped);
                free(escaped);
                first = false;
            }
        }
        line = line->next;
    }

    offset += snprintf(buffer + offset, MAX_REQUEST_SIZE - offset,
                      "],\"target_lang\":\"%s\"}", target_lang);

    return buffer;
}
#endif

// Parse DeepL API response and populate translation fields (DEPRECATED - now using individual line translation)
#if 0
static bool parse_and_populate_translations(struct curl_memory_buffer *response,
                                             struct lyrics_data *data) {
    if (!response || !response->data || !data) return false;

    // Response format: {"translations": [{"text": "..."}, {"text": "..."}, ...]}
    // Find translations array
    char *translations_start = strstr(response->data, "\"translations\":");
    if (!translations_start) {
        log_error("Invalid DeepL API response: no translations field");
        return false;
    }

    // Iterate through lyrics lines and translation results in parallel
    struct lyrics_line *line = data->lines;
    const char *search_pos = translations_start;
    int translation_count = 0;

    while (line) {
        // Only process lines with ruby_segments (LRC format)
        if (line->ruby_segments && line->text && line->text[0] != '\0') {
            // Check if line would be included in translation request
            // (same logic as build_json_request)
            char *stripped = strip_ruby_notation(line->text);
            bool should_translate = stripped && stripped[0] != '\0';
            if (stripped) free(stripped);

            if (should_translate) {
                // Extract next translation from JSON array
                char *translation = json_extract_string_from(response->data,
                                                             "text",
                                                             search_pos);
                if (translation) {
                    line->translation = translation;
                    translation_count++;

                    // Move search position past the current translation
                    // Find where this translation was extracted from
                    const char *text_pos = strstr(search_pos, "\"text\":\"");
                    if (text_pos) {
                        // Move past "text":"<translation>"
                        // We need to skip the entire value, so find the closing quote
                        const char *value_start = text_pos + strlen("\"text\":\"");
                        const char *value_end = value_start;
                        while (*value_end && *value_end != '"') {
                            if (*value_end == '\\' && *(value_end + 1)) {
                                value_end += 2; // Skip escaped character
                            } else {
                                value_end++;
                            }
                        }
                        if (*value_end == '"') {
                            search_pos = value_end + 1; // Move past closing quote
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                } else {
                    log_warn("Missing translation for text: %s", line->text);
                    break;
                }
            }
        }
        line = line->next;
    }

    if (translation_count > 0) {
        log_info("Received %d translations from DeepL API", translation_count);
    }

    return true;
}
#endif

// Translate a single line (for individual requests)
static char* translate_single_line(const char *text, const char *target_lang, const char *api_key) {
    if (!text || !target_lang || !api_key) return NULL;

    // Get retry settings from config
    struct config *cfg = config_get();
    const int max_retries = cfg->translation.max_retries;

    char *translation = NULL;
    int attempt = 0;

    while (attempt < max_retries && !translation) {
        attempt++;

        // Build JSON request for single line
        char request_body[4096];
        char *escaped = json_escape_string(text);
        if (!escaped) return NULL;

        snprintf(request_body, sizeof(request_body),
                 "{\"text\":[\"%s\"],\"target_lang\":\"%s\"}",
                 escaped, target_lang);
        free(escaped);

        // Get API endpoint
        const char *endpoint = get_deepl_endpoint(api_key);

        // Perform CURL request
        struct curl_memory_buffer response;
        curl_memory_buffer_init(&response);

        CURL *curl = curl_easy_init();
        if (!curl) return NULL;

        struct curl_slist *headers = NULL;
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header),
                 "Authorization: DeepL-Auth-Key %s", api_key);
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl, CURLOPT_URL, endpoint);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && http_code == HTTP_OK && response.data) {
            // Success - parse response
            char *raw_translation = json_extract_string_from(response.data, "text", response.data);
            if (raw_translation) {
                translation = translator_extract_last_line(raw_translation);
                free(raw_translation);
            }
            curl_memory_buffer_free(&response);
            break;
        } else if (http_code == HTTP_UNAUTHORIZED || http_code == 403) {
            // Authentication/permission error - don't retry
            log_error("deepl_translator: Authentication error (HTTP %ld)", http_code);
            curl_memory_buffer_free(&response);
            return NULL;
        } else {
            // Temporary error - retry with delay
            int retry_delay_ms = 5000 * attempt; // Exponential backoff

            log_warn("deepl_translator: HTTP error %ld, retrying in %dms (attempt %d/%d)",
                    http_code, retry_delay_ms, attempt, max_retries);

            curl_memory_buffer_free(&response);

            if (attempt < max_retries) {
                struct timespec delay = {
                    .tv_sec = retry_delay_ms / 1000,
                    .tv_nsec = (retry_delay_ms % 1000) * 1000000L
                };
                nanosleep(&delay, NULL);
            }
        }
    }

    return translation;
}

// Async translation thread function
static void* translate_lyrics_async(void *arg) {
    struct translation_thread_args *args = (struct translation_thread_args*)arg;
    struct lyrics_data *data = args->data;
    const char *target_lang = args->target_lang;
    const char *api_key = args->api_key;
    const char *cache_path = args->cache_path;

    // Count translatable lines
    int translatable_count = translator_count_translatable_lines(data);
    data->translation_total = translatable_count;
    data->translation_in_progress = true;

    // Try loading from cache first
    int already_translated = 0;
    bool cache_loaded = translator_load_from_cache(cache_path, data);

    if (cache_loaded) {
        if (translator_check_cache_complete(data, translatable_count, &already_translated)) {
            // Cache is complete
            log_success("deepl_translator: Loaded complete translation from cache (%d lines)",
                       already_translated);
            data->translation_current = already_translated;
            data->translation_in_progress = false;
            free(args->target_lang);
            free(args->api_key);
            free(args->cache_path);
            free(args);
            return NULL;
        } else {
            // Partial cache - continue translation
            // Re-validate last N lines to ensure they were fully translated
            struct config *cfg = config_get();
            int revalidate_count = cfg->translation.revalidate_count;
            translator_prepare_cache_resume(data, &already_translated, revalidate_count);
            log_info("deepl_translator: Loaded partial cache (%d/%d), re-validating last %d lines",
                    already_translated, translatable_count, revalidate_count);
            data->translation_current = already_translated;
        }
    } else {
        data->translation_current = 0;
        log_info("deepl_translator: Starting translation of %d lines", translatable_count);
    }

    // Translate each line (skip already translated lines from cache)
    struct lyrics_line *line = data->lines;
    int current = already_translated;
    while (line) {
        // Check if translation should be cancelled
        if (data->translation_should_cancel) {
            log_info("deepl_translator: Translation cancelled (%d/%d completed)",
                     current, translatable_count);
            break;
        }

        if (line->text && strlen(line->text) > 0) {
            // Skip if already translated from cache
            if (line->translation && strlen(line->translation) > 0) {
                line = line->next;
                continue;
            }

            // Strip ruby notation
            char *stripped = strip_ruby_notation(line->text);
            if (!stripped || stripped[0] == '\0') {
                free(stripped);
                line = line->next;
                continue;
            }

            // Translate this line
            current++;
            data->translation_current = current;

            // Skip if text is already in target language
            char *skipped_translation = NULL;
            if (translator_should_skip_translation(stripped, target_lang, &skipped_translation)) {
                line->translation = skipped_translation;
                log_info("deepl_translator: [%d/%d] Already in target language: %s",
                         current, translatable_count, skipped_translation);
                free(stripped);
                line = line->next;
                continue;
            }

            char *translation = translate_single_line(stripped, target_lang, api_key);

            if (translation) {
                // Language validation: warn if original and translation are in same language
                if (is_same_language(stripped, translation)) {
                    log_warn("deepl_translator: Possible translation failure - same language detected");
                    log_warn("  Original: [%.30s...] → Translation: [%.30s...]",
                             stripped, translation);
                    // Translation is still displayed - user decides
                }

                line->translation = translation;
                log_info("deepl_translator: [%d/%d] Translated: %s",
                       current, translatable_count, translation);
            } else {
                log_warn("deepl_translator: [%d/%d] Translation failed",
                       current, translatable_count);
            }

            free(stripped);

            // Rate limit delay
            if (line->next) {
                translator_rate_limit_delay(RATE_LIMIT_DELAY_MS);
            }
        }
        line = line->next;
    }

    // Save to cache if translation is complete or threshold reached
    struct config *cfg = config_get();
    float cache_threshold = config_get_cache_threshold(cfg->translation.cache_policy);
    float completion_ratio = (float)data->translation_current / (float)data->translation_total;

    if (data->translation_current == data->translation_total) {
        translator_save_to_cache(cache_path, data, target_lang);
        log_success("deepl_translator: Translation completed");
    } else if (completion_ratio >= cache_threshold) {
        translator_save_to_cache(cache_path, data, target_lang);
        log_warn("deepl_translator: Translation incomplete but cached (%d/%d, %.0f%%)",
                 data->translation_current, data->translation_total, completion_ratio * 100);
    } else {
        log_warn("deepl_translator: Translation incomplete (%d/%d, %.0f%%), not cached (threshold: %.0f%%)",
                 data->translation_current, data->translation_total, completion_ratio * 100, cache_threshold * 100);
        // Delete incomplete cache file if it exists
        unlink(cache_path);
    }

    // Mark translation complete
    data->translation_in_progress = false;

    // Free thread args
    free(args->target_lang);
    free(args->api_key);
    free(args->cache_path);
    free(args);

    return NULL;
}

// Perform API translation request (DEPRECATED - now using individual line translation)
#if 0
static bool perform_api_translation(struct lyrics_data *data, const char *target_lang) {
    struct config *cfg = config_get();

    log_info("Requesting translation from DeepL API...");

    // Build JSON request body
    char *request_body = build_json_request(data, target_lang);
    if (!request_body) {
        log_error("Failed to build JSON request body");
        return false;
    }

    // Get API endpoint
    const char *endpoint = get_deepl_endpoint(cfg->translation.api_key);
    if (!endpoint) {
        log_error("Invalid API key format");
        free(request_body);
        return false;
    }

    log_info("DeepL API request to: %s", endpoint);
    log_info("Target language: %s", target_lang);

    // Initialize CURL
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("Failed to initialize CURL");
        free(request_body);
        return false;
    }

    // Prepare response buffer
    struct curl_memory_buffer response;
    curl_memory_buffer_init(&response);

    // Build Authorization header
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: DeepL-Auth-Key %s", cfg->translation.api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    // Configure CURL request
    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);  // 30 second timeout
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    // Cleanup CURL resources
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(request_body);

    // Check for errors
    if (res != CURLE_OK) {
        log_error("DeepL API request failed: %s", curl_easy_strerror(res));
        curl_memory_buffer_free(&response);
        return false;
    }

    if (http_code != HTTP_OK) {
        if (http_code == HTTP_UNAUTHORIZED) {
            log_error("DeepL API authentication failed (HTTP %ld): Invalid API key", http_code);
        } else if (http_code == HTTP_TOO_MANY_REQUESTS) {
            log_warn("DeepL API rate limit exceeded (HTTP %ld): Please try again later", http_code);
        } else if (http_code == HTTP_QUOTA_EXCEEDED) {
            log_warn("DeepL API quota exceeded (HTTP %ld): Check your usage limits", http_code);
        } else {
            log_error("DeepL API returned HTTP %ld", http_code);
        }
        curl_memory_buffer_free(&response);
        return false;
    }

    // Check response data
    if (!response.data || response.size < 2) {
        log_error("DeepL API returned empty response");
        curl_memory_buffer_free(&response);
        return false;
    }

    // Parse response and populate translations
    bool success = parse_and_populate_translations(&response, data);
    curl_memory_buffer_free(&response);

    if (!success) {
        return false;
    }

    log_success("Translation successful");
    return true;
}
#endif

// Main translation function
bool deepl_translate_lyrics(struct lyrics_data *data) {
    struct config *cfg = config_get();

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
    struct translation_thread_args *args = malloc(sizeof(struct translation_thread_args));
    if (!args) {
        log_error("Failed to allocate memory for translation thread args");
        return false;
    }

    args->data = data;
    args->target_lang = strdup(cfg->translation.target_language);
    args->api_key = strdup(cfg->translation.api_key);
    args->cache_path = cache_path[0] != '\0' ? strdup(cache_path) : strdup("");

    if (!args->target_lang || !args->api_key || !args->cache_path) {
        log_error("Failed to allocate memory for translation thread args");
        free(args->target_lang);
        free(args->api_key);
        free(args->cache_path);
        free(args);
        return false;
    }

    // Start translation thread
    if (pthread_create(&data->translation_thread, NULL, translate_lyrics_async, args) != 0) {
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
