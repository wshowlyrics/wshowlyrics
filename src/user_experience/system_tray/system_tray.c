// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "system_tray.h"
#include "../../main.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/thread/thread_cancel.h"
#include "../../utils/runtime/runtime_dir.h"
#include "../../provider/itunes/itunes_artwork.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/icon/icon_utils.h"
#include "../../utils/url/url_utils.h"
#include "../../core/rendering/rendering_manager.h"
#include "../config/config.h"
#include <stdio.h>
#include "../../constants.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libappindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <curl/curl.h>
#include <cairo/cairo.h>

// State
static AppIndicator *indicator = NULL;
static GtkWidget *menu = NULL;
// The art URL of the last download, and the track hash whose cache file holds
// its image — so the next track with the same URL can copy it (see
// reuse_art_from_same_url).
static char *last_art_url = NULL;
static char last_art_url_hash[MD5_DIGEST_STRING_LENGTH] = {0};
static char last_metadata_hash[MD5_DIGEST_STRING_LENGTH] = {0};

// Menu items that need dynamic updates
static GtkWidget *track_info_item = NULL;
static GtkWidget *overlay_item = NULL;
static GtkWidget *edit_settings_item = NULL;
static struct lyrics_state *g_tray_state = NULL;

// Maximum display length for track info (UTF-8 characters, not bytes)
#define TRACK_INFO_MAX_CHARS 10

// Icon paths (initialized from runtime dir)
static char g_icon_dir[512] = {0};
static char g_icon_path[600] = {0};
static char g_notification_icon_path[600] = {0};
static char g_disabled_icon_path[600] = {0};
static const char *ICON_NAME = "album-art";

// Initialize icon paths from runtime directory
static void init_icon_paths(void) {
    const char *runtime = get_runtime_dir();
    snprintf(g_icon_dir, sizeof(g_icon_dir), "%s", runtime);
    snprintf(g_icon_path, sizeof(g_icon_path), "%s/album-art.png", runtime);
    snprintf(g_notification_icon_path, sizeof(g_notification_icon_path), "%s/notification-icon.png", runtime);
    snprintf(g_disabled_icon_path, sizeof(g_disabled_icon_path), "%s/disabled-icon.png", runtime);
}

// Album art is a small square image; cap downloads well above any realistic
// cover size to bound memory if a rogue art source streams unbounded data.
#define MAX_ARTWORK_DOWNLOAD_BYTES (8 * 1024 * 1024)

// Accept only common, well-tested raster image formats by sniffing the actual
// bytes. art_url may come from a rogue MPRIS player, and the file extension is
// meaningless here (the cache is always re-encoded to .png regardless of the
// source format). This covers every format real cover art ships as (JPEG/PNG/
// WebP/GIF/BMP) while still keeping bytes out of gdk-pixbuf's more obscure,
// historically CVE-prone loaders (ICO/ANI/XPM/TGA/...) and the SVG loader
// (XML/external-resource surface), which album art never legitimately uses.
static bool is_allowed_image_format(const unsigned char *data, size_t len) {
    if (!data) {
        return false;
    }
    if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return true;  // JPEG
    }
    if (len >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E &&
        data[3] == 0x47 && data[4] == 0x0D && data[5] == 0x0A &&
        data[6] == 0x1A && data[7] == 0x0A) {
        return true;  // PNG
    }
    if (len >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
        return true;  // WebP
    }
    if (len >= 6 && memcmp(data, "GIF8", 4) == 0 &&
        (data[4] == '7' || data[4] == '9') && data[5] == 'a') {
        return true;  // GIF (87a / 89a)
    }
    if (len >= 2 && data[0] == 'B' && data[1] == 'M') {
        return true;  // BMP
    }
    return false;
}

// Download image from URL to memory. The body is limited to
// MAX_ARTWORK_DOWNLOAD_BYTES, announced or not (see curl_write_to_memory).
static bool download_image(const char *url, struct curl_memory_buffer *buffer) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    curl_memory_buffer_init(buffer);
    buffer->max_size = MAX_ARTWORK_DOWNLOAD_BYTES;

    if (!curl_setup_get(curl, url, buffer)) {
        log_error("system_tray: Failed to set CURL options");
        curl_easy_cleanup(curl);
        return false;
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        curl_memory_buffer_free(buffer);
        return false;
    }

    return true;
}

// Check if image is approximately square (allow small aspect ratio differences)
// Most people cannot perceive aspect ratio differences below ~5%
static bool is_square_image(GdkPixbuf *const pixbuf) {
    if (!pixbuf) {
        return false;
    }

    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);

    if (width <= 0 || height <= 0) {
        return false;
    }

    // Allow 5% tolerance for aspect ratio (e.g., 100x95, 95x100 accepted)
    float ratio = (float)width / (float)height;
    return (ratio >= 0.95f && ratio <= 1.05f);
}

// Centre-crop a non-square pixbuf to a square (side = shorter edge). Used so a
// rectangular remote thumbnail — e.g. a 480x360 YouTube cover exposed via
// mpris:artUrl — becomes a usable square tray icon instead of being discarded.
// Returns a new pixbuf the caller owns, or NULL on failure.
static GdkPixbuf* crop_to_square(GdkPixbuf *const pixbuf) {
    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);
    int side = width < height ? width : height;
    int x = (width - side) / 2;
    int y = (height - side) / 2;

    // new_subpixbuf shares the source pixels (and holds a ref on it); copy to an
    // independent buffer so the square survives after the source is unref'd.
    GdkPixbuf *view = gdk_pixbuf_new_subpixbuf(pixbuf, x, y, side, side);
    if (!view) {
        return NULL;
    }
    GdkPixbuf *square = gdk_pixbuf_copy(view);
    g_object_unref(view);
    return square;
}

// Write pixbuf to path as a PNG readable and writable by the owner only. The
// image is encoded in memory and written through fopen_private(), so no umask
// toggle is needed (see fopen_private) and it is safe to call from the artwork
// worker thread. The file appears under its name complete or not at all
// (fopen_private_tmp): a dropped artwork worker and its successor can write
// the same {md5}.png while the main thread reads it as a cache hit (#66).
// On failure returns false and sets *error (if given).
static bool save_png_private(GdkPixbuf *pixbuf, const char *path, GError **error) {
    gchar *data = NULL;
    gsize size = 0;
    if (!gdk_pixbuf_save_to_buffer(pixbuf, &data, &size, "png", error, NULL)) {
        return false;
    }

    char tmp_path[PATH_MAX];
    FILE *f = fopen_private_tmp(path, tmp_path, sizeof(tmp_path));
    bool ok = (f != NULL);
    if (f) {
        fwrite(data, 1, size, f);   // a short write shows in fclose_into_place
        ok = fclose_into_place(f, tmp_path, path);
    }
    if (!ok) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "cannot write %s: %s", path, g_strerror(errno));
    }
    g_free(data);
    return ok;
}

