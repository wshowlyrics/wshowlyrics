#include "translator_common.h"
#include "../../constants.h"
#include "../../utils/lang_detect/lang_detect.h"
#include "../../utils/render/render_common.h"
#include "../../user_experience/config/config.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * Extract the last non-empty line from text
 * This handles cases where AI includes the original text before the translation
 */
char* translator_extract_last_line(const char *text) {
    if (!text || !*text) {
        return NULL;
    }

    // Find the last non-empty line
    const char *last_line_start = text;
    const char *p = text;

    while (*p) {
        if (*p == '\n') {
            // Move to next character
            const char *next = p + 1;
            // Skip whitespace
            while (*next && (*next == ' ' || *next == '\t' || *next == '\r')) {
                next++;
            }
            // If we found non-whitespace and it's not another newline, this is a new line
            if (*next && *next != '\n') {
                last_line_start = next;
            }
        }
        p++;
    }

    // Find end of last line
    const char *end = last_line_start;
    while (*end && *end != '\n' && *end != '\r') {
        end++;
    }

    // Trim trailing whitespace
    while (end > last_line_start && (*(end-1) == ' ' || *(end-1) == '\t')) {
        end--;
    }

    size_t len = end - last_line_start;
    if (len == 0) {
        // No valid line found, return original text
        return strdup(text);
    }

    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, last_line_start, len);
    result[len] = '\0';
    return result;
}

/**
 * Count translatable lines in lyrics data
 */
int translator_count_translatable_lines(struct lyrics_data *data) {
    if (!data || !data->lines) {
        return 0;
    }

    int count = 0;
    struct lyrics_line *line = data->lines;
    while (line) {
        if (line->text && strlen(line->text) > 0) {
            count++;
        }
        line = line->next;
    }
    return count;
}

/**
 * Count already translated lines in lyrics data
 */
int translator_count_translated_lines(struct lyrics_data *data) {
    if (!data || !data->lines) {
        return 0;
    }

    int count = 0;
    struct lyrics_line *line = data->lines;
    while (line) {
        if (line->translation && strlen(line->translation) > 0) {
            count++;
        }
        line = line->next;
    }
    return count;
}

/**
 * Save translation to cache
 */
