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

// Generic segment rendering macro to avoid code duplication between ruby_render and word_render
// Both ruby_segment and word_segment have identical structure for rendering purposes
#define RENDER_SEGMENTS_IMPL(segment_type, calc_ruby_height_func) \
    do { \
        if (!segments) { \
            *width = 0; \
            *height = 0; \
            return; \
        } \
        \
        cairo_set_source_u32(cairo, foreground); \
        \
        /* Calculate maximum ruby height */ \
        int max_ruby_height = calc_ruby_height_func(cairo, font, segments, scale); \
        \
        /* Get base text height */ \
        int base_text_h; \
        get_text_size(cairo, font, NULL, &base_text_h, NULL, scale, "A"); \
        \
        /* Render all segments */ \
        int x_offset = 0; \
        int y_offset = 0; \
        int total_width = 0; \
        int line_width = 0; \
        int line_count = 1; \
        \
        segment_type *seg = segments; \
        while (seg) { \
            if (seg->text && strchr(seg->text, '\n')) { \
                if (line_width > total_width) { \
                    total_width = line_width; \
                } \
                y_offset += base_text_h + max_ruby_height; \
                x_offset = 0; \
                line_width = 0; \
                line_count++; \
                seg = seg->next; \
                continue; \
            } \
            \
            int seg_w, seg_h; \
            if (seg->ruby) { \
                get_ruby_text_size(cairo, font, &seg_w, &seg_h, scale, seg->text, seg->ruby); \
            } else { \
                get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, seg->text); \
            } \
            /* Use pango_printf_ruby for all text - ensures consistent baseline */ \
            cairo_move_to(cairo, x_offset, y_offset + max_ruby_height); \
            pango_printf_ruby(cairo, font, scale, seg->text, seg->ruby); \
            \
            x_offset += seg_w; \
            line_width += seg_w; \
            seg = seg->next; \
        } \
        \
        if (line_width > total_width) { \
            total_width = line_width; \
        } \
        \
        int total_height = line_count * (base_text_h + max_ruby_height); \
        \
        *width = total_width; \
        *height = total_height; \
    } while (0)

#endif // RENDER_COMMON_H
