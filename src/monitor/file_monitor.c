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
        log_info("%s file changed, reloading: %s", file_type_name, sanitize_path(file_path));

        // Call the reload callback
        reload_callback(state, file_path);

        // Update stored checksum
        snprintf(stored_checksum, checksum_size, "%s", current_checksum);

        rendering_manager_set_dirty(state);
        return true;
    }

    return false;
}

// Helper: Check if string value has changed (NULL-safe comparison)
static bool has_string_changed(const char *old_value, const char *new_value) {
    // If one is NULL and the other is not, they're different
    if ((old_value == NULL) != (new_value == NULL)) {
        return true;
    }

    // If both are NULL, they're the same
    if (old_value == NULL && new_value == NULL) {
        return false;
    }

    // Both are non-NULL, compare strings
    return strcmp(old_value, new_value) != 0;
}

// Helper: Update state colors from config
static void update_state_colors(struct lyrics_state *state) {
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
}

// Helper: Check if translation config changed and reload if needed
// Returns: true if translation was reloaded
static bool check_translation_config_changed(struct lyrics_state *state,
                                             const char *old_api_key,
                                             const char *old_provider,
                                             const char *old_target_lang) {
    bool changed = false;

    // Check if any translation setting changed
    if (has_string_changed(old_api_key, g_config.translation.api_key) ||
        has_string_changed(old_provider, g_config.translation.provider) ||
        has_string_changed(old_target_lang, g_config.translation.target_language)) {
        changed = true;
    }

    if (changed) {
        log_info("Translation config changed, reloading lyrics...");

        // Cancel current translation if in progress
        if (state->lyrics.translation_in_progress) {
            state->lyrics.translation_should_cancel = true;
            pthread_join(state->lyrics.translation_thread, NULL);
        }

        // Reload lyrics with new translation config
        lyrics_manager_load_lyrics(state);
    }

    return changed;
}

// Helper: Check if font settings changed and warn if needed
static void check_font_changed(const char *old_font_family, int old_font_size) {
    bool changed = false;

    if (has_string_changed(old_font_family, g_config.display.font_family) ||
        old_font_size != g_config.display.font_size) {
        changed = true;
    }

    if (changed) {
        log_warn("Font settings changed - restart application to apply");
    }
}

// Helper: Check if layout settings changed and warn if needed
static void check_layout_changed(const char *old_anchor, int old_margin) {
    bool changed = false;

    if (has_string_changed(old_anchor, g_config.display.anchor) ||
        old_margin != g_config.display.margin) {
        changed = true;
    }

    if (changed) {
        log_warn("Layout settings (anchor/margin) changed - restart application to apply");
    }
}

void file_monitor_reload_lyrics(struct lyrics_state *state, const char *path) {
    (void)path;  // Required by callback signature
    rendering_manager_render_transparent(state);
    lyrics_manager_load_lyrics(state);
}

void file_monitor_reload_config(struct lyrics_state *state, const char *path) {
    (void)path;  // Required by callback signature

    // Save old config values for comparison
    char *old_api_key = g_config.translation.api_key ? strdup(g_config.translation.api_key) : NULL;
    char *old_provider = g_config.translation.provider ? strdup(g_config.translation.provider) : NULL;
    char *old_target_lang = g_config.translation.target_language ? strdup(g_config.translation.target_language) : NULL;
    char *old_font_family = g_config.display.font_family ? strdup(g_config.display.font_family) : NULL;
    char *old_anchor = g_config.display.anchor ? strdup(g_config.display.anchor) : NULL;
    int old_font_size = g_config.display.font_size;
    int old_margin = g_config.display.margin;

    // Reload config from file
    config_free(&g_config);
    config_init_defaults(&g_config);

    if (config_load(&g_config, path)) {
        // Update state colors from new config
        update_state_colors(state);

        // Check and handle config changes
        check_translation_config_changed(state, old_api_key, old_provider, old_target_lang);
        check_font_changed(old_font_family, old_font_size);
        check_layout_changed(old_anchor, old_margin);

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