// Save default icon from theme to file
static bool save_default_icon(const char *output_path) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    GError *error = NULL;

    // Try to load audio-player icon from theme
    GdkPixbuf *icon = gtk_icon_theme_load_icon(icon_theme, "audio-player", 48, 0, &error);

    if (!icon) {
        // Fallback to audio-headphones or multimedia-player
        g_clear_error(&error);
        icon = gtk_icon_theme_load_icon(icon_theme, "audio-headphones", 48, 0, &error);
    }

    if (!icon) {
        // Final fallback to multimedia-player
        g_clear_error(&error);
        icon = gtk_icon_theme_load_icon(icon_theme, "multimedia-player", 48, 0, &error);
    }

    if (!icon) {
        log_error("Failed to load default icon from theme: %s",
                  error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return false;
    }

    // Save to file (owner-only access for privacy)
    g_clear_error(&error);
    bool save_result = save_png_private(icon, output_path, &error);
    if (!save_result) {
        log_error("Failed to save default icon: %s", error->message);
        g_error_free(error);
        g_object_unref(icon);
        return false;
    }

    g_object_unref(icon);
    log_info("Default icon saved to: %s", output_path);
    return true;
}

// Decode an in-memory image, enforcing the format allowlist. Critically, the
// magic-byte check and the decode read the SAME buffer, so a rogue source
// cannot swap the underlying file between validation and decode (TOCTOU) — the
// only bytes gdk-pixbuf ever sees are the ones we already vetted. Returns a new
// reference (caller unrefs) or NULL.
static GdkPixbuf* decode_allowed_image(const unsigned char *data, size_t size) {
    if (!is_allowed_image_format(data, size)) {
        log_warn("system_tray: artwork is not an allowed image format, ignoring");
        return NULL;
    }

    GError *error = NULL;
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    if (!gdk_pixbuf_loader_write(loader, (const guchar *)data, size, &error)) {
        log_error("Failed to load image data: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        g_object_unref(loader);
        return NULL;
    }

    GError *close_error = NULL;
    if (!gdk_pixbuf_loader_close(loader, &close_error) && close_error) {
        log_warn("Failed to close pixbuf loader: %s", close_error->message);
        g_error_free(close_error);
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pixbuf) {
        g_object_ref(pixbuf);
    }
    g_object_unref(loader);
    return pixbuf;
}

// Read a vetted regular file fully into memory (bounded). Returns a malloc'd
// buffer (caller frees) and stores the length in *out_len, or NULL on error.
static unsigned char* read_art_file(const char *resolved, size_t *out_len) {
    // Open first, then validate the *open descriptor* with fstat(). Checking the
    // path with stat() and opening it in a separate step is a TOCTOU window (the
    // path could be swapped in between); fstat on the fd we actually read closes
    // it. O_NONBLOCK keeps open() from blocking if the path is a FIFO, O_CLOEXEC
    // avoids leaking the fd across any future exec.
    int fd = open(resolved, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        log_warn("system_tray: cannot open art file, ignoring");
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        log_warn("system_tray: art file is not a regular file, ignoring");
        close(fd);
        return NULL;
    }
    if (st.st_size <= 0 || (uintmax_t)st.st_size > MAX_ARTWORK_DOWNLOAD_BYTES) {
        log_warn("system_tray: art file size out of range, ignoring");
        close(fd);
        return NULL;
    }

    FILE *fp = fdopen(fd, "rb");
    if (!fp) {
        close(fd);
        return NULL;
    }

    size_t cap = (size_t)st.st_size;
    unsigned char *data = malloc(cap);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    size_t nread = fread(data, 1, cap, fp);
    fclose(fp);

    *out_len = nread;
    return data;
}

// Load image from URL (http:// or file://)
static GdkPixbuf* load_image_from_url(const char *url) {
    GdkPixbuf *pixbuf = NULL;

    // Handle file:// URLs
    if (strncmp(url, "file://", 7) == 0) {
        const char *file_path = url + 7;

        // art_url may come from a rogue MPRIS player. Canonicalize (resolving
        // symlinks and ..) and require a regular file so a device/FIFO/pseudo-
        // file is never opened (CWE-73). Read the bytes once, then sniff and
        // decode from that same copy so the file cannot be swapped between the
        // format check and the decode.
        //
        // A file:// URI is percent-encoded ("My%20Album/cover.jpg"), so decode
        // it first. Fall back to the text as given for players that pass a raw
        // path, where a literal "%41" in a file name must not turn into "A".
        char resolved[PATH_MAX];
        char *decoded = url_decode_string(file_path);
        const bool found = (decoded && realpath(decoded, resolved)) ||
                           realpath(file_path, resolved);
        free(decoded);
        if (!found) {
            log_warn("system_tray: cannot resolve art file path, ignoring");
            return NULL;
        }
        // A dropped lookup may only now be coming back from a file system
        // that had stopped answering; it does not go on to read the file
        if (thread_cancel_requested()) {
            return NULL;
        }

        size_t data_len = 0;
        unsigned char *data = read_art_file(resolved, &data_len);
        if (!data) {
            return NULL;
        }

        pixbuf = decode_allowed_image(data, data_len);
        free(data);
    }
    // Handle http:// or https:// URLs
    else if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        struct curl_memory_buffer buffer;
        curl_memory_buffer_init(&buffer);

        if (!download_image(url, &buffer)) {
            log_error("Failed to download image from URL");
            return NULL;
        }

        // art_url is untrusted; decode_allowed_image sniffs the format and
        // decodes from the same downloaded buffer.
        pixbuf = decode_allowed_image((const unsigned char *)buffer.data, buffer.size);
        curl_memory_buffer_free(&buffer);
    }

    // A tray icon must be square. Rather than discard a rectangular image
    // (which would drop e.g. a 480x360 YouTube thumbnail from mpris:artUrl and
    // fall back to the default icon), centre-crop it to a square. Images already
    // within the 5% tolerance are left untouched.
    if (pixbuf && !is_square_image(pixbuf)) {
        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);
        GdkPixbuf *square = crop_to_square(pixbuf);
        g_object_unref(pixbuf);
        if (!square) {
            log_warn("Failed to centre-crop %dx%d image, using default icon", width, height);
            return NULL;
        }
        log_info("Centre-cropped %dx%d image to square for tray icon", width, height);
        pixbuf = square;
    }

    return pixbuf;
}

// Apply circular mask to a pixbuf (CD-like appearance)
static GdkPixbuf* apply_circular_mask(GdkPixbuf *const pixbuf) {
    if (!pixbuf) {
        return NULL;
    }

    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);

    // Create Cairo surface from pixbuf
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    // Create circular clipping path
    double center_x = width / 2.0;
    double center_y = height / 2.0;
    double radius = (width < height ? width : height) / 2.0;

    cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
    cairo_clip(cr);

    // Draw the original pixbuf
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
    cairo_paint(cr);

    // Convert Cairo surface back to GdkPixbuf
    cairo_surface_flush(surface);

    GdkPixbuf *rounded = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);

    // Cleanup
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return rounded;
}

