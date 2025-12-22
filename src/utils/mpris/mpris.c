#include "mpris.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

// MPRIS2 D-Bus constants
#define MPRIS_INTERFACE "org.mpris.MediaPlayer2"
#define MPRIS_PLAYER_INTERFACE "org.mpris.MediaPlayer2.Player"
#define MPRIS_OBJECT_PATH "/org/mpris/MediaPlayer2"
#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"

// Global D-Bus connection
static GDBusConnection *dbus_connection = NULL;

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
        if (strncmp(name, "org.mpris.MediaPlayer2.", 23) == 0) {
            const char *player_name = name + 23;

            // Skip browsers
            if (strstr(player_name, "chromium") || strstr(player_name, "firefox")) {
                continue;
            }

            // Expand array if needed
            if (*count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                players = realloc(players, capacity * sizeof(char*));
            }

            players[*count] = strdup(player_name);
            (*count)++;
        }
    }

    g_variant_unref(names_variant);
    g_variant_unref(result);

    return players;
}

// Helper: Find best player based on preferred_players config
static char* find_best_player(void) {
    int player_count = 0;
    char **all_players = list_mpris_players(&player_count);

    if (!all_players || player_count == 0) {
        return NULL;
    }

    struct config *cfg = config_get();
    char *best_player = NULL;

    // If no preferred players, use first available
    if (!cfg->lyrics.preferred_players || cfg->lyrics.preferred_players[0] == '\0') {
        best_player = strdup(all_players[0]);
        goto cleanup;
    }

    // Parse preferred_players (comma-separated)
    char *players_copy = strdup(cfg->lyrics.preferred_players);
    char *saveptr;
    char *preferred = strtok_r(players_copy, ",", &saveptr);

    while (preferred) {
        // Trim whitespace
        while (*preferred == ' ') preferred++;
        char *end = preferred + strlen(preferred) - 1;
        while (end > preferred && *end == ' ') *end-- = '\0';

        // Check if this preferred player is available
        for (int i = 0; i < player_count; i++) {
            if (strcmp(all_players[i], preferred) == 0) {
                // Check if it's playing
                char bus_name[SMALL_BUFFER_SIZE];
                snprintf(bus_name, sizeof(bus_name), "org.mpris.MediaPlayer2.%s", preferred);

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

                if (!error && status_variant) {
                    GVariant *status_value = g_variant_get_child_value(status_variant, 0);
                    GVariant *status_str = g_variant_get_variant(status_value);
                    const char *status = g_variant_get_string(status_str, NULL);

                    if (strcmp(status, "Playing") == 0) {
                        best_player = strdup(preferred);
                        g_variant_unref(status_str);
                        g_variant_unref(status_value);
                        g_variant_unref(status_variant);
                        free(players_copy);
                        goto cleanup;
                    }

                    g_variant_unref(status_str);
                    g_variant_unref(status_value);
                    g_variant_unref(status_variant);
                }

                if (error) {
                    g_error_free(error);
                }

                // If not playing but available, remember it
                if (!best_player) {
                    best_player = strdup(preferred);
                }
            }
        }

        preferred = strtok_r(NULL, ",", &saveptr);
    }

    free(players_copy);

    // If no preferred player found, use first available
    if (!best_player) {
        best_player = strdup(all_players[0]);
    }

cleanup:
    for (int i = 0; i < player_count; i++) {
        free(all_players[i]);
    }
    free(all_players);

    return best_player;
}

bool mpris_init(void) {
    GError *error = NULL;
    dbus_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

    if (error) {
        log_error("Failed to connect to D-Bus: %s", error->message);
        g_error_free(error);
        return false;
    }

    return true;
}

bool mpris_get_metadata(struct track_metadata *metadata) {
    if (!metadata || !dbus_connection) {
        return false;
    }

    memset(metadata, 0, sizeof(struct track_metadata));

    // Find best player
    char *player = find_best_player();
    if (!player) {
        return false;
    }

    // Save player name
    metadata->player_name = strdup(player);

    // Build D-Bus service name
    char bus_name[SMALL_BUFFER_SIZE];
    snprintf(bus_name, sizeof(bus_name), "org.mpris.MediaPlayer2.%s", player);

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

    // Art URL (mpris:artUrl)
    value = get_dict_value(metadata_dict, "mpris:artUrl");
    if (value) {
        metadata->art_url = strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }

    // Length (mpris:length) - microseconds
    value = get_dict_value(metadata_dict, "mpris:length");
    if (value) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
            metadata->length_us = g_variant_get_int64(value);
        }
        g_variant_unref(value);
    }

    g_variant_unref(metadata_dict);
    g_variant_unref(metadata_variant);
    g_variant_unref(result);

    // Get Position property separately
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
            metadata->position_us = g_variant_get_int64(position_value);
        }
        g_variant_unref(position_value);
        g_variant_unref(position_variant);
        g_variant_unref(result);
    }

    if (error) {
        g_error_free(error);
    }

    free(player);

    // Ignore Spotify advertisements
    if (metadata->title && strcmp(metadata->title, "Advertisement") == 0 &&
        metadata->url && strstr(metadata->url, "spotify.com/ad/")) {
        mpris_free_metadata(metadata);
        return false;
    }

    return metadata->title != NULL;
}

int64_t mpris_get_position(void) {
    if (!dbus_connection) {
        return 0;
    }

    char *player = find_best_player();
    if (!player) {
        return 0;
    }

    char bus_name[SMALL_BUFFER_SIZE];
    snprintf(bus_name, sizeof(bus_name), "org.mpris.MediaPlayer2.%s", player);

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

    char *player = find_best_player();
    if (!player) {
        return false;
    }

    char bus_name[SMALL_BUFFER_SIZE];
    snprintf(bus_name, sizeof(bus_name), "org.mpris.MediaPlayer2.%s", player);

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

    bool playing = false;

    if (!error && result) {
        GVariant *status_variant = g_variant_get_child_value(result, 0);
        GVariant *status_value = g_variant_get_variant(status_variant);
        const char *status = g_variant_get_string(status_value, NULL);
        playing = (strcmp(status, "Playing") == 0);
        g_variant_unref(status_value);
        g_variant_unref(status_variant);
        g_variant_unref(result);
    }

    if (error) {
        g_error_free(error);
    }

    free(player);
    return playing;
}

void mpris_free_metadata(struct track_metadata *metadata) {
    if (!metadata) {
        return;
    }

    free(metadata->title);
    free(metadata->artist);
    free(metadata->album);
    free(metadata->url);
    free(metadata->art_url);
    free(metadata->player_name);
    memset(metadata, 0, sizeof(struct track_metadata));
}

void mpris_cleanup(void) {
    if (dbus_connection) {
        g_object_unref(dbus_connection);
        dbus_connection = NULL;
    }
}
