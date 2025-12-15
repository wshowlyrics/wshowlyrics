#ifndef RENDER_COMMON_H
#define RENDER_COMMON_H

#include <cairo.h>
#include <stdint.h>

// Strip ruby notation from text (removes {ruby} syntax)
// Returns newly allocated string, caller must free
// Example: "心{こころ}音{ね}" -> "心音"
char* strip_ruby_notation(const char *text);

// Render a segment with ruby text at the specified position
// Returns the width of the rendered segment
int render_segment_with_ruby(cairo_t *cairo, const char *font, int scale,
                             int x_offset, int y_offset, int max_ruby_height,
                             const char *text, const char *ruby);

// Render a plain segment without ruby text at the specified position
// Returns the width of the rendered segment
int render_segment_plain(cairo_t *cairo, const char *font, int scale,
                        int x_offset, int y_offset, int max_ruby_height,
                        const char *text);

// Create dimmed version of a color (50% alpha)
uint32_t create_dimmed_color(uint32_t color);

// Create color with custom opacity (0.0 - 1.0)
uint32_t create_color_with_opacity(uint32_t color, double opacity);

// Render plain text without segments
void render_plain_text(cairo_t *cairo, const char *font, int scale,
                      const char *text, uint32_t foreground,
                      int *width, int *height);

#endif // RENDER_COMMON_H