// Truncate UTF-8 string to max_chars characters, adding "..." if truncated
// Returns newly allocated string (caller must free)
static char* truncate_utf8(const char *str, int max_chars) {
    if (!str) {
        return strdup("Unknown");
    }

    glong len = g_utf8_strlen(str, -1);
    if (len <= max_chars) {
        return strdup(str);
    }

    // Find position of max_chars-th character
    const char *end = g_utf8_offset_to_pointer(str, max_chars);
    size_t byte_len = end - str;

    // Allocate space for truncated string + "..." + null
    char *result = malloc(byte_len + 4);
    if (!result) {
        return strdup(str);
    }

    memcpy(result, str, byte_len);
    memcpy(result + byte_len, "...", 4);  // includes null terminator

    return result;
}

// Menu callbacks
static void on_overlay_toggled(GtkCheckMenuItem *const item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (!g_tray_state) {
        return;
    }

    g_tray_state->runtime.overlay_enabled = !g_tray_state->runtime.overlay_enabled;
    system_tray_set_overlay_state(g_tray_state->runtime.overlay_enabled);
    rendering_manager_set_dirty(g_tray_state);

    log_info("Menu: Overlay %s", g_tray_state->runtime.overlay_enabled ? "enabled" : "disabled");
}

static void on_timing_adjust(GtkMenuItem *const item, gpointer user_data) {
    (void)item;
    int delta = GPOINTER_TO_INT(user_data);

    if (!g_tray_state) {
        return;
    }

    if (delta == 0) {
        // Reset to global offset
        g_tray_state->playback.timing_offset_ms = g_config.lyrics.global_offset_ms;
        log_info("Menu: Timing offset reset to %dms", g_tray_state->playback.timing_offset_ms);
    } else {
        g_tray_state->playback.timing_offset_ms += delta;
        // Clamp to reasonable range
        if (g_tray_state->playback.timing_offset_ms < -5000) g_tray_state->playback.timing_offset_ms = -5000;
        if (g_tray_state->playback.timing_offset_ms > 5000) g_tray_state->playback.timing_offset_ms = 5000;
        log_info("Menu: Timing offset adjusted by %+dms, now: %dms", delta, g_tray_state->playback.timing_offset_ms);
    }

    rendering_manager_set_dirty(g_tray_state);
}

static void on_edit_settings(GtkMenuItem *const item, gpointer user_data) {
    (void)item;
    (void)user_data;

    const char *editor = getenv("EDITOR");
    const char *terminal = getenv("TERMINAL");

    if (!editor || !terminal) {
        return;
    }

    // Build config path
    char config_path[512];
    const char *home = getenv("HOME");
    if (!home) {
        return;
    }
    snprintf(config_path, sizeof(config_path), "%s/.config/wshowlyrics/settings.ini", home);

    log_info("Menu: Opening settings with: %s -e %s \"%s\"",
             terminal, editor, sanitize_path(config_path));

    char *term_arg = strdup(terminal);
    char *editor_arg = strdup(editor);
    if (!term_arg || !editor_arg) {
        free(term_arg);
        free(editor_arg);
        return;
    }
    char *argv[] = { term_arg, "-e", editor_arg, config_path, NULL };
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        log_warn("Menu: Failed to open editor: %s", error->message);
        g_error_free(error);
    }
    free(term_arg);
    free(editor_arg);
}

static void on_quit(GtkMenuItem *const item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (g_tray_state) {
        g_tray_state->runtime.run = false;
        log_info("Menu: Quit requested");
    }
}

// Create context menu
static GtkWidget* create_menu(void) {
    GtkWidget *tray_menu = gtk_menu_new();

    // Track info (disabled, info display only)
    track_info_item = gtk_menu_item_new_with_label("♪ No track");
    gtk_widget_set_sensitive(track_info_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), track_info_item);

    // Separator
    gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), gtk_separator_menu_item_new());

    // Show Overlay (checkbox)
    overlay_item = gtk_check_menu_item_new_with_label("Show Overlay");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overlay_item), TRUE);
    g_signal_connect(overlay_item, "toggled", G_CALLBACK(on_overlay_toggled), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), overlay_item);

    // Timing Offset submenu
    GtkWidget *timing_item = gtk_menu_item_new_with_label("Timing Offset");
    GtkWidget *timing_submenu = gtk_menu_new();

    GtkWidget *timing_plus = gtk_menu_item_new_with_label("+100ms");
    g_signal_connect(timing_plus, "activate", G_CALLBACK(on_timing_adjust), GINT_TO_POINTER(100));
    gtk_menu_shell_append(GTK_MENU_SHELL(timing_submenu), timing_plus);

    GtkWidget *timing_minus = gtk_menu_item_new_with_label("-100ms");
    g_signal_connect(timing_minus, "activate", G_CALLBACK(on_timing_adjust), GINT_TO_POINTER(-100));
    gtk_menu_shell_append(GTK_MENU_SHELL(timing_submenu), timing_minus);

    GtkWidget *timing_reset = gtk_menu_item_new_with_label("Reset");
    g_signal_connect(timing_reset, "activate", G_CALLBACK(on_timing_adjust), GINT_TO_POINTER(0));
    gtk_menu_shell_append(GTK_MENU_SHELL(timing_submenu), timing_reset);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(timing_item), timing_submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), timing_item);

    // Separator
    gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), gtk_separator_menu_item_new());

    // Edit Settings (only if $EDITOR and $TERMINAL are set)
    const char *editor = getenv("EDITOR");
    const char *terminal = getenv("TERMINAL");
    if (editor && editor[0] != '\0' && terminal && terminal[0] != '\0') {
        edit_settings_item = gtk_menu_item_new_with_label("Edit Settings");
        g_signal_connect(edit_settings_item, "activate", G_CALLBACK(on_edit_settings), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), edit_settings_item);

        // Separator
        gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), gtk_separator_menu_item_new());
    }

    // Quit
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), quit_item);

    gtk_widget_show_all(tray_menu);

    log_info("Context menu created");

    return tray_menu;
}

bool system_tray_init(void) {
    // Initialize icon paths from runtime directory
    init_icon_paths();

    // Initialize GTK if not already initialized
    if (!gtk_init_check(NULL, NULL)) {
        log_error("Failed to initialize GTK");
        return false;
    }

    // Initialize CURL globally
    CURLcode curl_res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curl_res != CURLE_OK) {
        log_error("Failed to initialize CURL: %s", curl_easy_strerror(curl_res));
        return false;
    }

    // Create AppIndicator
    indicator = app_indicator_new(
        "lyrics-indicator",
        "audio-player",  // Default icon name
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );

    if (!indicator) {
        log_error("Failed to create AppIndicator");
        return false;
    }

    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);

    // Create minimal menu (required by AppIndicator)
    menu = create_menu();
    app_indicator_set_menu(indicator, GTK_MENU(menu));

    // Set initial default icon
    log_info("Setting initial default icon");
    if (mkdir(g_icon_dir, 0700) != 0 && errno != EEXIST) {
        log_warn("Failed to create icon directory: %s", strerror(errno));
    }
    if (save_default_icon(g_icon_path)) {
        app_indicator_set_icon_theme_path(indicator, g_icon_dir);
        app_indicator_set_icon_full(indicator, ICON_NAME, "Music Player");
        log_info("Initial icon set to default");
    } else {
        log_warn("Could not save initial default icon, using system icon");
    }

    return true;
}

