#ifndef RENDER_PARAMS_H
#define RENDER_PARAMS_H

#include <cairo.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../lyrics_types.h"

// Base rendering parameters shared across all render functions
// Reduces parameter count from 7+ to 1 struct pointer
struct render_base_params {
    // Core rendering context
    cairo_t *cairo;
    const char *font;
    int scale;
    uint32_t foreground;

    // Output dimensions (non-NULL)
    int *width;
    int *height;
};

// Parameters for karaoke segment rendering with timing
// Used by: render_karaoke_segments()
struct karaoke_params {
    struct render_base_params base;
    struct word_segment *segments;
    int64_t position_us;  // Current playback position
};

// Parameters for multi-line LRCX rendering (prev, current, next)
// Used by: render_karaoke_multiline()
struct multiline_params {
    struct render_base_params base;
    struct lyrics_line *prev_line;
    struct lyrics_line *current_line;
    struct lyrics_line *next_line;
    int64_t position_us;  // Current playback position
};

// Parameters for ruby segment rendering with translation
// Used by: render_ruby_segments_with_translation()
struct translation_params {
    struct render_base_params base;
    struct ruby_segment *segments;
    const char *translation_mode;  // "both", "translation_only", etc.
    const char *translation;       // Translated line text (or NULL)
};

// Parameters for segment-level rendering (ruby and plain)
// Used by: render_segment_with_ruby(), render_segment_plain()
struct segment_params {
    cairo_t *cairo;
    const char *font;
    int scale;
    int x_offset;
    int y_offset;
    int max_ruby_height;
};

// Parameters for static word segment rendering (no timing)
// Used by: render_word_segments_static()
struct word_static_params {
    struct render_base_params base;
    struct word_segment *segments;
};

// Parameters for ruby segment rendering (no translation)
// Used by: render_ruby_segments()
struct ruby_params {
    struct render_base_params base;
    struct ruby_segment *segments;
};

#endif // RENDER_PARAMS_H
