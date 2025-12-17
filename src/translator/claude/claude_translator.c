#include "claude_translator.h"
#include "../common/translator_common.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/lang_detect/lang_detect.h"
#include "../../utils/render/render_common.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// Claude API endpoint
#define CLAUDE_API_ENDPOINT "https://api.anthropic.com/v1/messages"

// API version
#define CLAUDE_API_VERSION "2023-06-01"

// Global CURL handle
static CURL *curl_handle = NULL;

// Thread argument structure
struct translation_thread_args {
    struct lyrics_data *data;
    char *target_lang;
    char *api_key;
    char *model_name;
    char *cache_path;
};

// CURL write callback
struct curl_response {
    char *data;
    size_t size;
};

static size_t claude_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct curl_response *response = (struct curl_response *)userp;

    char *ptr = realloc(response->data, response->size + realsize + 1);
    if (!ptr) {
        log_error("claude_translator: Memory allocation failed");
        return 0;
    }

    response->data = ptr;
    memcpy(&(response->data[response->size]), contents, realsize);
    response->size += realsize;
    response->data[response->size] = 0;

    return realsize;
}

/**
 * Build Claude API request JSON
 * Request format: {"model": "...", "max_tokens": 1024, "messages": [{"role": "user", "content": "..."}]}
 */
static char* build_request_json(const char *text, const char *target_lang, const char *model_name) {
    json_object *root = json_object_new_object();
    json_object *messages_array = json_object_new_array();
    json_object *message_obj = json_object_new_object();

    // Build prompt: clear instruction for translation only
    char prompt[8192];
    snprintf(prompt, sizeof(prompt),
             "You are a professional translator. "
             "Translate the following text to %s. "
             "Output ONLY the translated text with no additional explanations:\n\n%s",
             target_lang, text);

    json_object_object_add(root, "model", json_object_new_string(model_name));
    json_object_object_add(root, "max_tokens", json_object_new_int(1024));

    json_object_object_add(message_obj, "role", json_object_new_string("user"));
    json_object_object_add(message_obj, "content", json_object_new_string(prompt));
    json_object_array_add(messages_array, message_obj);

    json_object_object_add(root, "messages", messages_array);

    const char *json_str = json_object_to_json_string(root);
    char *result = strdup(json_str);
    json_object_put(root);

    return result;
}

/**
 * Parse Claude API response JSON
 * Response format: {"content": [{"text": "..."}]}
 */
static char* parse_response_json(const char *json_str) {
    json_object *root = json_tokener_parse(json_str);
    if (!root) {
        log_error("claude_translator: Failed to parse JSON response");
        return NULL;
    }

    // Navigate: content[0].text
    json_object *content;
    if (!json_object_object_get_ex(root, "content", &content)) {
        log_error("claude_translator: No 'content' in response");
        json_object_put(root);
        return NULL;
    }

    json_object *content_item = json_object_array_get_idx(content, 0);
    if (!content_item) {
        log_error("claude_translator: Empty content array");
        json_object_put(root);
        return NULL;
    }

    json_object *text_obj;
    if (!json_object_object_get_ex(content_item, "text", &text_obj)) {
        log_error("claude_translator: No 'text' in content item");
        json_object_put(root);
        return NULL;
    }

    const char *text = json_object_get_string(text_obj);
    // Extract last line to handle cases where AI includes original text
    char *result = translator_extract_last_line(text);
    json_object_put(root);

    return result;
}

/**
 * Parse retry delay from error response (similar to Gemini)
 */
static int parse_retry_delay_claude(const char *response_json) {
    if (!response_json) {
        return 0;
    }

    // Look for retry-related patterns in Claude error responses
    // Claude may return different error formats
    const char *retry_str = strstr(response_json, "retry");
    if (retry_str) {
        // Try to find seconds value
        float seconds = 0;
        if (sscanf(retry_str, "retry in %fs", &seconds) == 1 ||
            sscanf(retry_str, "retry after %fs", &seconds) == 1) {
            return (int)(seconds * 1000) + 1000;
        }
    }

    return 0;
}

/**
 * Translate a single line using Claude API with retry logic
 */
