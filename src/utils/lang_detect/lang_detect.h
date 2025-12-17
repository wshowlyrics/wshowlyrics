#ifndef _LANG_DETECT_H
#define _LANG_DETECT_H

#include <stdbool.h>

/**
 * Initialize language detection system.
 * Tries to initialize available detection libraries (libexttextcat).
 *
 * @return true if at least one detection method is available, false otherwise
 */
bool lang_detect_init(void);

/**
 * Cleanup language detection system.
 * Releases resources allocated by lang_detect_init().
 */
void lang_detect_cleanup(void);

/**
 * Detect the language of given text.
 * Uses libexttextcat for language detection.
 *
 * @param text The text to analyze
 * @param max_len Maximum length of text to analyze (-1 for no limit)
 * @return ISO 639-1 language code (e.g., "ko", "ja", "en") or NULL if detection failed
 *         The returned string is statically allocated and must NOT be freed.
 */
char* detect_language(const char *text, int max_len);

/**
 * Check if two texts are in the same language.
 *
 * @param text1 First text to compare
 * @param text2 Second text to compare
 * @return true if both texts are in the same language, false otherwise
 *         Returns false if language detection is not available or detection fails.
 */
bool is_same_language(const char *text1, const char *text2);

/**
 * Check if text is already in the target language.
 * Useful for skipping unnecessary translation API calls.
 *
 * @param text The text to check
 * @param target_lang Target language name (e.g., "Korean", "English", "Japanese")
 * @return true if text is already in target language, false otherwise
 *         Returns false if language detection is not available or detection fails.
 */
bool is_already_in_language(const char *text, const char *target_lang);

#endif // _LANG_DETECT_H
