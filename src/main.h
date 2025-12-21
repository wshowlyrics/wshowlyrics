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
#include "parser/lrc/lrcx_parser.h"
#include "parser/srt/srt_parser.h"
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
    int current_line_index; // 0-based index of current_line in lyrics.lines (-1 if no current line)
    struct word_segment *current_segment; // For karaoke highlighting (LRCX)
    struct lyrics_line *prev_line;  // Previous line for multi-line display
    struct lyrics_line *next_line;  // Next line for multi-line display
    int64_t track_start_time_us; // When the track started (monotonic clock)
    bool track_changed;
    bool in_instrumental_break; // True when in instrumental break (no lyrics)
    bool need_lyrics_search; // True when lyrics file was missing and needs re-search

    // Config file hot reload tracking
    char *config_file_path; // Path to loaded config file
    char config_md5_checksum[33]; // MD5 checksum of config file (32 hex chars + null)

    // Timing offset for sync adjustment (milliseconds)
    int timing_offset_ms; // Runtime timing offset (-1000 to +1000 ms)
    int fifo_fd; // FIFO file descriptor for IPC commands

    bool run;
    bool needs_reconnect; // Set when layer surface is closed
    bool reconnecting; // Set during reconnection to ignore layer_surface_closed
    bool overlay_enabled; // FIFO controlled: show/hide overlay (default: true)
    struct wayland_connection *wl_conn; // Wayland connection manager

    // Surface configuration for reinitialization
    uint32_t anchor;
    int32_t margin;
};

/* Main function */
int main(int argc, char *argv[]);

#endif /* _LYRICS_MAIN_H */
