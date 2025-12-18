#ifndef _GEMINI_TRANSLATOR_H
#define _GEMINI_TRANSLATOR_H

#include "../../lyrics_types.h"
#include <stdbool.h>

/**
 * Translate lyrics data using Gemini API
 *
 * Performs asynchronous translation of lyrics lines using Google's Gemini API.
 * Uses MD5-based caching to avoid redundant API calls.
 *
 * @param data Lyrics data to translate (modified in-place)
 * @return true if translation started successfully (or loaded from cache), false otherwise
 */
bool gemini_translate_lyrics(struct lyrics_data *data, int64_t track_length_us);

/**
 * Initialize Gemini translator
 *
 * Sets up CURL and other resources needed for Gemini API calls.
 * Must be called before using gemini_translate_lyrics().
 *
 * @return true if initialization succeeded, false otherwise
 */
bool gemini_translator_init(void);

/**
 * Cleanup Gemini translator
 *
 * Releases resources allocated by gemini_translator_init().
 * Should be called on application shutdown.
 */
void gemini_translator_cleanup(void);

#endif // _GEMINI_TRANSLATOR_H
