#include "system_tray.h"
#include "../../utils/curl/curl_utils.h"
#include "../../provider/itunes/itunes_artwork.h"
#include "../../utils/file/file_utils.h"
#include "../../utils/string/string_utils.h"
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

// Fixed paths
static const char *ICON_DIR = "/tmp/wshowlyrics";
static const char *ICON_PATH = "/tmp/wshowlyrics/album-art.png";
static const char *ICON_NAME = "album-art";

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

// Create a minimal dummy menu (required by AppIndicator)
static GtkWidget* create_menu(void) {
    GtkWidget *tray_menu = gtk_menu_new();

    // Add a simple info item
    GtkWidget *info_item = gtk_menu_item_new_with_label("Lyrics - Album Art Display");
    gtk_widget_set_sensitive(info_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(tray_menu), info_item);

    gtk_widget_show_all(tray_menu);

    log_info("Minimal tray menu created (album art display only)");

    return tray_menu;
}

bool system_tray_init(void) {
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
    app_indicator_set_title(indicator, "Lyrics");

    // Create minimal menu (required by AppIndicator)
    menu = create_menu();
    app_indicator_set_menu(indicator, GTK_MENU(menu));

    // Set initial default icon
    log_info("Setting initial default icon");
    mkdir(ICON_DIR, 0700);  // Owner-only access for privacy
    if (save_default_icon(ICON_PATH)) {
        app_indicator_set_icon_theme_path(indicator, ICON_DIR);
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
    mkdir(ICON_DIR, 0700);  // Owner-only access for privacy

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
    bool save_result = gdk_pixbuf_save(rounded, ICON_PATH, "png", &error, NULL);
    umask(old_mask);
    if (!save_result) {
        log_error("Failed to save icon: %s", error->message);
        g_error_free(error);
        g_object_unref(rounded);
        return false;
    }

    g_object_unref(rounded);

    log_info("Album art saved to: %s", ICON_PATH);

    // Set the icon theme path to our directory
    app_indicator_set_icon_theme_path(indicator, ICON_DIR);

    // Update indicator icon using just the name (without extension or path)
    app_indicator_set_icon_full(indicator, ICON_NAME, "Album Art");

    log_info("Icon updated: name=%s, theme_path=%s", ICON_NAME, ICON_DIR);

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
    mkdir(ICON_DIR, 0700);  // Owner-only access for privacy

    // Save default icon to file
    if (!save_default_icon(ICON_PATH)) {
        log_warn("Could not save default icon");
        // Fallback: use system icon name without file
        app_indicator_set_icon_theme_path(indicator, NULL);
        app_indicator_set_icon_full(indicator, "audio-player", "Music Player");
        return;
    }

    // Set the icon theme path to our directory
    app_indicator_set_icon_theme_path(indicator, ICON_DIR);

    // Update indicator icon
    app_indicator_set_icon_full(indicator, ICON_NAME, "Music Player");

    log_info("Icon reset to default: %s", ICON_PATH);
}

void system_tray_send_notification(const struct notification_info *info) {
    if (!info || !info->title) {
        return;
    }

    // Build notification title: "🎵 Title"
    char notification_title[512];
    snprintf(notification_title, sizeof(notification_title), "🎵 %s", info->title);

    // Build notification body: "Album · Artist\nPlayer" (2 lines, capitalized player)
    char body[512];
    const char *artist_display = (info->artist && info->artist[0] != '\0') ? info->artist : "Unknown";
    const char *album_display = (info->album && info->album[0] != '\0') ? info->album : "Unknown";
    const char *player_display = (info->player_name && info->player_name[0] != '\0') ? info->player_name : "Unknown";

    // Capitalize first letter of player name for notification
    char player_capitalized[256];
    snprintf(player_capitalized, sizeof(player_capitalized), "%s", player_display);
    if (player_capitalized[0] >= 'a' && player_capitalized[0] <= 'z') {
        player_capitalized[0] = player_capitalized[0] - 'a' + 'A';
    }

    snprintf(body, sizeof(body), "%s · %s\n%s", album_display, artist_display, player_capitalized);

    // Escape title and body for shell
    char escaped_title[1024];
    char escaped_body[1024];
    escape_shell_string(notification_title, escaped_title, sizeof(escaped_title));
    escape_shell_string(body, escaped_body, sizeof(escaped_body));

    // Check if album art exists, otherwise use default icon
    const char *icon_path = ICON_PATH;
    struct stat st;
    if (stat(icon_path, &st) != 0) {
        // Album art not found, use default icon name
        icon_path = "audio-player";
    }

    // Build notify-send command with ephemeral flag and timeout
    // -e: ephemeral (don't save to notification center)
    // -t: timeout in milliseconds (configurable, default 5 seconds)
    char cmd[3072];
    snprintf(cmd, sizeof(cmd), "notify-send -a wshowlyrics -i \"%s\" -e -t %d \"%s\" \"%s\" 2>/dev/null",
             icon_path, g_config.lyrics.notification_timeout, escaped_title, escaped_body);

    // Create log-friendly version: "Album · Artist (player)" (lowercase, with parentheses)
    char log_body[512];
    snprintf(log_body, sizeof(log_body), "%s · %s (%s)", album_display, artist_display, player_display);

    log_info("Sending desktop notification: %s - %s", notification_title, log_body);

    // Execute notify-send in background
    int ret = system(cmd);
    if (ret != 0) {
        log_warn("notify-send command failed with exit code: %d", ret);
    }
}

// Cache current artwork from ICON_PATH to cache_path
static bool cache_current_artwork(const char *cache_path) {
    GError *error = NULL;
    GdkPixbuf *current = gdk_pixbuf_new_from_file(ICON_PATH, &error);

    if (current) {
        mode_t old_mask = umask(0077);
        bool success = gdk_pixbuf_save(current, cache_path, "png", NULL, NULL);
        umask(old_mask);
        g_object_unref(current);
        if (success) {
            log_info("Cached album art: %s", cache_path);
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

    log_success("Found cached album art: %s", cache_path);

    GError *error = NULL;
    GdkPixbuf *cached = gdk_pixbuf_new_from_file(cache_path, &error);

    if (cached) {
        // Set the icon theme path and update
        app_indicator_set_icon_theme_path(indicator, ICON_DIR);

        // Copy cached file to active icon path
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(cached, 48, 48, GDK_INTERP_BILINEAR);
        g_object_unref(cached);

        mode_t old_mask = umask(0077);
        bool save_result = gdk_pixbuf_save(scaled, ICON_PATH, "png", NULL, NULL);
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
    // Clean up icon file
    unlink(ICON_PATH);

    // Remove icon directory
    rmdir(ICON_DIR);

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
    GdkPixbuf *icon = gtk_icon_theme_load_icon(icon_theme, "audio-headphones", 48, 0, &error);
    if (!icon) {
        // Fallback chain (same as save_default_icon)
        g_clear_error(&error);
        icon = gtk_icon_theme_load_icon(icon_theme, "audio-player", 48, 0, &error);
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
    static const char *DISABLED_ICON_PATH = "/tmp/wshowlyrics/disabled-icon.png";
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
        if (stat(ICON_PATH, &st) == 0) {
            // Album art file exists, restore it
            app_indicator_set_icon_theme_path(indicator, ICON_DIR);
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
        bool save_result = gdk_pixbuf_save(disabled, DISABLED_ICON_PATH, "png", &error, NULL);
        umask(old_mask);

        if (!save_result) {
            log_error("Failed to save disabled icon: %s", error ? error->message : "unknown");
            if (error) g_error_free(error);
            g_object_unref(disabled);
            return;
        }

        g_object_unref(disabled);

        // Update indicator icon
        app_indicator_set_icon_theme_path(indicator, ICON_DIR);
        app_indicator_set_icon_full(indicator, DISABLED_ICON_NAME, "Overlay Disabled");

        log_info("Overlay disabled - icon updated");
    }
}
