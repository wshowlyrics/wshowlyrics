#ifndef TRANSLATOR_COMMON_H
#define TRANSLATOR_COMMON_H

#include "../../lyrics_types.h"
#include <stdbool.h>

/**
 * Extract the last non-empty line from text.
 * This handles cases where AI includes the original text before the translation.
 *
 * @param text The text to process
 * @return Newly allocated string containing the last line, or NULL on error.
 *         Caller must free the returned string.
 */
char* translator_extract_last_line(const char *text);

/**
 * Count translatable lines in lyrics data.
 *
 * @param data Lyrics data to analyze
 * @return Number of lines with non-empty text
 */
int translator_count_translatable_lines(struct lyrics_data *data);

/**
 * Count already translated lines in lyrics data.
 *
 * @param data Lyrics data to analyze
 * @return Number of lines with non-empty translation
 */
int translator_count_translated_lines(struct lyrics_data *data);

/**
 * Save translation to cache file.
 * Supports partial caching (saves all lines, including empty translations).
 *
 * @param cache_path Path to cache file
 * @param data Lyrics data with translations
 * @param target_lang Target language code
 * @return true on success, false on failure
 */
bool translator_save_to_cache(const char *cache_path, struct lyrics_data *data,
                               const char *target_lang);

/**
 * Load translation from cache file.
 *
 * @param cache_path Path to cache file
 * @param data Lyrics data to populate with translations
 * @return true if cache was loaded (may be partial), false if cache doesn't exist or is invalid
 */
bool translator_load_from_cache(const char *cache_path, struct lyrics_data *data);

/**
 * Handle partial cache and determine if translation should continue.
 * Call this after loading cache to check completion status.
 *
 * @param data Lyrics data with cache loaded
 * @param translatable_count Total number of translatable lines
 * @param already_translated Number of already translated lines (output)
 * @return true if translation is complete, false if should continue
 */
bool translator_check_cache_complete(struct lyrics_data *data, int translatable_count,
                                      int *already_translated);

/**
 * Check if lyrics data should be translated.
 * Only LRC format is supported. LRCX, SRT, VTT are excluded.
 *
 * @param data Lyrics data to check
 * @return true if should translate (LRC format), false otherwise
 */
bool translator_should_translate(struct lyrics_data *data);

/**
 * Prepare partial cache for resume by clearing last N translations for re-validation.
 * This ensures that incomplete translations at the end are re-done.
 *
 * @param data Lyrics data with partial cache loaded
 * @param already_translated Number of already translated lines (input/output)
 * @param revalidate_count Number of last translations to clear (default: 2)
 */
void translator_prepare_cache_resume(struct lyrics_data *data, int *already_translated,
                                      int revalidate_count);

/**
 * Check if translation should be skipped (already in target language).
 * If yes, copies original text to output.
 *
 * @param stripped_text Text without ruby notation
 * @param target_lang Target language code
 * @param out_translation Output pointer for translation (if skipped)
 * @return true if should skip (already in target language), false if should translate
 */
bool translator_should_skip_translation(const char *stripped_text, const char *target_lang,
                                         char **out_translation);

/**
 * Apply rate limit delay between translation requests.
 *
 * @param delay_ms Delay in milliseconds
 */
void translator_rate_limit_delay(int delay_ms);

/**
 * Check if translation will complete enough lines to be cached.
 * Warns if translation will be discarded (below cache_policy threshold).
 * Threshold is based on cache_policy: comfort (50%), balanced (75%), aggressive (90%).
 *
 * @param data Lyrics data to analyze
 * @param rate_limit_ms Rate limit delay in milliseconds
 * @param track_length_us Song duration in microseconds (0 to skip check)
 */
void translator_check_time_feasibility(struct lyrics_data *data, int rate_limit_ms,
                                        int64_t track_length_us);

// --- Common CURL and Threading Utilities ---

/**
 * Initialize a CURL handle with TLS 1.2+ enforcement.
 * Common logic for OpenAI, Gemini, Claude translators.
 *
 * @param handle Pointer to CURL handle pointer
 * @param name Translator name for logging (e.g., "openai_translator")
 * @return true on success, false on failure
 */
bool translator_init_curl_handle(void **handle, const char *name);

/**
 * Cleanup a CURL handle.
 * Common logic for OpenAI, Gemini, Claude translators.
 *
 * @param handle Pointer to CURL handle pointer
 */
void translator_cleanup_curl_handle(void **handle);

/**
 * Common CURL response buffer structure.
 * Used by all translator implementations to collect API responses.
 */
struct translator_curl_response {
    char *data;
    size_t size;
};

/**
 * CURL write callback for collecting API responses.
 * Compatible with CURLOPT_WRITEFUNCTION.
 *
 * @param contents Response data chunk
 * @param size Size of each element
 * @param nmemb Number of elements
 * @param userp Pointer to translator_curl_response structure
 * @return Number of bytes processed
 */
size_t translator_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp);

/**
 * Initialize translator CURL response buffer.
 *
 * @param response Response buffer to initialize
 */
void translator_curl_response_init(struct translator_curl_response *response);

/**
 * Free translator CURL response buffer.
 *
 * @param response Response buffer to free
 */
void translator_curl_response_free(struct translator_curl_response *response);

/**
 * Function pointer type for single line translation.
 * Each translator provides its own API-specific implementation.
 *
 * @param text Text to translate (ruby notation already stripped)
 * @param target_lang Target language code
 * @param api_key API key for authentication
 * @param model_name Model name (may be NULL for some providers)
 * @return Translated text (caller must free), or NULL on error
 */
