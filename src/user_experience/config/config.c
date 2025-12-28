#define _GNU_SOURCE
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
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

// Global configuration instance
struct config g_config;

// Forward declarations
static bool validate_config_path(const char *path);
static bool parse_hex_color(const char *hex, double rgba[4]);

// Parse rate limit value with support for intuitive formats
// Returns milliseconds between requests
static int parse_rate_limit_value(const char *value) {
    char *endptr;
    long number = strtol(value, &endptr, 10);

    if (number <= 0) {
        log_warn("Invalid rate_limit value: %s (must be positive)", value);
        return 6000; // Default: 6 seconds
    }

    if (*endptr == 'm' || *endptr == 'M') {
        // Requests per minute
        return 60000 / number;
    } else if (*endptr == 's' || *endptr == 'S') {
        // Requests per second
        return 1000 / number;
    } else if (*endptr == '\0') {
        // Raw milliseconds (backward compatible)
        return number;
    } else {
        log_warn("Invalid rate_limit format: %s (use: 50m, 5s, or 1200)", value);
        return 6000; // Default: 6 seconds
    }
}

// Parse [display] section
static void parse_display_section(struct config *cfg, const char *key, const char *value) {
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
    } else if (strcmp(key, "enable_multiline_lrcx") == 0) {
        cfg->display.enable_multiline_lrcx = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
    }
}

// Parse [lyrics] section
static void parse_lyrics_section(struct config *cfg, const char *key, const char *value) {
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
    } else if (strcmp(key, "global_offset_ms") == 0) {
        cfg->lyrics.global_offset_ms = atoi(value);
        // Clamp to reasonable range [-5000, +5000] (-5s to +5s)
        if (cfg->lyrics.global_offset_ms < -5000) cfg->lyrics.global_offset_ms = -5000;
        if (cfg->lyrics.global_offset_ms > 5000) cfg->lyrics.global_offset_ms = 5000;
    }
}

// Parse [spotify] section
static void parse_spotify_section(struct config *cfg, const char *key, const char *value) {
    if (strcmp(key, "auto_position_fix") == 0) {
        cfg->spotify.auto_position_fix = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(key, "position_fix_delay_ms") == 0) {
        cfg->spotify.position_fix_delay_ms = atoi(value);
        // Clamp to reasonable range [1, 1000] (1ms to 1 second)
        if (cfg->spotify.position_fix_delay_ms < 1) cfg->spotify.position_fix_delay_ms = 1;
        if (cfg->spotify.position_fix_delay_ms > 1000) cfg->spotify.position_fix_delay_ms = 1000;
    } else if (strcmp(key, "position_fix_wait_ms") == 0) {
        cfg->spotify.position_fix_wait_ms = atoi(value);
        // Clamp to reasonable range [0, 5000] (0ms to 5 seconds)
        if (cfg->spotify.position_fix_wait_ms < 0) cfg->spotify.position_fix_wait_ms = 0;
        if (cfg->spotify.position_fix_wait_ms > 5000) cfg->spotify.position_fix_wait_ms = 5000;
    }
}

// Parse common translation fields (shared between [translation] and deprecated [deepl])
static bool parse_common_translation_fields(struct config *cfg, const char *key, const char *value) {
    if (strcmp(key, "api_key") == 0) {
        free(cfg->translation.api_key);
        cfg->translation.api_key = strdup(value);
        return true;
    } else if (strcmp(key, "target_language") == 0) {
        free(cfg->translation.target_language);
        cfg->translation.target_language = strdup(value);
        return true;
    } else if (strcmp(key, "translation_display") == 0) {
        free(cfg->translation.translation_display);
        cfg->translation.translation_display = strdup(value);
        return true;
    } else if (strcmp(key, "translation_opacity") == 0) {
        cfg->translation.translation_opacity = atof(value);
        // Clamp to valid range [0.0, 1.0]
        if (cfg->translation.translation_opacity < 0.0) cfg->translation.translation_opacity = 0.0;
        if (cfg->translation.translation_opacity > 1.0) cfg->translation.translation_opacity = 1.0;
        return true;
    }
    return false;
}

