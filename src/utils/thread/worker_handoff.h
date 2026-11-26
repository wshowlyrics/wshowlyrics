// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WORKER_HANDOFF_H
#define WORKER_HANDOFF_H

#include <stdatomic.h>
#include <stdbool.h>

// A worker thread that the main thread can walk away from.
//
// The lyrics search and the artwork lookup read the music file, which can sit
// on a network mount. When the mount stops answering, the worker blocks in a
// file system call where no cancel flag reaches it, and a main thread that
// joined it froze with it (#62). So these workers are detached and never
// joined. Each one works on a heap-allocated job that embeds this struct, and
// the job is settled exactly once:
//
//   worker: worker_handoff_finish()   true  -> the result waits for the main
//                                              thread, which frees the job
//                                     false -> abandoned meanwhile: the worker
//                                              frees the job
//   main:   worker_handoff_abandon()  true  -> the worker frees the job when
//                                              it gets there
//                                     false -> the worker had finished: the
//                                              main thread frees the job
//
// Whoever is told to free the job owns it alone from then on; the other side
// does not touch it again. Everything the worker reads or writes must live in
// its job, because a dropped worker and its successor can be alive together.
struct worker_handoff {
    atomic_int state;
    _Atomic bool should_cancel;   // bound as the worker's cancel flag
};

// Main thread. Start a detached worker; arg is the job that embeds h. False if
// the thread could not be started (the job is still the caller's).
bool worker_handoff_start(struct worker_handoff *h, void *(*fn)(void *), void *arg);

// Worker, first thing: name the thread and bind h's cancel flag
// (thread_cancel.h).
void worker_handoff_enter(const struct worker_handoff *h, const char *thread_name);

// Worker, last thing before freeing or returning. See above.
bool worker_handoff_finish(struct worker_handoff *h);

// Main thread. Has the worker finished? Its result may then be read.
bool worker_handoff_done(const struct worker_handoff *h);

// Main thread. Ask the worker to stop and leave it behind. Never blocks. See
// above for the return value.
bool worker_handoff_abandon(struct worker_handoff *h);

// Main thread, at shutdown. Wait until every worker - dropped ones too - has
// finished, or timeout_ms has passed. False if some are still running: those
// are stuck in a file system call and are not waited for.
bool worker_handoff_wait_all(int timeout_ms);

#endif // WORKER_HANDOFF_H
