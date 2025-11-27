#include "render_helpers.h"
#include "../mpris/mpris.h"
#include "../../constants.h"
#include <string.h>
#include <ctype.h>

// Helper to set cairo source color from uint32_t
static void cairo_set_source_u32(cairo_t *cairo, const uint32_t color) {
    cairo_set_source_rgba(cairo,
        (double)((color >> 24) & 0xFF) / 255.0,
        (double)((color >> 16) & 0xFF) / 255.0,
        (double)((color >> 8) & 0xFF) / 255.0,
        (double)(color & 0xFF) / 255.0);
}

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

void render_karaoke_segments(cairo_t *cairo, const char *font, int scale,
                             struct word_segment *segments, uint32_t foreground,
                             int64_t position_us, int *width, int *height) {
    if (!segments) {
        *width = 0;
        *height = 0;
        return;
    }

    // Calculate maximum ruby height
    int max_ruby_height = calculate_max_ruby_height_word(cairo, font, segments, scale);

    // First pass: draw all text in dimmed color
    uint32_t dimmed = create_dimmed_color(foreground);
    cairo_set_source_u32(cairo, dimmed);

    struct word_segment *seg_iter = segments;
    int x_iter = 0;
    while (seg_iter) {
        bool is_empty_seg = (!seg_iter->text || seg_iter->text[0] == '\0');

        if (!is_empty_seg) {
            int seg_w, seg_h;
            if (seg_iter->ruby) {
                get_ruby_text_size(cairo, font, &seg_w, &seg_h, scale, seg_iter->text, seg_iter->ruby);
            } else {
                get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, "%s", seg_iter->text);
            }
            // All text at same baseline (max_ruby_height gives space for ruby above)
            cairo_move_to(cairo, x_iter, max_ruby_height);
            pango_printf_ruby(cairo, font, scale, seg_iter->text, seg_iter->ruby);

            x_iter += seg_w;
            if (seg_iter->next) {
                int space_w, space_h;
                get_text_size(cairo, font, &space_w, &space_h, NULL, scale, " ");
                x_iter += space_w;
            }
        }
        seg_iter = seg_iter->next;
    }

    // Second pass: draw filled portions with clipping
    int x_offset = 0;
    struct word_segment *segment = segments;

    while (segment) {
        // Check for active unfill
        bool has_active_unfill = false;
        double unfill_override_ratio = 0.0;

        if (segment->text && segment->text[0] != '\0') {
            struct word_segment *look = segment->next;
            while (look) {
                bool is_unfill = look->is_unfill && (!look->text || look->text[0] == '\0');
                if (is_unfill && position_us >= look->timestamp_us &&
                    (look->end_timestamp_us == 0 || position_us < look->end_timestamp_us)) {
                    has_active_unfill = true;
                    int64_t unfill_end = look->end_timestamp_us ? look->end_timestamp_us :
                        (look->next ? look->next->timestamp_us : 0);
                    if (unfill_end > 0) {
                        int64_t duration = unfill_end - look->timestamp_us;
                        if (duration > 0) {
                            int64_t elapsed = position_us - look->timestamp_us;
                            double ratio = (double)elapsed / (double)duration;
                            unfill_override_ratio = (1.0 - ratio) * 0.5;
                        }
                    }
                    break;
                }
                if (look->text && look->text[0] != '\0') break;
                look = look->next;
            }
        }

        const char *display_text = segment->text;
        const char *display_ruby = segment->ruby;
        bool is_empty_unfill = segment->is_unfill && (!segment->text || segment->text[0] == '\0');

        if (is_empty_unfill) {
            segment = segment->next;
            continue;
        }

        int seg_w, seg_h;
        if (display_ruby) {
            get_ruby_text_size(cairo, font, &seg_w, &seg_h, scale, display_text, display_ruby);
        } else {
            get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, "%s", display_text ? display_text : "");
        }

        // Calculate fill ratio
        double fill_ratio = 0.0;

        if (has_active_unfill) {
            fill_ratio = unfill_override_ratio;
        } else if (position_us >= segment->timestamp_us) {
            int64_t segment_end_us = segment->end_timestamp_us;
            if (segment_end_us == 0 && segment->next) {
                segment_end_us = segment->next->timestamp_us;
            }

            if (segment_end_us > 0 && position_us < segment_end_us) {
                int64_t segment_duration = segment_end_us - segment->timestamp_us;
                if (segment_duration > 0) {
                    int64_t elapsed = position_us - segment->timestamp_us;
                    fill_ratio = (double)elapsed / (double)segment_duration;
                    if (fill_ratio > 1.0) fill_ratio = 1.0;
                    if (fill_ratio < 0.0) fill_ratio = 0.0;

                    if (segment->is_unfill) {
                        fill_ratio = (1.0 - fill_ratio) * 0.5;
                    }
                } else {
                    fill_ratio = segment->is_unfill ? 0.0 : 1.0;
                }
            } else if (segment_end_us == 0 || position_us >= segment_end_us) {
                fill_ratio = segment->is_unfill ? 0.0 : 1.0;
            }
        }

        if (fill_ratio > 0.0) {
            cairo_save(cairo);

            double fill_width = seg_w * fill_ratio;
            int clip_height = seg_h;
            if (!display_ruby) {
                clip_height += max_ruby_height;
            }
            cairo_rectangle(cairo, x_offset, 0, fill_width, clip_height);
            cairo_clip(cairo);

            cairo_set_source_u32(cairo, foreground);
            // Use pango_printf_ruby for both - ensures consistent baseline
            cairo_move_to(cairo, x_offset, max_ruby_height);
            pango_printf_ruby(cairo, font, scale, display_text, display_ruby);

            cairo_restore(cairo);
        }

        if (display_text && display_text[0] != '\0') {
            x_offset += seg_w;
            if (segment->next) {
                int space_w, space_h;
                get_text_size(cairo, font, &space_w, &space_h, NULL, scale, " ");
                x_offset += space_w;
            }
        }

        segment = segment->next;
    }

    // Calculate total width and height
    int total_w = 0;
    int total_h = 0;

    struct word_segment *size_iter = segments;
    while (size_iter) {
        int seg_w, seg_h;
        if (size_iter->ruby) {
            get_ruby_text_size(cairo, font, &seg_w, &seg_h, scale, size_iter->text, size_iter->ruby);
        } else {
            get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, "%s", size_iter->text);
            seg_h += max_ruby_height;
        }

        total_w += seg_w;
        if (seg_h > total_h) {
            total_h = seg_h;
        }

        if (size_iter->next) {
            int space_w, space_h;
            get_text_size(cairo, font, &space_w, &space_h, NULL, scale, " ");
            total_w += space_w;
        }

        size_iter = size_iter->next;
    }

    *width = total_w;
    *height = total_h;
}

// Generic segment rendering macro to avoid code duplication
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
                get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, "%s", seg->text); \
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

void render_ruby_segments(cairo_t *cairo, const char *font, int scale,
                         struct ruby_segment *segments, uint32_t foreground,
                         int *width, int *height) {
    RENDER_SEGMENTS_IMPL(struct ruby_segment, calculate_max_ruby_height_ruby);
}

void render_word_segments_static(cairo_t *cairo, const char *font, int scale,
                                 struct word_segment *segments, uint32_t foreground,
                                 int *width, int *height) {
    RENDER_SEGMENTS_IMPL(struct word_segment, calculate_max_ruby_height_word);
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
