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

// One album art lookup that has to leave the main thread (opaque; see below).
struct tray_art_job;

// Main thread. Resolve album art from what cannot block - the per-track cache,
// or a copy of the previous track's cache when the art URL is unchanged - and
// set the tray icon. Nothing that downloads or runs ffmpeg is tried here: when
// that is needed, a job is returned for the caller to run on a worker thread
// and then apply. NULL means the icon is settled (applied, or default).
struct tray_art_job *system_tray_art_prepare(const struct tray_art_request *request);

// Worker thread. Resolve the job's artwork into its cache file from the MPRIS
// art URL, the local file's embedded cover, a local video frame, then (if
// enabled) the iTunes API. Blocking (network, ffmpeg), no GTK. Stops early when
// the calling thread is cancelled (thread_cancel_bind).
void system_tray_art_run(struct tray_art_job *job);

// Main thread, after the worker is joined. Set the tray icon from what the job
// found, or the default icon if it found nothing.
void system_tray_art_apply(const struct tray_art_job *job);

// The request the job was prepared for (the job's own copy; valid until freed).
const struct tray_art_request *system_tray_art_request(const struct tray_art_job *job);

// Free a job, applied or not. NULL is allowed.
void system_tray_art_free(struct tray_art_job *job);

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
