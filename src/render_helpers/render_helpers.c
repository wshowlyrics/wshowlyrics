#include "render_helpers.h"
#include <string.h>
#include <ctype.h>

#define COLOR_EXTRACT_A(c) ((c) & 0xFF)

int calculate_max_ruby_height_word(cairo_t *cairo, const char *font,
                                    struct word_segment *segments, int scale) {
    int max_ruby_height = 0;
    struct word_segment *seg = segments;

    while (seg) {
        if (seg->ruby) {
            int ruby_w, ruby_h;
            get_text_size(cairo, font, &ruby_w, &ruby_h, NULL, scale * 0.5, "%s", seg->ruby);
            if (ruby_h > max_ruby_height) {
                max_ruby_height = ruby_h;
            }
        }
        seg = seg->next;
    }

    return max_ruby_height;
}

int calculate_max_ruby_height_ruby(cairo_t *cairo, const char *font,
                                    struct ruby_segment *segments, int scale) {
    int max_ruby_height = 0;
    struct ruby_segment *seg = segments;

    while (seg) {
        if (seg->ruby) {
            int ruby_w, ruby_h;
            get_text_size(cairo, font, &ruby_w, &ruby_h, NULL, scale * 0.5, "%s", seg->ruby);
            if (ruby_h > max_ruby_height) {
                max_ruby_height = ruby_h;
            }
        }
        seg = seg->next;
    }

    return max_ruby_height;
}

int render_segment_with_ruby(cairo_t *cairo, const char *font, int scale,
                             int x_offset, int y_offset, int max_ruby_height,
                             const char *text, const char *ruby) {
    int seg_w, seg_h;
    get_ruby_text_size(cairo, font, &seg_w, &seg_h, scale, text, ruby);
    cairo_move_to(cairo, x_offset, y_offset);
    pango_printf_ruby(cairo, font, scale, text, ruby);
    return seg_w;
}

int render_segment_plain(cairo_t *cairo, const char *font, int scale,
                        int x_offset, int y_offset, int max_ruby_height,
                        const char *text) {
    int seg_w, seg_h;
    get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, "%s", text);
    cairo_move_to(cairo, x_offset, y_offset + max_ruby_height);
    pango_printf(cairo, font, scale, "%s", text);
    return seg_w;
}

double calculate_fill_progress(int64_t current_time, int64_t start_time,
                               int64_t end_time, bool is_unfill) {
    if (current_time < start_time) {
        return is_unfill ? 1.0 : 0.0;
    }
    if (end_time == 0 || current_time >= end_time) {
        return is_unfill ? 0.0 : 1.0;
    }

    double progress = (double)(current_time - start_time) / (double)(end_time - start_time);
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;

    return is_unfill ? (1.0 - progress) : progress;
}

uint32_t create_dimmed_color(uint32_t color) {
    uint8_t alpha = COLOR_EXTRACT_A(color);
    return (color & 0xFFFFFF00) | (alpha / 2);
}

// Placeholder implementations - these will be implemented based on main.c logic
void render_karaoke_segments(cairo_t *cairo, const char *font, int scale,
                             struct word_segment *segments, uint32_t foreground,
                             int64_t position_us, int *width, int *height) {
    // To be implemented
    *width = 100;
    *height = 30;
}

void render_ruby_segments(cairo_t *cairo, const char *font, int scale,
                         struct ruby_segment *segments, uint32_t foreground,
                         int *width, int *height) {
    // To be implemented
    *width = 100;
    *height = 30;
}

void render_plain_text(cairo_t *cairo, const char *font, int scale,
                      const char *text, uint32_t foreground,
                      int *width, int *height) {
    // To be implemented
    *width = 100;
    *height = 30;
}
