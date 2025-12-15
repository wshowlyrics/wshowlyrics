#include "render_common.h"
#include "../pango/pango_utils.h"
#include "../../constants.h"
#include <string.h>
#include <stdlib.h>

char* strip_ruby_notation(const char *text) {
    if (!text) {
        return NULL;
    }

    size_t len = strlen(text);
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }

    const char *src = text;
    char *dst = result;
    int brace_depth = 0;

    while (*src) {
        if (*src == '{') {
            brace_depth++;
            src++;
        } else if (*src == '}') {
            if (brace_depth > 0) {
                brace_depth--;
            }
            src++;
        } else if (brace_depth == 0) {
            *dst++ = *src++;
        } else {
            src++;
        }
    }

    *dst = '\0';
    return result;
}

int render_segment_with_ruby(cairo_t *cairo, const char *font, int scale,
                             int x_offset, int y_offset, int max_ruby_height,
                             const char *text, const char *ruby) {
    int seg_w, seg_h;
    get_ruby_text_size(cairo, font, &seg_w, &seg_h, scale, text, ruby);

    // Bottom-align: position at y_offset so base text aligns with plain text
    cairo_move_to(cairo, x_offset, y_offset);
    pango_printf_ruby(cairo, font, scale, text, ruby);
    return seg_w;
}

int render_segment_plain(cairo_t *cairo, const char *font, int scale,
                        int x_offset, int y_offset, int max_ruby_height,
                        const char *text) {
    int seg_w, seg_h;
    get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, "%s", text);
    // Bottom-align: position at same y as ruby text's base text would be
    cairo_move_to(cairo, x_offset, y_offset);
    pango_printf(cairo, font, scale, "%s", text);
    return seg_w;
}

uint32_t create_dimmed_color(uint32_t color) {
    uint8_t alpha = COLOR_EXTRACT_A(color);
    return (color & 0xFFFFFF00) | (alpha / 2);
}

uint32_t create_color_with_opacity(uint32_t color, double opacity) {
    uint8_t alpha = COLOR_EXTRACT_A(color);
    uint8_t new_alpha = (uint8_t)(alpha * opacity);
    return (color & 0xFFFFFF00) | new_alpha;
}

void render_plain_text(cairo_t *cairo, const char *font, int scale,
                      const char *text, uint32_t foreground,
                      int *width, int *height) {
    cairo_set_source_u32(cairo, foreground);

    int w, h;
    get_text_size(cairo, font, &w, &h, NULL, scale, "%s", text);

    cairo_move_to(cairo, 0, 0);
    pango_printf(cairo, font, scale, "%s", text);

    *width = w;
    *height = h;
}
