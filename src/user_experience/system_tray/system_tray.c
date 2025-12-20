#include "system_tray.h"
#include "../../utils/curl/curl_utils.h"
#include "../../provider/itunes/itunes_artwork.h"
#include "../../utils/file/file_utils.h"
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
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);

    curl_memory_buffer_init(buffer);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

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

        gdk_pixbuf_loader_close(loader, NULL);
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
    curl_global_init(CURL_GLOBAL_DEFAULT);

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

void system_tray_update_tooltip(const char *text) {
    if (indicator && text) {
        log_info("Updating tray tooltip: %s", text);
        app_indicator_set_title(indicator, text);
    }
}

void system_tray_send_notification(const char *artist, const char *title) {
    if (!artist && !title) {
        return;
    }

    // Build notification body
    char body[512];
    if (artist && strlen(artist) > 0 && title && strlen(title) > 0) {
        snprintf(body, sizeof(body), "%s - %s", artist, title);
    } else if (title && strlen(title) > 0) {
        snprintf(body, sizeof(body), "%s", title);
    } else {
        return;
    }

    // Escape special characters for shell
    char escaped_body[1024];
    const char *src = body;
    char *dst = escaped_body;
    size_t remaining = sizeof(escaped_body) - 1;

    while (*src && remaining > 1) {
        if (*src == '"' || *src == '\\' || *src == '$' || *src == '`') {
            if (remaining > 2) {
                *dst++ = '\\';
                remaining--;
            }
        }
        *dst++ = *src++;
        remaining--;
    }
    *dst = '\0';

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
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "notify-send -a wshowlyrics -i \"%s\" -e -t %d \"🎵 Now Playing\" \"%s\" 2>/dev/null",
             icon_path, g_config.lyrics.notification_timeout, escaped_body);

    log_info("Sending desktop notification: %s", body);

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
    if (!art_url || strlen(art_url) == 0) {
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
    if (!g_config.lyrics.enable_itunes || !track || strlen(track) == 0) {
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
