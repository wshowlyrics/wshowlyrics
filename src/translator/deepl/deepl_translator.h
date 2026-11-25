// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _DEEPL_TRANSLATOR_H
#define _DEEPL_TRANSLATOR_H

#include "../../lyrics_types.h"
#include <stdbool.h>

// Start translating the lyrics on the translation thread (LRC only; see
// translator_translate_lyrics_generic). Returns true if the thread was started.
bool deepl_translate_lyrics(struct lyrics_data *data, int64_t track_length_us);

#endif // _DEEPL_TRANSLATOR_H
