#ifndef RUBY_RENDER_H
#define RUBY_RENDER_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../lyrics_types.h"

// Calculate maximum ruby text height from ruby segments
// Returns the maximum height needed to display ruby text
int calculate_max_ruby_height_ruby(cairo_t *cairo, const char *font,
                                    struct ruby_segment *segments, int scale);

// Render LRC/SRT-style lyrics with ruby segments
void render_ruby_segments(cairo_t *cairo, const char *font, int scale,
                         struct ruby_segment *segments, uint32_t foreground,
                         int *width, int *height);

// Render LRC-style lyrics with ruby segments and translation support
// translation_mode: "both", "translation_only", or any other value for original only
// translation: the translated line text (NULL if no translation available)
void render_ruby_segments_with_translation(cairo_t *cairo, const char *font, int scale,
                                          struct ruby_segment *segments, uint32_t foreground,
                                          const char *translation_mode, const char *translation,
                                          int *width, int *height);

// Check if any segment has translation (for SRT <sub> tags)
bool has_segment_translation(struct ruby_segment *segments);

#endif // RUBY_RENDER_H
