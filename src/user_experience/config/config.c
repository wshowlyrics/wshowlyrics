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

    cfg->display.anchor = strdup("bottom");
    cfg->display.margin = 32;
    cfg->display.line_spacing = 10;

    // Lyrics defaults
    cfg->lyrics.search_dirs = strdup("");  // Empty = use hardcoded defaults
    cfg->lyrics.extensions = strdup("lrcx,lrc,srt");  // All formats
    cfg->lyrics.preferred_players = strdup("");  // Empty = use %any
    cfg->lyrics.enable_lrclib = true;
    cfg->lyrics.enable_itunes = true;
    cfg->lyrics.enable_notifications = true;  // Enabled by default
    cfg->lyrics.notification_timeout = 5000;  // 5 seconds by default

    // DeepL translation defaults
    cfg->deepl.enable_deepl = false;  // Disabled by default
    cfg->deepl.api_key = strdup("");  // No API key by default
    cfg->deepl.target_language = strdup("EN");  // English by default
    cfg->deepl.translation_display = strdup("both");  // Show both original and translation
    cfg->deepl.translation_opacity = 0.7;  // 70% opacity by default
}

void config_free(struct config *cfg) {
    free(cfg->display.font_family);
    free(cfg->display.font_weight);
    free(cfg->display.anchor);
    free(cfg->lyrics.search_dirs);
    free(cfg->lyrics.extensions);
    free(cfg->lyrics.preferred_players);
    free(cfg->deepl.api_key);
    free(cfg->deepl.target_language);
    free(cfg->deepl.translation_display);
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

// Check config file permissions and fix them if they're too permissive
static void check_and_fix_config_permissions(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return;  // Can't stat file, skip validation
    }

    // Get file permissions (last 9 bits)
    mode_t mode = st.st_mode & 0777;

    // Check if file is readable/writable by group or others (unsafe)
    bool group_readable = (mode & S_IRGRP) != 0;
    bool group_writable = (mode & S_IWGRP) != 0;
    bool other_readable = (mode & S_IROTH) != 0;
    bool other_writable = (mode & S_IWOTH) != 0;

    if (group_readable || group_writable || other_readable || other_writable) {
        log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        log_warn("⚠️  Config file has insecure permissions: %04o", mode);
        log_warn("File: %s", path);

        // Try to automatically fix permissions
        if (chmod(path, 0600) == 0) {
            log_info("✓ Automatically fixed permissions to 0600");
            log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        } else {
            log_warn("Failed to automatically fix permissions: %s", strerror(errno));
            log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            log_warn("");
            log_warn("Your config file may contain sensitive information (e.g., DeepL API key)");
            log_warn("and should only be readable by you.");
            log_warn("");
            log_warn("Recommended permissions: 0600 or 0400");
            log_warn("");
            log_warn("Please manually run:");
            log_warn("  chmod 600 %s", path);
            log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

            // Send error notification only on failure
            struct config *cfg = config_get();
            if (cfg->lyrics.enable_notifications) {
                char cmd[2048];
                snprintf(cmd, sizeof(cmd),
                    "notify-send -a wshowlyrics -u critical -t %d "
                    "\"🔒 Security Warning\" "
                    "\"Config file has insecure permissions (%04o)\\n\\n"
                    "File: %s\\n\\n"
                    "Please run: chmod 600 %s\" 2>/dev/null",
                    cfg->lyrics.notification_timeout,
                    mode,
                    path,
                    path);
                // Explicitly ignore return value - notification failure is not critical
                (void)system(cmd);
            }
        }
    }
}

// Simple INI parser
bool config_load(struct config *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn("Config file not found: %s (using defaults)", path);
        return false;
    }

    // Check and fix file permissions for security
    check_and_fix_config_permissions(path);

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
            } else if (strcmp(key, "anchor") == 0) {
                free(cfg->display.anchor);
                cfg->display.anchor = strdup(value);
            } else if (strcmp(key, "margin") == 0) {
                cfg->display.margin = atoi(value);
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
            } else if (strcmp(key, "preferred_players") == 0) {
                free(cfg->lyrics.preferred_players);
                cfg->lyrics.preferred_players = strdup(value);
            } else if (strcmp(key, "enable_lrclib") == 0) {
                cfg->lyrics.enable_lrclib = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "enable_itunes") == 0) {
                cfg->lyrics.enable_itunes = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "enable_notifications") == 0) {
                cfg->lyrics.enable_notifications = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "notification_timeout") == 0) {
                cfg->lyrics.notification_timeout = atoi(value);
            }
        } else if (strcmp(section, "deepl") == 0) {
            if (strcmp(key, "enable_deepl") == 0) {
                cfg->deepl.enable_deepl = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "api_key") == 0) {
                free(cfg->deepl.api_key);
                cfg->deepl.api_key = strdup(value);
            } else if (strcmp(key, "target_language") == 0) {
                free(cfg->deepl.target_language);
                cfg->deepl.target_language = strdup(value);
            } else if (strcmp(key, "translation_display") == 0) {
                free(cfg->deepl.translation_display);
                cfg->deepl.translation_display = strdup(value);
            } else if (strcmp(key, "translation_opacity") == 0) {
                cfg->deepl.translation_opacity = atof(value);
                // Clamp to valid range [0.0, 1.0]
                if (cfg->deepl.translation_opacity < 0.0) cfg->deepl.translation_opacity = 0.0;
                if (cfg->deepl.translation_opacity > 1.0) cfg->deepl.translation_opacity = 1.0;
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

// Structure to hold config key information
struct config_key {
    char section[64];
    char key[64];
    struct config_key *next;
};

// Free config key list
static void free_config_keys(struct config_key *head) {
    while (head) {
        struct config_key *next = head->next;
        free(head);
        head = next;
    }
}

// Parse settings.ini.example to extract all config keys
static struct config_key* parse_example_config_keys(const char *example_path) {
    FILE *f = fopen(example_path, "r");
    if (!f) {
        return NULL;
    }

    struct config_key *head = NULL;
    struct config_key *tail = NULL;
    char line[CONFIG_LINE_SIZE];
    char section[64] = {0};

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
        if (!equals || section[0] == '\0') continue;

        *equals = '\0';
        char *key = config_trim_whitespace(trimmed);

        // Add to list
        struct config_key *node = malloc(sizeof(struct config_key));
        if (!node) continue;

        strncpy(node->section, section, sizeof(node->section) - 1);
        node->section[sizeof(node->section) - 1] = '\0';
        strncpy(node->key, key, sizeof(node->key) - 1);
        node->key[sizeof(node->key) - 1] = '\0';
        node->next = NULL;

        if (!head) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }

    fclose(f);
    return head;
}

