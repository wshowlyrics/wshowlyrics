#include "icon_utils.h"
#include "../../constants.h"
#include <gtk/gtk.h>
#include <cairo.h>
#include <string.h>
#include <strings.h>

const char* icon_utils_get_player_icon_name(const char *player_name) {
    if (!player_name || player_name[0] == '\0') {
        return "audio-player";
    }

    // Map common music players to their icon names
    if (strcasecmp(player_name, "spotify") == 0) {
        return "spotify-client";
    } else if (strcasecmp(player_name, "mpv") == 0) {
        return "mpv";
    } else if (strcasecmp(player_name, "vlc") == 0) {
        return "vlc";
    } else if (strcasecmp(player_name, "rhythmbox") == 0) {
        return "rhythmbox";
    } else if (strcasecmp(player_name, "audacious") == 0) {
        return "audacious";
    } else if (strcasecmp(player_name, "clementine") == 0) {
        return "clementine";
    } else if (strcasecmp(player_name, "strawberry") == 0) {
        return "strawberry";
    } else if (strcasecmp(player_name, "amarok") == 0) {
        return "amarok";
    } else if (strcasecmp(player_name, "deadbeef") == 0) {
        return "deadbeef";
    } else if (strcasecmp(player_name, "elisa") == 0) {
        return "elisa";
    } else if (strcasecmp(player_name, "lollypop") == 0) {
        return "lollypop";
    } else if (strcasecmp(player_name, "quodlibet") == 0) {
        return "quodlibet";
    } else if (strcasecmp(player_name, "gmusicbrowser") == 0) {
        return "gmusicbrowser";
    } else if (strcasecmp(player_name, "banshee") == 0) {
        return "banshee";
    } else if (strcasecmp(player_name, "cantata") == 0) {
        return "cantata";
    }

    // Fallback to generic audio player icon
    return "audio-player";
}

GdkPixbuf* icon_utils_load_player_icon(const char *player_name, int size) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    GError *error = NULL;

    // Get the appropriate icon name for this player
    const char *icon_name = icon_utils_get_player_icon_name(player_name);

    // Try to load the specific player icon
    GdkPixbuf *icon = gtk_icon_theme_load_icon(icon_theme, icon_name, size, 0, &error);

    if (!icon) {
        // Fallback to generic audio player icon
        g_clear_error(&error);
        log_info("Player icon '%s' not found, using fallback", icon_name);
        icon = gtk_icon_theme_load_icon(icon_theme, "audio-player", size, 0, &error);
    }

    if (!icon) {
        // Last resort: try audio-headphones
        g_clear_error(&error);
        log_warn("Could not load audio-player icon, trying audio-headphones");
        icon = gtk_icon_theme_load_icon(icon_theme, "audio-headphones", size, 0, &error);
    }

    if (!icon) {
        log_error("Failed to load any fallback icon: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return NULL;
    }

    if (error) g_error_free(error);
    return icon;
}

GdkPixbuf* icon_utils_add_badge(GdkPixbuf *base, GdkPixbuf *badge) {
    if (!base) {
        log_error("Base pixbuf is NULL");
        return NULL;
    }

    // If no badge provided, return a copy of the base image
    if (!badge) {
        return gdk_pixbuf_copy(base);
    }

    int base_width = gdk_pixbuf_get_width(base);
    int base_height = gdk_pixbuf_get_height(base);

    // Create Cairo surface from base image
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                           base_width, base_height);
    cairo_t *cr = cairo_create(surface);

    // Draw base image
    gdk_cairo_set_source_pixbuf(cr, base, 0, 0);
    cairo_paint(cr);

    // Calculate badge size and position (1/4 size, bottom-right corner with 2px padding)
    int badge_size = base_width / 4;
    int badge_x = base_width - badge_size - 2;
    int badge_y = base_height - badge_size - 2;

    // Scale badge to fit
    GdkPixbuf *scaled_badge = gdk_pixbuf_scale_simple(badge, badge_size, badge_size,
                                                       GDK_INTERP_BILINEAR);
    if (!scaled_badge) {
        log_error("Failed to scale badge");
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return gdk_pixbuf_copy(base);
    }

    // Draw badge with slight transparency to blend better
    gdk_cairo_set_source_pixbuf(cr, scaled_badge, badge_x, badge_y);
    cairo_paint_with_alpha(cr, 0.95);  // 95% opacity for subtle blend

    // Convert Cairo surface back to GdkPixbuf
    cairo_surface_flush(surface);
    GdkPixbuf *result = gdk_pixbuf_get_from_surface(surface, 0, 0, base_width, base_height);

    // Cleanup
    g_object_unref(scaled_badge);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return result;
}
