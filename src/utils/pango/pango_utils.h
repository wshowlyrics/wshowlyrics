#ifndef _LYRICS_PANGO_UTILS_H
#define _LYRICS_PANGO_UTILS_H
#include <stdbool.h>
#include <stdint.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

PangoLayout *get_pango_layout(cairo_t *cairo, const char *font,
        const char *text, double scale);
void get_text_size(cairo_t *cairo, const char *font, int *width, int *height,
        int *baseline, double scale, const char *text);
void pango_printf(cairo_t *cairo, const char *font,
        double scale, const char *text);

// Ruby text (furigana) rendering
// Renders text with ruby text above it
// Returns total width of rendered text
int pango_printf_ruby(cairo_t *cairo, const char *font, double scale,
        const char *base_text, const char *ruby_text);

// Get size of text with ruby text above it
void get_ruby_text_size(cairo_t *cairo, const char *font, int *width, int *height,
        double scale, const char *base_text, const char *ruby_text);

#endif
