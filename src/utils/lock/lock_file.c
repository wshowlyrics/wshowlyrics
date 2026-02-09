#define _GNU_SOURCE
#include "lock_file.h"
#include "../runtime/runtime_dir.h"
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
static char g_lock_path[600] = {0};

// Get lock file path (runtime_dir + "/wshowlyrics.lock")
static const char* get_lock_path(void) {
    if (g_lock_path[0] == '\0') {
        const char *runtime = get_runtime_dir();
        if (!runtime) {
            return NULL;
        }
        snprintf(g_lock_path, sizeof(g_lock_path), "%s/wshowlyrics.lock", runtime);
    }
    return g_lock_path;
}

// Check if process is still running
static bool is_process_running(pid_t pid) {
    // kill with signal 0 checks if process exists without sending a signal
    return kill(pid, 0) == 0;
}

// Helper: Read PID from lock file
static pid_t read_lock_pid(int fd) {
    char pid_str[32];
    ssize_t n = read(fd, pid_str, sizeof(pid_str) - 1);
    if (n <= 0) {
        return -1;
    }
    pid_str[n] = '\0';
    return atoi(pid_str);
}

// Helper: Write PID to lock file
static bool write_pid_to_lock(int fd) {
    if (ftruncate(fd, 0) == -1) {
        log_warn("Failed to truncate lock file: %s", strerror(errno));
    }

    char pid_str[32];
    int len = snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    if (write(fd, pid_str, len) != len) {
        log_warn("Failed to write PID to lock file: %s", strerror(errno));
        return false;
    }

    log_info("Lock acquired (PID %d)", getpid());
    return true;
}

// Helper: Try to clear stale lock and re-acquire
static bool try_clear_stale_lock(int fd, pid_t old_pid, struct flock *fl) {
    if (old_pid <= 0 || is_process_running(old_pid)) {
        return false;
    }

    // Stale lock file - reuse the file descriptor to avoid TOCTOU
    log_warn("Clearing stale lock (PID %d not running)", old_pid);

    // Truncate the file and reset position (keep fd open to avoid race)
    if (ftruncate(fd, 0) == -1) {
        log_warn("Failed to truncate lock file: %s", strerror(errno));
    }
    if (lseek(fd, 0, SEEK_SET) == -1) {
        log_warn("Failed to reset lock file position: %s", strerror(errno));
    }

    // Try to acquire lock again (file is still open)
    if (fcntl(fd, F_SETLK, fl) == -1) {
        log_error("Failed to acquire lock after clearing stale lock: %s", strerror(errno));
        return false;
    }

    return true;
}

// Helper: Handle lock contention (another process holds the lock)
static bool handle_lock_contention(int fd, struct flock *fl) {
    pid_t old_pid = read_lock_pid(fd);

    if (old_pid < 0) {
        log_info("Another instance is already running");
        return false;
    }

    // Try to clear stale lock
    if (try_clear_stale_lock(fd, old_pid, fl)) {
        return write_pid_to_lock(fd);
    }

    // Process is still running
    log_info("Another instance is already running (PID %d)", old_pid);
    return false;
}

bool lock_file_acquire(void) {
    const char *lock_path = get_lock_path();
    if (!lock_path) {
        return false;
    }

    // Try to open/create lock file (owner-only access for security)
    lock_fd = open(lock_path, O_RDWR | O_CREAT, 0600);
    if (lock_fd < 0) {
        log_error("Failed to open lock file %s: %s", lock_path, strerror(errno));
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
        // Lock acquisition failed
        bool success = false;
        if (errno == EACCES || errno == EAGAIN) {
            // Lock is held by another process - try to handle contention
            success = handle_lock_contention(lock_fd, &fl);
        } else {
            log_error("Failed to acquire lock: %s", strerror(errno));
        }

        if (!success) {
            close(lock_fd);
            lock_fd = -1;
            return false;
        }
        return true;  // handle_lock_contention already wrote PID
    }

    // Lock acquired on first try - write PID
    return write_pid_to_lock(lock_fd);
}

void lock_file_release(void) {
    if (lock_fd >= 0) {
        // Remove lock file
        if (unlink(get_lock_path()) != 0 && errno != ENOENT) {
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
