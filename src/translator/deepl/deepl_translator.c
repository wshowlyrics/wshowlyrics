// SPDX-License-Identifier: GPL-3.0-or-later
#include "deepl_translator.h"
#include <json-c/json.h>
#include "../common/translator_common.h"
#include "../../user_experience/config/config.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// DeepL API endpoints
#define DEEPL_FREE_API_URL "https://api-free.deepl.com/v2/translate"
#define DEEPL_PRO_API_URL "https://api.deepl.com/v2/translate"

// Get DeepL API endpoint based on API key format
// Free API keys end with ":fx"
static const char* get_deepl_endpoint(const char *api_key) {
    size_t len = strlen(api_key);
    if (len >= 3 && strcmp(&api_key[len - 3], ":fx") == 0) {
        return DEEPL_FREE_API_URL;
    }
    return DEEPL_PRO_API_URL;
}

/**
 * Build DeepL API request JSON
 * Request format: {"text": ["..."], "target_lang": "KO"}
 */
static char* build_request_json(const char *text, const char *target_lang) {
    json_object *root = json_object_new_object();
    json_object *text_array = json_object_new_array();

    json_object_array_add(text_array, json_object_new_string(text));
    json_object_object_add(root, "text", text_array);
    json_object_object_add(root, "target_lang", json_object_new_string(target_lang));

    char *result = strdup(json_object_to_json_string(root));
    json_object_put(root);

    return result;
}

/**
 * Parse DeepL API response JSON
 * Response format: {"translations": [{"detected_source_language": "JA", "text": "..."}]}
 */
static char* parse_response_json(const char *json_str) {
    char *text = json_extract_text_by_path(json_str, "translations[0].text", "deepl_translator");
    if (!text) {
        return NULL;
    }

    char *result = translator_extract_last_line(text);
    free(text);
    return result;
}

/**
 * Build DeepL-specific HTTP headers
 */
static struct curl_slist* build_deepl_headers(const char *api_key, const void *extra_data) {
    (void)extra_data;  // DeepL doesn't need extra data

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: DeepL-Auth-Key %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    return headers;
}

/**
 * Translate a single line using DeepL API with retry logic
 */
static char* translate_single_line(const char *text, const char *target_lang,
                                   const char *api_key, const char *model_name) {
    (void)model_name;  // DeepL doesn't use model names

    if (!text || !target_lang || !api_key) {
        return NULL;
    }

    char *request_json = build_request_json(text, target_lang);
    if (!request_json) {
        return NULL;
    }

    struct translator_http_params params = {
        .endpoint = get_deepl_endpoint(api_key),
        .request_body = request_json,
        .provider_name = "deepl_translator",
        .api_key = api_key,
        .extra_data = NULL,
        .max_retries = config_get()->translation.max_retries,
        .build_headers = build_deepl_headers
    };

    // Perform HTTP request with common retry logic
    char *response_json = translator_perform_http_request(&params);
    free(request_json);

    if (!response_json) {
        return NULL;
    }

    char *translation = parse_response_json(response_json);
    free(response_json);
    return translation;
}

// Provider registration (see struct translator_provider in translator_common.h).
static bool deepl_matches(const char *provider) {
    return strcmp(provider, "deepl") == 0;
}
const struct translator_provider deepl_provider = {
    .name             = "deepl",
    .matches          = deepl_matches,
    .translate_lyrics = deepl_translate_lyrics,
};

bool deepl_translate_lyrics(struct lyrics_data *data, int64_t track_length_us) {
    return translator_translate_lyrics_generic(data, track_length_us,
                                                "deepl_translator",
                                                translate_single_line);
}