bool translator_save_to_cache(const char *cache_path, struct lyrics_data *data,
                               const char *target_lang) {
    if (!cache_path || !data || !target_lang) {
        return false;
    }

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
    mode_t old_mask = umask(0022);  // Ensure rw-r--r-- permissions
    FILE *f = fopen(cache_path, "w");
    umask(old_mask);
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
bool translator_load_from_cache(const char *cache_path, struct lyrics_data *data) {
    if (!cache_path || !data) {
        return false;
    }

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
 * Check if cache is complete
 */
bool translator_check_cache_complete(struct lyrics_data *data, int translatable_count,
                                      int *already_translated) {
    if (!data || !already_translated) {
        return false;
    }

    *already_translated = translator_count_translated_lines(data);
    return (*already_translated == translatable_count);
}

/**
 * Check if lyrics should be translated
 * Only LRC format is supported (LRCX, SRT, VTT excluded)
 */
bool translator_should_translate(struct lyrics_data *data) {
    if (!data || !data->lines) {
        return false;
    }

    // Check for SRT/VTT/LRCX by file extension (exclude them)
    if (data->source_file_path) {
        const char *ext = strrchr(data->source_file_path, '.');
        if (ext && (strcasecmp(ext, ".srt") == 0 ||
                    strcasecmp(ext, ".vtt") == 0 ||
                    strcasecmp(ext, ".lrcx") == 0)) {
            return false;  // SRT/VTT/LRCX not supported
        }
    }

    // Check if lyrics format is LRC (only translate LRC files)
    // ruby_segments are used by LRC and SRT, but we need to exclude SRT/VTT
    // Note: First line might be empty, so check until we find a non-empty line
    struct lyrics_line *line = data->lines;
    bool has_ruby_segments = false;
    bool has_word_segments = false;

    while (line) {
        if (line->ruby_segments) {
            has_ruby_segments = true;
            break;
        }
        if (line->segments) {
            has_word_segments = true;
            break;
        }
        line = line->next;
    }

    if (has_word_segments || !has_ruby_segments) {
        return false;  // LRCX format (word_segments) or no segments at all
    }

    return true;  // LRC format
}

/**
 * Prepare partial cache for resume by clearing last N translations for re-validation.
 * This ensures that incomplete translations at the end are re-done.
 */
void translator_prepare_cache_resume(struct lyrics_data *data, int *already_translated,
                                      int revalidate_count) {
    if (!data || !already_translated || *already_translated <= 0) {
        return;
    }

    // Calculate new already_translated count (subtract revalidate_count)
    int new_already_translated = *already_translated > revalidate_count ?
                                  *already_translated - revalidate_count : 0;

    // Clear translations for lines to be re-validated
    struct lyrics_line *line = data->lines;
    int index = 0;

    while (line) {
        if (line->text && strlen(line->text) > 0) {
            // Clear translation if in re-validation range
            if (index >= new_already_translated && index < *already_translated && line->translation) {
                free(line->translation);
                line->translation = NULL;
            }
            index++;
        }
        line = line->next;
    }

    *already_translated = new_already_translated;
}

bool translator_should_skip_translation(const char *stripped_text, const char *target_lang,
                                         char **out_translation) {
    if (!stripped_text || !target_lang || !out_translation) {
        return false;
    }

    // Check if text is already in target language
    if (is_already_in_language(stripped_text, target_lang)) {
        *out_translation = NULL;
        return true;
    }

    return false;
}

void translator_rate_limit_delay(int delay_ms) {
    if (delay_ms <= 0) {
        return;
    }

    struct timespec delay = {
        .tv_sec = delay_ms / 1000,
        .tv_nsec = (delay_ms % 1000) * 1000000L
    };
    nanosleep(&delay, NULL);
}

void translator_check_time_feasibility(struct lyrics_data *data, int rate_limit_ms,
                                        int64_t track_length_us) {
    if (!data || rate_limit_ms <= 0) {
        return;
    }

    // If track length is not available (0), skip check
    if (track_length_us == 0) {
        return;
    }

    // Count translatable lines (lines with text but no translation)
    int translatable_count = 0;
    struct lyrics_line *line = data->lines;
    while (line) {
        if (line->text && line->text[0] != '\0' && !line->translation) {
            translatable_count++;
        }
        line = line->next;
    }

    if (translatable_count == 0) {
        return;  // Nothing to translate
    }

    // Calculate estimated translation time (in microseconds for precision)
    int64_t estimated_time_us = (int64_t)rate_limit_ms * 1000 * translatable_count;

    // Check if translation will complete before song ends
    if (estimated_time_us > track_length_us) {
        // Translation won't complete - calculate how much will be done
        double completion_ratio = (double)track_length_us / estimated_time_us;
        int lines_completed = (int)(translatable_count * completion_ratio);

        // Get cache threshold based on policy (comfort: 50%, balanced: 75%, aggressive: 90%)
        struct config *cfg = config_get();
        float threshold = config_get_cache_threshold(cfg->translation.cache_policy);
        int threshold_lines = (int)(translatable_count * threshold);

        // Only warn if below threshold (cache will be discarded)
        if (lines_completed < threshold_lines) {
            int threshold_percent = (int)(threshold * 100);
            double estimated_sec = estimated_time_us / 1000000.0;
            double track_sec = track_length_us / 1000000.0;
            int completion_percent = (int)(completion_ratio * 100);
            int required_rate_ms = (int)(track_length_us / 1000 / translatable_count / threshold);

            log_warn("Translation will be DISCARDED (below cache threshold)!");
            log_warn("  Estimated time: %.1fs (%d lines × %dms)",
                     estimated_sec, translatable_count, rate_limit_ms);
            log_warn("  Song duration: %.1fs (only %d%% / %d lines will complete)",
                     track_sec, completion_percent, lines_completed);
            log_warn("  Cache threshold: %d%% (%d lines required)",
                     threshold_percent, threshold_lines);
            log_warn("  REDUCE rate_limit to at least %dms to save progress!",
                     required_rate_ms);
        }
    }
}

// --- Common CURL and Threading Utilities Implementation ---

size_t translator_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct translator_curl_response *response = (struct translator_curl_response *)userp;

    char *ptr = realloc(response->data, response->size + realsize + 1);
    if (!ptr) {
        log_error("translator: Memory allocation failed in CURL callback");
        return 0;
    }

    response->data = ptr;
    memcpy(&(response->data[response->size]), contents, realsize);
    response->size += realsize;
    response->data[response->size] = 0;

    return realsize;
}

void translator_curl_response_init(struct translator_curl_response *response) {
    if (response) {
        response->data = NULL;
        response->size = 0;
    }
}

void translator_curl_response_free(struct translator_curl_response *response) {
    if (response && response->data) {
        free(response->data);
        response->data = NULL;
        response->size = 0;
    }
}

bool translator_handle_cache_loading_ex(struct translator_thread_args *args,
                                          struct lyrics_data *data,
                                          int translatable_count,
                                          int *already_translated) {
    bool cache_loaded = translator_load_from_cache(args->cache_path, data);

    if (!cache_loaded) {
        data->translation_current = 0;
        log_info("%s: Starting translation of %d lines",
                 args->provider_name, translatable_count);
        return true;  // Continue translation
    }

    // Cache loaded - check if complete
    if (translator_check_cache_complete(data, translatable_count, already_translated)) {
        log_success("Found cached translation: %s", args->cache_path);
        log_success("%s: Loaded complete translation from cache (%d lines)",
                   args->provider_name, *already_translated);
        data->translation_current = *already_translated;
        data->translation_in_progress = false;
        return false;  // Translation complete
    }

    // Partial cache - continue translation
    struct config *cfg = config_get();
    int revalidate_count = cfg->translation.revalidate_count;
    translator_prepare_cache_resume(data, already_translated, revalidate_count);
    log_info("Found cached translation: %s", args->cache_path);
    log_info("%s: Loaded partial cache (%d/%d), re-validating last %d lines",
            args->provider_name, *already_translated, translatable_count, revalidate_count);
    data->translation_current = *already_translated;
    return true;  // Continue translation
}

bool translator_process_line_translation_ex(struct lyrics_line *line,
                                              struct translator_thread_args *args,
                                              struct lyrics_data *data,
                                              int *current,
                                              int translatable_count) {
    // Check cancellation
    if (data->translation_should_cancel) {
        log_info("%s: Translation cancelled (%d/%d completed)",
                 args->provider_name, *current, translatable_count);
        return false;  // Break
    }

    // Skip empty lines
    if (!line->text || strlen(line->text) == 0) {
        return true;  // Continue
    }

    // Skip already translated lines
    if (line->translation && strlen(line->translation) > 0) {
        return true;  // Continue
    }

    // Update counter
    (*current)++;
    data->translation_current = *current;

    // Strip ruby notation
    char *stripped = strip_ruby_notation(line->text);
    if (!stripped || stripped[0] == '\0') {
        free(stripped);
        return true;  // Continue
    }

    // Check if already in target language
    char *skipped_translation = NULL;
    if (translator_should_skip_translation(stripped, args->target_lang, &skipped_translation)) {
        line->translation = skipped_translation;
        log_info("%s: [%d/%d] Already in target language: %s",
               args->provider_name, *current, translatable_count, stripped);
        free(stripped);
        return true;  // Continue
    }

    // Translate using provider-specific function
    char *translation = args->translate_line_fn(stripped, args->target_lang,
                                                args->api_key, args->model_name);
    if (translation) {
        // Language validation
        if (is_same_language(stripped, translation)) {
            log_warn("%s: [%d/%d] Skipped (same language after translation) - API cost wasted",
                   args->provider_name, *current, translatable_count);
            free(translation);
            free(line->translation);
            line->translation = NULL;
        } else {
            free(line->translation);
            line->translation = translation;
            log_info("%s: [%d/%d] Translated: %s",
                   args->provider_name, *current, translatable_count, translation);
        }
    } else {
        log_warn("%s: [%d/%d] Translation failed",
               args->provider_name, *current, translatable_count);
    }

    free(stripped);

    return true;  // Continue
}

void translator_save_to_cache_ex(const char *cache_path,
                                   struct lyrics_data *data,
                                   const char *target_lang,
                                   const char *provider_name) {
    struct config *cfg = config_get();
    float cache_threshold = config_get_cache_threshold(cfg->translation.cache_policy);
    float completion_ratio = (float)data->translation_current / (float)data->translation_total;

    if (data->translation_current == data->translation_total) {
        translator_save_to_cache(cache_path, data, target_lang);
        log_success("%s: Translation completed", provider_name);
        return;
    }

    if (completion_ratio >= cache_threshold) {
        translator_save_to_cache(cache_path, data, target_lang);
        log_warn("%s: Translation incomplete but cached (%d/%d, %.0f%%)",
                 provider_name, data->translation_current, data->translation_total,
                 completion_ratio * 100);
        return;
    }

    // Below threshold - delete cache
    log_warn("%s: Translation incomplete (%d/%d, %.0f%%), not cached (threshold: %.0f%%)",
             provider_name, data->translation_current, data->translation_total,
             completion_ratio * 100, cache_threshold * 100);
    unlink(cache_path);
}

void* translator_async_worker(void *arg) {
    struct translator_thread_args *args = (struct translator_thread_args *)arg;
    struct lyrics_data *data = args->data;

    // Count translatable lines
    int translatable_count = translator_count_translatable_lines(data);
    data->translation_total = translatable_count;
    data->translation_in_progress = true;

    // Check if translation can complete before song ends
    struct config *cfg_time = config_get();
    translator_check_time_feasibility(data, cfg_time->translation.rate_limit_ms,
                                       args->track_length_us);

    // Try loading from cache
    int already_translated = 0;
    if (!translator_handle_cache_loading_ex(args, data, translatable_count, &already_translated)) {
        // Translation already complete from cache
        free(args->target_lang);
        free(args->api_key);
        free(args->model_name);
        free(args->cache_path);
        free(args);
        return NULL;
    }

    // Translate each line
    struct lyrics_line *line = data->lines;
    int current = already_translated;
    while (line) {
        if (!translator_process_line_translation_ex(line, args, data, &current, translatable_count)) {
            break;  // Cancelled
        }

        // Rate limiting
        if (line->next) {
            struct config *cfg = config_get();
            translator_rate_limit_delay(cfg->translation.rate_limit_ms);
        }

        line = line->next;
    }

    // Save to cache
    translator_save_to_cache_ex(args->cache_path, data, args->target_lang,
                                  args->provider_name);

    data->translation_in_progress = false;

    // Free thread args
    free(args->target_lang);
    free(args->api_key);
    free(args->model_name);
    free(args->cache_path);
    free(args);
    return NULL;
}

/**
 * Build standard translation prompt for all providers
 */
int translator_build_translation_prompt(char *buffer, size_t buffer_size,
                                         const char *text, const char *target_lang) {
    if (!buffer || !text || !target_lang || buffer_size == 0) {
        return -1;
    }

    int written = snprintf(buffer, buffer_size,
                           "You are a professional translator. "
                           "Translate the following text to %s. "
                           "Output ONLY the translated text with no additional explanations:\n\n%s",
                           target_lang, text);

    if (written < 0 || written >= (int)buffer_size) {
        return -1;
    }

    return written;
}

/**
 * Parse retry delay from API error response
 * Supports multiple patterns: "retry in X.XXs", "retry after X.XXs", "Please retry in X.XXs"
 */
int translator_parse_retry_delay(const char *response_json) {
    if (!response_json) {
        return 0;
    }

    // Pattern 1: "Please retry in X.XXs" (Gemini)
    const char *please_retry = strstr(response_json, "Please retry in ");
    if (please_retry) {
        float seconds = 0;
        if (sscanf(please_retry, "Please retry in %fs", &seconds) == 1) {
            return (int)(seconds * 1000) + 1000; // Add 1 second buffer
        }
    }

    // Pattern 2: "retry in X.XXs" or "retry after X.XXs" (Claude, others)
    const char *retry_str = strstr(response_json, "retry");
    if (retry_str) {
        float seconds = 0;
        if (sscanf(retry_str, "retry in %fs", &seconds) == 1 ||
            sscanf(retry_str, "retry after %fs", &seconds) == 1) {
            return (int)(seconds * 1000) + 1000; // Add 1 second buffer
        }
    }

    return 0;
}
