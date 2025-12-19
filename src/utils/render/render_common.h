#ifndef RENDER_COMMON_H
#define RENDER_COMMON_H

#include <cairo.h>
#include <stdint.h>
#include "render_params.h"

// Strip ruby notation from text (removes {ruby} syntax)
// Returns newly allocated string, caller must free
// Example: "心{こころ}音{ね}" -> "心音"
char* strip_ruby_notation(const char *text);

// Render a segment with ruby text at the specified position
// Returns the width of the rendered segment
// Reduced from 8 parameters to 1 struct + 2 text parameters
int render_segment_with_ruby(const struct segment_params *params,
                             const char *text, const char *ruby);

// Render a plain segment without ruby text at the specified position
// Returns the width of the rendered segment
// Reduced from 7 parameters to 1 struct + 1 text parameter
int render_segment_plain(const struct segment_params *params, const char *text);

// Create dimmed version of a color (50% alpha)
uint32_t create_dimmed_color(uint32_t color);

// Create color with custom opacity (0.0 - 1.0)
uint32_t create_color_with_opacity(uint32_t color, double opacity);

// Render plain text without segments
// Reduced from 7 parameters to 1 struct + 1 text parameter
void render_plain_text(const struct render_base_params *params, const char *text);

#endif // RENDER_COMMON_H