// Check if a key exists in user config file
static bool key_exists_in_file(const char *user_path, const char *section, const char *key) {
    FILE *f = fopen(user_path, "r");
    if (!f) {
        return false;
    }

    char line[CONFIG_LINE_SIZE];
    char current_section[64] = {0};
    bool found = false;

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
                strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
            }
            continue;
        }

        // Key-value pair
        char *equals = strchr(trimmed, '=');
        if (!equals) continue;

        *equals = '\0';
        char *file_key = config_trim_whitespace(trimmed);

        if (strcmp(current_section, section) == 0 && strcmp(file_key, key) == 0) {
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

// Validate user config against settings.ini.example
void config_validate_user_config(void) {
    // Get user config path
    char *user_path = config_get_user_path();
    if (!user_path) {
        return;
    }

    // Check if user config exists
    struct stat st;
    if (stat(user_path, &st) != 0) {
        free(user_path);
        return;  // User config doesn't exist, no validation needed
    }

    // Find settings.ini.example in multiple locations
    const char *example_paths[] = {
        "/etc/wshowlyrics/settings.ini.example",
        "/usr/share/wshowlyrics/settings.ini.example",
        "settings.ini.example",  // Current directory (for local builds)
        NULL
    };

    struct config_key *example_keys = NULL;
    const char *found_example_path = NULL;

    for (int i = 0; example_paths[i] != NULL; i++) {
        example_keys = parse_example_config_keys(example_paths[i]);
        if (example_keys) {
            found_example_path = example_paths[i];
            break;
        }
    }

    if (!example_keys) {
        free(user_path);
        return;  // Could not find example config, skip validation
    }

    // Check for missing keys
    struct config_key *missing_head = NULL;
    struct config_key *missing_tail = NULL;

    for (struct config_key *node = example_keys; node != NULL; node = node->next) {
        if (!key_exists_in_file(user_path, node->section, node->key)) {
            // Add to missing list
            struct config_key *missing = malloc(sizeof(struct config_key));
            if (missing) {
                strncpy(missing->section, node->section, sizeof(missing->section) - 1);
                missing->section[sizeof(missing->section) - 1] = '\0';
                strncpy(missing->key, node->key, sizeof(missing->key) - 1);
                missing->key[sizeof(missing->key) - 1] = '\0';
                missing->next = NULL;

                if (!missing_head) {
                    missing_head = missing_tail = missing;
                } else {
                    missing_tail->next = missing;
                    missing_tail = missing;
                }
            }
        }
    }

    // Display warnings for missing keys
    if (missing_head) {
        log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        log_warn("Your configuration file is missing some new settings:");
        log_warn("User config: %s", user_path);
        log_warn("");

        // Build missing keys list for notification
        char missing_keys_list[1024] = {0};
        int count = 0;
        int total_count = 0;

        for (struct config_key *node = missing_head; node != NULL; node = node->next) {
            log_warn("  [%s] %s", node->section, node->key);
            total_count++;

            // Add to notification message (limit to first 5 keys to avoid too long message)
            if (count < 5) {
                char entry[256];
                snprintf(entry, sizeof(entry), "• [%s] %s\\n", node->section, node->key);
                strncat(missing_keys_list, entry, sizeof(missing_keys_list) - strlen(missing_keys_list) - 1);
                count++;
            }
        }

        // Add "and X more..." if there are more than 5 missing keys
        if (total_count > 5) {
            char more[64];
            snprintf(more, sizeof(more), "...and %d more", total_count - 5);
            strncat(missing_keys_list, more, sizeof(missing_keys_list) - strlen(missing_keys_list) - 1);
        }

        log_warn("");
        log_warn("Please check the example configuration file for details:");
        log_warn("  %s", found_example_path);
        log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

        // Send desktop notification
        // Check if notifications are explicitly disabled in config
        bool should_notify = true;

        // Check if enable_notifications key exists in user config
        if (key_exists_in_file(user_path, "lyrics", "enable_notifications")) {
            // Key exists, use its value
            should_notify = g_config.lyrics.enable_notifications;
        }
        // If key doesn't exist, default to true (show notification)

        if (should_notify) {
            char cmd[2048];
            snprintf(cmd, sizeof(cmd),
                "notify-send -a wshowlyrics -u normal -t %d "
                "\"⚠️ Configuration Update Required\" "
                "\"Your config is missing new settings:\\n%s\\n"
                "Check: %s\" 2>/dev/null",
                g_config.lyrics.notification_timeout,
                missing_keys_list,
                found_example_path);

            int ret = system(cmd);
            if (ret != 0) {
                log_warn("Failed to send desktop notification (notify-send may not be available)");
            }
        }
    }

    // Cleanup
    free_config_keys(example_keys);
    free_config_keys(missing_head);
    free(user_path);
}
