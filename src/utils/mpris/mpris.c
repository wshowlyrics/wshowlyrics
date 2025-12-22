#include "mpris.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// Simple implementation using playerctl command
// For a full implementation, we would use D-Bus directly

// Execute a shell command and return the output
static char* execute_command(const char *cmd, int *exit_code) {
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        if (exit_code) *exit_code = -1;
        return NULL;
    }

    char *result = NULL;
    char buffer[PATH_BUFFER_SIZE];
    size_t total_size = 0;

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t len = strlen(buffer);
        // Remove trailing newline
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            len--;
        }

        char *new_result = realloc(result, total_size + len + 1);
        if (!new_result) {
            free(result);
            pclose(fp);
            if (exit_code) *exit_code = -1;
            return NULL;
        }
        result = new_result;

        // Use memcpy instead of strcat for O(n) performance
        memcpy(result + total_size, buffer, len);
        total_size += len;
        result[total_size] = '\0';
    }

    int status = pclose(fp);
    if (exit_code) {
        *exit_code = WEXITSTATUS(status);
    }
    return result;
}

// Build player argument for playerctl command
// Returns "--player=<player>" for the best available player
// Prioritizes playing players over paused ones
static char* build_player_arg(void) {
    struct config *cfg = config_get();

    // If preferred_players is empty or not set, use %any
    if (!cfg->lyrics.preferred_players || cfg->lyrics.preferred_players[0] == '\0') {
        return strdup("--player=%any");
    }

    // Parse preferred_players (comma-separated list)
    char *players_copy = strdup(cfg->lyrics.preferred_players);
    if (!players_copy) return strdup("--player=%any");

    char *playing_player = NULL;
    char *first_available = NULL;

    // Check each preferred player
    char *saveptr;
    char *player = strtok_r(players_copy, ",", &saveptr);
    while (player) {
        // Trim whitespace
        while (*player == ' ') player++;
        char *end = player + strlen(player) - 1;
        while (end > player && *end == ' ') *end-- = '\0';

        // Check if this player is available and get its status
        char cmd[SMALL_BUFFER_SIZE];
        snprintf(cmd, sizeof(cmd), "playerctl --player=%s status 2>/dev/null", player);

        int exit_code = 0;
        char *status = execute_command(cmd, &exit_code);

        if (status && exit_code == 0) {
            // Player is available
            if (!first_available) {
                first_available = strdup(player);
            }

            // Check if it's playing
            if (strcmp(status, "Playing") == 0) {
                playing_player = strdup(player);
                free(status);
                break;  // Found a playing player, use it
            }
        }

        // Always free status if allocated, regardless of exit_code
        free(status);

        player = strtok_r(NULL, ",", &saveptr);
    }

    free(players_copy);

    // Build the result
    char *result = malloc(SMALL_BUFFER_SIZE);
    if (!result) {
        free(playing_player);
        free(first_available);
        return strdup("--player=%any");
    }

    // Priority: playing player > first available player > all preferred players
    if (playing_player) {
        snprintf(result, SMALL_BUFFER_SIZE, "--player=%s", playing_player);
        free(playing_player);
        free(first_available);
    } else if (first_available) {
        snprintf(result, SMALL_BUFFER_SIZE, "--player=%s", first_available);
        free(first_available);
    } else {
        // No players available, fall back to checking all preferred players
        snprintf(result, SMALL_BUFFER_SIZE, "--player=%s", cfg->lyrics.preferred_players);
    }

    return result;
}

bool mpris_init(void) {
    // Check if playerctl is available
    int ret = system("which playerctl > /dev/null 2>&1");
    return ret == 0;
}

