// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "worker_handoff.h"
#include "thread_cancel.h"
#include <pthread.h>
#include <time.h>

enum { HANDOFF_RUNNING = 0, HANDOFF_DONE, HANDOFF_ABANDONED };

#define WAIT_POLL_MS 20

// Workers that have not finished yet, dropped ones included
static atomic_int g_running_workers;

bool worker_handoff_start(struct worker_handoff *h, void *(*fn)(void *), void *arg) {
    atomic_store(&h->state, HANDOFF_RUNNING);
    atomic_store(&h->should_cancel, false);

    pthread_t thread;
    atomic_fetch_add(&g_running_workers, 1);
    if (pthread_create(&thread, NULL, fn, arg) != 0) {
        atomic_fetch_sub(&g_running_workers, 1);
        return false;
    }
    pthread_detach(thread);
    return true;
}

void worker_handoff_enter(const struct worker_handoff *h, const char *thread_name) {
    pthread_setname_np(pthread_self(), thread_name);
    thread_cancel_bind(&h->should_cancel);
}

bool worker_handoff_finish(struct worker_handoff *h) {
    int expected = HANDOFF_RUNNING;
    const bool handed_over =
        atomic_compare_exchange_strong(&h->state, &expected, HANDOFF_DONE);
    // h may already be freed by the main thread here; only the counter is touched
    atomic_fetch_sub(&g_running_workers, 1);
    return handed_over;
}

bool worker_handoff_done(const struct worker_handoff *h) {
    return atomic_load(&h->state) == HANDOFF_DONE;
}

bool worker_handoff_abandon(struct worker_handoff *h) {
    atomic_store(&h->should_cancel, true);
    int expected = HANDOFF_RUNNING;
    return atomic_compare_exchange_strong(&h->state, &expected, HANDOFF_ABANDONED);
}

bool worker_handoff_wait_all(int timeout_ms) {
    const struct timespec slice = { .tv_nsec = WAIT_POLL_MS * 1000000L };
    int waited_ms = 0;
    while (atomic_load(&g_running_workers) > 0 && waited_ms < timeout_ms) {
        nanosleep(&slice, NULL);
        waited_ms += WAIT_POLL_MS;
    }
    return atomic_load(&g_running_workers) == 0;
}
