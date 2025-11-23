#include "pango_utils.h"
#include <stdarg.h>
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
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER); // Center align text
	pango_layout_set_attributes(layout, attrs);
	pango_attr_list_unref(attrs);
	pango_font_description_free(desc);
	return layout;
}

void get_text_size(cairo_t *cairo, const char *font, int *width, int *height,
		int *baseline, double scale, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	// Add one since vsnprintf excludes null terminator.
	const int length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *buf = malloc(length);
	if (buf == NULL) {
		return;
	}
	va_start(args, fmt);
	vsnprintf(buf, length, fmt, args);
	va_end(args);

	PangoLayout *layout = get_pango_layout(cairo, font, buf, scale);
	pango_cairo_update_layout(cairo, layout);
	pango_layout_get_pixel_size(layout, width, height);
	if (baseline) {
		*baseline = pango_layout_get_baseline(layout) / PANGO_SCALE;
	}
	g_object_unref(layout);
	free(buf);
}

void pango_printf(cairo_t *cairo, const char *font, double scale,
		const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	// Add one since vsnprintf excludes null terminator.
	const int length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *buf = malloc(length);
	if (buf == NULL) {
		return;
	}
	va_start(args, fmt);
	vsnprintf(buf, length, fmt, args);
	va_end(args);

	PangoLayout *layout = get_pango_layout(cairo, font, buf, scale);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_get_font_options(cairo, fo);
	pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
	cairo_font_options_destroy(fo);
	pango_cairo_update_layout(cairo, layout);
	pango_cairo_show_layout(cairo, layout);
	g_object_unref(layout);
	free(buf);
}

void get_ruby_text_size(cairo_t *cairo, const char *font, int *width, int *height,
		double scale, const char *base_text, const char *ruby_text) {
	if (!base_text) {
		*width = 0;
		*height = 0;
		return;
	}

	// Get base text size
	int base_w, base_h;
	get_text_size(cairo, font, &base_w, &base_h, NULL, scale, "%s", base_text);

	// Get ruby text size (smaller font)
	int ruby_w = 0, ruby_h = 0;
	if (ruby_text && ruby_text[0] != '\0') {
		get_text_size(cairo, font, &ruby_w, &ruby_h, NULL, scale * 0.5, "%s", ruby_text);
	}

	// Width is maximum of base and ruby
	*width = base_w > ruby_w ? base_w : ruby_w;

	// Height is base + ruby (if present)
	*height = base_h + ruby_h;
}

int pango_printf_ruby(cairo_t *cairo, const char *font, double scale,
		const char *base_text, const char *ruby_text) {
	if (!base_text) {
		return 0;
	}

	// Calculate sizes
	int base_w, base_h, ruby_w = 0, ruby_h = 0;
	get_text_size(cairo, font, &base_w, &base_h, NULL, scale, "%s", base_text);

	if (ruby_text && ruby_text[0] != '\0') {
		get_text_size(cairo, font, &ruby_w, &ruby_h, NULL, scale * 0.5, "%s", ruby_text);
	}

	// Calculate total width (max of base and ruby)
	int total_w = base_w > ruby_w ? base_w : ruby_w;

	// Save current position
	double x, y;
	cairo_get_current_point(cairo, &x, &y);

	// Draw ruby text above (centered over base text)
	if (ruby_text && ruby_text[0] != '\0') {
		cairo_save(cairo);
		double ruby_offset_x = (total_w - ruby_w) / 2.0;
		cairo_move_to(cairo, x + ruby_offset_x, y);
		pango_printf(cairo, font, scale * 0.5, "%s", ruby_text);
		cairo_restore(cairo);
	}

	// Draw base text below (centered if narrower than ruby)
	double base_offset_x = (total_w - base_w) / 2.0;
	cairo_move_to(cairo, x + base_offset_x, y + ruby_h);
	pango_printf(cairo, font, scale, "%s", base_text);

	// Move to end of this ruby text segment (x + total_w, original y)
	// This ensures next segment starts at correct position
	cairo_move_to(cairo, x + total_w, y);

	return total_w;
}
