#ifndef RUBY_RENDER_H
#define RUBY_RENDER_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../lyrics_types.h"
#include "render_params.h"

// Calculate maximum ruby text height from ruby segments
// Returns the maximum height needed to display ruby text
int calculate_max_ruby_height_ruby(cairo_t *cairo, const char *font,
                                    struct ruby_segment *segments, int scale);

// Render LRC/SRT-style lyrics with ruby segments
// Reduced from 7 parameters to 1 struct pointer
void render_ruby_segments(const struct ruby_params *params);

// Static ruby segment rendering without translation logic
// Uses RENDER_SEGMENTS_IMPL macro for consistency with word_render.c
void render_ruby_segments_static(const struct ruby_static_params *params);

// Render LRC-style lyrics with ruby segments and translation support
// translation_mode: "both", "translation_only", or any other value for original only
// translation: the translated line text (NULL if no translation available)
// Reduced from 9 parameters to 1 struct pointer
void render_ruby_segments_with_translation(const struct translation_params *params);

// Check if any segment has translation (for SRT <sub> tags)
bool has_segment_translation(struct ruby_segment *segments);

#endif // RUBY_RENDER_H
