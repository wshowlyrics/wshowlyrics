#include "file_monitor.h"
#include "../user_experience/config/config.h"
#include "../core/rendering/rendering_manager.h"
#include "../lyrics/lyrics_manager.h"
#include "../utils/file/file_utils.h"
#include "../constants.h"
#include <pthread.h>
#include <stdlib.h>
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
    // Save old config values for comparison
    char *old_api_key = g_config.translation.api_key ? strdup(g_config.translation.api_key) : NULL;
    char *old_provider = g_config.translation.provider ? strdup(g_config.translation.provider) : NULL;
    char *old_target_lang = g_config.translation.target_language ? strdup(g_config.translation.target_language) : NULL;
    char *old_font_family = g_config.display.font_family ? strdup(g_config.display.font_family) : NULL;
    char *old_anchor = g_config.display.anchor ? strdup(g_config.display.anchor) : NULL;
    int old_font_size = g_config.display.font_size;
    int old_margin = g_config.display.margin;

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

        // Check if translation config changed
        bool translation_changed = false;

        // Compare API key
        if ((old_api_key == NULL) != (g_config.translation.api_key == NULL)) {
            translation_changed = true;
        } else if (old_api_key && g_config.translation.api_key &&
                   strcmp(old_api_key, g_config.translation.api_key) != 0) {
            translation_changed = true;
        }

        // Compare provider
        if ((old_provider == NULL) != (g_config.translation.provider == NULL)) {
            translation_changed = true;
        } else if (old_provider && g_config.translation.provider &&
                   strcmp(old_provider, g_config.translation.provider) != 0) {
            translation_changed = true;
        }

        // Compare target language
        if ((old_target_lang == NULL) != (g_config.translation.target_language == NULL)) {
            translation_changed = true;
        } else if (old_target_lang && g_config.translation.target_language &&
                   strcmp(old_target_lang, g_config.translation.target_language) != 0) {
            translation_changed = true;
        }

        if (translation_changed) {
            log_info("Translation config changed, reloading lyrics...");

            // Cancel current translation if in progress
            if (state->lyrics.translation_in_progress) {
                state->lyrics.translation_should_cancel = true;
                pthread_join(state->lyrics.translation_thread, NULL);
            }

            // Reload lyrics with new translation config
            lyrics_manager_load_lyrics(state);
        }

        // Check if font changed (warning only)
        bool font_changed = false;
        if ((old_font_family == NULL) != (g_config.display.font_family == NULL)) {
            font_changed = true;
        } else if (old_font_family && g_config.display.font_family &&
                   strcmp(old_font_family, g_config.display.font_family) != 0) {
            font_changed = true;
        }

        if (!font_changed && old_font_size != g_config.display.font_size) {
            font_changed = true;
        }

        if (font_changed) {
            log_warn("Font settings changed - restart application to apply");
        }

        // Check if layout changed (warning only)
        bool layout_changed = false;
        if ((old_anchor == NULL) != (g_config.display.anchor == NULL)) {
            layout_changed = true;
        } else if (old_anchor && g_config.display.anchor &&
                   strcmp(old_anchor, g_config.display.anchor) != 0) {
            layout_changed = true;
        }

        if (!layout_changed && old_margin != g_config.display.margin) {
            layout_changed = true;
        }

        if (layout_changed) {
            log_warn("Layout settings (anchor/margin) changed - restart application to apply");
        }

        log_info("Config reloaded successfully");
    } else {
        log_warn("Failed to reload config, keeping old settings");
    }

    // Free saved old config
    free(old_api_key);
    free(old_provider);
    free(old_target_lang);
    free(old_font_family);
    free(old_anchor);
}
