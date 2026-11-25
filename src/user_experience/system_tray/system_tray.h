// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _LYRICS_SYSTEM_TRAY_H
#define _LYRICS_SYSTEM_TRAY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

// Result of the main-thread (non-blocking) artwork resolution.
enum tray_art_status {
    TRAY_ART_APPLIED,      // the cache had it; the tray icon was set
    TRAY_ART_NEED_WORKER,  // not cached; resolve it off-thread
    TRAY_ART_NONE,         // no source to try; default icon set
};

// What to resolve album art for: where the player says the art and the media
// are, and the metadata the cache key and the iTunes search are built from.
// Any field may be NULL.
struct tray_art_request {
    const char *art_url;   // mpris:artUrl
    const char *file_url;  // xesam:url (file:// for local media)
    const char *artist;
    const char *album;
    const char *track;
    int64_t length_us;     // mpris:length, 0 if unknown (places the video thumbnail)
};

// Resolve album art from what cannot block — the per-track cache, or a copy of
// the previous track's cache when the art URL is unchanged — and set the tray
// icon on success. Nothing that downloads or runs ffmpeg is tried here: on a
// miss this returns TRAY_ART_NEED_WORKER and fills md5_out/cache_out for the
// caller to resolve on a worker thread (system_tray_resolve_art) and then apply
// with system_tray_apply_cached_icon. Main thread only.
enum tray_art_status system_tray_update_icon_fast(const struct tray_art_request *request,
                                                  char *md5_out, size_t md5_sz,
                                                  char *cache_out, size_t cache_sz);

// Worker-safe: resolve artwork into out_path ({md5}.png) from the MPRIS art
// URL, the local file's embedded cover, a local video frame, then (if
// use_itunes) the iTunes API. Blocking (network, ffmpeg), no GTK. Stops early when
// the calling thread is cancelled (thread_cancel_bind). *from_art_url tells
// whether the image is the art URL's, for system_tray_apply_cached_icon.
bool system_tray_resolve_art(const struct tray_art_request *request, bool use_itunes,
                             const char *out_path, bool *from_art_url);

// Main thread: apply a resolved cache PNG ({md5}.png) as the tray icon. Pass
// the art URL the image was downloaded from (NULL for any other source) so the
// next track with the same URL reuses it. Returns false if the file is unusable.
bool system_tray_apply_cached_icon(const char *metadata_hash, const char *art_url);

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
