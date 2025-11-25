#include "wayland_manager.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool wayland_manager_init(struct wayland_connection *conn) {
    if (!conn) {
        return false;
    }

    // Initialize connection struct
    memset(conn, 0, sizeof(struct wayland_connection));

    // Connect to Wayland display
    conn->display = wl_display_connect(NULL);
    if (!conn->display) {
        fprintf(stderr, "\033[1;31mERROR:\033[0m Failed to connect to Wayland display: %s\n", strerror(errno));
        return false;
    }

    conn->connected = true;
    printf("Connected to Wayland compositor\n");
    return true;
}

bool wayland_manager_is_connected(struct wayland_connection *conn) {
    return conn && conn->connected && conn->display != NULL;
}

bool wayland_manager_reconnect(struct wayland_connection *conn) {
    if (!conn) {
        return false;
    }

    // Cleanup existing connection
    wayland_manager_cleanup(conn);

    // Wait 5 seconds before reconnecting
    printf("Waiting 5 seconds before reconnecting to Wayland compositor...\n");
    sleep(5);

    // Attempt reconnection
    printf("Attempting to reconnect to Wayland compositor...\n");

    conn->display = wl_display_connect(NULL);
    if (!conn->display) {
        fprintf(stderr, "\033[1;31mERROR:\033[0m Reconnection failed: %s\n", strerror(errno));
        conn->connected = false;
        return false;
    }

    conn->connected = true;
    printf("Successfully reconnected to Wayland compositor\n");
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
        fprintf(stderr, "\033[1;31mERROR:\033[0m Wayland display dispatch error: %s (errno=%d)\n",
                strerror(errno), errno);

        if (errno == EPIPE || errno == ECONNRESET) {
            fprintf(stderr, "\033[1;31mERROR:\033[0m Wayland connection lost (possibly due to screen lock or compositor restart)\n");
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

    errno = 0;
    do {
        if (wl_display_flush(conn->display) == -1 && errno != EAGAIN) {
            fprintf(stderr, "\033[1;31mERROR:\033[0m Wayland display flush error: %s (errno=%d)\n",
                    strerror(errno), errno);

            if (errno == EPIPE || errno == ECONNRESET) {
                fprintf(stderr, "\033[1;31mERROR:\033[0m Wayland connection lost (possibly due to screen lock or compositor restart)\n");
                conn->connected = false;
            }

            return false;
        }
    } while (errno == EAGAIN);

    return true;
}
