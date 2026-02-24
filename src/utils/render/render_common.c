#include "render_common.h"
#include "render_params.h"
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

int render_segment_with_ruby(const struct segment_params *params,
                             const char *text, const char *ruby) {
    if (!params) {
        return 0;
    }

    int seg_w;
    int seg_h;
    get_ruby_text_size(params->cairo, params->font, &seg_w, &seg_h,
                      params->scale, text, ruby);

    // Bottom-align: position at y_offset so base text aligns with plain text
    cairo_move_to(params->cairo, params->x_offset, params->y_offset);
    pango_printf_ruby(params->cairo, params->font, params->scale, text, ruby);
    return seg_w;
}

int render_segment_plain(const struct segment_params *params, const char *text) {
    if (!params) {
        return 0;
    }

    int seg_w;
    int seg_h;
    get_text_size(params->cairo, params->font, &seg_w, &seg_h, NULL,
                 params->scale, text);
    // Bottom-align: position at same y as ruby text's base text would be
    cairo_move_to(params->cairo, params->x_offset, params->y_offset);
    pango_printf(params->cairo, params->font, params->scale, text);
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

void render_plain_text(const struct render_base_params *params, const char *text) {
    if (!params || !text) {
        if (params && params->width && params->height) {
            *params->width = 0;
            *params->height = 0;
        }
        return;
    }

    cairo_set_source_u32(params->cairo, params->foreground);

    int w;
    int h;
    get_text_size(params->cairo, params->font, &w, &h, NULL, params->scale, text);

    cairo_move_to(params->cairo, 0, 0);
    pango_printf(params->cairo, params->font, params->scale, text);

    *params->width = w;
    *params->height = h;
}
