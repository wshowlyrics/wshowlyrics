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

// ============================================================================
// HTTP Status Codes
// ============================================================================

#define HTTP_OK 200

#endif // CONSTANTS_H