// Parse [translation] section
static void parse_translation_section(struct config *cfg, const char *key, const char *value) {
    if (strcmp(key, "provider") == 0) {
        free(cfg->translation.provider);
        cfg->translation.provider = strdup(value);
    } else if (parse_common_translation_fields(cfg, key, value)) {
        // Handled by common fields parser
    } else if (strcmp(key, "rate_limit_ms") == 0 || strcmp(key, "rate_limit") == 0) {
        cfg->translation.rate_limit_ms = parse_rate_limit_value(value);
        // Clamp to reasonable range [0, 60000] (0-60 seconds)
        if (cfg->translation.rate_limit_ms < 0) cfg->translation.rate_limit_ms = 0;
        if (cfg->translation.rate_limit_ms > 60000) cfg->translation.rate_limit_ms = 60000;
    } else if (strcmp(key, "max_retries") == 0) {
        cfg->translation.max_retries = atoi(value);
        // Clamp to reasonable range [0, 10]
        if (cfg->translation.max_retries < 0) cfg->translation.max_retries = 0;
        if (cfg->translation.max_retries > 10) cfg->translation.max_retries = 10;
    } else if (strcmp(key, "revalidate_count") == 0) {
        cfg->translation.revalidate_count = atoi(value);
        // Clamp to reasonable range [1, 10]
        if (cfg->translation.revalidate_count < 1) cfg->translation.revalidate_count = 1;
        if (cfg->translation.revalidate_count > 10) cfg->translation.revalidate_count = 10;
    } else if (strcmp(key, "cache_policy") == 0) {
        // Parse cache policy: comfort, balanced, aggressive
        if (strcasecmp(value, "comfort") == 0) {
            cfg->translation.cache_policy = CACHE_POLICY_COMFORT;
        } else if (strcasecmp(value, "balanced") == 0) {
            cfg->translation.cache_policy = CACHE_POLICY_BALANCED;
        } else if (strcasecmp(value, "aggressive") == 0) {
            cfg->translation.cache_policy = CACHE_POLICY_AGGRESSIVE;
        } else {
            log_warn("Unknown cache_policy '%s', using default 'balanced'", value);
            cfg->translation.cache_policy = CACHE_POLICY_BALANCED;
        }
    }
}

// Parse [cache] section
static void parse_cache_section(struct config *cfg, const char *key, const char *value) {
    if (strcmp(key, "cleanup_policy") == 0) {
        if (strcmp(value, "off") == 0) {
            cfg->cache.cleanup_policy = CACHE_CLEANUP_OFF;
        } else if (strcmp(value, "aggressive") == 0) {
            cfg->cache.cleanup_policy = CACHE_CLEANUP_AGGRESSIVE;
        } else if (strcmp(value, "normal") == 0) {
            cfg->cache.cleanup_policy = CACHE_CLEANUP_NORMAL;
        } else if (strcmp(value, "conservative") == 0) {
            cfg->cache.cleanup_policy = CACHE_CLEANUP_CONSERVATIVE;
        } else {
            log_warn("Invalid cache.cleanup_policy value: %s (using default: normal)", value);
            cfg->cache.cleanup_policy = CACHE_CLEANUP_NORMAL;
        }
    }
}

