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

#endif // TRANSLATOR_COMMON_H
