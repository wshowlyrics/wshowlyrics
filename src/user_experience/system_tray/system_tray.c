#include "system_tray.h"
#include "../../main.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/runtime/runtime_dir.h"
#include "../../provider/itunes/itunes_artwork.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/icon/icon_utils.h"
#include "../../core/rendering/rendering_manager.h"
#include "../config/config.h"
#include <stdio.h>
#include "../../constants.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <libappindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <curl/curl.h>
#include <cairo/cairo.h>

// State
static AppIndicator *indicator = NULL;
static GtkWidget *menu = NULL;
static char *last_art_url = NULL;
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

// Download image from URL to memory
static bool download_image(const char *url, struct curl_memory_buffer *buffer) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    // Enforce TLS 1.2 or higher for security
    if (curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2) != CURLE_OK) {
        log_error("system_tray: Failed to set SSL version");
        curl_easy_cleanup(curl);
        return false;
    }

    curl_memory_buffer_init(buffer);

    if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)buffer) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L) != CURLE_OK) {
        log_error("system_tray: Failed to set CURL options");
        curl_memory_buffer_free(buffer);
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
static bool is_square_image(GdkPixbuf *pixbuf) {
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
    mode_t old_mask = umask(0077);
    bool save_result = gdk_pixbuf_save(icon, output_path, "png", &error, NULL);
    umask(old_mask);
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

// Load image from URL (http:// or file://)
static GdkPixbuf* load_image_from_url(const char *url) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = NULL;

    // Handle file:// URLs
    if (strncmp(url, "file://", 7) == 0) {
        const char *file_path = url + 7;
        pixbuf = gdk_pixbuf_new_from_file(file_path, &error);

        if (error) {
            log_error("Failed to load image from file: %s", error->message);
            g_error_free(error);
            return NULL;
        }
    }
    // Handle http:// or https:// URLs
    else if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        struct curl_memory_buffer buffer;
        curl_memory_buffer_init(&buffer);

        if (!download_image(url, &buffer)) {
            log_error("Failed to download image from URL");
            return NULL;
        }

        GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

        if (!gdk_pixbuf_loader_write(loader, (const guchar *)buffer.data, buffer.size, &error)) {
            log_error("Failed to load image data: %s", error->message);
            g_error_free(error);
            g_object_unref(loader);
            curl_memory_buffer_free(&buffer);
            return NULL;
        }

        GError *close_error = NULL;
        if (!gdk_pixbuf_loader_close(loader, &close_error)) {
            if (close_error) {
                log_warn("Failed to close pixbuf loader: %s", close_error->message);
                g_error_free(close_error);
            }
        }
        pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);

        if (pixbuf) {
            g_object_ref(pixbuf);
        }

        g_object_unref(loader);
        curl_memory_buffer_free(&buffer);
    }

    // Verify image is approximately square (allow 5% tolerance)
    if (pixbuf && !is_square_image(pixbuf)) {
        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);
        log_warn("Image aspect ratio too far from square (%dx%d), using default icon", width, height);
        g_object_unref(pixbuf);
        return NULL;
    }

    return pixbuf;
}

// Apply circular mask to a pixbuf (CD-like appearance)
static GdkPixbuf* apply_circular_mask(GdkPixbuf *pixbuf) {
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
static void on_overlay_toggled(GtkCheckMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (!g_tray_state) {
        return;
    }

    g_tray_state->overlay_enabled = !g_tray_state->overlay_enabled;
    system_tray_set_overlay_state(g_tray_state->overlay_enabled);
    rendering_manager_set_dirty(g_tray_state);

    log_info("Menu: Overlay %s", g_tray_state->overlay_enabled ? "enabled" : "disabled");
}

static void on_timing_adjust(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    int delta = GPOINTER_TO_INT(user_data);

    if (!g_tray_state) {
        return;
    }

    if (delta == 0) {
        // Reset to global offset
        g_tray_state->timing_offset_ms = g_config.lyrics.global_offset_ms;
        log_info("Menu: Timing offset reset to %dms", g_tray_state->timing_offset_ms);
    } else {
        g_tray_state->timing_offset_ms += delta;
        // Clamp to reasonable range
        if (g_tray_state->timing_offset_ms < -5000) g_tray_state->timing_offset_ms = -5000;
        if (g_tray_state->timing_offset_ms > 5000) g_tray_state->timing_offset_ms = 5000;
        log_info("Menu: Timing offset adjusted by %+dms, now: %dms", delta, g_tray_state->timing_offset_ms);
    }

    rendering_manager_set_dirty(g_tray_state);
}

static void on_edit_settings(GtkMenuItem *item, gpointer user_data) {
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

    char *argv[] = { (char *)terminal, "-e", (char *)editor, config_path, NULL };
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        log_warn("Menu: Failed to open editor: %s", error->message);
        g_error_free(error);
    }
}

