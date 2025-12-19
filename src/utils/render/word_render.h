#ifndef WORD_RENDER_H
#define WORD_RENDER_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../lyrics_types.h"
#include "render_params.h"

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
// Reduced from 8 parameters to 1 struct pointer
void render_karaoke_segments(const struct karaoke_params *params);

// Render LRCX word segments without karaoke effect (static display)
// Reduced from 7 parameters to 1 struct pointer
void render_word_segments_static(const struct word_static_params *params);

// Render multi-line LRCX display (prev, current, next)
// Reduced from 10 parameters to 1 struct pointer
void render_karaoke_multiline(const struct multiline_params *params);

#endif // WORD_RENDER_H
