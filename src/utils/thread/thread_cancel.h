// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef THREAD_CANCEL_H
#define THREAD_CANCEL_H

#include <stdatomic.h>
#include <stdbool.h>

// Cooperative cancellation of a worker thread. A worker (lyrics fetch, artwork,
// translation) binds its should-cancel flag once when it starts; everything it
// then does on that thread - curl transfers (curl_install_thread_cancel),
// waits - watches that one flag, so a track change does not have to wait out a
// timeout. The binding is per thread and ends with it: nothing to clear, and
// two workers never share it. A thread that never bound a flag (the main
// thread) is never cancelled.
void thread_cancel_bind(const _Atomic bool *flag);

// Whether the calling thread has been asked to stop
bool thread_cancel_requested(void);

#endif // THREAD_CANCEL_H
