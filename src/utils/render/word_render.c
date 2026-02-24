#include "word_render.h"
#include "render_common.h"
#include "render_params.h"
#include "../pango/pango_utils.h"
#include "../../constants.h"
#include <string.h>

// Shared render context for word segment rendering functions
struct word_render_context {
    cairo_t *cairo;
    const char *font;
    int scale;
    uint32_t foreground;
};

// Find active unfill segment at current playback position
// Returns the unfill segment if active, NULL otherwise
static struct word_segment* find_active_unfill_segment(struct word_segment *start, int64_t position_us) {
    struct word_segment *look = start;
    while (look) {
        // Check if this is an empty unfill segment
        bool is_unfill = look->is_unfill && (!look->text || look->text[0] == '\0');
        if (is_unfill && position_us >= look->timestamp_us &&
            (look->end_timestamp_us == 0 || position_us < look->end_timestamp_us)) {
            return look;
        }
        // Stop at first non-empty segment
        if (look->text && look->text[0] != '\0') {
            break;
        }
        look = look->next;
    }
    return NULL;
}

// Calculate unfill ratio (opacity reduction) based on elapsed time
// Returns value between 0.0 and 0.5 for oscillating effect
static double calculate_unfill_ratio(const struct word_segment *unfill_seg, int64_t position_us) {
    int64_t unfill_end;
    if (unfill_seg->end_timestamp_us) {
        unfill_end = unfill_seg->end_timestamp_us;
    } else if (unfill_seg->next) {
        unfill_end = unfill_seg->next->timestamp_us;
    } else {
        unfill_end = 0;
    }

    if (unfill_end > 0) {
        int64_t duration = unfill_end - unfill_seg->timestamp_us;
        if (duration > 0) {
            int64_t elapsed = position_us - unfill_seg->timestamp_us;
            double ratio = (double)elapsed / (double)duration;
            return (1.0 - ratio) * 0.5;
        }
    }
    return 0.0;
}

