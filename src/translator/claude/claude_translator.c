#include "claude_translator.h"
#include "../common/translator_common.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/lang_detect/lang_detect.h"
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

/**
 * Build Claude API request JSON
 * Request format: {"model": "...", "max_tokens": 1024, "messages": [{"role": "user", "content": "..."}]}
 */
static char* build_request_json(const char *text, const char *target_lang, const char *model_name) {
    json_object *root = json_object_new_object();
    json_object *messages_array = json_object_new_array();
    json_object *message_obj = json_object_new_object();

    // Build standard translation prompt
    char prompt[8192];
    if (translator_build_translation_prompt(prompt, sizeof(prompt), text, target_lang) < 0) {
        return NULL;
    }

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

    // Enforce TLS 1.2 or higher for security
    curl_easy_setopt(local_curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

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
        struct translator_curl_response response;
        translator_curl_response_init(&response);
        curl_easy_setopt(local_curl, CURLOPT_URL, CLAUDE_API_ENDPOINT);
        curl_easy_setopt(local_curl, CURLOPT_POSTFIELDS, request_json);
        curl_easy_setopt(local_curl, CURLOPT_WRITEFUNCTION, translator_curl_write_callback);
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
            translator_curl_response_free(&response);
            curl_easy_cleanup(local_curl);
            return NULL;
        }

        // Check HTTP response code
        long http_code = 0;
        curl_easy_getinfo(local_curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 200) {
            // Success - parse response
            translation = parse_response_json(response.data);
            translator_curl_response_free(&response);
            break;
        } else if (http_code == 401 || http_code == 403) {
            // Authentication/permission error - don't retry
            log_error("claude_translator: Authentication error (HTTP %ld)", http_code);
            translator_curl_response_free(&response);
            curl_easy_cleanup(local_curl);
            return NULL;
        } else {
            // Temporary error - retry with delay
            int retry_delay_ms = translator_parse_retry_delay(response.data);
            if (retry_delay_ms == 0) {
                retry_delay_ms = 5000 * attempt; // Exponential backoff
            }

            log_warn("claude_translator: HTTP error %ld, retrying in %dms (attempt %d/%d)",
                    http_code, retry_delay_ms, attempt, max_retries);

            translator_curl_response_free(&response);

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

bool claude_translator_init(void) {
    if (curl_handle) {
        return true; // Already initialized
    }

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        log_error("claude_translator: Failed to initialize CURL");
        return false;
    }

    // Enforce TLS 1.2 or higher for security
    curl_easy_setopt(curl_handle, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    return true;
}

void claude_translator_cleanup(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
}

bool claude_translate_lyrics(struct lyrics_data *data, int64_t track_length_us) {
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

    // Create cache directories
    if (!ensure_cache_directories()) {
        log_error("claude_translator: Failed to create cache directories");
        return false;
    }

    // Prepare thread arguments
    struct translator_thread_args *args = malloc(sizeof(struct translator_thread_args));
    if (!args) {
        return false;
    }

    args->data = data;
    args->target_lang = strdup(target_lang);
    args->api_key = strdup(api_key);
    args->model_name = strdup(model_name);
    args->cache_path = strdup(cache_path);
    args->track_length_us = track_length_us;
    args->provider_name = "claude_translator";
    args->translate_line_fn = translate_single_line;

    // Launch async translation thread
    if (pthread_create(&data->translation_thread, NULL, translator_async_worker, args) != 0) {
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