// Render an image URL (http/https or file://) to a PNG at out_path with the
// standard 48x48 + circular-mask processing. Pure download/decode/pixbuf/save
// with NO AppIndicator calls, so it is safe to run on a worker thread. The
// caller must ensure out_path's directory exists.
static bool render_art_to_cache(const char *art_url, const char *out_path) {
    if (!art_url || !out_path) {
        return false;
    }

    GdkPixbuf *pixbuf = load_image_from_url(art_url);
    if (!pixbuf) {
        return false;
    }

    // Scale to reasonable size (48x48)
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 48, 48, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);
    if (!scaled) {
        return false;
    }

    // Apply circular mask (CD-like appearance)
    GdkPixbuf *rounded = apply_circular_mask(scaled);
    g_object_unref(scaled);
    if (!rounded) {
        log_error("Failed to apply circular mask");
        return false;
    }

    // Save as PNG (owner-only access for privacy)
    GError *error = NULL;
    bool save_result = save_png_private(rounded, out_path, &error);
    g_object_unref(rounded);
    if (!save_result) {
        log_error("Failed to save icon: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        return false;
    }

    return true;
}

void system_tray_reset_icon(void) {
    if (!indicator) {
        return;
    }

    log_info("Resetting tray icon to default");

    // Clear the metadata hash to force a reload on the next update. The art
    // URL record stays: it names a cache file, which is still that URL's image.
    last_metadata_hash[0] = '\0';

    // Create directory if it doesn't exist
    if (mkdir(g_icon_dir, 0700) != 0 && errno != EEXIST) {
        log_warn("Failed to create icon directory: %s", strerror(errno));
    }

    // Save default icon to file
    if (!save_default_icon(g_icon_path)) {
        log_warn("Could not save default icon");
        // Fallback: use system icon name without file
        app_indicator_set_icon_theme_path(indicator, NULL);
        app_indicator_set_icon_full(indicator, "audio-player", "Music Player");
        return;
    }

    // Set the icon theme path to our directory
    app_indicator_set_icon_theme_path(indicator, g_icon_dir);

    // Update indicator icon
    app_indicator_set_icon_full(indicator, ICON_NAME, "Music Player");

    log_info("Icon reset to default: %s", g_icon_path);
}

// How long to wait for the notification server to list its capabilities
#define NOTIFY_CAPABILITIES_TIMEOUT_MS 1000

// Whether the notification server renders markup in the body ("body-markup"
// capability of org.freedesktop.Notifications). Asked once and remembered.
// Servers with it treat <b>, <a href> and "&" in the body as markup; servers
// without it show the text as is. If the server cannot be asked, assume markup:
// escaping for a plain-text server only costs a literal "&amp;", while leaving
// metadata unescaped for a markup server lets it inject links or break the text.
static bool notification_body_is_markup(void) {
    static bool asked = false;
    static bool markup = true;
    if (asked) {
        return markup;
    }
    asked = true;

    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!bus) {
        return markup;
    }
    GVariant *reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "GetCapabilities", NULL,
        G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE,
        NOTIFY_CAPABILITIES_TIMEOUT_MS, NULL, NULL);
    g_object_unref(bus);
    if (!reply) {
        return markup;
    }

    const gchar **capabilities = NULL;
    g_variant_get(reply, "(^a&s)", &capabilities);
    markup = capabilities && g_strv_contains(capabilities, "body-markup");
    g_free(capabilities);
    g_variant_unref(reply);

    log_info("Notification server %s body markup", markup ? "renders" : "does not render");
    return markup;
}

// Copy of text for the notification body: markup-escaped when the server
// renders markup, so metadata is always shown as the text it is. Caller g_free()s.
static char* notification_body_text(const char *text) {
    return notification_body_is_markup() ? g_markup_escape_text(text, -1) : g_strdup(text);
}

// Helper: Build notification body with artist, album, and player.
// Returns a newly allocated string the caller g_free()s.
static char* build_notification_body(const struct notification_info *info) {
    const char *artist = (info->artist && info->artist[0]) ? info->artist : "Unknown";
    const char *album = (info->album && info->album[0]) ? info->album : "Unknown";
    const char *player = (info->player_name && info->player_name[0]) ? info->player_name : "Unknown";

    // Capitalize first letter of player name
    char player_capitalized[256];
    snprintf(player_capitalized, sizeof(player_capitalized), "%s", player);
    if (player_capitalized[0] >= 'a' && player_capitalized[0] <= 'z') {
        player_capitalized[0] = player_capitalized[0] - 'a' + 'A';
    }

    // These come from MPRIS metadata (with --allow-browsers, from a web page)
    char *album_text = notification_body_text(album);
    char *artist_text = notification_body_text(artist);
    char *player_text = notification_body_text(player_capitalized);
    char *body = g_strdup_printf("%s · %s\n%s", album_text, artist_text, player_text);
    g_free(album_text);
    g_free(artist_text);
    g_free(player_text);
    return body;
}

// Helper: Create badged notification icon (album art + player badge)
// Returns: icon path to use (NOTIFICATION_g_icon_path if successful, g_icon_path otherwise)
static const char* create_badged_icon(const char *player_display,
                                     const char *notification_icon_path) {
    GError *error = NULL;
    GdkPixbuf *album_art = gdk_pixbuf_new_from_file(g_icon_path, &error);

    if (!album_art) {
        log_warn("Failed to load album art for notification: %s",
                error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return g_icon_path;
    }

    // Load player icon and add badge
    GdkPixbuf *player_icon = icon_utils_load_player_icon(player_display, 16);
    GdkPixbuf *badged = icon_utils_add_badge(album_art, player_icon);

    if (badged) {
        // Save badged image
        GError *save_error = NULL;
        if (save_png_private(badged, notification_icon_path, &save_error)) {
            log_info("Created notification icon with %s badge", player_display);
            g_object_unref(badged);
            if (player_icon) g_object_unref(player_icon);
            g_object_unref(album_art);
            return notification_icon_path;
        } else {
            log_warn("Failed to save notification icon: %s",
                    save_error ? save_error->message : "unknown error");
            if (save_error) g_error_free(save_error);
        }
        g_object_unref(badged);
    }

    if (player_icon) g_object_unref(player_icon);
    g_object_unref(album_art);
    return g_icon_path;
}

// Helper: Prepare notification icon (album art with badge, or player icon)
static const char* prepare_notification_icon(const char *player_display,
                                            const char *notification_icon_path) {
    struct stat st;
    bool has_album_art = (stat(g_icon_path, &st) == 0);

    if (has_album_art && player_display && strcmp(player_display, "Unknown") != 0) {
        // Album art exists - add player badge
        return create_badged_icon(player_display, notification_icon_path);
    } else if (!has_album_art) {
        // No album art - use player-specific icon
        const char *player_icon_name = icon_utils_get_player_icon_name(player_display);
        log_info("Using player icon '%s' for notification", player_icon_name);
        return player_icon_name;
    }

    return g_icon_path;
}

void system_tray_send_notification(const struct notification_info *info) {
    if (!info || !info->title) {
        return;
    }

    // Build notification title: "🎵 Title"
    char notification_title[512];
    snprintf(notification_title, sizeof(notification_title), "🎵 %s", info->title);

    // Build notification body
    char *body = build_notification_body(info);

    // Get player display name for logging
    const char *player_display = (info->player_name && info->player_name[0]) ? info->player_name : "Unknown";

    // Prepare notification icon
    const char *icon_path = prepare_notification_icon(player_display, g_notification_icon_path);

    // Log notification
    const char *artist = (info->artist && info->artist[0]) ? info->artist : "Unknown";
    const char *album = (info->album && info->album[0]) ? info->album : "Unknown";
    log_info("Sending desktop notification: %s - %s · %s (%s)",
            notification_title, album, artist, player_display);

    // Execute notify-send via g_spawn_async (no shell interpretation).
    // "--" ends option parsing: the body starts with the album name, and an
    // album such as "-ERROR" would otherwise be read as an option, making
    // notify-send exit with "Unknown option" and no notification.
    char timeout_str[16];
    snprintf(timeout_str, sizeof(timeout_str), "%d", g_config.lyrics.notification_timeout);
    char *icon_arg = strdup(icon_path);
    if (!icon_arg) {
        g_free(body);
        return;
    }
    char *argv[] = {
        "notify-send", "-a", "wshowlyrics",
        "-i", icon_arg,
        "-e", "-t", timeout_str,
        "--", notification_title, body, NULL
    };
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        log_warn("notify-send failed: %s", error->message);
        g_error_free(error);
    }
    free(icon_arg);
    g_free(body);
}

