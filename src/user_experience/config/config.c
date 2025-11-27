#include "config.h"
#include "../../constants.h"
#include "../../utils/string/string_utils.h"
#include "../../utils/file/file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

// Global configuration instance
struct config g_config;

// Get pointer to global config (encapsulated access)
struct config* config_get(void) {
    return &g_config;
}

// Parse hex color string (RRGGBBAA) to RGBA array
static bool parse_hex_color(const char *hex, double rgba[4]) {
    if (!hex) return false;

    // Skip leading # if present
    if (hex[0] == '#') hex++;

    size_t len = strlen(hex);
    if (len != 8) return false;  // Must be exactly 8 hex digits

    char *endptr;
    unsigned long color = strtoul(hex, &endptr, 16);
    if (*endptr != '\0') return false;

    // Extract RGBA components using color macros
    rgba[0] = COLOR_CAIRO_R(color);
    rgba[1] = COLOR_CAIRO_G(color);
    rgba[2] = COLOR_CAIRO_B(color);
    rgba[3] = COLOR_CAIRO_A(color);

    return true;
}

// Deprecated: Use trim_whitespace from string_utils.h instead
// Kept for backwards compatibility with existing code
char* config_trim_whitespace(char *str) {
    return trim_whitespace(str);
}

void config_init_defaults(struct config *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    // Display defaults (matching original hardcoded values)
    cfg->display.font_family = strdup("Sans");
    cfg->display.font_size = 20;
    cfg->display.font_weight = strdup("normal");

    // Default colors (matching original hardcoded values)
    parse_hex_color("FFFFFFFF", cfg->display.color_active);
    parse_hex_color("00000080", cfg->display.color_background);

    cfg->display.margin_bottom = 32;
    cfg->display.line_spacing = 10;

    // Lyrics defaults
    cfg->lyrics.search_dirs = strdup("");  // Empty = use hardcoded defaults
    cfg->lyrics.extensions = strdup("lrcx,lrc,srt");  // All formats
    cfg->lyrics.enable_lrclib = true;
    cfg->lyrics.enable_itunes = true;
}

void config_free(struct config *cfg) {
    free(cfg->display.font_family);
    free(cfg->display.font_weight);
    free(cfg->lyrics.search_dirs);
    free(cfg->lyrics.extensions);
    memset(cfg, 0, sizeof(*cfg));
}

// Get user config path (~/.config/wshowlyrics/settings.ini)
static char* config_get_user_path(void) {
    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    if (!config_home && !home) {
        return NULL;
    }

    char *path = malloc(CONFIG_PATH_SIZE);
    if (!path) return NULL;

    int result;
    if (config_home) {
        result = build_path(path, CONFIG_PATH_SIZE, "%s/wshowlyrics/settings.ini", config_home);
    } else {
        result = build_path(path, CONFIG_PATH_SIZE, "%s/.config/wshowlyrics/settings.ini", home);
    }

    if (result < 0) {
        free(path);
        return NULL;
    }

    return path;
}

// Get config path (for backward compatibility - returns user path)
char* config_get_path(void) {
    return config_get_user_path();
}

// Simple INI parser
bool config_load(struct config *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn("Config file not found: %s (using defaults)", path);
        return false;
    }

    char line[CONFIG_LINE_SIZE];
    char section[SMALL_BUFFER_SIZE] = {0};

    while (fgets(line, sizeof(line), f)) {
        char *trimmed = config_trim_whitespace(line);

        // Skip empty lines and comments
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        // Section header
        if (trimmed[0] == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                strncpy(section, trimmed + 1, sizeof(section) - 1);
            }
            continue;
        }

        // Key-value pair
        char *equals = strchr(trimmed, '=');
        if (!equals) continue;

        *equals = '\0';
        char *key = config_trim_whitespace(trimmed);
        char *value = config_trim_whitespace(equals + 1);

        // Parse based on section
        if (strcmp(section, "display") == 0) {
            if (strcmp(key, "font_family") == 0) {
                free(cfg->display.font_family);
                cfg->display.font_family = strdup(value);
            } else if (strcmp(key, "font_size") == 0) {
                cfg->display.font_size = atoi(value);
            } else if (strcmp(key, "font_weight") == 0) {
                free(cfg->display.font_weight);
                cfg->display.font_weight = strdup(value);
            } else if (strcmp(key, "color_active") == 0) {
                parse_hex_color(value, cfg->display.color_active);
            } else if (strcmp(key, "color_background") == 0) {
                parse_hex_color(value, cfg->display.color_background);
            } else if (strcmp(key, "margin_bottom") == 0) {
                cfg->display.margin_bottom = atoi(value);
            } else if (strcmp(key, "line_spacing") == 0) {
                cfg->display.line_spacing = atoi(value);
            }
        } else if (strcmp(section, "lyrics") == 0) {
            if (strcmp(key, "search_dirs") == 0) {
                free(cfg->lyrics.search_dirs);
                cfg->lyrics.search_dirs = strdup(value);
            } else if (strcmp(key, "extensions") == 0) {
                free(cfg->lyrics.extensions);
                cfg->lyrics.extensions = strdup(value);
            } else if (strcmp(key, "enable_lrclib") == 0) {
                cfg->lyrics.enable_lrclib = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "enable_itunes") == 0) {
                cfg->lyrics.enable_itunes = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            }
        }
    }

    fclose(f);
    log_info("Loaded configuration from: %s", path);
    return true;
}

bool config_is_extension_enabled(const char *ext) {
    if (!ext || !g_config.lyrics.extensions) return true;

    // If extensions is empty, enable all
    if (g_config.lyrics.extensions[0] == '\0') return true;

    // Skip leading dot if present
    if (ext[0] == '.') ext++;

    // Check if extension is in the comma-separated list
    char *exts = strdup(g_config.lyrics.extensions);
    if (!exts) return true;

    bool found = false;
    char *token = strtok(exts, ",");
    while (token) {
        char *trimmed = config_trim_whitespace(token);
        if (strcasecmp(trimmed, ext) == 0) {
            found = true;
            break;
        }
        token = strtok(NULL, ",");
    }

    free(exts);
    return found;
}
