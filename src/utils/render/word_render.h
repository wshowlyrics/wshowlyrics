#ifndef WORD_RENDER_H
#define WORD_RENDER_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../lyrics_types.h"

// Calculate maximum ruby text height from word segments
// Returns the maximum height needed to display ruby text
int calculate_max_ruby_height_word(cairo_t *cairo, const char *font,
                                    struct word_segment *segments, int scale);

// Calculate karaoke fill progress for a segment
// Returns value between 0.0 (empty) and 1.0 (full)
double calculate_fill_progress(int64_t current_time, int64_t start_time,
                               int64_t end_time, bool is_unfill);

// Render karaoke-style lyrics with word segments (single line with timing)
// NOTE: For LRCX format, prefer render_karaoke_multiline() for better UX
void render_karaoke_segments(cairo_t *cairo, const char *font, int scale,
                             struct word_segment *segments, uint32_t foreground,
                             int64_t position_us, int *width, int *height);

// Render LRCX word segments without karaoke effect (static display)
void render_word_segments_static(cairo_t *cairo, const char *font, int scale,
                                 struct word_segment *segments, uint32_t foreground,
                                 int *width, int *height);

// Render multi-line LRCX display (prev, current, next)
void render_karaoke_multiline(cairo_t *cairo, const char *font, int scale,
                              struct lyrics_line *prev_line,
                              struct lyrics_line *current_line,
                              struct lyrics_line *next_line,
                              uint32_t foreground, int64_t position_us,
                              int *width, int *height);

#endif // WORD_RENDER_H
