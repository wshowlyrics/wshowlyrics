#ifndef WAYLAND_MANAGER_H
#define WAYLAND_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// Wayland connection state
struct wayland_connection {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    bool configured;
    bool connected;
};

// Initialize Wayland connection
// Returns true on success, false on failure
bool wayland_manager_init(struct wayland_connection *conn);

// Check if connection is healthy
// Returns true if connected and working, false if error detected
bool wayland_manager_is_connected(struct wayland_connection *conn);

// Attempt to reconnect to Wayland compositor
// Waits 5 seconds before attempting reconnection
// Logs reconnection attempts to stdout
// Returns true on success, false on failure
bool wayland_manager_reconnect(struct wayland_connection *conn);

// Cleanup and disconnect from Wayland
void wayland_manager_cleanup(struct wayland_connection *conn);

// Dispatch Wayland events with error handling
// Returns true on success, false on error (connection lost)
bool wayland_manager_dispatch(struct wayland_connection *conn);

// Flush Wayland display with error handling
// Returns true on success, false on error (connection lost)
bool wayland_manager_flush(struct wayland_connection *conn);

#endif // WAYLAND_MANAGER_H
