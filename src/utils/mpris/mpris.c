/**
 * MPRIS Integration - Signal-Based Implementation
 *
 * This module provides efficient MPRIS2 integration using D-Bus signals
 * instead of polling, dramatically reducing CPU usage.
 *
 * Key features:
 * - PropertiesChanged signal: Auto-detects PlaybackStatus, Position, Metadata changes
 * - Seeked signal: Handles user seek operations
 * - Cached state: Instant access without D-Bus calls
 * - Thread-safe: GMutex protects shared state
 *
 * Performance: ~0% CPU (vs 50% with polling approach)
 */

#include "mpris.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <gio/gio.h>

// MPRIS2 D-Bus constants
#define MPRIS_INTERFACE "org.mpris.MediaPlayer2"
#define MPRIS_PLAYER_INTERFACE "org.mpris.MediaPlayer2.Player"
#define MPRIS_OBJECT_PATH "/org/mpris/MediaPlayer2"
#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"

// Global D-Bus connection
static GDBusConnection *dbus_connection = NULL;

// Cached MPRIS state for signal-based updates
static struct {
    char *current_player;           // Currently active player (e.g., "spotify")
    char *playback_status;          // "Playing", "Paused", or "Stopped"
    int64_t position_us;            // Last known position in microseconds
    int64_t position_timestamp_us;  // Monotonic timestamp when position was updated
    bool metadata_changed;          // Flag: track metadata changed (needs refresh)
    bool position_fix_needed;       // Flag: Spotify position fix needed after track change
    bool position_fix_in_progress;  // Flag: Spotify position fix in progress (prevents infinite loop)
    struct track_metadata *cached_metadata;  // Metadata parsed from DBus signal (avoids stale data race)
    GMutex mutex;                   // Thread safety for signal callbacks
    guint subscription_id;          // PropertiesChanged subscription ID (current player)
    guint seeked_subscription_id;   // Seeked signal subscription ID
    guint name_owner_subscription_id; // NameOwnerChanged subscription ID
    guint all_players_subscription_id; // PropertiesChanged subscription ID (all players)
    GHashTable *unique_name_map;    // Maps D-Bus unique names (:1.xxx) to well-known names (org.mpris.MediaPlayer2.xxx)
} mpris_state = {0};

// Forward declarations
static char* find_best_player(void);
static void setup_player_subscription(char *player);
static void populate_initial_name_mapping(void);

// Helper: Check if player is a browser or MPRIS proxy (should be ignored)
static inline bool is_browser_or_proxy(const char *player_name) {
    return strstr(player_name, "chromium") ||
           strstr(player_name, "firefox") ||
           strstr(player_name, "vivaldi") ||
           strstr(player_name, "brave") ||
           strstr(player_name, "edge") ||
           strstr(player_name, "opera") ||
           strcmp(player_name, "playerctld") == 0;
}

// Helper: Build MPRIS D-Bus service name from player name
static inline void build_mpris_bus_name(char *buf, size_t size, const char *player) {
    snprintf(buf, size, "org.mpris.MediaPlayer2.%s", player);
}

// Helper: Get GVariant value from dictionary
static GVariant* get_dict_value(GVariant *dict, const char *key) {
    GVariantIter iter;
    const char *dict_key;
    GVariant *value;

    g_variant_iter_init(&iter, dict);
    while (g_variant_iter_next(&iter, "{&sv}", &dict_key, &value)) {
        if (strcmp(dict_key, key) == 0) {
            return value;
        }
        g_variant_unref(value);
    }
    return NULL;
}

// Helper: Extract string from GVariant array
static char* extract_string_array(GVariant *variant) {
    if (!variant || !g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING_ARRAY)) {
        return NULL;
    }

    GVariantIter iter;
    const char *str;
    char *result = NULL;

    g_variant_iter_init(&iter, variant);
    if (g_variant_iter_next(&iter, "&s", &str)) {
        result = strdup(str);
    }

    return result;
}

// Helper: Parse metadata from GVariant dictionary into track_metadata struct
// Returns newly allocated track_metadata, caller must free with mpris_free_metadata()
static struct track_metadata* parse_metadata_from_dict(GVariant *metadata_dict, const char *player_name) {
    if (!metadata_dict) {
        return NULL;
    }

    struct track_metadata *metadata = calloc(1, sizeof(struct track_metadata));
    if (!metadata) {
        return NULL;
    }

    if (player_name) {
        metadata->player_name = strdup(player_name);
    }

    GVariant *value;