typedef char* (*translator_line_fn)(const char *text, const char *target_lang,
                                     const char *api_key, const char *model_name);

/**
 * Thread arguments for async translation.
 * Used by all translator implementations.
 */
struct translator_thread_args {
    struct lyrics_data *data;
    char *target_lang;
    char *api_key;
    char *model_name;       // Optional (NULL for DeepL)
    char *cache_path;
    int64_t track_length_us;
    const char *provider_name;  // "claude", "openai", "gemini", "deepl"
    translator_line_fn translate_line_fn;  // API-specific translation function
};

/**
 * Handle cache loading and validation (common logic).
 * Returns true if translation should continue, false if already complete.
 *
 * @param args Thread arguments with cache path
 * @param data Lyrics data to populate
 * @param translatable_count Total number of translatable lines
 * @param already_translated Output: number of already translated lines
 * @return true to continue translation, false if complete
 */
bool translator_handle_cache_loading_ex(struct translator_thread_args *args,
                                          struct lyrics_data *data,
                                          int translatable_count,
                                          int *already_translated);

/**
 * Process single line translation with all validation steps (common logic).
 * Returns true to continue iteration, false to break.
 *
 * @param line Lyrics line to translate
 * @param args Thread arguments with translation function
 * @param data Lyrics data for cancellation check
 * @param current Current translation counter (input/output)
 * @param translatable_count Total number of translatable lines
 * @return true to continue, false to break
 */
bool translator_process_line_translation_ex(struct lyrics_line *line,
                                              struct translator_thread_args *args,
                                              struct lyrics_data *data,
                                              int *current,
                                              int translatable_count);

/**
 * Save translation to cache with provider name in logs (common logic).
 *
 * @param cache_path Path to cache file
 * @param data Lyrics data with translations
 * @param target_lang Target language code
 * @param provider_name Provider name for logging
 */
void translator_save_to_cache_ex(const char *cache_path,
                                   struct lyrics_data *data,
                                   const char *target_lang,
                                   const char *provider_name);

/**
 * Async translation thread worker (common logic).
 * Calls provider-specific translation function via callback.
 *
 * @param arg Pointer to translator_thread_args
 * @return NULL
 */
void* translator_async_worker(void *arg);

/**
 * Generic lyrics translation launcher (eliminates 75-line duplication).
 * Common logic for all translator implementations.
 *
 * @param data Lyrics data to translate
 * @param track_length_us Track length in microseconds
 * @param provider_name Provider name for logging (e.g., "openai_translator")
 * @param translate_line_fn Provider-specific line translation function
 * @return true if translation thread launched successfully
 */
bool translator_translate_lyrics_generic(struct lyrics_data *data,
                                          int64_t track_length_us,
                                          const char *provider_name,
                                          translator_line_fn translate_line_fn);

/**
 * Build standard translation prompt for all providers.
 * Creates a consistent prompt instructing the AI to translate text.
 *
 * @param buffer Output buffer for the prompt
 * @param buffer_size Size of the output buffer
 * @param text Text to translate
 * @param target_lang Target language code
 * @return Number of characters written, or -1 on error
 */
int translator_build_translation_prompt(char *buffer, size_t buffer_size,
                                         const char *text, const char *target_lang);

/**
 * Parse retry delay from API error response.
 * Common logic for extracting retry delay from error messages.
 *
 * @param response_json JSON error response from API
 * @return Retry delay in milliseconds, or 0 if not found
 */
int translator_parse_retry_delay(const char *response_json);

/**
 * Extract text from JSON response by following a path expression.
 *
 * Supports nested object access and array indexing:
 * - "field" → object field access
 * - "field.nested" → nested object access
 * - "array[0]" → array element access
 * - "field[0].nested[1].text" → complex path
 *
 * Path examples:
 * - Claude: "content[0].text"
 * - OpenAI: "choices[0].message.content"
 * - Gemini: "candidates[0].content.parts[0].text"
 *
 * @param json_str JSON response string to parse
 * @param path JSON path expression to the target text field
 * @param provider_name Provider name for error logging
 * @return Extracted text (caller must free), or NULL on error
 */
char* json_extract_text_by_path(const char *json_str, const char *path, const char *provider_name);

/**
 * Function pointer type for building provider-specific HTTP headers.
 * Each translator provides its own header builder (e.g., API key format).
 *
 * @param api_key API key for authentication
 * @param extra_data Optional provider-specific data (e.g., API version)
 * @return CURL header list (caller must free with curl_slist_free_all), or NULL on error
 */
typedef struct curl_slist* (*translator_build_headers_fn)(const char *api_key, const void *extra_data);

/**
 * Parameters for HTTP translation request.
 * Used by translator_perform_http_request() to eliminate parameter overload.
 */
struct translator_http_params {
    const char *endpoint;              // API endpoint URL
    const char *request_body;          // JSON request body
    const char *provider_name;         // Provider name for logging
    const char *api_key;               // API key for authentication
    const void *extra_data;            // Optional provider-specific data
    int max_retries;                   // Maximum retry attempts
    translator_build_headers_fn build_headers;  // Header builder function
};

/**
 * Perform HTTP request with retry logic (common for all translators).
 * Eliminates duplicate CURL setup/teardown/retry logic across providers.
 *
 * @param params Request parameters (endpoint, headers, retry settings)
 * @return Raw JSON response (caller must free), or NULL on error
 */
char* translator_perform_http_request(struct translator_http_params *params);

#endif // TRANSLATOR_COMMON_H