// Parse deprecated [deepl] section with migration warning
static void parse_deprecated_deepl_section(struct config *cfg, const char *key, const char *value) {
    static bool deepl_warning_shown = false;
    if (!deepl_warning_shown) {
        log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        log_warn("⚠️  [deepl] section is deprecated");
        log_warn("Please migrate to [translation] section in your config");
        log_warn("See settings.ini.example for new format");
        log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        deepl_warning_shown = true;
    }

    if (strcmp(key, "enable_deepl") == 0) {
        // Migrate enable_deepl to provider
        bool enable_deepl = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
        if (enable_deepl) {
            free(cfg->translation.provider);
            cfg->translation.provider = strdup("deepl");
        } else {
            free(cfg->translation.provider);
            cfg->translation.provider = strdup("false");
        }
    } else {
        // Use common fields parser
        parse_common_translation_fields(cfg, key, value);
    }
}

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
    cfg->display.enable_multiline_lrcx = true;  // Enabled by default

    // Lyrics defaults
    cfg->lyrics.search_dirs = strdup("");  // Empty = use hardcoded defaults
    cfg->lyrics.extensions = strdup("lrcx,lrc,srt");  // All formats
    cfg->lyrics.preferred_players = strdup("");  // Empty = use %any
    cfg->lyrics.enable_lrclib = true;
    cfg->lyrics.enable_itunes = true;
    cfg->lyrics.enable_notifications = true;  // Enabled by default
    cfg->lyrics.notification_timeout = 5000;  // 5 seconds by default
    cfg->lyrics.global_offset_ms = 0;  // No global offset by default

    // Spotify defaults
    cfg->spotify.auto_position_fix = true;  // Enabled by default
    cfg->spotify.position_fix_delay_ms = 1;  // 1ms by default (imperceptible)
    cfg->spotify.position_fix_wait_ms = 2000;  // 2 seconds by default (allows track to start)

    // Translation defaults (multi-provider support)
    cfg->translation.provider = strdup("false");  // Disabled by default
    cfg->translation.api_key = strdup("");  // No API key by default
    cfg->translation.target_language = strdup("EN");  // English by default
    cfg->translation.translation_display = strdup("both");  // Show both original and translation
    cfg->translation.translation_opacity = 0.7;  // 70% opacity by default
    cfg->translation.rate_limit_ms = 6000;  // 6 seconds by default (suitable for Gemini free tier)
    cfg->translation.max_retries = 3;  // Maximum 3 retry attempts
    cfg->translation.revalidate_count = 2;  // Re-validate last 2 translations on resume
    cfg->translation.cache_policy = CACHE_POLICY_BALANCED;  // 75% threshold by default

    // Cache defaults
    cfg->cache.cleanup_policy = CACHE_CLEANUP_NORMAL;  // 15 days by default
}

void config_free(struct config *cfg) {
    free(cfg->display.font_family);
    free(cfg->display.font_weight);
    free(cfg->display.anchor);
    free(cfg->lyrics.search_dirs);
    free(cfg->lyrics.extensions);
    free(cfg->lyrics.preferred_players);
    free(cfg->translation.provider);
    free(cfg->translation.api_key);
    free(cfg->translation.target_language);
    free(cfg->translation.translation_display);
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

// Validate path for safe use in shell commands
static bool is_safe_path_for_shell(const char *path) {
    if (!path || path[0] != '/') return false;  // Must be absolute path

    // Check for shell metacharacters that could enable command injection
    for (const char *p = path; *p; p++) {
        if (*p == ';' || *p == '|' || *p == '&' || *p == '`' ||
            *p == '$' || *p == '(' || *p == ')' || *p == '<' ||
            *p == '>' || *p == '\n' || *p == '\r' || *p == '\\' ||
            *p == '"' || *p == '\'') {
            return false;
        }
    }
    return true;
}

// Check config file permissions and fix them if they're too permissive
static void check_and_fix_config_permissions(const char *path) {
    // Use open() + fstat() + fchmod() to avoid TOCTOU race condition
    // This ensures we're always operating on the same file
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return;  // Can't open file, skip validation
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
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

        // Try to automatically fix permissions using fchmod to avoid TOCTOU
        if (fchmod(fd, 0600) == 0) {
            log_info("✓ Automatically fixed permissions to 0600");
            log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            close(fd);
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
            if (cfg->lyrics.enable_notifications && is_safe_path_for_shell(path)) {
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
                // Notification failure is not critical, but check return value to satisfy compiler
                if (system(cmd) != 0) {
                    // Notification failed, but we continue anyway
                }
            }
            close(fd);
        }
    } else {
        close(fd);
    }
}