static char* translate_single_line(const char *text, const char *target_lang,
                                   const char *api_key, const char *model_name) {
    if (!text || !target_lang || !api_key || !model_name) {
        return NULL;
    }

    // Create thread-local CURL handle to avoid race conditions
    CURL *local_curl = curl_easy_init();
    if (!local_curl) {
        log_error("claude_translator: Failed to initialize CURL for translation");
        return NULL;
    }

    // Get retry settings from config
    struct config *cfg = config_get();
    const int max_retries = cfg->translation.max_retries;

    char *translation = NULL;
    int attempt = 0;

    while (attempt < max_retries && !translation) {
        attempt++;

        // Build request JSON
        char *request_json = build_request_json(text, target_lang, model_name);
        if (!request_json) {
            curl_easy_cleanup(local_curl);
            return NULL;
        }

        // Setup CURL
        struct curl_response response = {0};
        curl_easy_setopt(local_curl, CURLOPT_URL, CLAUDE_API_ENDPOINT);
        curl_easy_setopt(local_curl, CURLOPT_POSTFIELDS, request_json);
        curl_easy_setopt(local_curl, CURLOPT_WRITEFUNCTION, claude_curl_write_callback);
        curl_easy_setopt(local_curl, CURLOPT_WRITEDATA, &response);

        // Setup headers
        char api_key_header[512];
        snprintf(api_key_header, sizeof(api_key_header), "x-api-key: %s", api_key);

        char version_header[128];
        snprintf(version_header, sizeof(version_header), "anthropic-version: %s", CLAUDE_API_VERSION);

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, api_key_header);
        headers = curl_slist_append(headers, version_header);
        curl_easy_setopt(local_curl, CURLOPT_HTTPHEADER, headers);

        // Perform request
        CURLcode res = curl_easy_perform(local_curl);
        curl_slist_free_all(headers);
        free(request_json);

        if (res != CURLE_OK) {
            log_error("claude_translator: CURL error: %s", curl_easy_strerror(res));
            free(response.data);
            curl_easy_cleanup(local_curl);
            return NULL;
        }

        // Check HTTP response code
        long http_code = 0;
        curl_easy_getinfo(local_curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 200) {
            // Success - parse response
            translation = parse_response_json(response.data);
            free(response.data);
            break;
        } else if (http_code == 401 || http_code == 403) {
            // Authentication/permission error - don't retry
            log_error("claude_translator: Authentication error (HTTP %ld)", http_code);
            free(response.data);
            curl_easy_cleanup(local_curl);
            return NULL;
        } else {
            // Temporary error - retry with delay
            int retry_delay_ms = parse_retry_delay_claude(response.data);
            if (retry_delay_ms == 0) {
                retry_delay_ms = 5000 * attempt; // Exponential backoff
            }

            log_warn("claude_translator: HTTP error %ld, retrying in %dms (attempt %d/%d)",
                    http_code, retry_delay_ms, attempt, max_retries);

            free(response.data);

            if (attempt < max_retries) {
                struct timespec delay = {
                    .tv_sec = retry_delay_ms / 1000,
                    .tv_nsec = (retry_delay_ms % 1000) * 1000000L
                };
                nanosleep(&delay, NULL);
            }
        }
    }

    curl_easy_cleanup(local_curl);
    return translation;
}

/**
 * Async translation thread function
 */
