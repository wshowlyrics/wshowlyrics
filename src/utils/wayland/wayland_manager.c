#include "wayland_manager.h"
#include <assert.h>
#include "../../constants.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <stdio.h>

bool wayland_manager_init(struct wayland_connection *conn) {
    if (!conn) {
        return false;
    }

    // Initialize connection struct
    memset(conn, 0, sizeof(struct wayland_connection));

    // Connect to Wayland display
    conn->display = wl_display_connect(NULL);
    if (!conn->display) {
        log_error("Failed to connect to Wayland display: %s", strerror(errno));
        return false;
    }

    conn->connected = true;
    log_info("Connected to Wayland compositor");
    return true;
}

bool wayland_manager_is_connected(const struct wayland_connection *conn) {
    return conn && conn->connected && conn->display != NULL;
}

bool wayland_manager_reconnect(struct wayland_connection *conn) {
    if (!conn) {
        return false;
    }

    // Cleanup existing connection
    wayland_manager_cleanup(conn);

    // Wait 5 seconds before reconnecting
    log_info("Waiting 5 seconds before reconnecting to Wayland compositor...");
    sleep(5);

    // Attempt reconnection
    log_info("Attempting to reconnect to Wayland compositor...");

    conn->display = wl_display_connect(NULL);
    if (!conn->display) {
        log_error("Reconnection failed: %s", strerror(errno));
        conn->connected = false;
        return false;
    }

    conn->connected = true;
    log_info("Successfully reconnected to Wayland compositor");
    return true;
}

void wayland_manager_cleanup(struct wayland_connection *conn) {
    if (!conn) {
        return;
    }

    if (conn->layer_surface) {
        zwlr_layer_surface_v1_destroy(conn->layer_surface);
        conn->layer_surface = NULL;
    }

    if (conn->surface) {
        wl_surface_destroy(conn->surface);
        conn->surface = NULL;
    }

    if (conn->layer_shell) {
        zwlr_layer_shell_v1_destroy(conn->layer_shell);
        conn->layer_shell = NULL;
    }

    if (conn->compositor) {
        wl_compositor_destroy(conn->compositor);
        conn->compositor = NULL;
    }

    if (conn->shm) {
        wl_shm_destroy(conn->shm);
        conn->shm = NULL;
    }

    if (conn->registry) {
        wl_registry_destroy(conn->registry);
        conn->registry = NULL;
    }

    if (conn->display) {
        wl_display_disconnect(conn->display);
        conn->display = NULL;
    }

    conn->connected = false;
    conn->configured = false;
}

bool wayland_manager_dispatch(struct wayland_connection *conn) {
    if (!wayland_manager_is_connected(conn)) {
        return false;
    }

    if (wl_display_dispatch(conn->display) == -1) {
        log_error("Wayland display dispatch error: %s (errno=%d)",
                strerror(errno), errno);

        if (errno == EPIPE || errno == ECONNRESET) {
            log_error("Wayland connection lost (possibly due to screen lock or compositor restart)");
            conn->connected = false;
        }

        return false;
    }

    return true;
}

bool wayland_manager_flush(struct wayland_connection *conn) {
    if (!wayland_manager_is_connected(conn)) {
        return false;
    }

    // Consecutive poll timeouts across calls. A healthy compositor drains the
    // socket within a poll interval, so this stays at 0. If it climbs, the peer
    // is wedged (hung-but-connected) and the buffered data would grow unbounded.
    static int stall_count = 0;

    // Flush the outgoing Wayland buffer. When the socket send buffer is full,
    // wl_display_flush() returns -1 with EAGAIN. Wait for the fd to become
    // writable and retry, rather than busy-spinning. The previous loop tested a
    // stale errno (wl_display_flush() does not clear it on success), so a single
    // EAGAIN wedged it in an infinite loop that never returned and never let the
    // main loop observe the shutdown flag — the process could only be SIGKILLed.
    while (wl_display_flush(conn->display) == -1) {
        if (errno != EAGAIN) {
            log_error("Wayland display flush error: %s (errno=%d)",
                    strerror(errno), errno);
            if (errno == EPIPE || errno == ECONNRESET) {
                log_error("Wayland connection lost (possibly due to screen lock or compositor restart)");
                conn->connected = false;
            }
            stall_count = 0;
            return false;
        }

        struct pollfd pfd = {
            .fd = wl_display_get_fd(conn->display),
            .events = POLLOUT,
        };
        int pr = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (pr < 0 && errno != EINTR) {
            log_error("Wayland flush poll error: %s (errno=%d)", strerror(errno), errno);
            stall_count = 0;
            return false;
        }
        if (pr > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            // The socket errored/closed under us — treat as a lost connection so
            // the main loop can trigger a reconnect instead of looping forever.
            log_error("Wayland flush poll reported error (revents=0x%x)", pfd.revents);
            conn->connected = false;
            stall_count = 0;
            return false;
        }
        if (pr <= 0) {
            // Timeout or interrupted: leave the remaining data buffered and let
            // the main loop iterate (re-checking run / handling signals). It
            // flushes again next pass, so shutdown and reconnect stay responsive.
            // But a compositor that never drains would buffer without bound, so
            // escalate to a lost connection after a sustained stall.
            if (pr == 0 && ++stall_count >= WAYLAND_FLUSH_MAX_STALLS) {
                log_error("Wayland flush stalled for ~%d ms — treating connection as lost",
                        WAYLAND_FLUSH_MAX_STALLS * POLL_TIMEOUT_MS);
                conn->connected = false;
                stall_count = 0;
                return false;
            }
            return true;
        }
        // Socket is writable now — loop retries the flush.
    }

    stall_count = 0;
    return true;
}

