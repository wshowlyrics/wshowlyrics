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

    bool enable_multiline_lrcx;  // Enable multi-line display for LRCX format (prev, current, next)

    char *layer;  // Wayland layer: "bottom", "top", "overlay" (default: "top")
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
    int global_offset_ms;       // Global timing offset in milliseconds (default: 0, range: -5000 to +5000)
};

// Translation cache policy (save threshold presets)
enum translation_cache_policy {
    CACHE_POLICY_COMFORT,       // Save at 50% completion (safe, early save)
    CACHE_POLICY_BALANCED,      // Save at 75% completion (default)
    CACHE_POLICY_AGGRESSIVE     // Save at 90% completion (late save, more complete)
};

// Cache storage mode
enum cache_mode {
    CACHE_MODE_PERSISTENT,  // ~/.cache/wshowlyrics (default, survives reboot)
    CACHE_MODE_SESSION,     // $XDG_RUNTIME_DIR/wshowlyrics/cache (RAM, cleared on reboot)
    CACHE_MODE_OFF          // No caching (API calls every time)
};

// Cache cleanup policy (automatic removal of old cache files)
enum cache_cleanup_policy {
    CACHE_CLEANUP_OFF,          // Disable automatic cleanup
    CACHE_CLEANUP_AGGRESSIVE,   // Delete files not accessed in 7 days
    CACHE_CLEANUP_NORMAL,       // Delete files not accessed in 15 days (default)
    CACHE_CLEANUP_CONSERVATIVE  // Delete files not accessed in 30 days
};

// Cache settings
struct cache_config {
    enum cache_mode mode;                       // Cache storage mode
    enum cache_cleanup_policy cleanup_policy;    // Automatic cleanup policy
};

// Spotify settings
struct spotify_config {
    bool auto_position_fix;      // Automatic position drift fix for track changes
    int position_fix_delay_ms;   // Delay in milliseconds for pause/play toggle (default: 1)
    int position_fix_wait_ms;    // Wait time before applying position fix (default: 2000)
};

// Translation settings (multi-provider support)
struct translation_config {
    char *provider;              // Provider and model: "deepl", "gemini-2.5-flash", "claude-sonnet-4-5", "false"
    char *api_key;               // API key for the selected provider
    char *target_language;       // Target language code (e.g., EN, KO, JA, ZH)
    char *translation_display;   // Display mode: "both" or "translation_only"
    double translation_opacity;  // Translation text opacity (0.0 - 1.0, default: 0.7)
    int rate_limit_ms;           // Rate limit delay in milliseconds (default: 6000 for Gemini, 500 for Claude, 200 for DeepL)
    int max_retries;             // Maximum retry attempts for rate limit errors (default: 3)
    int revalidate_count;        // Number of last translations to re-validate on partial cache resume (1-10, default: 2)
    enum translation_cache_policy cache_policy; // Cache save policy: comfort (50%), balanced (75%), aggressive (90%)
};

// Main configuration
struct config {
    struct display_config display;
    struct lyrics_config lyrics;
    struct spotify_config spotify;
    struct translation_config translation;
    struct cache_config cache;
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

// Load configuration with automatic fallback (user -> system)
// Returns path to loaded config file (must be freed by caller), or NULL if only defaults used
char* config_load_with_fallback(struct config *cfg);

// Helper: Check if extension is enabled in config
bool config_is_extension_enabled(const char *ext);

// Helper: Trim whitespace from string (modifies in-place)
char* config_trim_whitespace(char *str);

// Validate user config against settings.ini.example
void config_validate_user_config(void);

// Get cache threshold value (0.0 - 1.0) from policy
float config_get_cache_threshold(enum translation_cache_policy policy);

// Get cache cleanup days from policy (returns -1 for CACHE_CLEANUP_OFF)
int config_get_cleanup_days(enum cache_cleanup_policy policy);

#endif // CONFIG_H