// Point the tray indicator at the per-track album-art cache file, using the
// metadata md5 as the icon name. Because the icon name changes whenever the
// track changes, this forces the SNI host to invalidate its name-keyed icon
// cache and re-read the PNG — unlike the fixed ICON_NAME, which the host keeps
// cached even after album-art.png is overwritten on disk.
// cache_path is that file, {album_art_dir}/{md5}.png, and must exist.
static void set_indicator_icon_cached(const char *metadata_hash, const char *cache_path) {
    if (!indicator) {
        return;
    }

    // The theme directory is the cache path without "/{md5}.png"
    char theme_dir[512];
    snprintf(theme_dir, sizeof(theme_dir), "%s", cache_path);
    char *slash = strrchr(theme_dir, '/');
    if (!slash) {
        return;
    }
    *slash = '\0';

    // Icon name is the md5 (basename without extension); theme path is its dir
    app_indicator_set_icon_theme_path(indicator, theme_dir);
    app_indicator_set_icon_full(indicator, metadata_hash, "Album Art");

    log_info("Tray icon set from cache (name=%s)", metadata_hash);
}

// Resolve a file:// MPRIS URI to a canonical local regular-file path.
// Returns true and fills `resolved` (must be PATH_MAX) on success; false for a
// NULL/non-local URI, a decode failure, an unresolvable path, or a non-regular
// file. Canonicalizing and requiring S_ISREG keeps a device/FIFO/pseudo-file
// from ever being handed to ffmpeg (CWE-73), mirroring load_image_from_url.
static bool resolve_local_media_path(const char *file_url, char *resolved) {
    if (!file_url || strncmp(file_url, "file://", 7) != 0) {
        return false;
    }

    // MPRIS gives percent-encoded file:// URIs; decode to a real path first.
    char *decoded = url_decode_string(file_url + 7);
    if (!decoded) {
        return false;
    }

    bool ok = (realpath(decoded, resolved) != NULL);
    free(decoded);
    if (!ok) {
        log_warn("system_tray: cannot resolve local media path, skipping extraction");
        return false;
    }

    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
        log_warn("system_tray: local media path is not a regular file, skipping extraction");
        return false;
    }
    return true;
}

// Protocols ffmpeg may open while reading the local media file. The input is
// always a regular local file, so "file" is all it needs. Without this, a
// playlist-type input dressed up as a media file (HLS, concat) could make
// ffmpeg open other local files or network URLs.
#define FFMPEG_INPUT_PROTOCOLS "file"

// How long ffmpeg may run before it is killed, and how often the wait looks
// for its exit, the time limit and a cancel request.
#define FFMPEG_TIMEOUT_MS 10000
#define FFMPEG_POLL_MS 50

enum ffmpeg_result {
    FFMPEG_EXTRACTED,  // the image was written and rendered
    FFMPEG_NO_OUTPUT,  // ffmpeg ran to its end; the file has no such picture
    FFMPEG_STOPPED,    // ffmpeg was killed: cancel request or time limit
};

// Wait for the ffmpeg child without blocking past a cancel request or
// FFMPEG_TIMEOUT_MS; in either case the child is killed and reaped. Returns
// true, with wait_status filled, only when ffmpeg exited on its own. A file on
// a stalled network mount would otherwise keep the artwork worker and its
// ffmpeg alive indefinitely.
static bool wait_ffmpeg(GPid pid, int *wait_status) {
    const struct timespec slice = { .tv_sec = 0, .tv_nsec = FFMPEG_POLL_MS * 1000000L };
    int waited_ms = 0;
    pid_t reaped = waitpid(pid, wait_status, WNOHANG);
    while (reaped == 0 && waited_ms < FFMPEG_TIMEOUT_MS && !thread_cancel_requested()) {
        nanosleep(&slice, NULL);
        waited_ms += FFMPEG_POLL_MS;
        reaped = waitpid(pid, wait_status, WNOHANG);
    }
    if (reaped == pid) {
        return true;
    }
    if (reaped != 0) {
        log_warn("ffmpeg wait failed: %s", strerror(errno));
        return false;
    }

    if (waited_ms >= FFMPEG_TIMEOUT_MS) {
        log_warn("ffmpeg did not finish within %d ms, stopping it", FFMPEG_TIMEOUT_MS);
    }
    kill(pid, SIGKILL);
    do {
        reaped = waitpid(pid, wait_status, 0);
    } while (reaped < 0 && errno == EINTR);
    return false;
}

// Run ffmpeg with the given argument vector, which must write a PNG to
// out_path, and render that PNG into cache_path through the validated file://
// decode path. g_spawn_async runs ffmpeg with no shell interpretation so the
// resolved path can never be reinterpreted as arguments. ffmpeg exits non-zero
// when the requested stream is absent; it can also exit zero yet write nothing
// (e.g. a seek past end-of-file), so the exit code alone is not trusted: the
// decode path resolves the output, requires a regular file and reads it once,
// and fails on a missing or empty one. No stat() here - a separate check
// before that read would only be a check-then-use race (Coverity CID 1709922).
static enum ffmpeg_result run_ffmpeg_extract(char **argv, const char *out_path,
                                             const char *cache_path) {
    unlink(out_path);

    GPid pid = 0;
    GError *error = NULL;
    bool spawned = g_spawn_async(NULL, argv, NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                                     G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                                 NULL, NULL, &pid, &error);
    if (!spawned) {
        log_warn("ffmpeg spawn failed: %s", error ? error->message : "unknown");
        if (error) {
            g_error_free(error);
        }
        return FFMPEG_NO_OUTPUT;
    }

    int wait_status = 0;
    bool exited = wait_ffmpeg(pid, &wait_status);
    g_spawn_close_pid(pid);

    enum ffmpeg_result result = exited ? FFMPEG_NO_OUTPUT : FFMPEG_STOPPED;
    if (exited && WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0) {
        char out_url[700];
        snprintf(out_url, sizeof(out_url), "file://%s", out_path);
        if (render_art_to_cache(out_url, cache_path)) {
            result = FFMPEG_EXTRACTED;
        }
    }
    unlink(out_path);
    return result;
}