static void* translate_lyrics_async(void *arg) {
    struct translation_thread_args *args = (struct translation_thread_args *)arg;
    struct lyrics_data *data = args->data;

    // Count translatable lines
    int translatable_count = translator_count_translatable_lines(data);
    data->translation_total = translatable_count;
    data->translation_in_progress = true;

    // Try loading from cache first
    int already_translated = 0;
    bool cache_loaded = translator_load_from_cache(args->cache_path, data);

    if (cache_loaded) {
        if (translator_check_cache_complete(data, translatable_count, &already_translated)) {
            // Cache is complete
            log_success("Found cached translation: %s", args->cache_path);
            log_success("claude_translator: Loaded complete translation from cache (%d lines)",
                       already_translated);
            data->translation_current = already_translated;
            data->translation_in_progress = false;
            free(args->target_lang);
            free(args->api_key);
            free(args->model_name);
            free(args->cache_path);
            free(args);
            return NULL;
        } else {
            // Partial cache - continue translation
            // Re-validate last N lines to ensure they were fully translated
            struct config *cfg = config_get();
            int revalidate_count = cfg->translation.revalidate_count;
            translator_prepare_cache_resume(data, &already_translated, revalidate_count);
            log_info("Found cached translation: %s", args->cache_path);
            log_info("claude_translator: Loaded partial cache (%d/%d), re-validating last %d lines",
                    already_translated, translatable_count, revalidate_count);
            data->translation_current = already_translated;
        }
    } else {
        data->translation_current = 0;
        log_info("claude_translator: Starting translation of %d lines", translatable_count);
    }

    // Translate each line (skip already translated lines from cache)
    struct lyrics_line *line = data->lines;
    int current = already_translated;
    while (line) {
        // Check if translation should be cancelled
        if (data->translation_should_cancel) {
            log_info("claude_translator: Translation cancelled (%d/%d completed)",
                     current, translatable_count);
            break;
        }

        if (line->text && strlen(line->text) > 0) {
            // Skip if already translated from cache
            if (line->translation && strlen(line->translation) > 0) {
                line = line->next;
                continue;
            }

            current++;
            data->translation_current = current;

            // Strip ruby notation to reduce token usage
            char *stripped = strip_ruby_notation(line->text);
            if (!stripped || stripped[0] == '\0') {
                free(stripped);
                line = line->next;
                continue;
            }

            // Check if text is already in target language (skip API call)
            if (is_already_in_language(stripped, args->target_lang)) {
                log_info("claude_translator: [%d/%d] Skipped (already in target language)",
                       current, translatable_count);
                free(stripped);
                line = line->next;
                continue;
            }

            char *translation = translate_single_line(stripped, args->target_lang,
                                                     args->api_key, args->model_name);
            if (translation) {
                // Language validation: verify translation is different language
                // This is a fallback check in case pre-detection missed it
                if (is_same_language(stripped, translation)) {
                    log_info("claude_translator: [%d/%d] Skipped (same language after translation)",
                           current, translatable_count);
                    free(translation);
                    // Don't set translation - original text will be displayed
                } else {
                    free(line->translation);
                    line->translation = translation;
                    log_info("claude_translator: [%d/%d] Translated: %s",
                           current, translatable_count, translation);
                }
            } else {
                log_warn("claude_translator: [%d/%d] Translation failed",
                       current, translatable_count);
            }

            free(stripped);

            // Rate limiting
            if (line->next) {
                struct config *cfg = config_get();
                int rate_limit_ms = cfg->translation.rate_limit_ms;
                struct timespec delay = {
                    .tv_sec = rate_limit_ms / 1000,
                    .tv_nsec = (rate_limit_ms % 1000) * 1000000L
                };
                nanosleep(&delay, NULL);
            }
        }
        line = line->next;
    }

    // Save to cache if translation is complete or threshold reached
    struct config *cfg = config_get();
    float cache_threshold = config_get_cache_threshold(cfg->translation.cache_policy);
    float completion_ratio = (float)data->translation_current / (float)data->translation_total;

    if (data->translation_current == data->translation_total) {
        translator_save_to_cache(args->cache_path, data, args->target_lang);
        log_success("claude_translator: Translation completed");
    } else if (completion_ratio >= cache_threshold) {
        translator_save_to_cache(args->cache_path, data, args->target_lang);
        log_warn("claude_translator: Translation incomplete but cached (%d/%d, %.0f%%)",
                 data->translation_current, data->translation_total, completion_ratio * 100);
    } else {
        log_warn("claude_translator: Translation incomplete (%d/%d, %.0f%%), not cached (threshold: %.0f%%)",
                 data->translation_current, data->translation_total, completion_ratio * 100, cache_threshold * 100);
        // Delete incomplete cache file if it exists
        unlink(args->cache_path);
    }

    data->translation_in_progress = false;

    free(args->target_lang);
    free(args->api_key);
    free(args->model_name);
    free(args->cache_path);
    free(args);
    return NULL;
}

bool claude_translator_init(void) {
    if (curl_handle) {
        return true; // Already initialized
    }

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        log_error("claude_translator: Failed to initialize CURL");
        return false;
    }

    return true;
}

void claude_translator_cleanup(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
}

bool claude_translate_lyrics(struct lyrics_data *data) {
    if (!data || !curl_handle) {
        return false;
    }

    // Check if lyrics should be translated (LRC only)
    if (!translator_should_translate(data)) {
        return false;
    }

    // Get config
    const char *provider = g_config.translation.provider;
    const char *api_key = g_config.translation.api_key;
    const char *target_lang = g_config.translation.target_language;

    // Extract model name from provider (e.g., "claude-sonnet-4-5" -> "claude-sonnet-4-5")
    const char *model_name = provider;

    // Validate
    if (!api_key || strlen(api_key) == 0) {
        log_error("claude_translator: API key not configured");
        return false;
    }

    if (!model_name || strlen(model_name) == 0) {
        log_error("claude_translator: Model name not configured");
        return false;
    }

    // Validate MD5 checksum
    if (strlen(data->md5_checksum) == 0) {
        log_error("claude_translator: MD5 checksum is empty, cannot cache translation");
        return false;
    }

    // Build cache path
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path),
             "/tmp/wshowlyrics/translated/%s_%s.json",
             data->md5_checksum, target_lang);

    // Create cache directory
    if (system("mkdir -p /tmp/wshowlyrics/translated") != 0) {
        log_warn("claude_translator: Failed to create cache directory");
    }

    // Prepare thread arguments
    struct translation_thread_args *args = malloc(sizeof(struct translation_thread_args));
    if (!args) {
        return false;
    }

    args->data = data;
    args->target_lang = strdup(target_lang);
    args->api_key = strdup(api_key);
    args->model_name = strdup(model_name);
    args->cache_path = strdup(cache_path);

    // Launch async translation thread
    if (pthread_create(&data->translation_thread, NULL, translate_lyrics_async, args) != 0) {
        log_error("claude_translator: Failed to create translation thread");
        free(args->target_lang);
        free(args->api_key);
        free(args->model_name);
        free(args->cache_path);
        free(args);
        return false;
    }

    // Don't detach - we need the handle for cancellation
    return true;
}