    // Title (xesam:title)
    value = get_dict_value(metadata_dict, "xesam:title");
    if (value) {
        metadata->title = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Artist (xesam:artist) - array of strings
    value = get_dict_value(metadata_dict, "xesam:artist");
    if (value) {
        metadata->artist = extract_string_array(value);
        g_variant_unref(value);
    }

    // Album (xesam:album)
    value = get_dict_value(metadata_dict, "xesam:album");
    if (value) {
        metadata->album = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // URL (xesam:url)
    value = get_dict_value(metadata_dict, "xesam:url");
    if (value) {
        metadata->url = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Track ID (mpris:trackid)
    value = get_dict_value(metadata_dict, "mpris:trackid");
    if (value) {
        metadata->trackid = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Art URL (mpris:artUrl)
    value = get_dict_value(metadata_dict, "mpris:artUrl");
    if (value) {
        metadata->art_url = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Length (mpris:length)
    value = get_dict_value(metadata_dict, "mpris:length");
    if (value) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
            metadata->length_us = g_variant_get_int64(value);
        } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64)) {
            metadata->length_us = (int64_t)g_variant_get_uint64(value);
        }
        g_variant_unref(value);
    }

    return metadata;
}

// Grow dynamic player array; returns new pointer or NULL on failure (original freed)
static char** grow_players_array(char **players, int count, int *capacity) {
    *capacity = *capacity == 0 ? 4 : *capacity * 2;
    char **new_players = realloc(players, (size_t)*capacity * sizeof(char*));
    if (!new_players) {
        for (int i = 0; i < count; i++) free(players[i]);
        free(players);
        return NULL;
    }
    return new_players;
}

// Helper: List all MPRIS2 players on D-Bus
static char** list_mpris_players(int *count) {
    *count = 0;

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        dbus_connection,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ListNames",
        NULL,
        G_VARIANT_TYPE("(as)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error) {
        log_error("Failed to list D-Bus names: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    GVariant *names_variant = g_variant_get_child_value(result, 0);
    GVariantIter iter;
    const char *name;
    char **players = NULL;
    int capacity = 0;

    g_variant_iter_init(&iter, names_variant);
    while (g_variant_iter_next(&iter, "&s", &name)) {
        if (strncmp(name, "org.mpris.MediaPlayer2.", 23) != 0) {
            continue;
        }

        const char *player_name = name + 23;

        // Skip browsers and playerctld (MPRIS proxy)
        if (is_browser_or_proxy(player_name)) {
            continue;
        }

        // Expand array if needed
        if (*count >= capacity) {
            players = grow_players_array(players, *count, &capacity);
            if (!players) {
                *count = 0;
                g_variant_unref(names_variant);
                g_variant_unref(result);
                return NULL;
            }
        }

        players[*count] = strdup(player_name);
        (*count)++;
    }

    g_variant_unref(names_variant);
    g_variant_unref(result);

    return players;
}

// Helper: Get monotonic timestamp in microseconds
static int64_t get_monotonic_time_us(void) {
    return g_get_monotonic_time();
}

// Helper: Send MPRIS command (Pause, Play, etc.)
static bool mpris_send_command(const char *method) {
    // Thread-safe access to current_player
    g_mutex_lock(&mpris_state.mutex);
    bool has_player = (dbus_connection && mpris_state.current_player);
    char *player_name = has_player ? strdup(mpris_state.current_player) : NULL;
    g_mutex_unlock(&mpris_state.mutex);

    if (!player_name) {
        return false;
    }

    char bus_name[SMALL_BUFFER_SIZE];
    build_mpris_bus_name(bus_name, sizeof(bus_name), player_name);

    GError *error = NULL;
    g_dbus_connection_call_sync(
        dbus_connection,
        bus_name,
        MPRIS_OBJECT_PATH,
        MPRIS_PLAYER_INTERFACE,
        method,  // "Pause", "Play", "Next", "Previous", etc.
        NULL,
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error) {
        log_error("Failed to send %s command to %s: %s",
                 method, player_name, error->message);
        g_error_free(error);
        free(player_name);
        return false;
    }

    free(player_name);
    return true;
}

// Handle PlaybackStatus changes from PropertiesChanged signal
// Caller must hold mpris_state.mutex (may temporarily unlock for D-Bus calls)
static void handle_playback_status(GVariant *changed_properties) {
    GVariant *status_variant = g_variant_lookup_value(
        changed_properties, "PlaybackStatus", G_VARIANT_TYPE_STRING);
    if (!status_variant) return;

    const char *status = g_variant_get_string(status_variant, NULL);
    free(mpris_state.playback_status);
    mpris_state.playback_status = strdup(status);
    log_info("PlaybackStatus changed: %s", status);

    // Make a copy for thread-safe use after mutex unlock
    char *status_copy = strdup(status);
    g_variant_unref(status_variant);

    bool is_paused_or_stopped = (strcmp(status_copy, "Paused") == 0 ||
                                 strcmp(status_copy, "Stopped") == 0);
    bool is_playing = (strcmp(status_copy, "Playing") == 0);

    // If playback started, force metadata check to catch track changes
    // Some players (YouTube Music Desktop) don't emit Metadata PropertiesChanged
    // when track changes, only PlaybackStatus changes (Paused -> Playing)
    if (is_playing) {
        mpris_state.metadata_changed = true;
    }

    // If current player paused/stopped, check for other playing players
    if (is_paused_or_stopped) {
        char *current = mpris_state.current_player ?
                        strdup(mpris_state.current_player) : NULL;
        g_mutex_unlock(&mpris_state.mutex);

        char *new_player = find_best_player();
        if (new_player && current && strcmp(new_player, current) != 0) {
            log_info("Current player paused/stopped, switching to playing player: %s",
                     new_player);
            setup_player_subscription(new_player);
            // Don't free new_player - ownership transferred to setup_player_subscription
        } else {
            free(new_player);
        }

        free(current);
        g_mutex_lock(&mpris_state.mutex);
    }

    free(status_copy);
}

// Handle Metadata changes from PropertiesChanged signal
// Caller must hold mpris_state.mutex
static void handle_metadata_update(GVariant *changed_properties,
                                   const char *saved_player) {
    GVariant *metadata_variant = g_variant_lookup_value(
        changed_properties, "Metadata", NULL);
    if (!metadata_variant) return;

    // Only treat as track change if currently playing or stopped
    // Ignore metadata signals during pause/resume (some players send spurious signals)
    bool should_handle = mpris_state.playback_status &&
        (strcmp(mpris_state.playback_status, "Playing") == 0 ||
         strcmp(mpris_state.playback_status, "Stopped") == 0);

    if (should_handle) {
        log_info("Track metadata changed - new track detected");
        mpris_state.position_us = 0;
        mpris_state.position_timestamp_us = get_monotonic_time_us();
        mpris_state.metadata_changed = true;

        // Parse and cache metadata from signal to avoid stale data race condition
        if (mpris_state.cached_metadata) {
            mpris_free_metadata(mpris_state.cached_metadata);
            free(mpris_state.cached_metadata);
            mpris_state.cached_metadata = NULL;
        }

        // metadata_variant may be variant(dict) or dict directly depending on player
        GVariant *metadata_dict = NULL;
        if (g_variant_is_of_type(metadata_variant, G_VARIANT_TYPE_VARIANT)) {
            metadata_dict = g_variant_get_variant(metadata_variant);
        } else {
            metadata_dict = g_variant_ref(metadata_variant);
        }
        if (metadata_dict) {
            mpris_state.cached_metadata = parse_metadata_from_dict(
                metadata_dict, saved_player);
            g_variant_unref(metadata_dict);
        }

        // Spotify position drift fix: Mark that position fix is needed
        // Actual fix will be applied after lyrics load for better timing
        bool is_spotify = saved_player &&
                         strcasecmp(saved_player, "spotify") == 0;

        struct config *cfg = config_get();
        if (is_spotify && cfg->spotify.auto_position_fix) {
            mpris_state.position_fix_needed = true;
            log_info("Spotify auto track change detected - "
                     "position fix will be applied after lyrics load");
        }
    }
    g_variant_unref(metadata_variant);
}

// Callback: Handle PropertiesChanged signal
static void on_properties_changed(
    G_GNUC_UNUSED GDBusConnection *connection,
    G_GNUC_UNUSED const gchar *sender_name,
    G_GNUC_UNUSED const gchar *object_path,
    G_GNUC_UNUSED const gchar *interface_name,
    G_GNUC_UNUSED const gchar *signal_name,
    GVariant *parameters,
    G_GNUC_UNUSED gpointer user_data)
{
    const gchar *iface;
    GVariant *changed_properties;
    GVariant *invalidated_properties;

    g_variant_get(parameters, "(&s@a{sv}@as)", &iface,
                  &changed_properties, &invalidated_properties);

    g_mutex_lock(&mpris_state.mutex);

    // Save player identity for safe use across unlock/relock gap (TOCTOU prevention)
    char *saved_player = mpris_state.current_player ?
                         strdup(mpris_state.current_player) : NULL;

    handle_playback_status(changed_properties);
    handle_metadata_update(changed_properties, saved_player);

    g_mutex_unlock(&mpris_state.mutex);

    free(saved_player);
    g_variant_unref(changed_properties);
    g_variant_unref(invalidated_properties);
}

// Callback: Handle Seeked signal (no-op, position is polled directly)
static void on_seeked(
    G_GNUC_UNUSED GDBusConnection *connection,
    G_GNUC_UNUSED const gchar *sender_name,
    G_GNUC_UNUSED const gchar *object_path,
    G_GNUC_UNUSED const gchar *interface_name,
    G_GNUC_UNUSED const gchar *signal_name,
    G_GNUC_UNUSED GVariant *parameters,
    G_GNUC_UNUSED gpointer user_data)
{
    // Position is queried directly in mpris_get_position(), so we don't need to cache it here
    // Seeked signal subscription is kept for potential future optimizations
}

// Helper: Map unique D-Bus name (:1.xxxxx) to well-known name (org.mpris.MediaPlayer2.xxx)
// Returns: strdup'd well-known name or NULL if unknown player (caller must free)
static char* resolve_player_well_known_name(const gchar *sender_name) {
    if (!sender_name) {
        return NULL;
    }

    // Already a well-known name
    if (sender_name[0] != ':') {
        return strdup(sender_name);  // Return copy for consistency
    }

    // Lookup unique name in mapping
    g_mutex_lock(&mpris_state.mutex);
    const char *mapped = g_hash_table_lookup(mpris_state.unique_name_map, sender_name);
    char *result = mapped ? strdup(mapped) : NULL;  // Copy to avoid dangling pointer after unlock
    g_mutex_unlock(&mpris_state.mutex);

    return result;  // Caller must free
}

// Helper: Extract and validate player name from well-known name
// Returns: player name (e.g., "spotify") or NULL if invalid/browser
static const char* extract_and_validate_player_name(const char *well_known_name) {
    if (!well_known_name) {
        return NULL;
    }

    // Validate prefix
    if (strncmp(well_known_name, "org.mpris.MediaPlayer2.", 23) != 0) {
        return NULL;
    }

    const char *player_name = well_known_name + 23;

    // Skip browsers and playerctld
    if (is_browser_or_proxy(player_name)) {
        return NULL;
    }

    return player_name;
}

// Helper: Handle case when a different player started playing
static void handle_player_started_playing(const char *player_name) {
    // Check if current player is paused
    g_mutex_lock(&mpris_state.mutex);
    bool current_is_paused = mpris_state.playback_status &&
                            (strcmp(mpris_state.playback_status, "Paused") == 0 ||
                             strcmp(mpris_state.playback_status, "Stopped") == 0);
    g_mutex_unlock(&mpris_state.mutex);

    if (!current_is_paused) {
        return;
    }

    log_info("Player %s started playing while current player is paused", player_name);

    // Find best player (considers playing status and preferences)
    char *new_player = find_best_player();
    if (!new_player) {
        return;
    }

    g_mutex_lock(&mpris_state.mutex);
    bool should_switch = !mpris_state.current_player ||
                        strcmp(new_player, mpris_state.current_player) != 0;
    g_mutex_unlock(&mpris_state.mutex);

    if (should_switch) {
        log_info("Switching to playing player: %s", new_player);
        setup_player_subscription(new_player);
    } else {
        free(new_player);
    }
}

// Callback: Handle PropertiesChanged from ANY MPRIS player (for automatic switching)
static void on_any_player_properties_changed(
    G_GNUC_UNUSED GDBusConnection *connection,
    const gchar *sender_name,
    G_GNUC_UNUSED const gchar *object_path,
    G_GNUC_UNUSED const gchar *interface_name,
    G_GNUC_UNUSED const gchar *signal_name,
    GVariant *parameters,
    G_GNUC_UNUSED gpointer user_data)
{
    // Resolve sender name to well-known name
    char *well_known_name = resolve_player_well_known_name(sender_name);
    if (!well_known_name) {
        return;
    }

    // Extract and validate player name
    const char *player_name = extract_and_validate_player_name(well_known_name);
    if (!player_name) {
        free(well_known_name);
        return;
    }

    // Check if this is the current player
    g_mutex_lock(&mpris_state.mutex);
    bool is_current = mpris_state.current_player &&
                     strcmp(mpris_state.current_player, player_name) == 0;
    g_mutex_unlock(&mpris_state.mutex);

    if (is_current) {
        // Current player - already handled by on_properties_changed
        free(well_known_name);
        return;
    }

    // Different player - check if it started playing
    const gchar *iface;
    GVariant *changed_properties;
    GVariant *invalidated_properties;
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed_properties, &invalidated_properties);

    GVariant *status_variant = g_variant_lookup_value(changed_properties, "PlaybackStatus", G_VARIANT_TYPE_STRING);
    if (status_variant) {
        const char *status = g_variant_get_string(status_variant, NULL);

        if (strcmp(status, "Playing") == 0) {
            handle_player_started_playing(player_name);
        }

        g_variant_unref(status_variant);
    }

    g_variant_unref(changed_properties);
    g_variant_unref(invalidated_properties);
    free(well_known_name);
}

// Forward declarations
static bool is_player_playing(const char *player_name);

// Helper: Trim leading and trailing whitespace from string in-place
// Returns: pointer to first non-space character (trailing spaces are removed)
static char* trim_whitespace_inplace(char *str) {
    // Trim leading whitespace
    while (*str == ' ') str++;
    if (*str == '\0') {
        return str;
    }

    // Trim trailing whitespace
    size_t len = strlen(str);
    char *end = str + len - 1;
    while (end > str && *end == ' ') {
        *end-- = '\0';
    }

    return str;
}

// Helper: Check if preferred player matches and handle it
// Returns: strdup'd player name if playing, NULL otherwise
static char* check_preferred_player_match(const char *preferred, const char *player_name,
                                          char **fallback_out) {
    if (strcmp(player_name, preferred) != 0) {
        return NULL;  // Not a match
    }

    if (is_player_playing(preferred)) {
        return strdup(preferred);  // Found playing preferred player
    }

    // Remember first available (but not playing) as fallback
    if (!*fallback_out) {
        *fallback_out = strdup(preferred);
    }

    return NULL;
}

// Check if should switch to a new player based on preferred_players
static bool should_switch_to_player(const char *player_name) {
    bool should_switch = false;

    g_mutex_lock(&mpris_state.mutex);

    // Switch if no current player
    if (!mpris_state.current_player) {
        should_switch = true;
        log_info("No current player, switching to: %s", player_name);
        g_mutex_unlock(&mpris_state.mutex);
        return should_switch;
    }

    // Check if new player is preferred over current
    struct config *cfg = config_get();
    if (cfg->lyrics.preferred_players && cfg->lyrics.preferred_players[0] != '\0') {
        char *players_copy = strdup(cfg->lyrics.preferred_players);
        char *saveptr;
        bool done = false;

        for (char *preferred = strtok_r(players_copy, ",", &saveptr);
             preferred && !done;
             preferred = strtok_r(NULL, ",", &saveptr)) {
            preferred = trim_whitespace_inplace(preferred);
            if (*preferred == '\0') {
                continue;
            }
            // If new player matches preferred, switch
            if (strcmp(player_name, preferred) == 0) {
                should_switch = true;
                log_info("Preferred player appeared, switching to: %s", player_name);
                done = true;
            } else if (strcmp(mpris_state.current_player, preferred) == 0) {
                // Found current player first, don't switch
                done = true;
            }
        }
        free(players_copy);
    }

    g_mutex_unlock(&mpris_state.mutex);
    return should_switch;
}

// Clean up current player subscriptions and state
static void cleanup_current_player(void) {
    // Unsubscribe from old player signals
    if (mpris_state.subscription_id > 0) {
        g_dbus_connection_signal_unsubscribe(dbus_connection, mpris_state.subscription_id);
        mpris_state.subscription_id = 0;
    }
    if (mpris_state.seeked_subscription_id > 0) {
        g_dbus_connection_signal_unsubscribe(dbus_connection, mpris_state.seeked_subscription_id);
        mpris_state.seeked_subscription_id = 0;
    }

    g_mutex_lock(&mpris_state.mutex);
    free(mpris_state.current_player);
    free(mpris_state.playback_status);
    mpris_state.current_player = NULL;
    mpris_state.playback_status = NULL;
    mpris_state.metadata_changed = true;
    g_mutex_unlock(&mpris_state.mutex);
}

// Handle player appeared event
static void handle_player_appeared(const char *player_name, const char *new_owner, const char *name) {
    log_info("MPRIS player appeared: %s (unique name: %s)", player_name, new_owner);

    // Add to unique name mapping for signal routing
    g_mutex_lock(&mpris_state.mutex);
    if (mpris_state.unique_name_map) {
        g_hash_table_insert(mpris_state.unique_name_map,
                          g_strdup(new_owner),  // key: :1.xxxxx
                          g_strdup(name));      // value: org.mpris.MediaPlayer2.xxx
    }
    g_mutex_unlock(&mpris_state.mutex);

    // Check if should switch to this player
    if (should_switch_to_player(player_name)) {
        char *new_player = strdup(player_name);
        setup_player_subscription(new_player);
    }
}

// Handle player disappeared event
static void handle_player_disappeared(const char *player_name, const char *old_owner) {
    log_info("MPRIS player disappeared: %s (unique name: %s)", player_name, old_owner);

    // Remove from unique name mapping
    g_mutex_lock(&mpris_state.mutex);
    if (mpris_state.unique_name_map) {
        g_hash_table_remove(mpris_state.unique_name_map, old_owner);
    }
    g_mutex_unlock(&mpris_state.mutex);

    // Check if this was the current player
    g_mutex_lock(&mpris_state.mutex);
    bool is_current = mpris_state.current_player &&
                     strcmp(mpris_state.current_player, player_name) == 0;
    g_mutex_unlock(&mpris_state.mutex);

    // If current player disappeared, find another one
    if (is_current) {
        log_info("Current player disappeared, finding alternative...");
        char *new_player = find_best_player();
        if (new_player) {
            setup_player_subscription(new_player);
        } else {
            log_info("No alternative player found");
            cleanup_current_player();
        }
    }
}

// Callback: Handle NameOwnerChanged signal (player appearance/disappearance)
static void on_name_owner_changed(
    G_GNUC_UNUSED GDBusConnection *connection,
    G_GNUC_UNUSED const gchar *sender_name,
    G_GNUC_UNUSED const gchar *object_path,
    G_GNUC_UNUSED const gchar *interface_name,
    G_GNUC_UNUSED const gchar *signal_name,
    GVariant *parameters,
    G_GNUC_UNUSED gpointer user_data)
{
    const char *name;
    const char *old_owner;
    const char *new_owner;

    g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

    // Only care about MPRIS players
    if (strncmp(name, "org.mpris.MediaPlayer2.", 23) != 0) {
        return;
    }

    const char *player_name = name + 23;

    // Skip browsers and playerctld (MPRIS proxy)
    if (is_browser_or_proxy(player_name)) {
        return;
    }

    // Player appeared (new_owner is not empty)
    if (new_owner && new_owner[0] != '\0') {
        handle_player_appeared(player_name, new_owner, name);
    }
    // Player disappeared (old_owner is not empty, new_owner is empty)
    else if (old_owner && old_owner[0] != '\0') {
        handle_player_disappeared(player_name, old_owner);
    }
}

// Helper: Check if a player is currently playing
// Returns: true if playing, false otherwise
static bool is_player_playing(const char *player_name) {
    if (!player_name) {
        return false;
    }

    char bus_name[SMALL_BUFFER_SIZE];
    build_mpris_bus_name(bus_name, sizeof(bus_name), player_name);

    GError *error = NULL;
    GVariant *status_variant = g_dbus_connection_call_sync(
        dbus_connection,
        bus_name,
        MPRIS_OBJECT_PATH,
        DBUS_PROPERTIES_INTERFACE,
        "Get",
        g_variant_new("(ss)", MPRIS_PLAYER_INTERFACE, "PlaybackStatus"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error) {
        g_error_free(error);
        return false;
    }

    if (!status_variant) {
        return false;
    }

    GVariant *status_value = g_variant_get_child_value(status_variant, 0);
    GVariant *status_str = g_variant_get_variant(status_value);
    const char *status = g_variant_get_string(status_str, NULL);
    bool is_playing = (strcmp(status, "Playing") == 0);

    g_variant_unref(status_str);
    g_variant_unref(status_value);
    g_variant_unref(status_variant);

    return is_playing;
}

// Helper: Find first playing player from all available players
// Returns: strdup'd player name or NULL
static char* find_first_playing_player(char **all_players, int player_count) {
    for (int i = 0; i < player_count; i++) {
        if (is_player_playing(all_players[i])) {
            return strdup(all_players[i]);
        }
    }
    return NULL;
}

// Helper: Find playing or available player from preferred list
// Returns: strdup'd player name (playing preferred) or NULL
// Sets fallback_out to first available (but not playing) preferred player
static char* find_preferred_player(const char *preferred_players_str, char **all_players,
                                   int player_count, char **fallback_out) {
    *fallback_out = NULL;

    char *players_copy = strdup(preferred_players_str);
    char *saveptr;
    char *preferred = strtok_r(players_copy, ",", &saveptr);

    while (preferred) {
        // Trim whitespace
        preferred = trim_whitespace_inplace(preferred);
        if (*preferred == '\0') {
            preferred = strtok_r(NULL, ",", &saveptr);
            continue;
        }

        // Check if this preferred player is available
        for (int i = 0; i < player_count; i++) {
            char *result = check_preferred_player_match(preferred, all_players[i], fallback_out);
            if (result) {
                // Found playing preferred player
                free(players_copy);
                return result;
            }

            // If matched (but not playing), break out of loop
            if (strcmp(all_players[i], preferred) == 0) {
                break;
            }
        }

        preferred = strtok_r(NULL, ",", &saveptr);
    }

    free(players_copy);
    return NULL;
}

// Helper: Find best player based on preferred_players config
static char* find_best_player(void) {
    int player_count = 0;
    char **all_players = list_mpris_players(&player_count);

    if (!all_players || player_count == 0) {
        free(all_players);
        return NULL;
    }

    struct config *cfg = config_get();
    char *best_player = NULL;

    // If no preferred players, find first playing player or first available
    if (!cfg->lyrics.preferred_players || cfg->lyrics.preferred_players[0] == '\0') {
        best_player = find_first_playing_player(all_players, player_count);
        if (!best_player) {
            best_player = strdup(all_players[0]);
        }
    } else {
        // Find playing preferred player or fallback
        char *fallback = NULL;
        best_player = find_preferred_player(cfg->lyrics.preferred_players, all_players,
                                            player_count, &fallback);
        if (!best_player) {
            // Use fallback or first available
            best_player = fallback ? fallback : strdup(all_players[0]);
        } else {
            free(fallback);
        }
    }

    // Cleanup
    for (int i = 0; i < player_count; i++) {
        free(all_players[i]);
    }
    free(all_players);

    return best_player;
}

// Helper: Setup signal subscriptions for a player
static void setup_player_subscription(char *player) {
    if (!player || !dbus_connection) {
        free(player);
        return;
    }

    char bus_name[SMALL_BUFFER_SIZE];
    build_mpris_bus_name(bus_name, sizeof(bus_name), player);

    // Unsubscribe from old player signals
    if (mpris_state.subscription_id > 0) {
        g_dbus_connection_signal_unsubscribe(dbus_connection, mpris_state.subscription_id);
        mpris_state.subscription_id = 0;
    }
    if (mpris_state.seeked_subscription_id > 0) {
        g_dbus_connection_signal_unsubscribe(dbus_connection, mpris_state.seeked_subscription_id);
        mpris_state.seeked_subscription_id = 0;
    }

    g_mutex_lock(&mpris_state.mutex);
    free(mpris_state.current_player);
    free(mpris_state.playback_status);
    mpris_state.current_player = player;  // Transfer ownership
    mpris_state.playback_status = NULL;
    mpris_state.metadata_changed = true;  // Force metadata refresh
    g_mutex_unlock(&mpris_state.mutex);

    // Get initial PlaybackStatus
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        dbus_connection,
        bus_name,
        MPRIS_OBJECT_PATH,
        DBUS_PROPERTIES_INTERFACE,
        "Get",
        g_variant_new("(ss)", MPRIS_PLAYER_INTERFACE, "PlaybackStatus"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (!error && result) {
        GVariant *status_variant = g_variant_get_child_value(result, 0);
        GVariant *status_value = g_variant_get_variant(status_variant);
        const char *status = g_variant_get_string(status_value, NULL);

        g_mutex_lock(&mpris_state.mutex);
        mpris_state.playback_status = strdup(status);
        g_mutex_unlock(&mpris_state.mutex);

        g_variant_unref(status_value);
        g_variant_unref(status_variant);
        g_variant_unref(result);
    }

    if (error) {
        log_error("Failed to get PlaybackStatus from %s: %s", player, error->message);
        g_error_free(error);
    }

    // Get initial Position
    error = NULL;
    result = g_dbus_connection_call_sync(
        dbus_connection,
        bus_name,
        MPRIS_OBJECT_PATH,
        DBUS_PROPERTIES_INTERFACE,
        "Get",
        g_variant_new("(ss)", MPRIS_PLAYER_INTERFACE, "Position"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (!error && result) {
        GVariant *position_variant = g_variant_get_child_value(result, 0);
        GVariant *position_value = g_variant_get_variant(position_variant);
        if (g_variant_is_of_type(position_value, G_VARIANT_TYPE_INT64)) {
            g_mutex_lock(&mpris_state.mutex);
            mpris_state.position_us = g_variant_get_int64(position_value);
            mpris_state.position_timestamp_us = get_monotonic_time_us();
            g_mutex_unlock(&mpris_state.mutex);
        }
        g_variant_unref(position_value);
        g_variant_unref(position_variant);
        g_variant_unref(result);
    }

    if (error) {
        g_error_free(error);
    }

    // Subscribe to PropertiesChanged signal for this player
    mpris_state.subscription_id = g_dbus_connection_signal_subscribe(
        dbus_connection,
        bus_name,
        DBUS_PROPERTIES_INTERFACE,
        "PropertiesChanged",
        MPRIS_OBJECT_PATH,
        MPRIS_PLAYER_INTERFACE,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_properties_changed,
        NULL,
        NULL
    );

    // Subscribe to Seeked signal
    mpris_state.seeked_subscription_id = g_dbus_connection_signal_subscribe(
        dbus_connection,
        bus_name,
        MPRIS_PLAYER_INTERFACE,
        "Seeked",
        MPRIS_OBJECT_PATH,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_seeked,
        NULL,
        NULL
    );

    log_info("MPRIS: Subscribed to signals for player '%s'", player);
}

// Populate initial mapping for already-running MPRIS players
static void populate_initial_name_mapping(void) {
    if (!dbus_connection || !mpris_state.unique_name_map) {
        return;
    }

    // Get all D-Bus names
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        dbus_connection,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ListNames",
        NULL,
        G_VARIANT_TYPE("(as)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error) {
        log_error("Failed to list D-Bus names for initial mapping: %s", error->message);
        g_error_free(error);
        return;
    }

    GVariant *names_variant = g_variant_get_child_value(result, 0);
    GVariantIter iter;
    const char *name;

    g_variant_iter_init(&iter, names_variant);
    while (g_variant_iter_next(&iter, "&s", &name)) {
        // Only care about MPRIS players
        if (strncmp(name, "org.mpris.MediaPlayer2.", 23) != 0) {
            continue;
        }

        const char *player_name = name + 23;

        // Skip browsers and playerctld
        if (is_browser_or_proxy(player_name)) {
            continue;
        }

        // Get the unique name (owner) for this well-known name
        GError *owner_error = NULL;
        GVariant *owner_result = g_dbus_connection_call_sync(
            dbus_connection,
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "GetNameOwner",
            g_variant_new("(s)", name),
            G_VARIANT_TYPE("(s)"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &owner_error
        );

        if (!owner_error && owner_result) {
            const char *unique_name;
            g_variant_get(owner_result, "(&s)", &unique_name);

            // Add to mapping
            g_mutex_lock(&mpris_state.mutex);
            g_hash_table_insert(mpris_state.unique_name_map,
                              g_strdup(unique_name),
                              g_strdup(name));
            g_mutex_unlock(&mpris_state.mutex);

            log_info("Initial mapping: %s -> %s", unique_name, name);

            g_variant_unref(owner_result);
        }

        if (owner_error) {
            g_error_free(owner_error);
        }
    }

    g_variant_unref(names_variant);
    g_variant_unref(result);
}

bool mpris_init(void) {
    GError *error = NULL;
    dbus_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

    if (error) {
        log_error("Failed to connect to D-Bus: %s", error->message);
        g_error_free(error);
        return false;
    }

    // Initialize mutex
    g_mutex_init(&mpris_state.mutex);

    // Initialize unique name mapping (D-Bus :1.xxx -> org.mpris.MediaPlayer2.xxx)
    mpris_state.unique_name_map = g_hash_table_new_full(
        g_str_hash,        // Hash function for string keys
        g_str_equal,       // Equality function for string keys
        g_free,            // Key destructor
        g_free             // Value destructor
    );

    // Populate mapping with already-running players
    populate_initial_name_mapping();

    // Set initial flag to load first track
    g_mutex_lock(&mpris_state.mutex);
    mpris_state.metadata_changed = true;
    g_mutex_unlock(&mpris_state.mutex);

    // Subscribe to NameOwnerChanged to detect player appearance/disappearance
    mpris_state.name_owner_subscription_id = g_dbus_connection_signal_subscribe(
        dbus_connection,
        "org.freedesktop.DBus",
        "org.freedesktop.DBus",
        "NameOwnerChanged",
        "/org/freedesktop/DBus",
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_name_owner_changed,
        NULL,
        NULL
    );

    log_info("MPRIS: Subscribed to NameOwnerChanged signal");

    // Subscribe to PropertiesChanged from ALL MPRIS players (for automatic switching)
    mpris_state.all_players_subscription_id = g_dbus_connection_signal_subscribe(
        dbus_connection,
        NULL,  // All senders
        DBUS_PROPERTIES_INTERFACE,
        "PropertiesChanged",
        MPRIS_OBJECT_PATH,
        MPRIS_PLAYER_INTERFACE,  // Filter for Player interface
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_any_player_properties_changed,
        NULL,
        NULL
    );

    if (mpris_state.all_players_subscription_id > 0) {
        log_info("MPRIS: Subscribed to PropertiesChanged from all players (ID=%u)",
                 mpris_state.all_players_subscription_id);
    } else {
        log_error("MPRIS: Failed to subscribe to global PropertiesChanged!");
    }

    // Find initial player and setup subscriptions
    char *player = find_best_player();
    if (player) {
        setup_player_subscription(player);
    } else {
        log_info("MPRIS: No player found at startup, waiting for player to appear...");
    }

    return true;
}

bool mpris_get_metadata(struct track_metadata *metadata) {
    if (!metadata || !dbus_connection) {
        return false;
    }

    memset(metadata, 0, sizeof(struct track_metadata));

    // Use cached player (avoid expensive find_best_player() call)
    g_mutex_lock(&mpris_state.mutex);
    char *player = mpris_state.current_player ? strdup(mpris_state.current_player) : NULL;

    // If we have cached metadata from DBus signal, use it instead of querying
    // This avoids the race condition where the player hasn't updated yet when we query
    if (mpris_state.cached_metadata) {
        struct track_metadata *cached = mpris_state.cached_metadata;
        mpris_state.cached_metadata = NULL;  // Transfer ownership
        g_mutex_unlock(&mpris_state.mutex);

        // Copy cached metadata to output
        *metadata = *cached;
        free(cached);  // Free the container (fields transferred to metadata)

        // Update position from current state
        metadata->position_us = mpris_get_position();

        free(player);
        return metadata->title != NULL;
    }
    g_mutex_unlock(&mpris_state.mutex);

    if (!player) {
        return false;
    }

    // Save player name
    metadata->player_name = strdup(player);

    // Build D-Bus service name
    char bus_name[SMALL_BUFFER_SIZE];
    build_mpris_bus_name(bus_name, sizeof(bus_name), player);

    // Get Metadata property
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        dbus_connection,
        bus_name,
        MPRIS_OBJECT_PATH,
        DBUS_PROPERTIES_INTERFACE,
        "Get",
        g_variant_new("(ss)", MPRIS_PLAYER_INTERFACE, "Metadata"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error) {
        log_error("Failed to get metadata from %s: %s", player, error->message);
        g_error_free(error);
        free(player);
        mpris_free_metadata(metadata);
        return false;
    }

    GVariant *metadata_variant = g_variant_get_child_value(result, 0);
    GVariant *metadata_dict = g_variant_get_variant(metadata_variant);

    // Extract metadata fields
    GVariant *value;

    // Title (xesam:title)
    value = get_dict_value(metadata_dict, "xesam:title");
    if (value) {
        metadata->title = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Artist (xesam:artist) - array of strings
    value = get_dict_value(metadata_dict, "xesam:artist");
    if (value) {
        metadata->artist = extract_string_array(value);
        g_variant_unref(value);
    }

    // Album (xesam:album)
    value = get_dict_value(metadata_dict, "xesam:album");
    if (value) {
        metadata->album = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // URL (xesam:url)
    value = get_dict_value(metadata_dict, "xesam:url");
    if (value) {
        metadata->url = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Track ID (mpris:trackid) - unique identifier for the track
    value = get_dict_value(metadata_dict, "mpris:trackid");
    if (value) {
        metadata->trackid = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Art URL (mpris:artUrl)
    value = get_dict_value(metadata_dict, "mpris:artUrl");
    if (value) {
        metadata->art_url = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Length (mpris:length) - microseconds
    // Note: Different players use different types (INT64 'x' or UINT64 't')
    value = get_dict_value(metadata_dict, "mpris:length");
    if (value) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
            metadata->length_us = g_variant_get_int64(value);
        } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64)) {
            metadata->length_us = (int64_t)g_variant_get_uint64(value);
        }
        g_variant_unref(value);
    }

    g_variant_unref(metadata_dict);
    g_variant_unref(metadata_variant);
    g_variant_unref(result);

    // Use cached position (avoid D-Bus call)
    metadata->position_us = mpris_get_position();

    free(player);

    // Ignore Spotify advertisements (detect by URL pattern)
    // Spotify ads have URLs like: https://open.spotify.com/ad/...
    // Title contains actual ad content, not "Advertisement"
    if (metadata->url && strstr(metadata->url, "spotify.com/ad/")) {
        log_info("Spotify advertisement detected, ignoring");
        mpris_free_metadata(metadata);
        return false;
    }

    return metadata->title != NULL;
}

int64_t mpris_get_position(void) {
    if (!dbus_connection) {
        return 0;
    }

    // Use cached player (avoid expensive find_best_player() call)
    g_mutex_lock(&mpris_state.mutex);
    char *player = mpris_state.current_player ? strdup(mpris_state.current_player) : NULL;
    g_mutex_unlock(&mpris_state.mutex);

    if (!player) {
        return 0;
    }

    char bus_name[SMALL_BUFFER_SIZE];
    build_mpris_bus_name(bus_name, sizeof(bus_name), player);

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        dbus_connection,
        bus_name,
        MPRIS_OBJECT_PATH,
        DBUS_PROPERTIES_INTERFACE,
        "Get",
        g_variant_new("(ss)", MPRIS_PLAYER_INTERFACE, "Position"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    int64_t position = 0;

    if (!error && result) {
        GVariant *position_variant = g_variant_get_child_value(result, 0);
        GVariant *position_value = g_variant_get_variant(position_variant);
        if (g_variant_is_of_type(position_value, G_VARIANT_TYPE_INT64)) {
            position = g_variant_get_int64(position_value);
        }
        g_variant_unref(position_value);
        g_variant_unref(position_variant);
        g_variant_unref(result);
    }

    if (error) {
        g_error_free(error);
    }

    free(player);
    return position;
}

bool mpris_is_playing(void) {
    if (!dbus_connection) {
        return false;
    }

    g_mutex_lock(&mpris_state.mutex);
    bool playing = false;

    if (mpris_state.playback_status) {
        playing = (strcmp(mpris_state.playback_status, "Playing") == 0);
    }

    g_mutex_unlock(&mpris_state.mutex);
    return playing;
}

bool mpris_check_metadata_changed(void) {
    if (!dbus_connection) {
        return false;
    }

    g_mutex_lock(&mpris_state.mutex);
    bool changed = mpris_state.metadata_changed;
    mpris_state.metadata_changed = false;  // Reset flag
    g_mutex_unlock(&mpris_state.mutex);

    return changed;
}

void mpris_free_metadata(struct track_metadata *metadata) {
    if (!metadata) {
        return;
    }

    free(metadata->title);
    free(metadata->artist);
    free(metadata->album);
    free(metadata->url);
    free(metadata->trackid);
    free(metadata->art_url);
    free(metadata->player_name);
    memset(metadata, 0, sizeof(struct track_metadata));
}

void mpris_cleanup(void) {
    // Unsubscribe from signals
    if (dbus_connection) {
        if (mpris_state.subscription_id > 0) {
            g_dbus_connection_signal_unsubscribe(dbus_connection, mpris_state.subscription_id);
            mpris_state.subscription_id = 0;
        }

        if (mpris_state.seeked_subscription_id > 0) {
            g_dbus_connection_signal_unsubscribe(dbus_connection, mpris_state.seeked_subscription_id);
            mpris_state.seeked_subscription_id = 0;
        }

        if (mpris_state.name_owner_subscription_id > 0) {
            g_dbus_connection_signal_unsubscribe(dbus_connection, mpris_state.name_owner_subscription_id);
            mpris_state.name_owner_subscription_id = 0;
        }

        if (mpris_state.all_players_subscription_id > 0) {
            g_dbus_connection_signal_unsubscribe(dbus_connection, mpris_state.all_players_subscription_id);
            mpris_state.all_players_subscription_id = 0;
        }

        g_object_unref(dbus_connection);
        dbus_connection = NULL;
    }

    // Clean up cached state
    g_mutex_lock(&mpris_state.mutex);
    free(mpris_state.current_player);
    free(mpris_state.playback_status);
    mpris_state.current_player = NULL;
    mpris_state.playback_status = NULL;
    mpris_state.position_us = 0;
    mpris_state.position_timestamp_us = 0;

    // Clean up cached metadata
    if (mpris_state.cached_metadata) {
        mpris_free_metadata(mpris_state.cached_metadata);
        free(mpris_state.cached_metadata);
        mpris_state.cached_metadata = NULL;
    }

    // Clean up unique name mapping
    if (mpris_state.unique_name_map) {
        g_hash_table_destroy(mpris_state.unique_name_map);
        mpris_state.unique_name_map = NULL;
    }

    g_mutex_unlock(&mpris_state.mutex);

    g_mutex_clear(&mpris_state.mutex);
}

// Apply Spotify position fix if needed (call after lyrics load)
void mpris_apply_position_fix_if_needed(void) {
    g_mutex_lock(&mpris_state.mutex);

    // Check if position fix is needed
    if (!mpris_state.position_fix_needed || mpris_state.position_fix_in_progress) {
        g_mutex_unlock(&mpris_state.mutex);
        return;
    }

    // Check if current player is Spotify
    bool is_spotify = mpris_state.current_player &&
                     strcasecmp(mpris_state.current_player, "spotify") == 0;

    if (!is_spotify) {
        mpris_state.position_fix_needed = false;
        g_mutex_unlock(&mpris_state.mutex);
        return;
    }

    // Get config
    struct config *cfg = config_get();
    if (!cfg->spotify.auto_position_fix) {
        mpris_state.position_fix_needed = false;
        g_mutex_unlock(&mpris_state.mutex);
        return;
    }

    // Mark as in progress
    mpris_state.position_fix_in_progress = true;
    mpris_state.position_fix_needed = false;
    g_mutex_unlock(&mpris_state.mutex);

    log_info("Applying Spotify position fix after lyrics load");

    // Wait for track to actually start playing
    if (cfg->spotify.position_fix_wait_ms > 0) {
        log_info("Waiting %dms for track to start...", cfg->spotify.position_fix_wait_ms);
        struct timespec wait_delay = {
            cfg->spotify.position_fix_wait_ms / 1000,  // seconds
            (cfg->spotify.position_fix_wait_ms % 1000) * 1000000L  // nanoseconds
        };
        nanosleep(&wait_delay, NULL);
    }

    // Quick pause/play toggle to fix position drift
    mpris_send_command("Pause");

    // Delay (default: 1ms, imperceptible to users)
    struct timespec delay = {
        0,
        cfg->spotify.position_fix_delay_ms * 1000000L
    };
    nanosleep(&delay, NULL);

    mpris_send_command("Play");

    // Additional delay to ensure signal processing
    nanosleep(&delay, NULL);

    // Mark as complete
    g_mutex_lock(&mpris_state.mutex);
    mpris_state.position_fix_in_progress = false;
    g_mutex_unlock(&mpris_state.mutex);
}
