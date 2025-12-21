#ifndef _LYRICS_SYSTEM_TRAY_H
#define _LYRICS_SYSTEM_TRAY_H

#include <stdbool.h>

// Initialize system tray icon
bool system_tray_init(void);

// Update tray icon with album art URL (artUrl from MPRIS)
// Returns true if icon was updated successfully
bool system_tray_update_icon(const char *art_url);

// Update tray icon with iTunes fallback
// Uses iTunes Search API if MPRIS art_url is unavailable
// Returns true if icon was updated successfully
bool system_tray_update_icon_with_fallback(const char *art_url, const char *artist, const char *album, const char *track);

// Reset icon to default (called before track change)
void system_tray_reset_icon(void);

// Update tooltip text (e.g., "Artist - Title")
void system_tray_update_tooltip(const char *text);

// Set overlay state and update icon (enabled: normal icon, disabled: headphones + red X)
void system_tray_set_overlay_state(bool enabled);

// Send desktop notification for track change
void system_tray_send_notification(const char *artist, const char *title);

// Update the system tray (process GTK events)
// Should be called periodically from main loop
void system_tray_update(void);

// Cleanup system tray resources
void system_tray_cleanup(void);

#endif