// Simple INI parser
bool config_load(struct config *cfg, const char *path) {
    // Validate path for security (path traversal prevention)
    if (!validate_config_path(path)) {
        log_warn("Config path validation failed: %s", path);
        return false;
    }

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
                snprintf(section, sizeof(section), "%s", trimmed + 1);
            }
            continue;
        }

        // Key-value pair
        char *equals = strchr(trimmed, '=');
        if (!equals) continue;

        *equals = '\0';
        char *key = config_trim_whitespace(trimmed);
        char *value = config_trim_whitespace(equals + 1);

        // Remove inline comments (# after value)
        char *comment = strchr(value, '#');
        if (comment) {
            *comment = '\0';
            value = config_trim_whitespace(value);
        }

        // Parse based on section using helper functions
        if (strcmp(section, "display") == 0) {
            parse_display_section(cfg, key, value);
        } else if (strcmp(section, "lyrics") == 0) {
            parse_lyrics_section(cfg, key, value);
        } else if (strcmp(section, "spotify") == 0) {
            parse_spotify_section(cfg, key, value);
        } else if (strcmp(section, "translation") == 0) {
            parse_translation_section(cfg, key, value);
        } else if (strcmp(section, "cache") == 0) {
            parse_cache_section(cfg, key, value);
        } else if (strcmp(section, "deepl") == 0) {
            parse_deprecated_deepl_section(cfg, key, value);
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
    char *saveptr;
    char *token = strtok_r(exts, ",", &saveptr);
    while (token) {
        char *trimmed = config_trim_whitespace(token);
        if (strcasecmp(trimmed, ext) == 0) {
            found = true;
            break;
        }
        token = strtok_r(NULL, ",", &saveptr);
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

// Common INI parser - extracts all section.key pairs from config file
static struct config_key* parse_config_keys_from_file(FILE *f) {
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
                snprintf(section, sizeof(section), "%s", trimmed + 1);
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

        snprintf(node->section, sizeof(node->section), "%s", section);
        snprintf(node->key, sizeof(node->key), "%s", key);
        node->next = NULL;

        if (!head) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }

    return head;
}

// Parse settings.ini.example to extract all config keys
static struct config_key* parse_example_config_keys(const char *example_path) {
    FILE *f = fopen(example_path, "r");
    if (!f) {
        return NULL;
    }

    struct config_key *result = parse_config_keys_from_file(f);
    fclose(f);
    return result;
}

// Check if a key exists in user config file
static bool key_exists_in_file(const char *user_path, const char *section, const char *key) {
    // Validate path for security
    if (!validate_config_path(user_path)) {
        return false;
    }

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
                snprintf(current_section, sizeof(current_section), "%s", trimmed + 1);
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

// Structure to hold section names
struct section_list {
    char section[64];
    struct section_list *next;
};

// Free section list
static void free_section_list(struct section_list *head) {
    while (head) {
        struct section_list *next = head->next;
        free(head);
        head = next;
    }
}

// Validate config file path for security (path traversal prevention)
static bool validate_config_path(const char *path) {
    if (!path) {
        return false;
    }

    char resolved_path[PATH_MAX];

    // Try to resolve the full path (for existing files)
    if (!realpath(path, resolved_path)) {
        // If file doesn't exist, try to validate the directory path instead

        // Extract directory from path
        char dir_path[PATH_MAX];
        snprintf(dir_path, sizeof(dir_path), "%s", path);
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash) {
            *last_slash = '\0';

            // Try to resolve the directory
            if (!realpath(dir_path, resolved_path)) {
                // Directory doesn't exist either - this might be okay for new files
                // Just validate the input path directly (must be absolute)
                if (path[0] != '/') {
                    return false;  // Relative paths not allowed
                }
                snprintf(resolved_path, sizeof(resolved_path), "%s", path);
            }
        } else {
            // No directory component - invalid path
            return false;
        }
    }

    // Config files must be in one of these safe directories:
    // 1. User's home directory or XDG config (for user configs)
    // 2. /etc/ (for system-wide configs)
    // 3. /usr/share/ (for installed example configs)
    // 4. Current working directory (for local development builds only)
    const char *home = getenv("HOME");
    const char *xdg_config = getenv("XDG_CONFIG_HOME");

    // Check if resolved path is in a safe location
    bool is_safe = false;

    // Check user directories
    if (home && strncmp(resolved_path, home, strlen(home)) == 0) {
        is_safe = true;
    }
    if (xdg_config && strncmp(resolved_path, xdg_config, strlen(xdg_config)) == 0) {
        is_safe = true;
    }

    // Check system directories
    if (strncmp(resolved_path, "/etc/", 5) == 0) {
        is_safe = true;
    }
    if (strncmp(resolved_path, "/usr/share/", 11) == 0) {
        is_safe = true;
    }

    // Check current directory (only for local builds)
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) && strncmp(resolved_path, cwd, strlen(cwd)) == 0) {
        is_safe = true;
    }

    return is_safe;
}

// Parse user config to extract all sections (including empty ones)
static struct section_list* parse_user_config_sections(const char *user_path) {
    // Validate path for security
    if (!validate_config_path(user_path)) {
        return NULL;
    }

    FILE *f = fopen(user_path, "r");
    if (!f) {
        return NULL;
    }

    struct section_list *head = NULL;
    struct section_list *tail = NULL;
    char line[CONFIG_LINE_SIZE];

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
                char *section_name = config_trim_whitespace(trimmed + 1);

