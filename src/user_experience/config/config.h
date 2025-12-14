#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

// Display settings
struct display_config {
    char *font_family;
    int font_size;
    char *font_weight;  // "normal", "bold", etc.

    // Colors in RGBA format (0.0 - 1.0)
    double color_active[4];
    double color_background[4];

    char *anchor;       // Anchor position: "top", "bottom", "left", "right"
    int margin;         // Margin from edge in pixels
    int line_spacing;
};

// Lyrics settings
struct lyrics_config {
    char *search_dirs;  // Colon-separated paths
    char *extensions;   // Comma-separated extensions (e.g., "lrcx,lrc,srt")
    char *preferred_players;  // Comma-separated player names (e.g., "mpv,spotify")
    bool enable_lrclib;
    bool enable_itunes;
    bool enable_notifications;  // Desktop notifications for track changes
    int notification_timeout;   // Notification timeout in milliseconds (default: 5000)
};

// DeepL translation settings
struct deepl_config {
    bool enable_deepl;           // Enable DeepL translation feature
    char *api_key;               // DeepL API key (Free keys end with :fx)
    char *target_language;       // Target language code (e.g., EN, KO, JA, ZH)
    char *translation_display;   // Display mode: "both" or "translation_only"
    double translation_opacity;  // Translation text opacity (0.0 - 1.0, default: 0.7)
};

// Main configuration
struct config {
    struct display_config display;
    struct lyrics_config lyrics;
    struct deepl_config deepl;
};

// Global configuration instance (prefer using config_get() instead of direct access)
extern struct config g_config;

// Get pointer to global config (use this instead of accessing g_config directly)
struct config* config_get(void);

// Initialize configuration with defaults
void config_init_defaults(struct config *cfg);

// Load configuration from file
bool config_load(struct config *cfg, const char *path);

// Free configuration resources
void config_free(struct config *cfg);

// Get config file path (~/.config/wshowlyrics/settings.ini)
char* config_get_path(void);

// Helper: Check if extension is enabled in config
bool config_is_extension_enabled(const char *ext);

// Helper: Trim whitespace from string (modifies in-place)
char* config_trim_whitespace(char *str);

// Validate user config against settings.ini.example
void config_validate_user_config(void);

#endif // CONFIG_H
