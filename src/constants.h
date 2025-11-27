#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>

// ============================================================================
// Buffer Sizes
// ============================================================================

// Path and filename buffers
#define PATH_BUFFER_SIZE 1024
#define FILENAME_BUFFER_SIZE 256

// Configuration buffers
#define CONFIG_LINE_SIZE 512
#define CONFIG_PATH_SIZE 512

// Content buffers
#define CONTENT_BUFFER_SIZE 4096
#define SMALL_BUFFER_SIZE 256

// UI text buffers
#define TOOLTIP_BUFFER_SIZE 512
#define TITLE_BUFFER_SIZE 256
#define FONT_STRING_SIZE 256

// JSON parsing
#define JSON_PATTERN_SIZE 256

// Network buffers
#define URL_BUFFER_SIZE 2048
#define USER_AGENT_STRING "wshowlyrics/0.1.0"

// Cryptographic buffers
#define MD5_DIGEST_STRING_LENGTH 33  // 32 hex chars + null terminator

// ============================================================================
// Color Utilities
// ============================================================================

// Extract RGBA components from 32-bit color (0xRRGGBBAA format)
#define COLOR_EXTRACT_R(c) (((c) >> 24) & 0xFF)
#define COLOR_EXTRACT_G(c) (((c) >> 16) & 0xFF)
#define COLOR_EXTRACT_B(c) (((c) >> 8) & 0xFF)
#define COLOR_EXTRACT_A(c) ((c) & 0xFF)

// Create 32-bit color from RGBA components (0-255 range)
#define COLOR_MAKE_RGBA(r, g, b, a) \
    ((uint32_t)(r) << 24 | (uint32_t)(g) << 16 | (uint32_t)(b) << 8 | (uint32_t)(a))

// Convert color component to Cairo range (0.0 - 1.0)
#define COLOR_TO_CAIRO(component) ((component) / 255.0)

// Extract RGBA components as Cairo doubles
#define COLOR_CAIRO_R(c) (COLOR_TO_CAIRO(COLOR_EXTRACT_R(c)))
#define COLOR_CAIRO_G(c) (COLOR_TO_CAIRO(COLOR_EXTRACT_G(c)))
#define COLOR_CAIRO_B(c) (COLOR_TO_CAIRO(COLOR_EXTRACT_B(c)))
#define COLOR_CAIRO_A(c) (COLOR_TO_CAIRO(COLOR_EXTRACT_A(c)))

// Helper function to set Cairo source color from uint32_t (inline for performance)
#include <cairo.h>
static inline void cairo_set_source_u32(cairo_t *cairo, const uint32_t color) {
    cairo_set_source_rgba(cairo,
        COLOR_CAIRO_R(color),
        COLOR_CAIRO_G(color),
        COLOR_CAIRO_B(color),
        COLOR_CAIRO_A(color));
}

// ============================================================================
// Logging Macros
// ============================================================================

// ANSI color codes for terminal output
#define LOG_COLOR_RED     "\033[1;31m"
#define LOG_COLOR_YELLOW  "\033[1;33m"
#define LOG_COLOR_GREEN   "\033[1;32m"
#define LOG_COLOR_BLUE    "\033[1;34m"
#define LOG_COLOR_RESET   "\033[0m"

// Log level tags with colors (3-character format)
#define LOG_ERROR   LOG_COLOR_RED "ERR:" LOG_COLOR_RESET
#define LOG_WARN    LOG_COLOR_YELLOW "WRN:" LOG_COLOR_RESET
#define LOG_SUCCESS LOG_COLOR_GREEN "SUC:" LOG_COLOR_RESET
#define LOG_INFO    LOG_COLOR_BLUE "INF:" LOG_COLOR_RESET

// Logging functions - consistent interface
#define log_error(fmt, ...)   fprintf(stderr, LOG_ERROR " " fmt "\n", ##__VA_ARGS__)
#define log_warn(fmt, ...)    fprintf(stderr, LOG_WARN " " fmt "\n", ##__VA_ARGS__)
#define log_success(fmt, ...) fprintf(stdout, LOG_SUCCESS " " fmt "\n", ##__VA_ARGS__)
#define log_info(fmt, ...)    fprintf(stdout, LOG_INFO " " fmt "\n", ##__VA_ARGS__)

// ============================================================================
// HTTP Status Codes
// ============================================================================

#define HTTP_OK 200

// ============================================================================
// Event Loop and Timing
// ============================================================================

// Poll timeout for main event loop (milliseconds)
#define POLL_TIMEOUT_MS 100

// Maximum retry attempts for Wayland surface configuration
#define WAYLAND_CONFIGURE_RETRY_LIMIT 10

// How often to check for track updates (in poll intervals)
// 20 polls × 100ms = 2 seconds
#define TRACK_UPDATE_CHECK_INTERVAL 20

// Maximum retry attempts for operations
#define RETRY_MAX_COUNT 10

// ============================================================================
// Memory Allocation
// ============================================================================

// Default allocation size for single structure
#define ALLOC_SINGLE 1

#endif // CONSTANTS_H
