#include "wayland_events.h"
#include "../core/rendering/rendering_manager.h"
#include "../utils/wayland/wayland_manager.h"
#include "../utils/shm/shm.h"
#include "../constants.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations for static functions
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1, uint32_t serial, uint32_t width, uint32_t height);
static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1);
static void surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *output);
static void surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *output);
static void output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform);
static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
static void output_done(void *data, struct wl_output *wl_output);
static void output_scale(void *data, struct wl_output *wl_output, int32_t factor);
static void registry_global(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version);
static void registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name);

// Listener structures (defined early so they can be referenced by event handlers)

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static const struct wl_surface_listener wl_surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
};

static const struct wl_output_listener wl_output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// Event handlers

static void layer_surface_configure(void *data,
        struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1,
        uint32_t serial, uint32_t width, uint32_t height) {
    struct lyrics_state *state = data;
    state->width = width;
    state->height = height;
    zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);
    rendering_manager_set_dirty(state);
}

static void layer_surface_closed(void *data,
        struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1) {
    (void)zwlr_layer_surface_v1;
    struct lyrics_state *state = data;

    // Ignore close events during reconnection (expected behavior)
    if (state->reconnecting) {
        return;
    }

    log_warn("Layer surface closed by compositor");
    // Signal that we need to reconnect
    state->needs_reconnect = true;
    state->wl_conn->connected = false;
}

static void surface_enter(void *data,
        struct wl_surface *wl_surface, struct wl_output *output) {
    (void)wl_surface;
    struct lyrics_state *state = data;
    struct lyrics_output *lyrics_output = state->outputs;
    while (lyrics_output && lyrics_output->output != output) {
        lyrics_output = lyrics_output->next;
    }
    if (lyrics_output) {
        state->output = lyrics_output;
    }
}

static void surface_leave(void *data,
        struct wl_surface *wl_surface, struct wl_output *output) {
    (void)data;
    (void)wl_surface;
    (void)output;
    // Not needed for this application
}

static void output_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
        int32_t subpixel, const char *make, const char *model,
        int32_t transform) {
    (void)wl_output;
    (void)x;
    (void)y;
    (void)physical_width;
    (void)physical_height;
    (void)make;
    (void)model;
    (void)transform;
    struct lyrics_output *output = data;
    output->subpixel = subpixel;
}

static void output_mode(void *data, struct wl_output *wl_output,
        uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    (void)wl_output;
    (void)flags;
    (void)refresh;
    struct lyrics_output *output = data;
    output->width = width;
    output->height = height;
    log_info("Screen resolution: %dx%d", width, height);
}

static void output_done(void *data, struct wl_output *wl_output) {
    (void)data;
    (void)wl_output;
    // Not needed
}

static void output_scale(void *data,
        struct wl_output *wl_output, int32_t factor) {
    (void)wl_output;
    struct lyrics_output *output = data;
    output->scale = factor;
}

static void registry_global(void *data, struct wl_registry *wl_registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct lyrics_state *state = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(wl_registry,
                name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layer_shell = wl_registry_bind(wl_registry,
                name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct lyrics_output *output = calloc(1, sizeof(struct lyrics_output));
        if (!output) {
            fprintf(stderr, "Failed to allocate memory for output\n");
            return;
        }
        output->output = wl_registry_bind(wl_registry,
                name, &wl_output_interface, 3);
        output->scale = 1;
        output->height = 0;
        output->width = 0;
        struct lyrics_output **link = &state->outputs;
        while (*link) {
            link = &(*link)->next;
        }
        *link = output;
        wl_output_add_listener(output->output, &wl_output_listener, output);

        // Set first output as default
        if (!state->output) {
            state->output = output;
            log_info("Set primary output");
        }
    }
}

static void registry_global_remove(void *data,
        struct wl_registry *wl_registry, uint32_t name) {
    (void)data;
    (void)wl_registry;
    (void)name;
    /* This space deliberately left blank */
}

// Listener getters

const struct zwlr_layer_surface_v1_listener* wayland_events_get_layer_surface_listener(void) {
    return &layer_surface_listener;
}

const struct wl_surface_listener* wayland_events_get_surface_listener(void) {
    return &wl_surface_listener;
}

const struct wl_output_listener* wayland_events_get_output_listener(void) {
    return &wl_output_listener;
}

const struct wl_registry_listener* wayland_events_get_registry_listener(void) {
    return &registry_listener;
}

// Reconnection handler

bool wayland_events_handle_reconnection(struct lyrics_state *state,
        struct wayland_connection *wl_conn, struct pollfd *pollfd) {
    // Mark that we're reconnecting (to ignore layer_surface_closed events)
    state->reconnecting = true;

    // Clean up old buffers before reconnecting
    // (they were created with the old wl_shm)
    for (int i = 0; i < 2; i++) {
        if (state->buffers[i].buffer) {
            destroy_buffer(&state->buffers[i]);
        }
    }
    state->current_buffer = NULL;

    if (!wayland_manager_reconnect_full(wl_conn, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
            "lyrics", state->anchor, state->margin)) {
        log_error("Full reconnection failed, will retry...");
        state->reconnecting = false;
        return false;
    }

    // Update state pointers
    state->display = wl_conn->display;
    state->registry = wl_conn->registry;
    state->compositor = wl_conn->compositor;
    state->shm = wl_conn->shm;
    state->layer_shell = wl_conn->layer_shell;
    state->surface = wl_conn->surface;
    state->layer_surface = wl_conn->layer_surface;

    // Add listeners to new surfaces
    wl_surface_add_listener(state->surface, &wl_surface_listener, state);
    zwlr_layer_surface_v1_add_listener(state->layer_surface,
            &layer_surface_listener, state);

    // Commit the surface
    wl_surface_commit(state->surface);

    // Wait for configure event
    int retry = 0;
    state->width = state->height = 0;
    while ((state->width == 0 || state->height == 0) && retry < 10) {
        if (wl_display_roundtrip(state->display) == -1) {
            log_warn("Roundtrip failed, compositor may not be available yet");
            state->reconnecting = false;
            return false;
        }
        retry++;
    }

    if (state->width == 0 || state->height == 0) {
        log_warn("Layer surface configuration failed after reconnection (compositor not ready)");
        state->reconnecting = false;
        return false;
    }

    // Update pollfd with new display fd
    pollfd->fd = wl_display_get_fd(wl_conn->display);

    state->reconnecting = false;
    log_info("Successfully reconnected - overlay should be visible again");
    rendering_manager_set_dirty(state);
    return true;
}
