#ifndef _OPENAI_TRANSLATOR_H
#define _OPENAI_TRANSLATOR_H

#include "../../lyrics_types.h"
#include <stdbool.h>

/**
 * Translate lyrics data using OpenAI API
 *
 * Performs asynchronous translation of lyrics lines using OpenAI's Chat Completions API.
 * Uses MD5-based caching to avoid redundant API calls.
 *
 * @param data Lyrics data to translate (modified in-place)
 * @param track_length_us Song duration in microseconds (0 to skip feasibility check)
 * @return true if translation started successfully (or loaded from cache), false otherwise
 */
bool openai_translate_lyrics(struct lyrics_data *data, int64_t track_length_us);

/**
 * Initialize OpenAI translator
 *
 * Sets up CURL and other resources needed for OpenAI API calls.
 * Must be called before using openai_translate_lyrics().
 *
 * @return true if initialization succeeded, false otherwise
 */
bool openai_translator_init(void);

/**
 * Cleanup OpenAI translator
 *
 * Releases resources allocated by openai_translator_init().
 * Should be called on application shutdown.
 */
void openai_translator_cleanup(void);

#endif // _OPENAI_TRANSLATOR_H
