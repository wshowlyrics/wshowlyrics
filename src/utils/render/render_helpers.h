#ifndef RENDER_HELPERS_H
#define RENDER_HELPERS_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../lyrics_types.h"
#include "../pango/pango_utils.h"

// Calculate maximum ruby text height from segments
// Returns the maximum height needed to display ruby text
int calculate_max_ruby_height_word(cairo_t *cairo, const char *font,
                                    struct word_segment *segments, int scale);

int calculate_max_ruby_height_ruby(cairo_t *cairo, const char *font,
                                    struct ruby_segment *segments, int scale);

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

// Calculate karaoke fill progress for a segment
// Returns value between 0.0 (empty) and 1.0 (full)
double calculate_fill_progress(int64_t current_time, int64_t start_time,
                               int64_t end_time, bool is_unfill);

// Create dimmed version of a color (50% alpha)
uint32_t create_dimmed_color(uint32_t color);

// Render karaoke-style lyrics with word segments
void render_karaoke_segments(cairo_t *cairo, const char *font, int scale,
                             struct word_segment *segments, uint32_t foreground,
                             int64_t position_us, int *width, int *height);

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

// Render LRCX word segments without karaoke effect (static display)
void render_word_segments_static(cairo_t *cairo, const char *font, int scale,
                                 struct word_segment *segments, uint32_t foreground,
                                 int *width, int *height);

// Render plain text without segments
void render_plain_text(cairo_t *cairo, const char *font, int scale,
                      const char *text, uint32_t foreground,
                      int *width, int *height);

#endif // RENDER_HELPERS_H
