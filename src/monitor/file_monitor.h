#ifndef FILE_MONITOR_H
#define FILE_MONITOR_H

#include "../main.h"
#include <stdbool.h>
#include <stddef.h>

// Forward declaration
struct lyrics_state;

/**
 * Callback type for file reload operations
 *
 * @param state Lyrics state
 * @param path Path to file that was reloaded
 */
typedef void (*file_reload_callback_t)(struct lyrics_state *state, const char *path);

/**
 * Check if a file has changed and reload if necessary
 * Uses MD5 checksum to detect changes
 *
 * @param file_path Path to file to monitor
 * @param stored_checksum Buffer containing stored MD5 checksum (updated on reload)
 * @param checksum_size Size of checksum buffer (must be at least 33 bytes for MD5)
 * @param file_type_name Human-readable name for logging (e.g., "Lyrics", "Config")
 * @param reload_callback Function to call if file changed
 * @param state Lyrics state to pass to callback
 * @return true if file was reloaded, false otherwise
 */
bool file_monitor_check_and_reload(
    const char *file_path,
    char *stored_checksum,
    size_t checksum_size,
    const char *file_type_name,
    file_reload_callback_t reload_callback,
    struct lyrics_state *state
);

/**
 * Reload lyrics file callback
 *
 * @param state Lyrics state
 * @param path Path to lyrics file
 */
void file_monitor_reload_lyrics(struct lyrics_state *state, const char *path);

/**
 * Reload config file callback
 *
 * @param state Lyrics state
 * @param path Path to config file
 */
void file_monitor_reload_config(struct lyrics_state *state, const char *path);

#endif // FILE_MONITOR_H