                // Check if section already in list
                bool exists = false;
                for (struct section_list *node = head; node != NULL; node = node->next) {
                    if (strcmp(node->section, section_name) == 0) {
                        exists = true;
                        break;
                    }
                }

                // Add to list if not already present
                if (!exists) {
                    struct section_list *node = malloc(sizeof(struct section_list));
                    if (node) {
                        snprintf(node->section, sizeof(node->section), "%s", section_name);
                        node->next = NULL;

                        if (!head) {
                            head = tail = node;
                        } else {
                            tail->next = node;
                            tail = node;
                        }
                    }
                }
            }
        }
    }

    fclose(f);
    return head;
}

// Parse user config to extract all config keys
static struct config_key* parse_user_config_keys(const char *user_path) {
    // Validate path for security
    if (!validate_config_path(user_path)) {
        return NULL;
    }

    FILE *f = fopen(user_path, "r");
    if (!f) {
        return NULL;
    }

    struct config_key *result = parse_config_keys_from_file(f);
    fclose(f);
    return result;
}

// Check if a section exists in example config
static bool section_exists_in_example(struct config_key *example_keys, const char *section) {
    // Special handling for deprecated [deepl] section - it's known but deprecated
    if (strcmp(section, "deepl") == 0) {
        return true;  // Don't warn about deprecated section
    }

    for (struct config_key *node = example_keys; node != NULL; node = node->next) {
        if (strcmp(node->section, section) == 0) {
            return true;
        }
    }
    return false;
}

// Check if a key exists in example config
static bool key_exists_in_example(struct config_key *example_keys, const char *section, const char *key) {
    // Special handling for deprecated [deepl] section - it's known but deprecated
    if (strcmp(section, "deepl") == 0) {
        return true;  // Don't warn about deprecated section
    }

    for (struct config_key *node = example_keys; node != NULL; node = node->next) {
        if (strcmp(node->section, section) == 0 && strcmp(node->key, key) == 0) {
            return true;
        }
    }
    return false;
}

// Find example config file in multiple locations
// Returns NULL if not found, otherwise returns path and sets example_keys
static const char* find_example_config_path(struct config_key **example_keys) {
    const char *example_paths[] = {
        "/etc/wshowlyrics/settings.ini.example",
        "/usr/share/wshowlyrics/settings.ini.example",
        "settings.ini.example",  // Current directory (for local builds)
        NULL
    };

    for (int i = 0; example_paths[i] != NULL; i++) {
        *example_keys = parse_example_config_keys(example_paths[i]);
        if (*example_keys) {
            return example_paths[i];
        }
    }

    return NULL;
}

// Find unknown config entries (sections and keys not in example)
static struct config_key* find_unknown_config_entries(
    struct config_key *example_keys,
    struct section_list *user_sections,
    struct config_key *user_keys) {

    struct config_key *unknown_head = NULL;
    struct config_key *unknown_tail = NULL;

    // First check for unknown sections (including empty ones)
    if (user_sections) {
        for (struct section_list *sec = user_sections; sec != NULL; sec = sec->next) {
            if (!section_exists_in_example(example_keys, sec->section)) {
                // Unknown section - add it to unknown list
                struct config_key *unknown = malloc(sizeof(struct config_key));
                if (unknown) {
                    snprintf(unknown->section, sizeof(unknown->section), "%s", sec->section);
                    snprintf(unknown->key, sizeof(unknown->key), "(entire section)");
                    unknown->next = NULL;

                    if (!unknown_head) {
                        unknown_head = unknown_tail = unknown;
                    } else {
                        unknown_tail->next = unknown;
                        unknown_tail = unknown;
                    }
                }
            }
        }
    }

    // Then check for unknown keys in known sections
    if (user_keys) {
        for (struct config_key *node = user_keys; node != NULL; node = node->next) {
            // Only check keys in sections that exist in example
            if (section_exists_in_example(example_keys, node->section) &&
                !key_exists_in_example(example_keys, node->section, node->key)) {
                // Known section but unknown key
                struct config_key *unknown = malloc(sizeof(struct config_key));
                if (unknown) {
                    snprintf(unknown->section, sizeof(unknown->section), "%s", node->section);
                    snprintf(unknown->key, sizeof(unknown->key), "%s", node->key);
                    unknown->next = NULL;

                    if (!unknown_head) {
                        unknown_head = unknown_tail = unknown;
                    } else {
                        unknown_tail->next = unknown;
                        unknown_tail = unknown;
                    }
                }
            }
        }
    }

    return unknown_head;
}

