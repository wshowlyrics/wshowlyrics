#ifndef _DEEPL_TRANSLATOR_H
#define _DEEPL_TRANSLATOR_H

#include "../../lyrics_types.h"
#include <stdbool.h>

// Translate lyrics data in-place (modifies ruby_segments to add translation field)
// Only translates if:
//   1. enable_deepl is true in config
//   2. API key is configured
//   3. Lyrics format is LRC (has ruby_segments, not SRT/VTT/LRCX)
// Returns true if translation was performed or loaded from cache, false otherwise
bool deepl_translate_lyrics(struct lyrics_data *data, int64_t track_length_us);

// Initialize DeepL translator (called on startup)
bool deepl_translator_init(void);

// Cleanup DeepL translator (called on shutdown)
void deepl_translator_cleanup(void);

#endif // _DEEPL_TRANSLATOR_H
