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
    // Extract text using JSON path: content[0].text
    char *text = json_extract_text_by_path(json_str, "content[0].text", "claude_translator");
    if (!text) {
        return NULL;
    }

    // Extract last line to handle cases where AI includes original text
    char *result = translator_extract_last_line(text);
    free(text);
    return result;
}

/**
 * Build Claude-specific HTTP headers
 * extra_data is expected to be the API version string
 */
static struct curl_slist* build_claude_headers(const char *api_key, const void *extra_data) {
    const char *api_version = (const char *)extra_data;
    if (!api_version) {
        api_version = CLAUDE_API_VERSION;
    }

    char api_key_header[512];
    snprintf(api_key_header, sizeof(api_key_header), "x-api-key: %s", api_key);

    char version_header[128];
    snprintf(version_header, sizeof(version_header), "anthropic-version: %s", api_version);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, api_key_header);
    headers = curl_slist_append(headers, version_header);

    return headers;
}

/**
 * Translate a single line using Claude API with retry logic
 */
static char* translate_single_line(const char *text, const char *target_lang,
                                   const char *api_key, const char *model_name) {
    if (!text || !target_lang || !api_key || !model_name) {
        return NULL;
    }

    // Build request JSON
    char *request_json = build_request_json(text, target_lang, model_name);
    if (!request_json) {
        return NULL;
    }

    // Setup HTTP request parameters
    struct config *cfg = config_get();
    struct translator_http_params params = {
        .endpoint = CLAUDE_API_ENDPOINT,
        .request_body = request_json,
        .provider_name = "claude_translator",
        .api_key = api_key,
        .extra_data = CLAUDE_API_VERSION,
        .max_retries = cfg->translation.max_retries,
        .build_headers = build_claude_headers
    };

    // Perform HTTP request with common retry logic
    char *response_json = translator_perform_http_request(&params);
    free(request_json);

    if (!response_json) {
        return NULL;
    }

    // Parse response
    char *translation = parse_response_json(response_json);
    free(response_json);
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
    curl_easy_setopt(curl_handle, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);

    return true;
}

void claude_translator_cleanup(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
}

bool claude_translate_lyrics(struct lyrics_data *data, int64_t track_length_us) {
    if (!curl_handle) {
        return false;
    }

    return translator_translate_lyrics_generic(data, track_length_us,
                                                "claude_translator",
                                                translate_single_line);
}
