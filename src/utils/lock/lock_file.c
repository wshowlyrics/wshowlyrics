#define _GNU_SOURCE
#include "lock_file.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

static int lock_fd = -1;

// Check if process is still running
static bool is_process_running(pid_t pid) {
    // kill with signal 0 checks if process exists without sending a signal
    return kill(pid, 0) == 0;
}

bool lock_file_acquire(void) {
    // Try to open/create lock file (owner-only access for security)
    lock_fd = open(LOCK_FILE_PATH, O_RDWR | O_CREAT, 0600);
    if (lock_fd < 0) {
        log_error("Failed to open lock file %s: %s", LOCK_FILE_PATH, strerror(errno));
        return false;
    }

    // Try to acquire exclusive lock (non-blocking)
    struct flock fl = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
    };

    if (fcntl(lock_fd, F_SETLK, &fl) == -1) {
        if (errno == EACCES || errno == EAGAIN) {
            // Lock is held by another process
            // Read PID from lock file to verify it's still running
            char pid_str[32];
            ssize_t n = read(lock_fd, pid_str, sizeof(pid_str) - 1);
            if (n > 0) {
                pid_str[n] = '\0';
                pid_t old_pid = atoi(pid_str);

                if (old_pid > 0 && !is_process_running(old_pid)) {
                    // Stale lock file - reuse the file descriptor to avoid TOCTOU
                    log_warn("Clearing stale lock (PID %d not running)", old_pid);

                    // Truncate the file and reset position (keep fd open to avoid race)
                    if (ftruncate(lock_fd, 0) == -1) {
                        log_warn("Failed to truncate lock file: %s", strerror(errno));
                    }
                    if (lseek(lock_fd, 0, SEEK_SET) == -1) {
                        log_warn("Failed to reset lock file position: %s", strerror(errno));
                    }

                    // Try to acquire lock again (file is still open)
                    if (fcntl(lock_fd, F_SETLK, &fl) == -1) {
                        log_error("Failed to acquire lock after clearing stale lock: %s", strerror(errno));
                        close(lock_fd);
                        lock_fd = -1;
                        return false;
                    }

                    // Successfully acquired lock
                    goto write_pid;
                } else {
                    log_info("Another instance is already running (PID %d)", old_pid);
                    close(lock_fd);
                    lock_fd = -1;
                    return false;
                }
            } else {
                log_info("Another instance is already running");
                close(lock_fd);
                lock_fd = -1;
                return false;
            }
        } else {
            log_error("Failed to acquire lock: %s", strerror(errno));
            close(lock_fd);
            lock_fd = -1;
            return false;
        }
    }

write_pid:
    // Write our PID to lock file
    if (ftruncate(lock_fd, 0) == -1) {
        log_warn("Failed to truncate lock file: %s", strerror(errno));
    }

    char pid_str[32];
    int len = snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    if (write(lock_fd, pid_str, len) != len) {
        log_warn("Failed to write PID to lock file: %s", strerror(errno));
    }

    log_info("Lock acquired (PID %d)", getpid());
    return true;
}

void lock_file_release(void) {
    if (lock_fd >= 0) {
        // Remove lock file
        if (unlink(LOCK_FILE_PATH) != 0 && errno != ENOENT) {
            log_warn("Failed to unlink lock file: %s", strerror(errno));
        }

        // Release lock and close file
        struct flock fl = {
            .l_type = F_UNLCK,
            .l_whence = SEEK_SET,
            .l_start = 0,
            .l_len = 0,
        };
        if (fcntl(lock_fd, F_SETLK, &fl) == -1) {
            log_warn("Failed to unlock file: %s", strerror(errno));
        }

        if (close(lock_fd) == -1) {
            log_warn("Failed to close lock file: %s", strerror(errno));
        }
        lock_fd = -1;

        log_info("Lock released");
    }
}