bool mpris_get_metadata(struct track_metadata *metadata) {
    if (!metadata) {
        return false;
    }

    memset(metadata, 0, sizeof(struct track_metadata));

    // Get player argument from config
    char *player_arg = build_player_arg();
    if (!player_arg) {
        return false;
    }

    // Check player name first to filter out browsers
    int player_exit_code = 0;
    char cmd[SMALL_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd),
        "playerctl %s metadata --format '{{playerName}}' 2>/dev/null",
        player_arg);
    char *player_name = execute_command(cmd, &player_exit_code);

    // Ignore browsers (chromium includes chrome/edge, firefox)
    if (player_name) {
        if (player_exit_code == 0 &&
            (strstr(player_name, "chromium") || strstr(player_name, "firefox"))) {
            free(player_name);
            free(player_arg);
            return false;
        }
        // Save player name to metadata (will be freed in mpris_free_metadata)
        metadata->player_name = player_name;
    }

    // Get all metadata in a single command to ensure consistency
    // Format: title|artist|album|url|artUrl|length
    int exit_code = 0;
    snprintf(cmd, sizeof(cmd),
        "playerctl %s metadata --format "
        "'{{xesam:title}}|||{{xesam:artist}}|||{{xesam:album}}|||{{xesam:url}}|||{{mpris:artUrl}}|||{{mpris:length}}' 2>/dev/null",
        player_arg);
    char *result = execute_command(cmd, &exit_code);

    // If playerctl exits with code 1, it means no players found
    if (!result || exit_code != 0) {
        free(result);
        free(player_arg);
        return false;
    }

    // Parse the delimited result: title|||artist|||album|||url|||artUrl|||length
    // Note: fields can be empty, so we need to carefully split by delimiter
    char *title_start = result;

    char *artist_start = strstr(title_start, "|||");
    if (!artist_start) {
        free(result);
        free(player_arg);
        return false;
    }
    *artist_start = '\0';
    artist_start += 3;

    char *album_start = strstr(artist_start, "|||");
    if (!album_start) {
        free(result);
        free(player_arg);
        return false;
    }
    *album_start = '\0';
    album_start += 3;

    char *url_start = strstr(album_start, "|||");
    if (!url_start) {
        free(result);
        free(player_arg);
        return false;
    }
    *url_start = '\0';
    url_start += 3;

    char *art_url_start = strstr(url_start, "|||");
    if (!art_url_start) {
        free(result);
        free(player_arg);
        return false;
    }
    *art_url_start = '\0';
    art_url_start += 3;

    char *length_start = strstr(art_url_start, "|||");
    if (!length_start) {
        free(result);
        free(player_arg);
        return false;
    }
    *length_start = '\0';
    length_start += 3;

    // Copy the values (skip empty strings and "null" literals)
    metadata->title = strdup(title_start);
    if (artist_start[0] != '\0' && strcmp(artist_start, "null") != 0) {
        metadata->artist = strdup(artist_start);
    }
    if (album_start[0] != '\0' && strcmp(album_start, "null") != 0) {
        metadata->album = strdup(album_start);
    }
    if (url_start[0] != '\0' && strcmp(url_start, "null") != 0) {
        metadata->url = strdup(url_start);
    }
    if (art_url_start[0] != '\0' && strcmp(art_url_start, "null") != 0) {
        metadata->art_url = strdup(art_url_start);
    }
    if (length_start[0] != '\0' && strcmp(length_start, "null") != 0) {
        metadata->length_us = atoll(length_start);
    }

    // Get position separately (not available in metadata format)
    snprintf(cmd, sizeof(cmd), "playerctl %s position 2>/dev/null", player_arg);
    char *position_str = execute_command(cmd, NULL);
    if (position_str) {
        double position_sec = atof(position_str);
        metadata->position_us = (int64_t)(position_sec * 1000000);
        free(position_str);
    }

    free(result);
    free(player_arg);

    // Ignore Spotify advertisements (title="Advertisement" and URL contains "spotify.com/ad/")
    if (metadata->title && strcmp(metadata->title, "Advertisement") == 0 &&
        metadata->url && strstr(metadata->url, "spotify.com/ad/")) {
        mpris_free_metadata(metadata);
        return false;
    }

    return metadata->title != NULL;
}

int64_t mpris_get_position(void) {
    // Get player argument from config
    char *player_arg = build_player_arg();
    if (!player_arg) {
        return 0;
    }

    // Use metadata format to get position in microseconds
    char cmd[SMALL_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd),
        "playerctl %s metadata -f '{{position}}' 2>/dev/null",
        player_arg);
    char *position_str = execute_command(cmd, NULL);
    free(player_arg);

    if (!position_str) {
        return 0;
    }

    // Position from metadata is in microseconds
    int64_t position_us = atoll(position_str);
    free(position_str);
    return position_us;
}

bool mpris_is_playing(void) {
    // Get player argument from config
    char *player_arg = build_player_arg();
    if (!player_arg) {
        return false;
    }

    int exit_code = 0;
    char cmd[SMALL_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd),
        "playerctl %s status 2>/dev/null",
        player_arg);
    char *status = execute_command(cmd, &exit_code);
    free(player_arg);

    if (!status || exit_code != 0) {
        free(status);
        return false;
    }

    bool playing = (strcmp(status, "Playing") == 0);
    free(status);
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
    // Nothing to cleanup in this simple implementation
}
