#include "deepl_translator.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/json/json_utils.h"
#include "../../utils/string/string_utils.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/render/render_common.h"
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

// Save translations to cache file (JSON format)
static bool save_translation_to_cache(const char *cache_path, struct lyrics_data *data,
                                       const char *target_lang) {
    if (!cache_path || !data || !target_lang) return false;

    FILE *f = fopen(cache_path, "w");
    if (!f) {
        log_warn("Failed to create cache file: %s", cache_path);
        return false;
    }

    // Write JSON header
    fprintf(f, "{\"target_language\":\"%s\",\"translations\":[", target_lang);

    // Write all line translations
    struct lyrics_line *line = data->lines;
    bool first = true;

    while (line) {
        if (line->ruby_segments && line->translation) {
            if (!first) fprintf(f, ",");

            // Escape translation text for JSON
            char *escaped = json_escape_string(line->translation);
            if (escaped) {
                fprintf(f, "\"%s\"", escaped);
                free(escaped);
                first = false;
            }
        }
        line = line->next;
    }

    fprintf(f, "]}\n");
    fclose(f);

    log_info("Saved translation cache: %s", cache_path);
    return true;
}

// Load translations from cache file (JSON format)
static bool load_translation_from_cache(const char *cache_path, struct lyrics_data *data) {
    if (!cache_path || !data) return false;

    // Check if cache file exists
    struct stat st;
    if (stat(cache_path, &st) != 0) {
        return false;  // Cache doesn't exist
    }

    FILE *f = fopen(cache_path, "r");
    if (!f) return false;

    // Read entire file into buffer
    char *buffer = malloc(st.st_size + 1);
    if (!buffer) {
        fclose(f);
        return false;
    }

    size_t read_size = fread(buffer, 1, st.st_size, f);
    fclose(f);

    if (read_size != (size_t)st.st_size) {
        free(buffer);
        return false;
    }
    buffer[read_size] = '\0';

    // Find translations array start
    char *translations_start = strstr(buffer, "\"translations\":");
    if (!translations_start) {
        log_warn("Invalid cache file format: %s", cache_path);
        free(buffer);
        return false;
    }

    // Find the opening bracket of the translations array
    char *array_start = strchr(translations_start, '[');
    if (!array_start) {
        log_warn("Invalid cache file format: no array found");
        free(buffer);
        return false;
    }

    // Populate translation fields from cache
    struct lyrics_line *line = data->lines;
    char *search_pos = array_start + 1;  // Start after '['
    int loaded_count = 0;

    while (line) {
        if (line->ruby_segments && line->text && line->text[0] != '\0') {
            // Check if line would be included in translation request
            // (same logic as build_json_request)
            char *stripped = strip_ruby_notation(line->text);
            bool should_load = stripped && stripped[0] != '\0';
            if (stripped) free(stripped);

            if (should_load) {
                // Find next string in array
                char *str_start = strchr(search_pos, '"');
                if (!str_start) break;
                str_start++;  // Skip opening quote

                char *str_end = str_start;
                while (*str_end && *str_end != '"') {
                    if (*str_end == '\\' && *(str_end + 1)) {
                        str_end += 2;  // Skip escaped character
                    } else {
                        str_end++;
                    }
                }

                if (*str_end != '"') break;

                size_t len = str_end - str_start;
                char *translation = malloc(len + 1);
                if (translation) {
                    strncpy(translation, str_start, len);
                    translation[len] = '\0';

                    /* Unescape JSON string (simple version) */
                    /* Handle: \n, \t, \r, \", \\ */
                    char *src = translation;
                    char *dst = translation;
                    while (*src) {
                        if (*src == '\\' && *(src + 1)) {
                            src++;
                            switch (*src) {
                                case 'n': *dst++ = '\n'; break;
                                case 't': *dst++ = '\t'; break;
                                case 'r': *dst++ = '\r'; break;
                                case '"': *dst++ = '"'; break;
                                case '\\': *dst++ = '\\'; break;
                                default: *dst++ = *src; break;
                            }
                            src++;
                        } else {
                            *dst++ = *src++;
                        }
                    }
                    *dst = '\0';

                    line->translation = translation;
                    loaded_count++;
                }

                search_pos = str_end + 1;
            }
        }
        line = line->next;
    }

    free(buffer);
    if (loaded_count > 0) {
        log_info("Loaded %d translations from cache", loaded_count);
    }
    return true;
}

