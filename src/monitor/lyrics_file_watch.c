// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "lyrics_file_watch.h"
#include "../utils/file/file_utils.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// How often the watched file is looked at
#define WATCH_INTERVAL_SEC 2

// Everything below is guarded by g_lock. The watcher never holds it across a
// file system call, so the main thread can always take it at once.
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static char *g_path = NULL;            // watched file, or NULL
static unsigned g_generation = 0;      // bumped whenever g_path changes
static bool g_has_status = false;      // g_status describes g_path
static struct lyrics_file_status g_status;
static bool g_thread_started = false;
static bool g_stop = false;

// Look at the file. Returns false when nothing can be said about it - any
// error other than "not there", e.g. a mount that came back with an I/O error.
static bool read_status(const char *path, struct lyrics_file_status *status) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno != ENOENT && errno != ENOTDIR) {
            return false;
        }
        status->exists = false;
        status->md5[0] = '\0';
        return true;
    }
    status->exists = true;
    // A file in the cache directory is written by this program only; nobody
    // compares its checksum, so it is not read
    if (path_is_in_cache_dir(path) || !calculate_file_md5(path, status->md5)) {
        status->md5[0] = '\0';
    }
    return true;
}

// Sleep until the next look, or until the path changes / the watcher is stopped
static void wait_for_next_look(void) {
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += WATCH_INTERVAL_SEC;
    pthread_cond_timedwait(&g_wake, &g_lock, &until);
}

static void *watch_thread(void *arg) {
    (void)arg;
    pthread_setname_np(pthread_self(), "lyrics-watch");

    pthread_mutex_lock(&g_lock);
    while (!g_stop) {
        if (!g_path) {
            pthread_cond_wait(&g_wake, &g_lock);
            continue;
        }
        char *path = strdup(g_path);
        const unsigned generation = g_generation;
        pthread_mutex_unlock(&g_lock);

        // May block for as long as the file system takes; nothing waits on us
        struct lyrics_file_status seen;
        const bool ok = path && read_status(path, &seen);
        free(path);

        pthread_mutex_lock(&g_lock);
        // A result for a path that was replaced meanwhile is dropped
        if (ok && generation == g_generation) {
            g_status = seen;
            g_has_status = true;
        }
        // Changed path or stop (both bump the generation): look again at once
        if (generation == g_generation) {
            wait_for_next_look();
        }
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

// Start the watcher on first use. Detached: it is never joined (see _stop).
static void start_thread_locked(void) {
    if (g_thread_started) {
        return;
    }
    pthread_t thread;
    if (pthread_create(&thread, NULL, watch_thread, NULL) != 0) {
        log_warn("Failed to start the lyrics file watcher; file changes will not be noticed");
        return;
    }
    pthread_detach(thread);
    g_thread_started = true;
}

void lyrics_file_watch_set(const char *path) {
    pthread_mutex_lock(&g_lock);
    const bool same = (path == NULL && g_path == NULL) ||
                      (path && g_path && strcmp(path, g_path) == 0);
    if (!same) {
        free(g_path);
        g_path = path ? strdup(path) : NULL;
        g_generation++;
        g_has_status = false;
        if (g_path) {
            start_thread_locked();
        }
        pthread_cond_signal(&g_wake);
    }
    pthread_mutex_unlock(&g_lock);
}

bool lyrics_file_watch_get(struct lyrics_file_status *status) {
    pthread_mutex_lock(&g_lock);
    const bool has_status = g_has_status;
    if (has_status) {
        *status = g_status;
    }
    pthread_mutex_unlock(&g_lock);
    return has_status;
}

void lyrics_file_watch_stop(void) {
    pthread_mutex_lock(&g_lock);
    g_stop = true;
    free(g_path);
    g_path = NULL;
    g_generation++;
    g_has_status = false;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}