// Find missing config entries (keys in example but not in user config)
static struct config_key* find_missing_config_entries(
    struct config_key *example_keys,
    const char *user_path) {

    struct config_key *missing_head = NULL;
    struct config_key *missing_tail = NULL;

    for (struct config_key *node = example_keys; node != NULL; node = node->next) {
        if (!key_exists_in_file(user_path, node->section, node->key)) {
            // Add to missing list
            struct config_key *missing = malloc(sizeof(struct config_key));
            if (missing) {
                snprintf(missing->section, sizeof(missing->section), "%s", node->section);
                snprintf(missing->key, sizeof(missing->key), "%s", node->key);
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

    return missing_head;
}

// Display warning for unknown config keys
static void display_unknown_keys_warning(struct config_key *unknown_keys, const char *example_path) {
    if (!unknown_keys) return;

    log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    log_warn("⚠️  Unknown configuration fields detected:");
    log_warn("These settings are not recognized and will be ignored:");
    log_warn("");

    for (struct config_key *node = unknown_keys; node != NULL; node = node->next) {
        log_warn("  [%s] %s", node->section, node->key);
    }

    log_warn("");
    log_warn("These may be from an older version or typos.");
    log_warn("Please check: %s", example_path);
    log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
}

// Display warning for missing config keys and send notification
static void display_missing_keys_warning(struct config_key *missing_keys, const char *user_path, const char *example_path) {
    if (!missing_keys) return;

    log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    log_warn("Your configuration file is missing some new settings:");
    log_warn("User config: %s", user_path);
    log_warn("");

    // Build missing keys list for notification
    char missing_keys_list[PATH_BUFFER_SIZE] = {0};
    size_t offset = 0;
    int count = 0;
    int total_count = 0;

    for (struct config_key *node = missing_keys; node != NULL; node = node->next) {
        log_warn("  [%s] %s", node->section, node->key);
        total_count++;

        // Add to notification message (limit to first 5 keys to avoid too long message)
        if (count < 5 && offset < sizeof(missing_keys_list)) {
            int written = snprintf(missing_keys_list + offset,
                                  sizeof(missing_keys_list) - offset,
                                  "• [%s] %s\\n", node->section, node->key);
            if (written > 0) {
                offset += written;
            }
            count++;
        }
    }

    // Add "and X more..." if there are more than 5 missing keys
    if (total_count > 5 && offset < sizeof(missing_keys_list)) {
        snprintf(missing_keys_list + offset,
                sizeof(missing_keys_list) - offset,
                "...and %d more", total_count - 5);
    }

    log_warn("");
    log_warn("Please check the example configuration file for details:");
    log_warn("  %s", example_path);
    log_warn("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    // Send desktop notification
    bool should_notify = true;

    // Check if enable_notifications key exists in user config
    if (key_exists_in_file(user_path, "lyrics", "enable_notifications")) {
        // Key exists, use its value
        should_notify = g_config.lyrics.enable_notifications;
    }

    if (should_notify && is_safe_path_for_shell(example_path)) {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
            "notify-send -a wshowlyrics -u normal -t %d "
            "\"⚠️ Configuration Update Required\" "
            "\"Your config is missing new settings:\\n%s\\n"
            "Check: %s\" 2>/dev/null",
            g_config.lyrics.notification_timeout,
            missing_keys_list,
            example_path);

        int ret = system(cmd);
        if (ret != 0) {
            log_warn("Failed to send desktop notification (notify-send may not be available)");
        }
    }
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

    // Validate path for security (path traversal prevention)
    if (!validate_config_path(user_path)) {
        log_warn("Config path validation failed: %s", user_path);
        free(user_path);
        return;
    }

    // Find example config file
    struct config_key *example_keys = NULL;
    const char *found_example_path = find_example_config_path(&example_keys);
    if (!example_keys) {
        free(user_path);
        return;  // Could not find example config, skip validation
    }

    // Parse user config
    // SECURITY: Path is validated above using validate_config_path() which:
    // - Uses realpath() to canonicalize and resolve symlinks/.. sequences
    // - Validates path is within safe directories (HOME, XDG_CONFIG_HOME, /etc/, /usr/share/, cwd)
    // - Prevents path traversal attacks
    struct section_list *user_sections = parse_user_config_sections(user_path);
    struct config_key *user_keys = parse_user_config_keys(user_path);

    // Find unknown and missing config entries using helper functions
    struct config_key *unknown_keys = find_unknown_config_entries(example_keys, user_sections, user_keys);
    struct config_key *missing_keys = find_missing_config_entries(example_keys, user_path);

    // Display warnings
    display_unknown_keys_warning(unknown_keys, found_example_path);
    display_missing_keys_warning(missing_keys, user_path, found_example_path);

    // Cleanup
    free_config_keys(example_keys);
    free_section_list(user_sections);
    free_config_keys(user_keys);
    free_config_keys(unknown_keys);
    free_config_keys(missing_keys);
    free(user_path);
}

// Load configuration with automatic fallback (user -> system)
char* config_load_with_fallback(struct config *cfg) {
    if (!cfg) {
        return NULL;
    }

    bool config_loaded = false;
    char *config_loaded_path = NULL;

    // Try user config first
    char *user_config_path = config_get_path();
    if (user_config_path) {
        // Create config directory if it doesn't exist
        char *dir_path = strdup(user_config_path);
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (mkdir(dir_path, 0700) == -1 && errno != EEXIST) {
                // Failed to create directory and it's not because it already exists
                log_warn("Failed to create config directory %s: %s", dir_path, strerror(errno));
            }
        }
        free(dir_path);

        // Validate path before file operations (security)
        if (!validate_config_path(user_config_path)) {
            log_error("Config path validation failed: %s", user_config_path);
            free(user_config_path);
            return NULL;
        }

        // Try to create config file atomically - if it exists, open() will fail
        // This eliminates TOCTOU by combining check and create into single atomic operation
        mode_t old_mask = umask(0077);  // Ensure rw------- permissions (privacy)
        int fd = open(user_config_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        umask(old_mask);

        // If file was just created (fd >= 0), copy from system config
        if (fd >= 0) {
            const char *system_config = "/etc/wshowlyrics/settings.ini";
            FILE *src = fopen(system_config, "r");
            if (src) {
                FILE *dst = fdopen(fd, "w");
                if (dst) {
                    char buf[CONTENT_BUFFER_SIZE];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                        fwrite(buf, 1, n, dst);
                    }
                    fclose(dst);  // Also closes fd
                    printf("Copied system config to user config: %s\n", user_config_path);
                } else {
                    close(fd);  // fdopen failed, close manually
                }
                fclose(src);
            } else {
                close(fd);  // System config not found, close the empty file
            }
        }
        // If fd < 0 and errno == EEXIST, file already exists - continue normally

        // Try to load user config
        if (config_load(cfg, user_config_path)) {
            config_loaded = true;
            config_loaded_path = user_config_path;
        } else {
            free(user_config_path);
        }
    }

    // If user config not found, try system-wide config
    if (!config_loaded) {
        const char *system_config = "/etc/wshowlyrics/settings.ini";
        if (config_load(cfg, system_config)) {
            config_loaded_path = strdup(system_config);
        }
        // Note: config_load returns false if file doesn't exist, which is fine
        // We'll just use the defaults initialized by caller
    }

    return config_loaded_path;
}

float config_get_cache_threshold(enum translation_cache_policy policy) {
    switch (policy) {
        case CACHE_POLICY_COMFORT:
            return 0.50f;  // 50% - Early save, safe
        case CACHE_POLICY_BALANCED:
            return 0.75f;  // 75% - Default, balanced
        case CACHE_POLICY_AGGRESSIVE:
            return 0.90f;  // 90% - Late save, more complete
        default:
            return 0.75f;  // Default to balanced
    }
}

int config_get_cleanup_days(enum cache_cleanup_policy policy) {
    switch (policy) {
        case CACHE_CLEANUP_OFF:
            return -1;  // Disabled
        case CACHE_CLEANUP_AGGRESSIVE:
            return 7;   // 7 days
        case CACHE_CLEANUP_NORMAL:
            return 15;  // 15 days (default)
        case CACHE_CLEANUP_CONSERVATIVE:
            return 30;  // 30 days
        default:
            return 15;  // Default to normal
    }
}
