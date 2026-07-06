#ifndef _LYRICS_MAIN_H
#define _LYRICS_MAIN_H

/* System headers */
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
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
#include "lyrics_types.h"
#include "utils/pango/pango_utils.h"
#include "utils/shm/shm.h"
#include "utils/mpris/mpris.h"
#include "parser/lrc/lrc_parser.h"
#include "parser/lrc/lrcx_parser.h"
#include "parser/srt/srt_parser.h"
#include "provider/lyrics/lyrics_provider.h"
#include "user_experience/system_tray/system_tray.h"
#include "user_experience/config/config.h"
#include "utils/file/file_utils.h"

/* Forward declarations */
struct lyrics_output;
struct lyrics_state;
struct wayland_connection;

struct lyrics_output {
    struct wl_output *output;
    int scale;
    int width;
    int height;
    enum wl_output_subpixel subpixel;
    struct lyrics_output *next;
};

// Visual style: colors and font
struct style {
    uint32_t foreground;
    uint32_t background;
    const char *font;
};

// Surface / rendering state: dimensions, buffers, callbacks, layout config
struct surface_state {
    uint32_t width;
    uint32_t height;
    bool frame_scheduled;
    bool dirty;
    struct wl_callback *frame_callback;
    struct pool_buffer buffers[2];
    struct pool_buffer *current_buffer;
    struct lyrics_output *output;
    struct lyrics_output *outputs;
    uint32_t anchor;                              // Layer surface anchor
    int32_t margin;                               // Layer surface margin
    uint32_t layer;                               // wlr-layer-shell layer
};

// Runtime flags: lifecycle and behavior toggles
struct runtime {
    volatile sig_atomic_t run;                    // Cleared by the signal handler; must be async-safe
    bool needs_reconnect;                         // Set when layer surface is closed
    bool reconnecting;                            // Ignore layer_surface_closed during reconnect
    bool overlay_enabled;                         // D-Bus controlled: show/hide overlay
    bool need_lyrics_search;                      // Lyrics file was missing, needs re-search
};

struct lyrics_state {
    struct style style;
    struct surface_state surface;
    struct playback_state playback;
    struct config_state config;
    struct runtime runtime;
    struct wayland_connection *wl_conn;           // Wayland connection (R1: single source of truth)
};

/* Main function */
int main(int argc, char *argv[]);

#endif /* _LYRICS_MAIN_H */