// Extract the cover art embedded in a local file. This is the offline fallback
// for players (e.g. mpv) that never expose mpris:artUrl even when the file on
// disk carries an embedded cover.
static enum ffmpeg_result extract_embedded_cover(char *ffmpeg, char *media_path,
                                                 char *out_path, const char *cache_path) {
    // The stream selection is deliberate: "-map 0:v -map -0:V" keeps all video
    // streams then removes the non-attached-picture ones, leaving ONLY the
    // attached cover art. A plain "-frames:v 1" would instead grab the first
    // frame of a real video stream (e.g. an .mkv), so a video file would yield a
    // random still rather than correctly reporting "no cover". With this mapping
    // a file that has no attached_pic produces no output stream and ffmpeg exits
    // non-zero, which we treat as "no embedded artwork".
    char *argv[] = {
        ffmpeg, "-y", "-loglevel", "error",
        "-protocol_whitelist", FFMPEG_INPUT_PROTOCOLS,
        "-i", media_path,
        "-map", "0:v", "-map", "-0:V",
        "-frames:v", "1",
        "-f", "image2", "-c:v", "png",
        out_path, NULL
    };

    enum ffmpeg_result result = run_ffmpeg_extract(argv, out_path, cache_path);
    if (result == FFMPEG_EXTRACTED) {
        log_info("Using embedded album art from local file");
    } else if (result == FFMPEG_NO_OUTPUT) {
        log_info("No embedded cover art in local file");
    }
    return result;
}

// Where in the video the thumbnail is taken from: this share of its length,
// rounded to a whole second, so the frame comes from the song itself rather
// than from the logo, title card or fade-in a music video opens with.
//
// The frame at that point is used as is. ffmpeg's `thumbnail` filter was tried
// instead: it picks, out of the next 100 frames (~4 s), the one closest to their
// average colour histogram. In a music video those 4 s span several cuts, so
// the pick is an arbitrary one of them rather than a "representative" frame.
#define VIDEO_THUMBNAIL_SEEK_PERCENT 20

// Seek offset (seconds) when the length is unknown: just enough to skip a
// black opening frame.
#define VIDEO_THUMBNAIL_FALLBACK_SEEK_SECONDS 3

// Grab a frame from a local video file, used only when the file has no
// embedded cover (e.g. a downloaded music-video .mkv). Selects the first real
// (non-attached-picture) video stream via "0:V:0" — an audio-only file matches
// no stream and ffmpeg exits non-zero, so this never fires for pure audio. The
// frame is centre-cropped to a square and scaled so it passes the tray's
// square-image requirement.
static enum ffmpeg_result extract_video_thumbnail(char *ffmpeg, char *media_path,
                                                  int64_t length_us, char *out_path,
                                                  const char *cache_path) {
    // The length comes from MPRIS (mpris:length), so no ffprobe run is needed
    int64_t seek_seconds = VIDEO_THUMBNAIL_FALLBACK_SEEK_SECONDS;
    if (length_us > 0) {
        // Microseconds at the chosen share, rounded to the nearest second
        seek_seconds = (length_us / 100 * VIDEO_THUMBNAIL_SEEK_PERCENT + 500000) / 1000000;
    }
    char seek_arg[32];
    snprintf(seek_arg, sizeof(seek_arg), "%lld", (long long)seek_seconds);

    // Filtergraph commas inside min(iw,ih) are escaped (\,) so ffmpeg does not
    // read them as filter separators. No shell is involved (g_spawn_async), so
    // the backslashes are passed literally as ffmpeg wants.
    char *argv[] = {
        ffmpeg, "-y", "-loglevel", "error",
        "-ss", seek_arg,
        "-protocol_whitelist", FFMPEG_INPUT_PROTOCOLS,
        "-i", media_path,
        "-map", "0:V:0", "-frames:v", "1",
        "-vf", "crop=min(iw\\,ih):min(iw\\,ih),scale=256:256",
        "-f", "image2", "-c:v", "png",
        out_path, NULL
    };

    enum ffmpeg_result result = run_ffmpeg_extract(argv, out_path, cache_path);
    if (result == FFMPEG_EXTRACTED) {
        log_info("Using video thumbnail from local file");
    } else if (result == FFMPEG_NO_OUTPUT) {
        log_info("No video thumbnail extracted from local file");
    }
    return result;
}

// Resolve artwork from the local media file itself — its embedded cover, else a
// frame of its video — into cache_path. Runs before the iTunes API so a local
// track resolves without any network round-trip. ffmpeg is an optional runtime
// dependency: when absent, return false and let the next source take over.
// Worker-safe (no GTK).
static bool try_local_media_art(const char *file_url, int64_t length_us, const char *cache_path) {
    char media_path[PATH_MAX];
    // A dropped lookup may only now be coming back from a file system that had
    // stopped answering; it does not go on to start ffmpeg
    if (!resolve_local_media_path(file_url, media_path) || thread_cancel_requested()) {
        return false;
    }

    char *ffmpeg = g_find_program_in_path("ffmpeg");
    if (!ffmpeg) {
        log_info("ffmpeg not found, skipping local artwork extraction");
        return false;
    }

    char out_path[600];
    // Named after this thread: a dropped lookup's ffmpeg and its successor's
    // must not write the same file
    snprintf(out_path, sizeof(out_path), "%s/extracted-art.%d.png", get_runtime_dir(),
             (int)gettid());

    // Only a file with no cover is asked for a frame. One that ffmpeg could not
    // get through in time, or whose worker was cancelled, is not run again.
    enum ffmpeg_result result = extract_embedded_cover(ffmpeg, media_path, out_path, cache_path);
    if (result == FFMPEG_NO_OUTPUT) {
        result = extract_video_thumbnail(ffmpeg, media_path, length_us, out_path, cache_path);
    }
    g_free(ffmpeg);
    return result == FFMPEG_EXTRACTED;
}

// Resolve iTunes artwork for the track into out_path ({md5}.png): the search,
// then the image download. Honors the artwork cancel flag.
static bool resolve_itunes_art(const struct tray_art_request *request, const char *out_path) {
    if (!request->track || request->track[0] == '\0') {
        return false;
    }

    log_info("Trying iTunes Search API...");
    char *itunes_url = itunes_search_artwork(request->artist, request->album, request->track);
    if (!itunes_url) {
        log_info("iTunes Search API did not return artwork");
        return false;
    }

    bool ok = render_art_to_cache(itunes_url, out_path);
    free(itunes_url);
    return ok;
}

