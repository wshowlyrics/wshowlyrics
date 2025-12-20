#ifndef _LYRICS_LOCK_FILE_H
#define _LYRICS_LOCK_FILE_H

#include <stdbool.h>

// Lock file path
#define LOCK_FILE_PATH "/tmp/wshowlyrics.lock"

// Try to acquire lock file
// Returns true if lock acquired, false if already running
bool lock_file_acquire(void);

// Release lock file
void lock_file_release(void);

#endif // _LYRICS_LOCK_FILE_H
