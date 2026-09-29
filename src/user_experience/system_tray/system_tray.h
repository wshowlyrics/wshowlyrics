// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _LYRICS_SYSTEM_TRAY_H
#define _LYRICS_SYSTEM_TRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

// Forward declaration
struct lyrics_state;

// Notification metadata for desktop notifications
struct notification_info {
    const char *title;
    const char *artist;
    const char *album;
    const char *player_name;
};

// Initialize system tray icon
bool system_tray_init(void);

// Set lyrics state for menu callbacks (must be called after init)
void system_tray_set_state(struct lyrics_state *state);

// Update track info displayed in context menu
void system_tray_update_track_info(const char *artist, const char *title);

// Update tray icon with album art URL (artUrl from MPRIS)
// Returns true if icon was updated successfully
bool system_tray_update_icon(const char *art_url);

// Result of the fast (non-network) artwork resolution.
enum tray_art_status {
    TRAY_ART_APPLIED,      // a fast source hit; the tray icon was set
    TRAY_ART_NEED_ITUNES,  // fast sources missed; resolve iTunes off-thread
    TRAY_ART_NONE,         // no source and iTunes disabled; default icon set
};

// Resolve album art from the FAST sources only (cache, MPRIS artUrl, local
// embedded cover, local video thumbnail) and set the tray icon on success.
// The iTunes API (network) is NOT tried here — on a miss this returns
// TRAY_ART_NEED_ITUNES and fills md5_out/cache_out for the caller to resolve on
// a worker thread (system_tray_resolve_itunes_art) and then apply with
// system_tray_apply_cached_icon. Main thread only.
enum tray_art_status system_tray_update_icon_fast(const char *art_url, const char *file_url,
                                                  const char *artist, const char *album,
                                                  const char *track, char *md5_out, size_t md5_sz,
                                                  char *cache_out, size_t cache_sz);

// Worker-safe: resolve iTunes artwork into out_path ({md5}.png). Blocking
// network, no GTK. Honors the cancel flag set via system_tray_set_art_cancel_flag.
bool system_tray_resolve_itunes_art(const char *artist, const char *album,
                                    const char *track, const char *out_path);

// Render an image URL to a PNG at out_path (48x48 + circular mask). Worker-safe.
bool system_tray_render_art_to_cache(const char *art_url, const char *out_path);

// Main thread: apply a resolved cache PNG ({md5}.png) as the tray icon.
void system_tray_apply_cached_icon(const char *metadata_hash);

// Point the artwork worker's in-flight image download at a cancel flag (or NULL).
void system_tray_set_art_cancel_flag(_Atomic bool *flag);

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
