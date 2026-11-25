// SPDX-License-Identifier: GPL-3.0-or-later
#include "thread_cancel.h"
#include <stddef.h>

static _Thread_local const _Atomic bool *t_cancel_flag = NULL;

void thread_cancel_bind(const _Atomic bool *flag) {
    t_cancel_flag = flag;
}

bool thread_cancel_requested(void) {
    return t_cancel_flag && atomic_load(t_cancel_flag);
}
