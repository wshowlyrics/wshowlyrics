#ifndef ICON_UTILS_H
#define ICON_UTILS_H

#include <gdk-pixbuf/gdk-pixbuf.h>

/**
 * Get the icon name for a given player
 *
 * Maps player names (from MPRIS) to their corresponding icon theme names.
 * Examples:
 *   "spotify" -> "spotify-client"
 *   "mpv" -> "mpv"
 *   "vlc" -> "vlc"
 *
 * @param player_name MPRIS player name (e.g., "spotify", "mpv")
 * @return Icon theme name, or "audio-player" as fallback
 */
const char* icon_utils_get_player_icon_name(const char *player_name);

/**
 * Load player icon from GTK icon theme
 *
 * Attempts to load the player's icon from the system icon theme.
 * Falls back to "audio-player" if the specific icon is not found.
 *
 * @param player_name MPRIS player name (e.g., "spotify", "mpv")
 * @param size Icon size in pixels (e.g., 16, 24, 48)
 * @return GdkPixbuf containing the icon, or NULL on failure. Caller must unref.
 */
GdkPixbuf* icon_utils_load_player_icon(const char *player_name, int size);

/**
 * Add a badge overlay to a pixbuf (bottom-right corner)
 *
 * Composites a small badge icon onto the bottom-right corner of a base image.
 * Useful for adding player icons to album art or status indicators to app icons.
 *
 * The badge is scaled to 1/4 of the base image size and positioned with 2px padding
 * from the right and bottom edges.
 *
 * @param base Base image (e.g., album art). Must not be NULL.
 * @param badge Badge image (e.g., player icon). If NULL, returns copy of base.
 * @return New GdkPixbuf with badge overlay, or NULL on failure. Caller must unref.
 */
GdkPixbuf* icon_utils_add_badge(const GdkPixbuf *base, const GdkPixbuf *badge);

#endif // ICON_UTILS_H
