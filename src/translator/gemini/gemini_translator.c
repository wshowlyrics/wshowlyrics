#include "gemini_translator.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include "../../utils/file/file_utils.h"
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

static size_t gemini_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct curl_response *response = (struct curl_response *)userp;

    char *ptr = realloc(response->data, response->size + realsize + 1);
    if (!ptr) {
        log_error("gemini_translator: Memory allocation failed");
        return 0;
    }

    response->data = ptr;
    memcpy(&(response->data[response->size]), contents, realsize);
    response->size += realsize;
    response->data[response->size] = 0;

    return realsize;
}

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

    // Build prompt: clear instruction for translation only
    char prompt[8192];
    snprintf(prompt, sizeof(prompt),
             "You are a professional translator. "
             "Translate the following text to %s. "
             "Output ONLY the translated text with no additional explanations:\n\n%s",
             target_lang, text);

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
    json_object *root = json_tokener_parse(json_str);
    if (!root) {
        log_error("gemini_translator: Failed to parse JSON response");
        return NULL;
    }

    // Navigate: candidates[0].content.parts[0].text
    json_object *candidates;
    if (!json_object_object_get_ex(root, "candidates", &candidates)) {
        log_error("gemini_translator: No 'candidates' in response");
        json_object_put(root);
        return NULL;
    }

    json_object *candidate = json_object_array_get_idx(candidates, 0);
    if (!candidate) {
        log_error("gemini_translator: Empty candidates array");
        json_object_put(root);
        return NULL;
    }

    json_object *content;
    if (!json_object_object_get_ex(candidate, "content", &content)) {
        log_error("gemini_translator: No 'content' in candidate");
        json_object_put(root);
        return NULL;
    }

    json_object *parts;
    if (!json_object_object_get_ex(content, "parts", &parts)) {
        log_error("gemini_translator: No 'parts' in content");
        json_object_put(root);
        return NULL;
    }

    json_object *part = json_object_array_get_idx(parts, 0);
    if (!part) {
        log_error("gemini_translator: Empty parts array");
        json_object_put(root);
        return NULL;
    }

    json_object *text_obj;
    if (!json_object_object_get_ex(part, "text", &text_obj)) {
        log_error("gemini_translator: No 'text' in part");
        json_object_put(root);
        return NULL;
    }

    const char *text = json_object_get_string(text_obj);
    char *result = strdup(text);
    json_object_put(root);

    return result;
}

/**
 * Parse retry delay from error response
 * Returns delay in milliseconds, or 0 if not found
 */
static int parse_retry_delay(const char *response_json) {
    if (!response_json) {
        return 0;
    }

    // Look for "Please retry in X.XXs" pattern
    const char *retry_str = strstr(response_json, "Please retry in ");
    if (retry_str) {
        float seconds = 0;
        if (sscanf(retry_str, "Please retry in %fs", &seconds) == 1) {
            return (int)(seconds * 1000) + 1000; // Add 1 second buffer
        }
    }

    return 0;
}

/**
 * Translate a single line using Gemini API with retry logic
 */
static char* translate_single_line(const char *text, const char *target_lang,
                                   const char *api_key, const char *model_name) {
    if (!text || !target_lang || !api_key || !model_name) {
        return NULL;
    }

    // Create thread-local CURL handle to avoid race conditions
    CURL *local_curl = curl_easy_init();
    if (!local_curl) {
        log_error("gemini_translator: Failed to initialize CURL for translation");
        return NULL;
    }

    // Get retry settings from config
    struct config *cfg = config_get();
    const int max_retries = cfg->translation.max_retries;

    // Build endpoint URL with model and API key
    char url[512];
    snprintf(url, sizeof(url), GEMINI_API_ENDPOINT, model_name, api_key);

    char *translation = NULL;
    int attempt = 0;

    while (attempt < max_retries && !translation) {
        attempt++;

        // Build request JSON
        char *request_json = build_request_json(text, target_lang);
        if (!request_json) {
            curl_easy_cleanup(local_curl);
            return NULL;
        }

        // Setup CURL
        struct curl_response response = {0};
        curl_easy_setopt(local_curl, CURLOPT_URL, url);
        curl_easy_setopt(local_curl, CURLOPT_POSTFIELDS, request_json);
        curl_easy_setopt(local_curl, CURLOPT_WRITEFUNCTION, gemini_curl_write_callback);
        curl_easy_setopt(local_curl, CURLOPT_WRITEDATA, &response);

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(local_curl, CURLOPT_HTTPHEADER, headers);

        // Perform request
        CURLcode res = curl_easy_perform(local_curl);
        curl_slist_free_all(headers);
        free(request_json);

        if (res != CURLE_OK) {
            log_error("gemini_translator: CURL error: %s", curl_easy_strerror(res));
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
        } else if (http_code == 429 || http_code == 503) {
            // Rate limit or overload - retry with delay
            int retry_delay_ms = parse_retry_delay(response.data);
            if (retry_delay_ms == 0) {
                retry_delay_ms = 5000 * attempt; // Exponential backoff: 5s, 10s, 15s
            }

            log_warn("gemini_translator: Rate limit/overload (HTTP %ld), retrying in %dms (attempt %d/%d)",
                    http_code, retry_delay_ms, attempt, max_retries);

            free(response.data);

            if (attempt < max_retries) {
                struct timespec delay = {
                    .tv_sec = retry_delay_ms / 1000,
                    .tv_nsec = (retry_delay_ms % 1000) * 1000000L
                };
                nanosleep(&delay, NULL);
            }
        } else {
            // Other error - don't retry
            log_error("gemini_translator: HTTP error %ld", http_code);
            free(response.data);
            curl_easy_cleanup(local_curl);
            return NULL;
        }
    }

    curl_easy_cleanup(local_curl);
    return translation;
}

