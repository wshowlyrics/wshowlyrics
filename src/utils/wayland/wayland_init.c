#include "wayland_init.h"
#include "../../events/wayland_events.h"
#include "../../constants.h"
#include "../../provider/lyrics/lyrics_provider.h"
#include "../../utils/mpris/mpris.h"
#include "../../user_experience/system_tray/system_tray.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fontconfig/fontconfig.h>

bool wayland_init_surface(struct lyrics_state *state, uint32_t layer, unsigned int anchor, int margin) {
    if (!state) {
        return false;
    }

    // Fontconfig initialization
    if (!FcInit()) {
        log_error("Failed to initialize fontconfig");
        return false;
    }

    log_info("Compositor: %s", getenv("WAYLAND_DISPLAY") ?: "wayland-0");
    log_info("Using compositor interfaces...");

    // Initialize lyrics providers
    lyrics_providers_init();

    // Initialize MPRIS for automatic lyrics detection
    if (!mpris_init()) {
        log_error("Failed to initialize MPRIS (DBus connection failed?)");
        return false;
    }
    log_info("MPRIS mode enabled - will track currently playing music");

    // Initialize system tray
    if (system_tray_init()) {
        system_tray_set_state(state);
        log_info("System tray initialized (album art display)");
    } else {
        log_warn("Failed to initialize system tray");
    }

    // Connect to Wayland display
    state->display = wl_display_connect(NULL);
    if (!state->display) {
        log_error("wl_display_connect: %s", strerror(errno));
        return false;
    }

    // Get registry and add listener
    state->registry = wl_display_get_registry(state->display);
    assert(state->registry);
    wl_registry_add_listener(state->registry, wayland_events_get_registry_listener(), state);
    wl_display_roundtrip(state->display);

    // Check for required globals
    const struct {
        const char *name;
        void *ptr;
    } need_globals[] = {
        {"wl_compositor", state->compositor},
        {"wl_shm", state->shm},
        {"wlr_layer_shell", state->layer_shell},
    };
    for (size_t i = 0; i < sizeof(need_globals) / sizeof(need_globals[0]); ++i) {
        if (!need_globals[i].ptr) {
            log_error("Required Wayland interface '%s' is not present", need_globals[i].name);
            return false;
        }
    }

    // Create surface and layer surface
    state->surface = wl_compositor_create_surface(state->compositor);
    assert(state->surface);

    state->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            state->layer_shell, state->surface, NULL,
            layer, "lyrics");
    assert(state->layer_surface);

    // Add listeners
    wl_surface_add_listener(state->surface, wayland_events_get_surface_listener(), state);
    zwlr_layer_surface_v1_add_listener(
            state->layer_surface, wayland_events_get_layer_surface_listener(), state);

    // Configure layer surface
    zwlr_layer_surface_v1_set_size(state->layer_surface, 1, 1);
    zwlr_layer_surface_v1_set_anchor(state->layer_surface, anchor);
    zwlr_layer_surface_v1_set_margin(state->layer_surface,
            margin, margin, margin, margin);
    zwlr_layer_surface_v1_set_exclusive_zone(state->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(state->layer_surface, 0);

    // Set empty input region to allow clicks to pass through
    struct wl_region *region = wl_compositor_create_region(state->compositor);
    wl_surface_set_input_region(state->surface, region);
    wl_region_destroy(region);

    wl_surface_commit(state->surface);

    // Wait for configure event
    int retry_count = 0;
    while ((state->width == 0 || state->height == 0) && retry_count < WAYLAND_CONFIGURE_RETRY_LIMIT) {
        wl_display_roundtrip(state->display);
        retry_count++;
    }

    retry_count = 0;
    while ((state->width == 0 || state->height == 0) && retry_count < WAYLAND_CONFIGURE_RETRY_LIMIT) {
        wl_display_dispatch(state->display);
        retry_count++;
    }

    if (state->width == 0 || state->height == 0) {
        log_error("Layer surface configuration failed");
        return false;
    }

    // Store surface configuration for reinitialization
    state->anchor = anchor;
    state->margin = margin;
    state->layer = layer;

    return true;
}
