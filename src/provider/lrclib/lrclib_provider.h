// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _LRCLIB_PROVIDER_H
#define _LRCLIB_PROVIDER_H

#include "../lyrics/lyrics_provider.h"
#include <stdatomic.h>

// lrclib.net API provider
extern struct lyrics_provider lrclib_provider;

// Point lrclib's in-flight curl transfer at a cancellation flag (or NULL to
// clear). When *flag becomes true the transfer aborts promptly instead of
// waiting out the timeout. Set by the async fetch worker; single fetch at a time.
void lrclib_set_cancel_flag(_Atomic bool *flag);

#endif
