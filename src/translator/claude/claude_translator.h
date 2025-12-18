#ifndef _CLAUDE_TRANSLATOR_H
#define _CLAUDE_TRANSLATOR_H

#include "../../lyrics_types.h"
#include <stdbool.h>

/**
 * Translate lyrics data using Claude API
 *
 * Performs asynchronous translation of lyrics lines using Anthropic's Claude API.
 * Uses MD5-based caching to avoid redundant API calls.
 *
 * @param data Lyrics data to translate (modified in-place)
 * @return true if translation started successfully (or loaded from cache), false otherwise
 */
bool claude_translate_lyrics(struct lyrics_data *data, int64_t track_length_us);

/**
 * Initialize Claude translator
 *
 * Sets up CURL and other resources needed for Claude API calls.
 * Must be called before using claude_translate_lyrics().
 *
 * @return true if initialization succeeded, false otherwise
 */
bool claude_translator_init(void);

/**
 * Cleanup Claude translator
 *
 * Releases resources allocated by claude_translator_init().
 * Should be called on application shutdown.
 */
void claude_translator_cleanup(void);

#endif // _CLAUDE_TRANSLATOR_H
