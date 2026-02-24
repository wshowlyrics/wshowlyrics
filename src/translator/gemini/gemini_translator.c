#include "gemini_translator.h"
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

// Gemini API endpoint template
#define GEMINI_API_ENDPOINT "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s"

// Global CURL handle
static CURL *curl_handle = NULL;

/**
 * Build Gemini API request JSON
 * Request format: {"contents": [{"parts": [{"text": "..."}]}]}
 */
static char* build_request_json(const char *text, const char *target_lang) {
    json_object *root = json_object_new_object();
    json_object *contents_array = json_object_new_array();
    json_object *content_obj = json_object_new_object();
    json_object *parts_array = json_object_new_array();
    json_object *part_obj = json_object_new_object();

    // Build standard translation prompt
    char prompt[TRANSLATION_PROMPT_SIZE];
    if (translator_build_translation_prompt(prompt, sizeof(prompt), text, target_lang) < 0) {
        return NULL;
    }

    json_object_object_add(part_obj, "text", json_object_new_string(prompt));
    json_object_array_add(parts_array, part_obj);
    json_object_object_add(content_obj, "parts", parts_array);
    json_object_array_add(contents_array, content_obj);
    json_object_object_add(root, "contents", contents_array);

    const char *json_str = json_object_to_json_string(root);
    char *result = strdup(json_str);
    json_object_put(root);

    return result;
}

/**
 * Parse Gemini API response JSON
 * Response format: {"candidates": [{"content": {"parts": [{"text": "..."}]}}]}
 */
static char* parse_response_json(const char *json_str) {
    // Extract text using JSON path: candidates[0].content.parts[0].text
    char *text = json_extract_text_by_path(json_str, "candidates[0].content.parts[0].text", "gemini_translator");
    if (!text) {
        return NULL;
    }

    // Extract last line to handle cases where AI includes original text
    char *result = translator_extract_last_line(text);
    free(text);
    return result;
}

/**
 * Build Gemini-specific HTTP headers
 */
static struct curl_slist* build_gemini_headers(const char *api_key, const void *extra_data) {
    (void)api_key;      // Gemini uses API key in URL, not header
    (void)extra_data;   // Gemini doesn't need extra data

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    return headers;
}

/**
 * Translate a single line using Gemini API with retry logic
 */
static char* translate_single_line(const char *text, const char *target_lang,
                                   const char *api_key, const char *model_name) {
    if (!text || !target_lang || !api_key || !model_name) {
        return NULL;
    }

    // Build request JSON
    char *request_json = build_request_json(text, target_lang);
    if (!request_json) {
        return NULL;
    }

    // Build endpoint URL with model and API key (Gemini uses URL-based auth)
    char url[512];
    snprintf(url, sizeof(url), GEMINI_API_ENDPOINT, model_name, api_key);

    // Setup HTTP request parameters
    struct config *cfg = config_get();
    struct translator_http_params params = {
        .endpoint = url,
        .request_body = request_json,
        .provider_name = "gemini_translator",
        .api_key = api_key,
        .extra_data = NULL,
        .max_retries = cfg->translation.max_retries,
        .build_headers = build_gemini_headers
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

bool gemini_translator_init(void) {
    return translator_init_curl_handle(&curl_handle, "gemini_translator");
}

void gemini_translator_cleanup(void) {
    translator_cleanup_curl_handle(&curl_handle);
}

bool gemini_translate_lyrics(struct lyrics_data *data, int64_t track_length_us) {
    if (!curl_handle) {
        return false;
    }

    return translator_translate_lyrics_generic(data, track_length_us,
                                                "gemini_translator",
                                                translate_single_line);
}