int calculate_max_ruby_height_word(cairo_t *cairo, const char *font,
                                    struct word_segment *segments, int scale) {
    int max_ruby_height = 0;
    struct word_segment *seg = segments;

    while (seg) {
        if (seg->ruby) {
            int ruby_w;
            int ruby_h;
            get_text_size(cairo, font, &ruby_w, &ruby_h, NULL, scale * 0.5, seg->ruby);
            if (ruby_h > max_ruby_height) {
                max_ruby_height = ruby_h;
            }
        }
        seg = seg->next;
    }

    return max_ruby_height;
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

// Calculate segment dimensions (width and height)
static void calculate_segment_size(cairo_t *cairo, const char *font, int scale,
                                   const struct word_segment *segment,
                                   int *width, int *height) {
    if (segment->ruby) {
        get_ruby_text_size(cairo, font, width, height, scale, segment->text, segment->ruby);
    } else {
        get_text_size(cairo, font, width, height, NULL, scale, segment->text);
    }
}

// Render all segments in dimmed color (first pass)
static int render_dimmed_pass(cairo_t *cairo, const char *font, int scale,
                             struct word_segment *segments, uint32_t dimmed_color,
                             int max_ruby_height) {
    cairo_set_source_u32(cairo, dimmed_color);

    struct word_segment *seg_iter = segments;
    int x_iter = 0;

    while (seg_iter) {
        bool is_empty_seg = (!seg_iter->text || seg_iter->text[0] == '\0');

        if (!is_empty_seg) {
            int seg_w;
            int seg_h;
            calculate_segment_size(cairo, font, scale, seg_iter, &seg_w, &seg_h);

            cairo_move_to(cairo, x_iter, max_ruby_height);
            pango_printf_ruby(cairo, font, scale, seg_iter->text, seg_iter->ruby);

            x_iter += seg_w;
            if (seg_iter->next) {
                int space_w;
                int space_h;
                get_text_size(cairo, font, &space_w, &space_h, NULL, scale, " ");
                x_iter += space_w;
            }
        }
        seg_iter = seg_iter->next;
    }

    return x_iter;
}

// Calculate fill ratio for a segment
static double calculate_segment_fill_ratio(const struct word_segment *segment,
                                           int64_t position_us,
                                           bool has_active_unfill,
                                           double unfill_override_ratio) {
    if (has_active_unfill) {
        return unfill_override_ratio;
    }

    if (position_us < segment->timestamp_us) {
        return 0.0;
    }

    int64_t segment_end_us = segment->end_timestamp_us;
    if (segment_end_us == 0 && segment->next) {
        segment_end_us = segment->next->timestamp_us;
    }

    if (segment_end_us > 0 && position_us < segment_end_us) {
        int64_t segment_duration = segment_end_us - segment->timestamp_us;
        if (segment_duration > 0) {
            int64_t elapsed = position_us - segment->timestamp_us;
            double fill_ratio = (double)elapsed / (double)segment_duration;
            if (fill_ratio > 1.0) fill_ratio = 1.0;
            if (fill_ratio < 0.0) fill_ratio = 0.0;

            if (segment->is_unfill) {
                fill_ratio = (1.0 - fill_ratio) * 0.5;
            }
            return fill_ratio;
        } else {
            return segment->is_unfill ? 0.0 : 1.0;
        }
    }

    return segment->is_unfill ? 0.0 : 1.0;
}

// Render single segment with fill effect
static void render_segment_with_fill(const struct word_render_context *ctx,
                                    struct word_segment *segment,
                                    int x_offset, int max_ruby_height,
                                    double fill_ratio) {
    if (fill_ratio <= 0.0) {
        return;
    }

    int seg_w;
    int seg_h;
    calculate_segment_size(ctx->cairo, ctx->font, ctx->scale, segment, &seg_w, &seg_h);

    cairo_save(ctx->cairo);

    double fill_width = seg_w * fill_ratio;
    int clip_height = segment->ruby ? seg_h : (seg_h + max_ruby_height);

    cairo_rectangle(ctx->cairo, x_offset, 0, fill_width, clip_height);
    cairo_clip(ctx->cairo);

    cairo_set_source_u32(ctx->cairo, ctx->foreground);
    cairo_move_to(ctx->cairo, x_offset, max_ruby_height);
    pango_printf_ruby(ctx->cairo, ctx->font, ctx->scale, segment->text, segment->ruby);

    cairo_restore(ctx->cairo);
}

// Render filled portions with clipping (second pass)
static void render_filled_pass(const struct word_render_context *ctx,
                              struct word_segment *segments,
                              int64_t position_us, int max_ruby_height) {
    int x_offset = 0;
    struct word_segment *segment = segments;

    while (segment) {
        // Check for active unfill effect
        bool has_active_unfill = false;
        double unfill_override_ratio = 0.0;

        if (segment->text && segment->text[0] != '\0') {
            struct word_segment *unfill_seg = find_active_unfill_segment(segment->next, position_us);
            if (unfill_seg) {
                has_active_unfill = true;
                unfill_override_ratio = calculate_unfill_ratio(unfill_seg, position_us);
            }
        }

        bool is_empty_unfill = segment->is_unfill && (!segment->text || segment->text[0] == '\0');
        if (is_empty_unfill) {
            segment = segment->next;
            continue;
        }

        // Calculate fill ratio
        double fill_ratio = calculate_segment_fill_ratio(segment, position_us,
                                                         has_active_unfill, unfill_override_ratio);

        // Render segment with fill
        render_segment_with_fill(ctx, segment, x_offset, max_ruby_height, fill_ratio);

        // Update x_offset
        if (segment->text && segment->text[0] != '\0') {
            int seg_w;
            int seg_h;
            calculate_segment_size(ctx->cairo, ctx->font, ctx->scale, segment, &seg_w, &seg_h);
            x_offset += seg_w;

            if (segment->next) {
                int space_w;
                int space_h;
                get_text_size(ctx->cairo, ctx->font, &space_w, &space_h, NULL, ctx->scale, " ");
                x_offset += space_w;
            }
        }

        segment = segment->next;
    }
}

// Calculate total width and height of all segments
static void calculate_total_dimensions(cairo_t *cairo, const char *font, int scale,
                                      struct word_segment *segments, int max_ruby_height,
                                      int *total_width, int *total_height) {
    *total_width = 0;
    *total_height = 0;

    struct word_segment *size_iter = segments;
    while (size_iter) {
        int seg_w;
        int seg_h;
        calculate_segment_size(cairo, font, scale, size_iter, &seg_w, &seg_h);

        if (!size_iter->ruby) {
            seg_h += max_ruby_height;
        }

        *total_width += seg_w;
        if (seg_h > *total_height) {
            *total_height = seg_h;
        }

        if (size_iter->next) {
            int space_w;
            int space_h;
            get_text_size(cairo, font, &space_w, &space_h, NULL, scale, " ");
            *total_width += space_w;
        }

        size_iter = size_iter->next;
    }
}

void render_karaoke_segments(const struct karaoke_params *params) {
    if (!params || !params->segments) {
        if (params && params->base.width && params->base.height) {
            *params->base.width = 0;
            *params->base.height = 0;
        }
        return;
    }

    // Extract parameters for readability
    cairo_t *cairo = params->base.cairo;
    const char *font = params->base.font;
    int scale = params->base.scale;
    struct word_segment *segments = params->segments;
    uint32_t foreground = params->base.foreground;
    int64_t position_us = params->position_us;

    // Build render context
    struct word_render_context ctx = {
        .cairo = cairo,
        .font = font,
        .scale = scale,
        .foreground = foreground,
    };

    // Calculate maximum ruby height
    int max_ruby_height = calculate_max_ruby_height_word(cairo, font, segments, scale);

    // First pass: draw all text in dimmed color
    uint32_t dimmed = create_dimmed_color(foreground);
    render_dimmed_pass(cairo, font, scale, segments, dimmed, max_ruby_height);

    // Second pass: draw filled portions with clipping
    render_filled_pass(&ctx, segments, position_us, max_ruby_height);

    // Calculate total width and height
    int total_w;
    int total_h;
    calculate_total_dimensions(cairo, font, scale, segments, max_ruby_height, &total_w, &total_h);

    *params->base.width = total_w;
    *params->base.height = total_h;
}

// RENDER_SEGMENTS_IMPL macro is now defined in render_common.h

void render_word_segments_static(const struct word_static_params *params) {
    if (!params) {
        return;
    }

    cairo_t *cairo = params->base.cairo;
    const char *font = params->base.font;
    int scale = params->base.scale;
    struct word_segment *segments = params->segments;
    uint32_t foreground = params->base.foreground;
    int *width = params->base.width;
    int *height = params->base.height;

    RENDER_SEGMENTS_IMPL(struct word_segment, calculate_max_ruby_height_word);
}

// Helper: Render a context line (prev or next) with dimmed color
// Returns: line height (0 if line not rendered)
static int render_context_line(const struct word_render_context *ctx,
                               double context_scale, double opacity,
                               const struct lyrics_line *line,
                               int y_offset, int *total_width) {
    if (!line || !line->text || line->text[0] == '\0') {
        return 0;
    }

    char *stripped = strip_ruby_notation(line->text);
    if (!stripped || stripped[0] == '\0') {
        free(stripped);
        return 0;
    }

    // Render with dimmed color
    uint32_t dimmed = create_color_with_opacity(ctx->foreground, opacity);
    cairo_set_source_u32(ctx->cairo, dimmed);

    int w;
    int h;
    get_text_size(ctx->cairo, ctx->font, &w, &h, NULL,
                 ctx->scale * context_scale, stripped);

    cairo_save(ctx->cairo);
    cairo_translate(ctx->cairo, 0, y_offset);
    cairo_move_to(ctx->cairo, 0, 0);
    pango_printf(ctx->cairo, ctx->font, ctx->scale * context_scale, stripped);
    cairo_restore(ctx->cairo);

    if (w > *total_width) *total_width = w;
    free(stripped);
    return h;
}

// Helper: Render main karaoke line
// Returns: line height (0 if line not rendered)
static int render_main_karaoke_line(const struct word_render_context *ctx,
                                   int64_t position_us, struct lyrics_line *line,
                                   int y_offset, int *total_width) {
    if (!line || !line->segments) {
        return 0;
    }

    int w;
    int h;
    cairo_save(ctx->cairo);
    cairo_translate(ctx->cairo, 0, y_offset);

    struct karaoke_params seg_params = {
        .base = {
            .cairo = ctx->cairo,
            .font = ctx->font,
            .scale = ctx->scale,
            .foreground = ctx->foreground,
            .width = &w,
            .height = &h
        },
        .segments = line->segments,
        .position_us = position_us
    };
    render_karaoke_segments(&seg_params);

    cairo_restore(ctx->cairo);

    if (w > *total_width) *total_width = w;
    return h;
}

void render_karaoke_multiline(const struct multiline_params *params) {
    // For LRCX, we render prev/next lines even during instrumental breaks (current_line = NULL)
    if (!params || (!params->prev_line && !params->current_line && !params->next_line)) {
        if (params && params->base.width && params->base.height) {
            *params->base.width = 0;
            *params->base.height = 0;
        }
        return;
    }

    // Build render context from params
    struct word_render_context ctx = {
        .cairo = params->base.cairo,
        .font = params->base.font,
        .scale = params->base.scale,
        .foreground = params->base.foreground,
    };

    // Extract parameters
    const double context_scale = 0.7;  // 70% for prev/next
    const int context_spacing = 2;     // Minimal spacing for context lines
    const int main_spacing = 4;        // Spacing before/after main line

    int total_width = 0;
    int total_height = 0;
    int y_offset = 0;

    // Render previous line (75% opacity)
    int prev_h = render_context_line(&ctx, context_scale, 0.75,
                                     params->prev_line, y_offset, &total_width);
    if (prev_h > 0) {
        total_height += prev_h;
        y_offset += prev_h + main_spacing;
    }

    // Render current line (full karaoke)
    int main_h = render_main_karaoke_line(&ctx, params->position_us,
                                         params->current_line,
                                         y_offset, &total_width);
    if (main_h > 0) {
        total_height += main_h;
        y_offset += main_h + context_spacing;
    }

    // Render next line (50% opacity)
    int next_h = render_context_line(&ctx, context_scale, 0.5,
                                     params->next_line, y_offset, &total_width);
    if (next_h > 0) {
        total_height += next_h;
    }

    *params->base.width = total_width;
    *params->base.height = total_height;
}
