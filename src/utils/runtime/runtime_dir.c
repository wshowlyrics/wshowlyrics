#include "runtime_dir.h"
#include "../../constants.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static char g_runtime_dir[512] = {0};
static bool g_runtime_dir_initialized = false;

const char* get_runtime_dir(void) {
    if (g_runtime_dir_initialized) {
        return g_runtime_dir;
    }

    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");

    if (xdg_runtime && xdg_runtime[0] != '\0') {
        snprintf(g_runtime_dir, sizeof(g_runtime_dir), "%s/wshowlyrics", xdg_runtime);
    } else {
        // Fallback to /tmp (should rarely happen on modern systems)
        snprintf(g_runtime_dir, sizeof(g_runtime_dir), "/tmp/wshowlyrics");
        log_warn("XDG_RUNTIME_DIR not set, using fallback: %s", g_runtime_dir);
    }

    // Create directory (owner-only access for security)
    if (mkdir(g_runtime_dir, 0700) != 0 && errno != EEXIST) {
        log_warn("Failed to create runtime directory %s: %s", g_runtime_dir, strerror(errno));
    }

    g_runtime_dir_initialized = true;
    return g_runtime_dir;
}