// One album art lookup that has to leave the main thread. system_tray_art_prepare
// fills it, the worker's system_tray_art_run writes the outcome, and the main
// thread reads that back in system_tray_art_apply - only once the worker has
// handed the job over (worker_handoff.h), so no field is ever touched from two
// threads at once. A worker the main thread dropped frees the job itself.
struct tray_art_job {
    char *art_url;                          // owned copies; request points at them
    char *file_url;
    char *artist;
    char *album;
    char *track;
    struct tray_art_request request;        // the lookup, as handed out to callers
    char md5[MD5_DIGEST_STRING_LENGTH];     // metadata hash: cache key and icon name
    char cache_path[512];                   // {album_art_dir}/{md5}.png to write
    bool use_itunes;                        // [lyrics] enable_itunes when prepared
    bool found;                             // outcome: cache_path was written
    bool from_art_url;                      // outcome: it is request.art_url's image
};

static char *dup_or_null(const char *s) {
    return s ? strdup(s) : NULL;
}

static struct tray_art_job *art_job_new(const struct tray_art_request *request,
                                        const char *md5, const char *cache_path) {
    struct tray_art_job *job = calloc(1, sizeof(*job));
    if (!job) {
        return NULL;
    }
    job->art_url = dup_or_null(request->art_url);
    job->file_url = dup_or_null(request->file_url);
    job->artist = dup_or_null(request->artist);
    job->album = dup_or_null(request->album);
    job->track = dup_or_null(request->track);
    job->request = (struct tray_art_request){
        .art_url = job->art_url,
        .file_url = job->file_url,
        .artist = job->artist,
        .album = job->album,
        .track = job->track,
        .length_us = request->length_us,
    };
    snprintf(job->md5, sizeof(job->md5), "%s", md5);
    snprintf(job->cache_path, sizeof(job->cache_path), "%s", cache_path);
    // Copied here so the worker never reads the config the main thread reloads
    job->use_itunes = g_config.lyrics.enable_itunes;
    return job;
}

void system_tray_art_free(struct tray_art_job *job) {
    if (!job) {
        return;
    }
    free(job->art_url);
    free(job->file_url);
    free(job->artist);
    free(job->album);
    free(job->track);
    free(job);
}

const struct tray_art_request *system_tray_art_request(const struct tray_art_job *job) {
    return &job->request;
}

void system_tray_art_run(struct tray_art_job *job) {
    const struct tray_art_request *request = &job->request;

    // Priority order: MPRIS art URL → local file embedded cover → local video
    // thumbnail → iTunes. The two ffmpeg-based local steps come before iTunes
    // so a local track whose player omits mpris:artUrl still resolves offline.
    if (request->art_url && request->art_url[0] != '\0') {
        log_info("Using MPRIS album art: %s", request->art_url);
        if (render_art_to_cache(request->art_url, job->cache_path)) {
            job->from_art_url = true;
            job->found = true;
            return;
        }
    }
    if (!thread_cancel_requested() &&
        try_local_media_art(request->file_url, request->length_us, job->cache_path)) {
        job->found = true;
        return;
    }
    if (thread_cancel_requested() || !job->use_itunes) {
        return;
    }
    job->found = resolve_itunes_art(request, job->cache_path);
}

