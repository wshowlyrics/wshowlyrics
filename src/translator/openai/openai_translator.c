#include "openai_translator.h"
#include "../common/translator_common.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/lang_detect/lang_detect.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// OpenAI API endpoint
#define OPENAI_API_ENDPOINT "https://api.openai.com/v1/chat/completions"

// Global CURL handle
static CURL *curl_handle = NULL;

/**
 * Build OpenAI API request JSON
 * Request format: {"model": "...", "messages": [{"role": "user", "content": "..."}], "temperature": 0.3}
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
 * Parse OpenAI API response JSON
 * Response format: {"choices": [{"message": {"content": "..."}}]}
 */
static char* parse_response_json(const char *json_str) {
    // Extract text using JSON path: choices[0].message.content
    char *text = json_extract_text_by_path(json_str, "choices[0].message.content", "openai_translator");
    if (!text) {
        return NULL;
    }

    // Extract last line to handle cases where AI includes original text
    char *result = translator_extract_last_line(text);
    free(text);
    return result;
}

/**
 * Build OpenAI-specific HTTP headers
 */
static struct curl_slist* build_openai_headers(const char *api_key, const void *extra_data) {
    (void)extra_data;  // OpenAI doesn't need extra data

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    return headers;
}

/**
 * Translate a single line using OpenAI API with retry logic
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
        .endpoint = OPENAI_API_ENDPOINT,
        .request_body = request_json,
        .provider_name = "openai_translator",
        .api_key = api_key,
        .extra_data = NULL,
        .max_retries = cfg->translation.max_retries,
        .build_headers = build_openai_headers
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

bool openai_translator_init(void) {
    return translator_init_curl_handle((void **)&curl_handle, "openai_translator");
}

void openai_translator_cleanup(void) {
    translator_cleanup_curl_handle((void **)&curl_handle);
}

bool openai_translate_lyrics(struct lyrics_data *data, int64_t track_length_us) {
    if (!curl_handle) {
        return false;
    }

    return translator_translate_lyrics_generic(data, track_length_us,
                                                "openai_translator",
                                                translate_single_line);
}
