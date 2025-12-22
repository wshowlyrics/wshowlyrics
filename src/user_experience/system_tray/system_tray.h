#ifndef _LYRICS_SYSTEM_TRAY_H
#define _LYRICS_SYSTEM_TRAY_H

#include <stdbool.h>

// Notification metadata for desktop notifications
struct notification_info {
    const char *title;
    const char *artist;
    const char *album;
    const char *player_name;
};

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

// Set overlay state and update icon (enabled: normal icon, disabled: headphones + red X)
void system_tray_set_overlay_state(bool enabled);

// Send desktop notification for track change
// Title: "🎵 Title"
// Body: "Album · Artist\nPlayer" (shows "Unknown" for missing metadata, capitalizes player name)
void system_tray_send_notification(const struct notification_info *info);

// Update the system tray (process GTK events)
// Should be called periodically from main loop
void system_tray_update(void);

// Cleanup system tray resources
void system_tray_cleanup(void);

#endif
