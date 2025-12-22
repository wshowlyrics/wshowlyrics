#ifndef WAYLAND_EVENTS_H
#define WAYLAND_EVENTS_H

#include "../main.h"
#include <poll.h>
#include <stdbool.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// Forward declarations
struct lyrics_state;
struct wayland_connection;

/**
 * Get the layer surface listener
 * The listener handles configure and closed events
 *
 * @return Pointer to static layer surface listener
 */
const struct zwlr_layer_surface_v1_listener* wayland_events_get_layer_surface_listener(void);

/**
 * Get the surface listener
 * The listener handles enter and leave events
 *
 * @return Pointer to static surface listener
 */
const struct wl_surface_listener* wayland_events_get_surface_listener(void);

/**
 * Get the output listener
 * The listener handles geometry, mode, scale, and done events
 *
 * @return Pointer to static output listener
 */
const struct wl_output_listener* wayland_events_get_output_listener(void);

/**
 * Get the registry listener
 * The listener handles global and global_remove events
 *
 * @return Pointer to static registry listener
 */
const struct wl_registry_listener* wayland_events_get_registry_listener(void);

/**
 * Get the frame callback listener
 * The listener handles frame done events for vsync rendering
 *
 * @return Pointer to static frame callback listener
 */
const struct wl_callback_listener* wayland_events_get_frame_listener(void);

/**
 * Handle full Wayland reconnection after connection loss
 * Reconnects to compositor and reinitializes surfaces
 *
 * @param state Lyrics state
 * @param wl_conn Wayland connection manager
 * @param pollfd Poll file descriptor (updated with new display fd)
 * @return true if reconnection successful, false otherwise
 */
bool wayland_events_handle_reconnection(struct lyrics_state *state,
                                        struct wayland_connection *wl_conn,
                                        struct pollfd *pollfd);

#endif // WAYLAND_EVENTS_H
