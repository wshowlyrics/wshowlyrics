#include "file_monitor.h"
#include "../user_experience/config/config.h"
#include "../core/rendering/rendering_manager.h"
#include "../lyrics/lyrics_manager.h"
#include "../utils/file/file_utils.h"
#include "../constants.h"
#include <string.h>

bool file_monitor_check_and_reload(
    const char *file_path,
    char *stored_checksum,
    size_t checksum_size,
    const char *file_type_name,
    file_reload_callback_t reload_callback,
    struct lyrics_state *state
) {
    if (!file_path || !stored_checksum || stored_checksum[0] == '\0') {
        return false;
    }

    char current_checksum[MD5_DIGEST_STRING_LENGTH];
    if (!calculate_file_md5(file_path, current_checksum)) {
        return false;
    }

    if (strcmp(current_checksum, stored_checksum) != 0) {
        log_info("%s file changed, reloading: %s", file_type_name, file_path);

        // Call the reload callback
        reload_callback(state, file_path);

        // Update stored checksum
        snprintf(stored_checksum, checksum_size, "%s", current_checksum);

        rendering_manager_set_dirty(state);
        return true;
    }

    return false;
}

void file_monitor_reload_lyrics(struct lyrics_state *state, const char *path) {
    rendering_manager_render_transparent(state);
    lyrics_manager_load_lyrics(state);
}

void file_monitor_reload_config(struct lyrics_state *state, const char *path) {
    config_free(&g_config);
    config_init_defaults(&g_config);
    if (config_load(&g_config, path)) {
        // Update state colors from new config
        state->background =
            ((uint32_t)(g_config.display.color_background[0] * 255) << 24) |
            ((uint32_t)(g_config.display.color_background[1] * 255) << 16) |
            ((uint32_t)(g_config.display.color_background[2] * 255) << 8) |
            ((uint32_t)(g_config.display.color_background[3] * 255));

        state->foreground =
            ((uint32_t)(g_config.display.color_active[0] * 255) << 24) |
            ((uint32_t)(g_config.display.color_active[1] * 255) << 16) |
            ((uint32_t)(g_config.display.color_active[2] * 255) << 8) |
            ((uint32_t)(g_config.display.color_active[3] * 255));

        // Note: Font changes require restarting the application
        // as the font is set during initialization and used in rendering
        log_info("Config reloaded successfully");
    } else {
        log_warn("Failed to reload config, keeping old settings");
    }
}
