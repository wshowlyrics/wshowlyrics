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
bool wayland_manager_is_connected(const struct wayland_connection *conn);

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

// Initialize Wayland globals (compositor, shm, layer_shell) from registry
// Creates registry, binds required globals
// Returns true on success, false if required globals are missing
bool wayland_manager_init_globals(struct wayland_connection *conn);

// Initialize Wayland surface and layer surface
// Must be called after wayland_manager_init_globals()
// Parameters:
//   - conn: Wayland connection with valid compositor and layer_shell
//   - layer: Layer shell layer (e.g., ZWLR_LAYER_SHELL_V1_LAYER_TOP)
//   - namespace: Layer surface namespace string
//   - anchor: Anchor flags for positioning
//   - margin: Margin for all sides
// Returns true on success, false on failure
bool wayland_manager_init_surface(struct wayland_connection *conn,
        uint32_t layer, const char *namespace,
        uint32_t anchor, int32_t margin);

// Full reconnection and reinitialization after connection loss
// Combines reconnect + init_globals + init_surface
// Waits 5 seconds before attempting reconnection
// Parameters:
//   - conn: Wayland connection to reconnect
//   - layer: Layer shell layer (e.g., ZWLR_LAYER_SHELL_V1_LAYER_TOP)
//   - namespace: Layer surface namespace string
//   - anchor: Anchor flags for positioning
//   - margin: Margin for all sides
// Returns true on success, false on failure
bool wayland_manager_reconnect_full(struct wayland_connection *conn,
        uint32_t layer, const char *namespace,
        uint32_t anchor, int32_t margin);

#endif // WAYLAND_MANAGER_H