// Translate a single line (for individual requests)
static char* translate_single_line(const char *text, const char *target_lang, const char *api_key) {
    if (!text || !target_lang || !api_key) return NULL;

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

    // Parse response
    char *translation = NULL;
    if (res == CURLE_OK && http_code == HTTP_OK && response.data) {
        translation = json_extract_string_from(response.data, "text", response.data);
    }

    curl_memory_buffer_free(&response);
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
    int total = 0;
    struct lyrics_line *line = data->lines;
    while (line) {
        if (line->ruby_segments && line->text && line->text[0] != '\0') {
            char *stripped = strip_ruby_notation(line->text);
            if (stripped && stripped[0] != '\0') {
                total++;
            }
            if (stripped) free(stripped);
        }
        line = line->next;
    }

    data->translation_total = total;
    data->translation_in_progress = true;

    // Translate each line individually
    int current = 0;
    line = data->lines;
    while (line) {
        if (line->ruby_segments && line->text && line->text[0] != '\0') {
            // Strip ruby notation
            char *stripped = strip_ruby_notation(line->text);
            if (!stripped || stripped[0] == '\0') {
                if (stripped) free(stripped);
                line = line->next;
                continue;
            }

            // Translate this line
            current++;
            data->translation_current = current;

            char *translation = translate_single_line(stripped, target_lang, api_key);
            free(stripped);

            if (translation) {
                line->translation = translation;
            }

            // Rate limit delay
            if (current < total) {
                struct timespec delay = {
                    .tv_sec = 0,
                    .tv_nsec = RATE_LIMIT_DELAY_MS * 1000000L
                };
                nanosleep(&delay, NULL);
            }
        }
        line = line->next;
    }

    // Save to cache
    save_translation_to_cache(cache_path, data, target_lang);

    // Mark translation complete
    data->translation_in_progress = false;
    log_success("Translation completed");

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
    const char *endpoint = get_deepl_endpoint(cfg->deepl.api_key);
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
             "Authorization: DeepL-Auth-Key %s", cfg->deepl.api_key);

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

    // Check if translation is enabled
    if (!cfg->deepl.enable_deepl) {
        return false;
    }

    // Check if API key is configured
    if (!cfg->deepl.api_key || cfg->deepl.api_key[0] == '\0') {
        log_warn("DeepL translation enabled but API key not configured");
        return false;
    }

    // Check if lyrics data is valid
    if (!data || !data->lines) {
        return false;
    }

    // Check if lyrics format is LRC (only translate LRC files)
    // ruby_segments are used by LRC and SRT, but we need to exclude SRT/VTT
    if (!data->lines->ruby_segments) {
        return false;  // Not LRC format
    }

    // Check for SRT/VTT by file extension (exclude them)
    if (data->source_file_path) {
        const char *ext = strrchr(data->source_file_path, '.');
        if (ext && (strcasecmp(ext, ".srt") == 0 ||
                    strcasecmp(ext, ".vtt") == 0 ||
                    strcasecmp(ext, ".lrcx") == 0)) {
            return false;  // SRT/VTT/LRCX not supported
        }
    }

    // Initialize translation progress
    data->translation_in_progress = false;
    data->translation_current = 0;
    data->translation_total = 0;

    // Check cache first
    char cache_path[PATH_BUFFER_SIZE];
    if (build_translation_cache_path(cache_path, sizeof(cache_path),
                                      data->md5_checksum,
                                      cfg->deepl.target_language) > 0) {
        if (load_translation_from_cache(cache_path, data)) {
            log_info("Loaded translation from cache: %s", cache_path);
            return true;
        }
    } else {
        cache_path[0] = '\0';  // Mark as invalid
    }

    // Cache miss - start async translation
    log_info("Starting async translation to %s...", cfg->deepl.target_language);

    // Prepare thread args
    struct translation_thread_args *args = malloc(sizeof(struct translation_thread_args));
    if (!args) {
        log_error("Failed to allocate memory for translation thread args");
        return false;
    }

    args->data = data;
    args->target_lang = strdup(cfg->deepl.target_language);
    args->api_key = strdup(cfg->deepl.api_key);
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
    pthread_t thread;
    if (pthread_create(&thread, NULL, translate_lyrics_async, args) != 0) {
        log_error("Failed to create translation thread");
        free(args->target_lang);
        free(args->api_key);
        free(args->cache_path);
        free(args);
        return false;
    }

    // Detach thread so it runs independently
    pthread_detach(thread);

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