/**
 * Save translation to cache
 */
static bool save_translation_to_cache(const char *cache_path, struct lyrics_data *data,
                                     const char *target_lang) {
    json_object *root = json_object_new_object();
    json_object *translations_array = json_object_new_array();

    json_object_object_add(root, "target_language", json_object_new_string(target_lang));

    struct lyrics_line *line = data->lines;
    while (line) {
        if (line->translation) {
            json_object_array_add(translations_array, json_object_new_string(line->translation));
        } else {
            json_object_array_add(translations_array, json_object_new_string(""));
        }
        line = line->next;
    }

    json_object_object_add(root, "translations", translations_array);

    const char *json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);

    // Write to file
    FILE *f = fopen(cache_path, "w");
    bool success = false;
    if (f) {
        fprintf(f, "%s", json_str);
        fclose(f);
        success = true;
    }

    json_object_put(root);
    return success;
}

/**
 * Load translation from cache
 */
static bool load_translation_from_cache(const char *cache_path, struct lyrics_data *data) {
    // Check if file exists
    FILE *f = fopen(cache_path, "r");
    if (!f) {
        return false;
    }

    // Read file contents
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc(file_size + 1);
    if (!json_str) {
        fclose(f);
        return false;
    }

    size_t read_size = fread(json_str, 1, file_size, f);
    json_str[read_size] = '\0';
    fclose(f);

    // Parse JSON
    json_object *root = json_tokener_parse(json_str);
    free(json_str);

    if (!root) {
        return false;
    }

    json_object *translations_array;
    if (!json_object_object_get_ex(root, "translations", &translations_array)) {
        json_object_put(root);
        return false;
    }

    int translation_count = json_object_array_length(translations_array);
    struct lyrics_line *line = data->lines;
    int index = 0;

    while (line && index < translation_count) {
        json_object *translation_obj = json_object_array_get_idx(translations_array, index);
        const char *translation = json_object_get_string(translation_obj);

        if (translation && strlen(translation) > 0) {
            free(line->translation);
            line->translation = strdup(translation);
        }

        line = line->next;
        index++;
    }

    json_object_put(root);
    return true;
}

/**
 * Async translation thread function
 */
static void* translate_lyrics_async(void *arg) {
    struct translation_thread_args *args = (struct translation_thread_args *)arg;
    struct lyrics_data *data = args->data;

    // Try loading from cache first
    if (load_translation_from_cache(args->cache_path, data)) {
        log_success("gemini_translator: Loaded translation from cache");
        data->translation_in_progress = false;
        free(args->target_lang);
        free(args->api_key);
        free(args->model_name);
        free(args->cache_path);
        free(args);
        return NULL;
    }

    // Count translatable lines
    int translatable_count = 0;
    struct lyrics_line *line = data->lines;
    while (line) {
        if (line->text && strlen(line->text) > 0) {
            translatable_count++;
        }
        line = line->next;
    }

    data->translation_total = translatable_count;
    data->translation_current = 0;
    data->translation_in_progress = true;

    log_info("gemini_translator: Starting translation of %d lines", translatable_count);

    // Translate each line
    line = data->lines;
    int current = 0;
    while (line) {
        // Check if translation should be cancelled
        if (data->translation_should_cancel) {
            log_info("gemini_translator: Translation cancelled (%d/%d completed)",
                     current, translatable_count);
            break;
        }

        if (line->text && strlen(line->text) > 0) {
            current++;
            data->translation_current = current;

            char *translation = translate_single_line(line->text, args->target_lang,
                                                     args->api_key, args->model_name);
            if (translation) {
                free(line->translation);
                line->translation = translation;
                log_info("gemini_translator: [%d/%d] Translated: %s",
                       current, translatable_count, translation);
            } else {
                log_warn("gemini_translator: [%d/%d] Translation failed",
                       current, translatable_count);
            }

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

    // Save to cache only if translation is complete
    if (data->translation_current == data->translation_total) {
        save_translation_to_cache(args->cache_path, data, args->target_lang);
        log_success("gemini_translator: Translation completed");
    } else {
        log_warn("gemini_translator: Translation incomplete (%d/%d), deleting partial cache",
                 data->translation_current, data->translation_total);
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

bool gemini_translator_init(void) {
    if (curl_handle) {
        return true; // Already initialized
    }

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        log_error("gemini_translator: Failed to initialize CURL");
        return false;
    }

    return true;
}

void gemini_translator_cleanup(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
}

bool gemini_translate_lyrics(struct lyrics_data *data) {
    if (!data || !curl_handle) {
        return false;
    }

    // Get config
    const char *provider = g_config.translation.provider;
    const char *api_key = g_config.translation.api_key;
    const char *target_lang = g_config.translation.target_language;

    // Extract model name from provider (e.g., "gemini-2.5-flash" -> "gemini-2.5-flash")
    const char *model_name = provider;

    // Validate
    if (!api_key || strlen(api_key) == 0) {
        log_error("gemini_translator: API key not configured");
        return false;
    }

    if (!model_name || strlen(model_name) == 0) {
        log_error("gemini_translator: Model name not configured");
        return false;
    }

    // Build cache path
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path),
             "/tmp/wshowlyrics/translated/%s_%s.json",
             data->md5_checksum, target_lang);

    // Create cache directory
    if (system("mkdir -p /tmp/wshowlyrics/translated") != 0) {
        log_warn("gemini_translator: Failed to create cache directory");
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
        log_error("gemini_translator: Failed to create translation thread");
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