// Registry callback for binding globals
static void registry_global_handler(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct wayland_connection *conn = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        conn->compositor = wl_registry_bind(registry,
                name, &wl_compositor_interface, 4);
        log_info("Bound wl_compositor");
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        conn->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        log_info("Bound wl_shm");
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        conn->layer_shell = wl_registry_bind(registry,
                name, &zwlr_layer_shell_v1_interface, 1);
        log_info("Bound zwlr_layer_shell_v1");
    }
}

static void registry_global_remove_handler(void *data,
        struct wl_registry *const registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
    // Nothing to do for now
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler,
};

bool wayland_manager_init_globals(struct wayland_connection *conn) {
    if (!conn || !conn->display) {
        log_error("Invalid connection or display");
        return false;
    }

    // Create registry
    conn->registry = wl_display_get_registry(conn->display);
    if (!conn->registry) {
        log_error("Failed to get registry");
        return false;
    }

    // Add listener and roundtrip to get globals
    wl_registry_add_listener(conn->registry, &registry_listener, conn);
    wl_display_roundtrip(conn->display);

    // Check that all required globals were bound
    if (!conn->compositor) {
        log_error("wl_compositor not available");
        return false;
    }
    if (!conn->shm) {
        log_error("wl_shm not available");
        return false;
    }
    if (!conn->layer_shell) {
        log_error("zwlr_layer_shell_v1 not available");
        return false;
    }

    log_info("All required Wayland globals initialized");
    return true;
}

bool wayland_manager_init_surface(struct wayland_connection *conn,
        uint32_t layer, const char *namespace,
        uint32_t anchor, int32_t margin) {
    if (!conn || !conn->compositor || !conn->layer_shell) {
        log_error("Invalid connection or missing globals");
        return false;
    }

    // Create surface
    conn->surface = wl_compositor_create_surface(conn->compositor);
    if (!conn->surface) {
        log_error("Failed to create surface");
        return false;
    }

    // Create layer surface
    conn->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            conn->layer_shell, conn->surface, NULL, layer, namespace);
    if (!conn->layer_surface) {
        log_error("Failed to create layer surface");
        wl_surface_destroy(conn->surface);
        conn->surface = NULL;
        return false;
    }

    // Configure layer surface
    zwlr_layer_surface_v1_set_size(conn->layer_surface, 1, 1);
    zwlr_layer_surface_v1_set_anchor(conn->layer_surface, anchor);
    zwlr_layer_surface_v1_set_margin(conn->layer_surface,
            margin, margin, margin, margin);
    zwlr_layer_surface_v1_set_exclusive_zone(conn->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(conn->layer_surface, 0);

    // Set empty input region to allow clicks to pass through
    struct wl_region *region = wl_compositor_create_region(conn->compositor);
    wl_surface_set_input_region(conn->surface, region);
    wl_region_destroy(region);

    log_info("Wayland surface and layer surface initialized");
    return true;
}

bool wayland_manager_reconnect_full(struct wayland_connection *conn,
        uint32_t layer, const char *namespace,
        uint32_t anchor, int32_t margin) {
    if (!conn) {
        return false;
    }

    // Cleanup existing connection
    wayland_manager_cleanup(conn);

    // Wait 5 seconds before reconnecting
    log_info("Waiting 5 seconds before reconnecting to Wayland compositor...");
    sleep(5);

    // Attempt reconnection to display
    log_info("Attempting to reconnect to Wayland compositor...");
    conn->display = wl_display_connect(NULL);
    if (!conn->display) {
        log_error("Reconnection failed: %s", strerror(errno));
        conn->connected = false;
        return false;
    }

    log_info("Display reconnection successful");

    // Initialize globals (registry, compositor, shm, layer_shell)
    if (!wayland_manager_init_globals(conn)) {
        log_error("Failed to initialize globals after reconnection");
        wl_display_disconnect(conn->display);
        conn->display = NULL;
        conn->connected = false;
        return false;
    }

    // Initialize surface and layer surface
    if (!wayland_manager_init_surface(conn, layer, namespace, anchor, margin)) {
        log_error("Failed to initialize surface after reconnection");
        wayland_manager_cleanup(conn);
        return false;
    }

    conn->connected = true;
    log_info("Wayland connection and surfaces reinitialized");
    return true;
}
