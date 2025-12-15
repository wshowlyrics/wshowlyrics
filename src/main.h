#ifndef _LYRICS_MAIN_H
#define _LYRICS_MAIN_H

/* System headers */
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* External library headers */
#include <cairo/cairo.h>
#include <fontconfig/fontconfig.h>
#include <wayland-client.h>

/* Protocol headers */
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* Project headers */
#include "utils/pango/pango_utils.h"
#include "utils/shm/shm.h"
#include "utils/mpris/mpris.h"
#include "parser/lrc/lrc_parser.h"
#include "parser/srt/srt_parser.h"
#include "parser/lrcx/lrcx_parser.h"
#include "provider/lyrics/lyrics_provider.h"
#include "user_experience/system_tray/system_tray.h"
#include "utils/file/file_utils.h"

/* Forward declarations */
struct lyrics_output;
struct lyrics_state;
struct wayland_connection;

struct lyrics_output {
    struct wl_output *output;
    int scale, width, height;
    enum wl_output_subpixel subpixel;
    struct lyrics_output *next;
};

struct lyrics_state {
    uint32_t foreground, background;
    const char *font;

    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    uint32_t width, height;
    bool frame_scheduled, dirty;
    struct pool_buffer buffers[2];
    struct pool_buffer *current_buffer;
    struct lyrics_output *output, *outputs;

    struct lyrics_data lyrics;
    struct track_metadata current_track;
    struct lyrics_line *current_line;
    struct word_segment *current_segment; // For karaoke highlighting (LRCX)
    int64_t track_start_time_us; // When the track started (monotonic clock)
    bool track_changed;
    bool in_instrumental_break; // True when in instrumental break (no lyrics)
    bool need_lyrics_search; // True when lyrics file was missing and needs re-search

    // Config file hot reload tracking
    char *config_file_path; // Path to loaded config file
    char config_md5_checksum[33]; // MD5 checksum of config file (32 hex chars + null)

    bool run;
    bool needs_reconnect; // Set when layer surface is closed
    bool reconnecting; // Set during reconnection to ignore layer_surface_closed
    struct wayland_connection *wl_conn; // Wayland connection manager

    // Surface configuration for reinitialization
    uint32_t anchor;
    int32_t margin;
};

/* Function prototypes */
static cairo_subpixel_order_t to_cairo_subpixel_order(enum wl_output_subpixel subpixel);
static void render_to_cairo(cairo_t *cairo, struct lyrics_state *state,
        int scale, uint32_t *width, uint32_t *height);
static void render_transparent_frame(struct lyrics_state *state);
static void render_frame(struct lyrics_state *state);
static void set_dirty(struct lyrics_state *state);

/* Wayland listener callbacks */
static void layer_surface_configure(void *data,
        struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1,
        uint32_t serial, uint32_t width, uint32_t height);
static void layer_surface_closed(void *data,
        struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1);
static void surface_enter(void *data,
        struct wl_surface *wl_surface, struct wl_output *output);
static void surface_leave(void *data,
        struct wl_surface *wl_surface, struct wl_output *output);

/* Output event callbacks */
static void output_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
        int32_t subpixel, const char *make, const char *model,
        int32_t transform);
static void output_mode(void *data, struct wl_output *wl_output,
        uint32_t flags, int32_t width, int32_t height, int32_t refresh);
static void output_done(void *data, struct wl_output *wl_output);
static void output_scale(void *data, struct wl_output *wl_output, int32_t factor);

/* Registry event callbacks */
static void registry_global(void *data, struct wl_registry *wl_registry,
        uint32_t name, const char *interface, uint32_t version);
static void registry_global_remove(void *data,
        struct wl_registry *wl_registry, uint32_t name);

/* Utility functions */
static uint32_t parse_color(const char *color);
static bool update_track_info(struct lyrics_state *state);
static bool load_lyrics_for_track(struct lyrics_state *state);

/* Main function */
int main(int argc, char *argv[]);

#endif /* _LYRICS_MAIN_H */
