// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _LRCLIB_PROVIDER_H
#define _LRCLIB_PROVIDER_H

#include "../lyrics/lyrics_provider.h"

// How far lyrics from lrclib may be from the track in length and still be
// used. The search API matches by name only and returns whatever is closest,
// so without a limit a 90 s file got the lyrics of a 201 s song with a similar
// title (#71). The exact-match API (/api/get) allows 2 s; a little more here
// for a trailing silence or a differently trimmed file. Much more than that
// and the lyrics would be out of sync even if the song were the right one.
// Applied to search results, and to cached lyrics when they are read back.
#define LRCLIB_MAX_DURATION_DIFF_MS 5000

// lrclib.net API provider
extern struct lyrics_provider lrclib_provider;

#endif
