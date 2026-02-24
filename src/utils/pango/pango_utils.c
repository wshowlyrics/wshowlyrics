#include "pango_utils.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PangoLayout *get_pango_layout(cairo_t *cairo, const char *font,
        const char *text, double scale) {
    PangoLayout *layout = pango_cairo_create_layout(cairo);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_layout_set_text(layout, text, -1);
    pango_attr_list_insert(attrs, pango_attr_scale_new(scale));
    PangoFontDescription *desc = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_single_paragraph_mode(layout, 0); // Enable multi-line support
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT); // Left align for proper width calculation

    // Force baseline consistency for all text
    PangoContext *context = pango_layout_get_context(layout);
    pango_context_set_base_gravity(context, PANGO_GRAVITY_SOUTH);
    pango_context_set_gravity_hint(context, PANGO_GRAVITY_HINT_STRONG);

    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
    pango_font_description_free(desc);
    return layout;
}

void get_text_size(cairo_t *cairo, const char *font, int *width, int *height,
        int *baseline, double scale, const char *text) {
    if (!text) {
        if (width) *width = 0;
        if (height) *height = 0;
        if (baseline) *baseline = 0;
        return;
    }

    PangoLayout *layout = get_pango_layout(cairo, font, text, scale);
    pango_cairo_update_layout(cairo, layout);

    // Get both ink and logical extents
    PangoRectangle ink_rect;
    PangoRectangle logical_rect;
    pango_layout_get_pixel_extents(layout, &ink_rect, &logical_rect);

    // Use logical width
    if (width) {
        *width = logical_rect.width;
    }
    if (height) {
        // Use logical height with minimal bottom padding
        *height = logical_rect.height + 3;  // Add 3px bottom padding only
    }
    if (baseline) {
        *baseline = pango_layout_get_baseline(layout) / PANGO_SCALE;
    }
    g_object_unref(layout);
}

void pango_printf(cairo_t *cairo, const char *font, double scale,
        const char *text) {
    if (!text) {
        return;
    }

    PangoLayout *layout = get_pango_layout(cairo, font, text, scale);
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_get_font_options(cairo, fo);
    pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
    cairo_font_options_destroy(fo);
    pango_cairo_update_layout(cairo, layout);
    pango_cairo_show_layout(cairo, layout);
    g_object_unref(layout);
}

void get_ruby_text_size(cairo_t *cairo, const char *font, int *width, int *height,
        double scale, const char *base_text, const char *ruby_text) {
    if (!base_text) {
        *width = 0;
        *height = 0;
        return;
    }

    // Get base text size
    int base_w;
    int base_h;
    get_text_size(cairo, font, &base_w, &base_h, NULL, scale, base_text);

    // Get ruby text size (smaller font)
    int ruby_w = 0;
    int ruby_h = 0;
    if (ruby_text && ruby_text[0] != '\0') {
        get_text_size(cairo, font, &ruby_w, &ruby_h, NULL, scale * 0.5, ruby_text);
    }

    // Width is MAX(base_w, ruby_w) to prevent ruby text clipping
    // Example: 私{わたし} where ruby "わたし" is longer than base "私"
    *width = (ruby_w > base_w) ? ruby_w : base_w;

    // Height is base + ruby (if present), with tighter spacing
    int spacing = ruby_h > 0 ? -4 : 0;  // Reduce gap by 4px when ruby exists
    *height = base_h + ruby_h + spacing;
}

int pango_printf_ruby(cairo_t *cairo, const char *font, double scale,
        const char *base_text, const char *ruby_text) {
    if (!base_text) {
        return 0;
    }

    // Calculate sizes
    int base_w;
    int base_h;
    int ruby_w = 0;
    int ruby_h = 0;
    get_text_size(cairo, font, &base_w, &base_h, NULL, scale, base_text);

    if (ruby_text && ruby_text[0] != '\0') {
        get_text_size(cairo, font, &ruby_w, &ruby_h, NULL, scale * 0.5, ruby_text);
    }

    // Save current position
    double x;
    double y;
    cairo_get_current_point(cairo, &x, &y);

    // Calculate spacing
    int spacing = (ruby_text && ruby_text[0] != '\0') ? -4 : 0;

    // Calculate maximum width (to prevent clipping when ruby is longer)
    int max_w = (ruby_w > base_w) ? ruby_w : base_w;

    // Draw ruby text above, centered over available width
    // Position ruby so that base text will be at the SAME y position regardless of ruby
    if (ruby_text && ruby_text[0] != '\0') {
        cairo_save(cairo);
        // Center ruby over available width
        double ruby_offset_x = (max_w - ruby_w) / 2.0;
        // Move ruby UP so base text stays at y
        cairo_move_to(cairo, x + ruby_offset_x, y - ruby_h - spacing);
        pango_printf(cairo, font, scale * 0.5, ruby_text);
        cairo_restore(cairo);
    }

    // Draw base text at the original y position, centered over available width
    double base_offset_x = (max_w - base_w) / 2.0;
    cairo_move_to(cairo, x + base_offset_x, y);
    pango_printf(cairo, font, scale, base_text);

    // Return maximum width so next segment doesn't overlap
    return max_w;
}