// Apply a cache PNG ({md5}.png) as the tray icon. Pass the art URL the image
// was downloaded from (NULL for any other source) so the next track with the
// same URL reuses it. Returns false if the file is unusable.
static bool apply_cached_icon(const char *metadata_hash, const char *art_url) {
    char cache_path[512];
    if (!metadata_hash || metadata_hash[0] == '\0' ||
        build_album_art_cache_path(cache_path, sizeof(cache_path), metadata_hash) <= 0) {
        return false;
    }

    // Decode before pointing the tray at it: a damaged cache file must fall
    // through to the other sources instead of becoming a blank icon.
    GError *error = NULL;
    GdkPixbuf *art = gdk_pixbuf_new_from_file(cache_path, &error);
    if (!art) {
        log_warn("Failed to load cached album art: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return false;
    }

    set_indicator_icon_cached(metadata_hash, cache_path);
    snprintf(last_metadata_hash, sizeof(last_metadata_hash), "%s", metadata_hash);

    if (art_url) {
        free(last_art_url);
        last_art_url = strdup(art_url);
        snprintf(last_art_url_hash, sizeof(last_art_url_hash), "%s", metadata_hash);
    }

    // Mirror the applied art into the shared scratch PNG (album-art.png) so the
    // desktop-notification badge — which is built from g_icon_path — shows the
    // same image; set_indicator_icon_cached only points the tray at {md5}.png.
    // error is NULL here (the load above succeeded); reuse it for the save.
    // A failed mirror is non-fatal — the tray icon is already set above; this
    // only keeps the notification badge in sync — so warn and continue.
    if (!save_png_private(art, g_icon_path, &error)) {
        log_warn("system_tray: failed to mirror art to notification icon: %s",
                 error ? error->message : "unknown error");
        if (error) g_error_free(error);
    }
    g_object_unref(art);
    return true;
}

// A player that reports one art URL for a whole album would otherwise have the
// same image downloaded again for every track. When art_url is the one the
// last download was for, copy that track's cache file to cache_path instead.
static bool reuse_art_from_same_url(const char *art_url, const char *cache_path) {
    if (!art_url || art_url[0] == '\0' || !last_art_url ||
        last_art_url_hash[0] == '\0' || strcmp(last_art_url, art_url) != 0) {
        return false;
    }

    char previous_path[512];
    if (build_album_art_cache_path(previous_path, sizeof(previous_path), last_art_url_hash) <= 0) {
        return false;
    }
    GdkPixbuf *previous = gdk_pixbuf_new_from_file(previous_path, NULL);
    if (!previous) {
        return false;  // cleaned up or purged since; download it again
    }

    bool copied = save_png_private(previous, cache_path, NULL);
    g_object_unref(previous);
    if (copied) {
        log_info("Same art URL as the last download, reusing its artwork");
    }
    return copied;
}

struct tray_art_job *system_tray_art_prepare(const struct tray_art_request *request) {
    // Metadata hash (artist + track + album): the cache key and the icon name
    char metadata_hash[MD5_DIGEST_STRING_LENGTH];
    char cache_path[512];
    if (!calculate_metadata_md5(request->artist, request->track, request->album, metadata_hash) ||
        build_album_art_cache_path(cache_path, sizeof(cache_path), metadata_hash) <= 0) {
        log_error("Failed to calculate metadata hash");
        system_tray_reset_icon();
        return NULL;
    }

    // Check if we already loaded this artwork (same metadata hash)
    if (strcmp(last_metadata_hash, metadata_hash) == 0) {
        log_info("Same metadata hash, skipping artwork update");
        return NULL;
    }

    ensure_cache_directories();

    // Only what cannot block runs here: the per-track cache, and a copy of the
    // previous track's cache when the art URL is unchanged.
    struct stat st;
    if (stat(cache_path, &st) == 0) {
        touch_cache_file(cache_path);  // keeps it out of the automatic cleanup
        if (apply_cached_icon(metadata_hash, NULL)) {
            log_success("Loaded album art from cache: %s", sanitize_path(cache_path));
            return NULL;
        }
    }
    if (reuse_art_from_same_url(request->art_url, cache_path) &&
        apply_cached_icon(metadata_hash, request->art_url)) {
        return NULL;
    }

    // Everything else downloads or runs ffmpeg, so it belongs to the worker.
    // Leave the current icon and last_metadata_hash untouched until it lands.
    const bool has_art_url = request->art_url && request->art_url[0] != '\0';
    const bool has_local_media = request->file_url && strncmp(request->file_url, "file://", 7) == 0;
    const bool can_use_itunes = g_config.lyrics.enable_itunes &&
                                request->track && request->track[0] != '\0';
    if (!has_art_url && !has_local_media && !can_use_itunes) {
        if (!g_config.lyrics.enable_itunes) {
            log_info("iTunes API disabled in config");
        }
        log_info("No artwork available from any source, using default icon");
        system_tray_reset_icon();
        return NULL;
    }

    struct tray_art_job *job = art_job_new(request, metadata_hash, cache_path);
    if (!job) {
        log_warn("Out of memory for the artwork lookup, using default icon");
        system_tray_reset_icon();
    }
    return job;
}

void system_tray_art_apply(const struct tray_art_job *job) {
    // No source had artwork (or its file is unusable) - fall back to the default icon
    if (!job->found ||
        !apply_cached_icon(job->md5, job->from_art_url ? job->request.art_url : NULL)) {
        system_tray_reset_icon();
    }
}

void system_tray_update(void) {
    // Process pending GTK events
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

void system_tray_cleanup(void) {
    // Clean up icon files
    unlink(g_icon_path);
    unlink(g_notification_icon_path);
    unlink(g_disabled_icon_path);

    free(last_art_url);
    last_art_url = NULL;

    if (indicator) {
        g_object_unref(indicator);
        indicator = NULL;
    }

    // Cleanup CURL
    curl_global_cleanup();
}

// Create disabled icon with red X overlay (1/4 size in bottom-right corner)
static GdkPixbuf* create_disabled_icon(void) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    GError *error = NULL;

    // Load default icon (same as when no music is playing)
    GdkPixbuf *icon = gtk_icon_theme_load_icon(icon_theme, "audio-player", 48, 0, &error);
    if (!icon) {
        // Fallback to headphones icon
        g_clear_error(&error);
        icon = gtk_icon_theme_load_icon(icon_theme, "audio-headphones", 48, 0, &error);
    }
    if (!icon) {
        g_clear_error(&error);
        icon = gtk_icon_theme_load_icon(icon_theme, "multimedia-player", 48, 0, &error);
    }
    if (!icon) {
        log_error("Failed to load icon for disabled state");
        if (error) g_error_free(error);
        return NULL;
    }

    int width = gdk_pixbuf_get_width(icon);
    int height = gdk_pixbuf_get_height(icon);

    // Create Cairo surface
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    // Draw original icon
    gdk_cairo_set_source_pixbuf(cr, icon, 0, 0);
    cairo_paint(cr);

    // Draw red X in bottom-right corner (1/4 size = 12x12)
    double x_size = width / 4.0;
    double x_offset = width - x_size - 2;   // 2px from right edge
    double y_offset = height - x_size - 2;  // 2px from bottom edge

    // Red X mark (transparent background)
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);  // Pure red
    cairo_set_line_width(cr, 2.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    double padding = 2;
    // Draw X
    cairo_move_to(cr, x_offset + padding, y_offset + padding);
    cairo_line_to(cr, x_offset + x_size - padding, y_offset + x_size - padding);
    cairo_stroke(cr);

    cairo_move_to(cr, x_offset + x_size - padding, y_offset + padding);
    cairo_line_to(cr, x_offset + padding, y_offset + x_size - padding);
    cairo_stroke(cr);

    // Convert back to GdkPixbuf
    cairo_surface_flush(surface);
    GdkPixbuf *disabled = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(icon);

    return disabled;
}

// Set overlay state and update icon
void system_tray_set_overlay_state(bool enabled) {
    static bool current_state = true;
    static const char *DISABLED_ICON_NAME = "disabled-icon";

    if (!indicator) {
        return;
    }

    // Skip duplicate updates
    if (current_state == enabled) {
        return;
    }

    current_state = enabled;

    if (enabled) {
        // Overlay enabled: restore to album art or default icon
        struct stat st;
        if (stat(g_icon_path, &st) == 0) {
            // Album art file exists, restore it
            app_indicator_set_icon_theme_path(indicator, g_icon_dir);
            app_indicator_set_icon_full(indicator, ICON_NAME, "Album Art");
            log_info("Overlay enabled - album art restored");
        } else {
            // No album art, use default icon
            system_tray_reset_icon();
            log_info("Overlay enabled - default icon restored");
        }
    } else {
        // Overlay disabled: show headphones + red X
        GdkPixbuf *disabled = create_disabled_icon();
        if (!disabled) {
            log_error("Failed to create disabled icon");
            return;
        }

        // Save as PNG
        GError *error = NULL;
        bool save_result = save_png_private(disabled, g_disabled_icon_path, &error);

        if (!save_result) {
            log_error("Failed to save disabled icon: %s", error ? error->message : "unknown");
            if (error) g_error_free(error);
            g_object_unref(disabled);
            return;
        }

        g_object_unref(disabled);

        // Update indicator icon
        app_indicator_set_icon_theme_path(indicator, g_icon_dir);
        app_indicator_set_icon_full(indicator, DISABLED_ICON_NAME, "Overlay Disabled");

        log_info("Overlay disabled - icon updated");
    }
}

// Set lyrics state for menu callbacks
void system_tray_set_state(struct lyrics_state *state) {
    g_tray_state = state;

    // Update overlay checkbox to match current state
    if (overlay_item && state) {
        // Block signal to prevent callback during programmatic update
        g_signal_handlers_block_by_func(overlay_item, on_overlay_toggled, NULL);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overlay_item), state->runtime.overlay_enabled);
        g_signal_handlers_unblock_by_func(overlay_item, on_overlay_toggled, NULL);
    }
}

// Update track info in menu
void system_tray_update_track_info(const char *artist, const char *title) {
    if (!track_info_item) {
        return;
    }

    char label[128];

    if (!artist && !title) {
        snprintf(label, sizeof(label), "♪ No track");
    } else {
        // Truncate artist and title to max 10 chars each
        char *trunc_artist = truncate_utf8(artist, TRACK_INFO_MAX_CHARS);
        char *trunc_title = truncate_utf8(title, TRACK_INFO_MAX_CHARS);

        snprintf(label, sizeof(label), "♪ %s - %s", trunc_artist, trunc_title);

        free(trunc_artist);
        free(trunc_title);
    }

    gtk_menu_item_set_label(GTK_MENU_ITEM(track_info_item), label);
}