static void on_quit(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;

    if (g_tray_state) {
        g_tray_state->run = false;
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

bool system_tray_update_icon(const char *art_url) {
    if (!indicator || !art_url) {
        log_error("update_icon: indicator=%p, art_url=%p", (void*)indicator, (void*)art_url);
        return false;
    }

    log_info("Updating tray icon with art URL: %s", art_url);

    // Skip if same as last URL
    if (last_art_url && strcmp(last_art_url, art_url) == 0) {
        log_info("Same as last URL, skipping update");
        return true;
    }

    // Load image from URL
    log_info("Loading image from URL...");
    GdkPixbuf *pixbuf = load_image_from_url(art_url);

    if (!pixbuf) {
        // Fallback to default icon
        log_warn("Failed to load image, using default icon");
        app_indicator_set_icon(indicator, "audio-player");
        return false;
    }

    log_info("Image loaded successfully");

    // Create directory if it doesn't exist
    if (mkdir(g_icon_dir, 0700) != 0 && errno != EEXIST) {
        log_warn("Failed to create icon directory: %s", strerror(errno));
    }

    // Scale to reasonable size (48x48)
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 48, 48, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);

    // Apply circular mask (CD-like appearance)
    GdkPixbuf *rounded = apply_circular_mask(scaled);
    g_object_unref(scaled);

    if (!rounded) {
        log_error("Failed to apply circular mask");
        return false;
    }

    // Save as PNG (overwrites previous, owner-only access for privacy)
    GError *error = NULL;
    mode_t old_mask = umask(0077);
    bool save_result = gdk_pixbuf_save(rounded, g_icon_path, "png", &error, NULL);
    umask(old_mask);
    if (!save_result) {
        log_error("Failed to save icon: %s", error->message);
        g_error_free(error);
        g_object_unref(rounded);
        return false;
    }

    g_object_unref(rounded);

    log_info("Album art saved to: %s", g_icon_path);

    // Set the icon theme path to our directory
    app_indicator_set_icon_theme_path(indicator, g_icon_dir);

    // Update indicator icon using just the name (without extension or path)
    app_indicator_set_icon_full(indicator, ICON_NAME, "Album Art");

    log_info("Icon updated: name=%s, theme_path=%s", ICON_NAME, g_icon_dir);

    // Update last URL
    free(last_art_url);
    last_art_url = strdup(art_url);

    return true;
}

void system_tray_reset_icon(void) {
    if (!indicator) {
        return;
    }

    log_info("Resetting tray icon to default");

    // Clear last URL and metadata hash to force reload on next update
    free(last_art_url);
    last_art_url = NULL;
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

// Helper: Build notification body with artist, album, and player
static void build_notification_body(const struct notification_info *info,
                                   char *body, size_t body_size,
                                   char *player_capitalized, size_t player_cap_size) {
    const char *artist = (info->artist && info->artist[0]) ? info->artist : "Unknown";
    const char *album = (info->album && info->album[0]) ? info->album : "Unknown";
    const char *player = (info->player_name && info->player_name[0]) ? info->player_name : "Unknown";

    // Capitalize first letter of player name
    snprintf(player_capitalized, player_cap_size, "%s", player);
    if (player_capitalized[0] >= 'a' && player_capitalized[0] <= 'z') {
        player_capitalized[0] = player_capitalized[0] - 'a' + 'A';
    }

    snprintf(body, body_size, "%s · %s\n%s", album, artist, player_capitalized);
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
        if (gdk_pixbuf_save(badged, notification_icon_path, "png", &save_error, NULL)) {
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
    char body[512];
    char player_capitalized[256];
    build_notification_body(info, body, sizeof(body), player_capitalized, sizeof(player_capitalized));

    // Get player display name for logging
    const char *player_display = (info->player_name && info->player_name[0]) ? info->player_name : "Unknown";

    // Prepare notification icon
    const char *icon_path = prepare_notification_icon(player_display, g_notification_icon_path);

    // Log notification
    const char *artist = (info->artist && info->artist[0]) ? info->artist : "Unknown";
    const char *album = (info->album && info->album[0]) ? info->album : "Unknown";
    log_info("Sending desktop notification: %s - %s · %s (%s)",
            notification_title, album, artist, player_display);

    // Execute notify-send via g_spawn_async (no shell interpretation)
    char timeout_str[16];
    snprintf(timeout_str, sizeof(timeout_str), "%d", g_config.lyrics.notification_timeout);
    char *argv[] = {
        "notify-send", "-a", "wshowlyrics",
        "-i", (char *)icon_path,
        "-e", "-t", timeout_str,
        notification_title, body, NULL
    };
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        log_warn("notify-send failed: %s", error->message);
        g_error_free(error);
    }
}

// Cache current artwork from g_icon_path to cache_path
static bool cache_current_artwork(const char *cache_path) {
    GError *error = NULL;
    GdkPixbuf *current = gdk_pixbuf_new_from_file(g_icon_path, &error);

    if (current) {
        mode_t old_mask = umask(0077);
        bool success = gdk_pixbuf_save(current, cache_path, "png", NULL, NULL);
        umask(old_mask);
        g_object_unref(current);
        if (success) {
            log_info("Cached album art: %s", sanitize_path(cache_path));
        }
        return success;
    } else if (error) {
        log_warn("Failed to cache album art: %s", error->message);
        g_error_free(error);
    }
    return false;
}

// Load cached album art and set as icon
static bool load_cached_album_art(const char *cache_path, const char *metadata_hash) {
    struct stat st;
    if (stat(cache_path, &st) != 0) {
        return false;  // Cache doesn't exist
    }

    // Update access time to prevent automatic cleanup
    touch_cache_file(cache_path);

    log_success("Found cached album art: %s", sanitize_path(cache_path));

    GError *error = NULL;
    GdkPixbuf *cached = gdk_pixbuf_new_from_file(cache_path, &error);

    if (cached) {
        // Set the icon theme path and update
        app_indicator_set_icon_theme_path(indicator, g_icon_dir);

        // Copy cached file to active icon path
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(cached, 48, 48, GDK_INTERP_BILINEAR);
        g_object_unref(cached);

        mode_t old_mask = umask(0077);
        bool save_result = gdk_pixbuf_save(scaled, g_icon_path, "png", NULL, NULL);
        umask(old_mask);
        if (save_result) {
            g_object_unref(scaled);
            app_indicator_set_icon_full(indicator, ICON_NAME, "Album Art");

            // Update state
            snprintf(last_metadata_hash, sizeof(last_metadata_hash), "%s", metadata_hash);
            log_success("Loaded album art from cache");
            return true;
        } else {
            g_object_unref(scaled);
        }
    } else if (error) {
        log_warn("Failed to load cached album art: %s", error->message);
        g_error_free(error);
    }

    return false;
}

// Try to get artwork from MPRIS art URL
static bool try_mpris_artwork(const char *art_url, const char *cache_path, const char *metadata_hash) {
    if (!art_url || art_url[0] == '\0') {
        return false;
    }

    log_info("Using MPRIS album art: %s", art_url);
    bool success = system_tray_update_icon(art_url);

    // Cache MPRIS artwork if successful
    if (success && metadata_hash[0] != '\0') {
        cache_current_artwork(cache_path);
        snprintf(last_metadata_hash, sizeof(last_metadata_hash), "%s", metadata_hash);
    }

    return success;
}

// Try to get artwork from iTunes API
static bool try_itunes_artwork(const char *artist, const char *album, const char *track,
                               const char *cache_path, const char *metadata_hash) {
    if (!g_config.lyrics.enable_itunes || !track || track[0] == '\0') {
        if (!g_config.lyrics.enable_itunes) {
            log_info("iTunes API disabled in config");
        }
        return false;
    }

    log_info("Trying iTunes Search API...");
    char *itunes_url = itunes_search_artwork(artist, album, track);

    if (itunes_url) {
        bool success = system_tray_update_icon(itunes_url);
        free(itunes_url);

        // Cache iTunes artwork if successful
        if (success && metadata_hash[0] != '\0') {
            cache_current_artwork(cache_path);
            snprintf(last_metadata_hash, sizeof(last_metadata_hash), "%s", metadata_hash);
        }

        return success;
    } else {
        log_info("iTunes Search API did not return artwork");
    }

    return false;
}

bool system_tray_update_icon_with_fallback(const char *art_url, const char *artist, const char *album, const char *track) {
    // Calculate metadata hash for caching (using artist + track + album)
    char metadata_hash[MD5_DIGEST_STRING_LENGTH];
    if (!calculate_metadata_md5(artist, track, album, metadata_hash)) {
        log_error("Failed to calculate metadata hash");
        metadata_hash[0] = '\0';
    }

    // Check if we already loaded this artwork (same metadata hash)
    if (metadata_hash[0] != '\0' && strcmp(last_metadata_hash, metadata_hash) == 0) {
        log_info("Same metadata hash, skipping artwork update");
        return true;
    }

    // Ensure cache directories exist
    ensure_cache_directories();

    // Build cache path
    char cache_path[512];
    if (metadata_hash[0] == '\0' || build_album_art_cache_path(cache_path, sizeof(cache_path), metadata_hash) <= 0) {
        cache_path[0] = '\0';  // No cache path available
    }

    // Priority 1: Try cache first (fastest, no network request)
    if (cache_path[0] != '\0' && load_cached_album_art(cache_path, metadata_hash)) {
        return true;
    }

    // Priority 2: Try MPRIS art URL (network request if cache miss)
    if (try_mpris_artwork(art_url, cache_path, metadata_hash)) {
        return true;
    }

    // Priority 3: Try iTunes API (network request)
    if (try_itunes_artwork(artist, album, track, cache_path, metadata_hash)) {
        return true;
    }

    // No artwork available - reset to default icon
    log_info("No artwork available from any source, using default icon");
    system_tray_reset_icon();
    last_metadata_hash[0] = '\0';
    return false;
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
        mode_t old_mask = umask(0077);
        bool save_result = gdk_pixbuf_save(disabled, g_disabled_icon_path, "png", &error, NULL);
        umask(old_mask);

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
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overlay_item), state->overlay_enabled);
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
