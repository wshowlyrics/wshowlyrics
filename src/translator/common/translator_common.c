#include "translator_common.h"
#include "../../constants.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

    // Check if lyrics format is LRC (only translate LRC files)
    // ruby_segments are used by LRC and SRT, but we need to exclude SRT/VTT
    if (!data->lines->ruby_segments) {
        return false;  // Not LRC format (likely LRCX with word_segments)
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
            if (index >= new_already_translated && index < *already_translated) {
                if (line->translation) {
                    free(line->translation);
                    line->translation = NULL;
                }
            }
            index++;
        }
        line = line->next;
    }

    *already_translated = new_already_translated;
}
