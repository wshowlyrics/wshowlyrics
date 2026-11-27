// SPDX-License-Identifier: GPL-3.0-or-later
#include "thread_cancel.h"
#include <stddef.h>
#include <time.h>

static _Thread_local const _Atomic bool *t_cancel_flag = NULL;

void thread_cancel_bind(const _Atomic bool *flag) {
    t_cancel_flag = flag;
}

bool thread_cancel_requested(void) {
    return t_cancel_flag && atomic_load(t_cancel_flag);
}

// Longest stretch slept without looking at the cancel flag
#define SLEEP_SLICE_MS 50

bool thread_cancel_sleep_ms(int delay_ms) {
    int remaining_ms = delay_ms;
    while (remaining_ms > 0) {
        if (thread_cancel_requested()) {
            return false;
        }
        const int slice_ms = remaining_ms < SLEEP_SLICE_MS ? remaining_ms : SLEEP_SLICE_MS;
        const struct timespec delay = { .tv_sec = 0, .tv_nsec = slice_ms * 1000000L };
        nanosleep(&delay, NULL);
        remaining_ms -= slice_ms;
    }
    return !thread_cancel_requested();
}
