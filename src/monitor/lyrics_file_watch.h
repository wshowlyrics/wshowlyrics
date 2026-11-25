// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LYRICS_FILE_WATCH_H
#define LYRICS_FILE_WATCH_H

#include <stdbool.h>
#include "../constants.h"

// Watches the lyrics file that is on screen - is it still there, has it
// changed - from its own thread, so the main thread never touches that file.
// A lyrics file next to the music can sit on a network mount; when the mount
// stops answering, stat() and read() on it block for as long as it takes, and
// on the main thread that froze the overlay (#62, #69). Here only the watcher
// blocks: the main thread keeps the last status it was given and keeps drawing.

// What the watcher last saw of the watched file.
struct lyrics_file_status {
    bool exists;                          // false: the file is gone
    char md5[MD5_DIGEST_STRING_LENGTH];   // "" if it could not be read
};

// Main thread. Watch this file from now on (NULL: nothing). Cheap when the
// path is unchanged, so it can be called on every tick. Setting NULL before a
// reload drops the status, so what was seen of the old contents is never
// compared against the reloaded lyrics.
void lyrics_file_watch_set(const char *path);

// Main thread. The last status seen for the watched path. Returns false when
// there is none yet - not checked since the path was set, or the file system
// has not answered - in which case the caller should assume nothing changed.
bool lyrics_file_watch_get(struct lyrics_file_status *status);

// Main thread, at shutdown. Asks the watcher to stop; does not wait for it,
// because it may be stuck in the very call this module exists to keep away.
void lyrics_file_watch_stop(void);

#endif // LYRICS_FILE_WATCH_H
