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

    int margin_bottom;
    int line_spacing;
};

// Lyrics settings
struct lyrics_config {
    char *search_dirs;  // Colon-separated paths
    char *extensions;   // Comma-separated extensions (e.g., "lrcx,lrc,srt")
    bool enable_lrclib;
    bool enable_itunes;
};

// Main configuration
struct config {
    struct display_config display;
    struct lyrics_config lyrics;
};

// Global configuration instance
extern struct config g_config;

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

#endif // CONFIG_H
