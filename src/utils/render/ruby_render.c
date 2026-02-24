#include "ruby_render.h"
#include "render_common.h"
#include "render_params.h"
#include "../pango/pango_utils.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include <string.h>

// Context for ruby segment rendering with shared layout dimensions
struct ruby_render_context {
    cairo_t *cairo;
    const char *font;
    int scale;
    uint32_t foreground;
    int max_ruby_height;
    int base_text_h;
};

int calculate_max_ruby_height_ruby(cairo_t *cairo, const char *font,
                                    struct ruby_segment *segments, int scale) {
    int max_ruby_height = 0;
    struct ruby_segment *seg = segments;

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

void render_ruby_segments_static(const struct ruby_static_params *params) {
    if (!params) {
        return;
    }

    cairo_t *cairo = params->base.cairo;
    const char *font = params->base.font;
    int scale = params->base.scale;
    struct ruby_segment *segments = params->segments;
    uint32_t foreground = params->base.foreground;
    int *width = params->base.width;
    int *height = params->base.height;

    RENDER_SEGMENTS_IMPL(struct ruby_segment, calculate_max_ruby_height_ruby);
}

// Helper: Check if segment is translation-only
static bool is_translation_segment(const struct ruby_segment *seg) {
    return seg->text && seg->text[0] == '\0' &&
           seg->translation && seg->translation[0] != '\0';
}

// Helper: Update total width if current line is wider
static void update_total_width(int line_width, int *total_width) {
    if (line_width > *total_width) {
        *total_width = line_width;
    }
}

// Helper: Render translation segment
// Returns: segment width
static int render_translation_segment(cairo_t *cairo, const char *font,
                                      uint32_t foreground, double translation_scale,
                                      const char *translation, int x_offset, int y_offset) {
    // Use dimmed color with opacity
    const struct config *cfg = config_get();
    uint32_t dimmed = create_color_with_opacity(foreground, cfg->translation.translation_opacity);
    cairo_set_source_u32(cairo, dimmed);

    int trans_w;
    int trans_h;
    get_text_size(cairo, font, &trans_w, &trans_h, NULL, translation_scale, translation);

    // Place translation at same baseline level (text will naturally appear below due to font metrics)
    cairo_move_to(cairo, x_offset, y_offset);
    pango_printf(cairo, font, translation_scale, translation);

    cairo_set_source_u32(cairo, foreground);  // Restore original color
    return trans_w;
}

// Helper: Handle newline segment - updates offsets and dimensions
static void handle_newline_segment(int base_text_h, int max_ruby_height,
                                   int *line_width, int *line_count,
                                   int *x_offset, int *y_offset) {
    *y_offset += base_text_h + max_ruby_height;
    *x_offset = 0;
    *line_width = 0;
    (*line_count)++;
}

void render_ruby_segments(const struct ruby_params *params) {
    if (!params || !params->segments) {
        if (params && params->base.width && params->base.height) {
            *params->base.width = 0;
            *params->base.height = 0;
        }
        return;
    }

    // If no translation, use static implementation (eliminates duplication)
    if (!has_segment_translation(params->segments)) {
        cairo_t *cairo = params->base.cairo;
        const char *font = params->base.font;
        int scale = params->base.scale;
        struct ruby_segment *segments = params->segments;
        uint32_t foreground = params->base.foreground;
        int *width = params->base.width;
        int *height = params->base.height;

        RENDER_SEGMENTS_IMPL(struct ruby_segment, calculate_max_ruby_height_ruby);
        return;
    }

    // Extract parameters for translation rendering
    cairo_t *cairo = params->base.cairo;
    const char *font = params->base.font;
    int scale = params->base.scale;
    struct ruby_segment *segments = params->segments;
    uint32_t foreground = params->base.foreground;

    cairo_set_source_u32(cairo, foreground);

    // Calculate dimensions
    int max_ruby_height = calculate_max_ruby_height_ruby(cairo, font, segments, scale);
    int base_text_h;
    get_text_size(cairo, font, NULL, &base_text_h, NULL, scale, "A");
    double translation_scale = scale * 0.7;

    // Render all segments with translation logic
    int x_offset = 0;
    int y_offset = 0;
    int total_width = 0;
    int line_width = 0;
    int line_count = 1;

    struct ruby_segment *seg = segments;
    while (seg) {
        // Handle translation-only segment
        if (is_translation_segment(seg)) {
            update_total_width(line_width, &total_width);

            int trans_w = render_translation_segment(cairo, font, foreground,
                                                     translation_scale, seg->translation,
                                                     x_offset, y_offset);
            update_total_width(trans_w, &total_width);
            seg = seg->next;
            continue;
        }

        // Handle newline
        if (seg->text && strchr(seg->text, '\n')) {
            update_total_width(line_width, &total_width);
            handle_newline_segment(base_text_h, max_ruby_height,
                                  &line_width, &line_count, &x_offset, &y_offset);
            seg = seg->next;
            continue;
        }

        // Handle normal text segment
        int seg_w;
        int seg_h;
        if (seg->ruby) {
            get_ruby_text_size(cairo, font, &seg_w, &seg_h, scale, seg->text, seg->ruby);
        } else {
            get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, seg->text);
        }

        cairo_move_to(cairo, x_offset, y_offset + max_ruby_height);
        pango_printf_ruby(cairo, font, scale, seg->text, seg->ruby);

        x_offset += seg_w;
        line_width += seg_w;
        seg = seg->next;
    }

    update_total_width(line_width, &total_width);

    int total_height = line_count * (base_text_h + max_ruby_height);
    total_height -= max_ruby_height;  // Translation line doesn't need ruby space

    *params->base.width = total_width;
    *params->base.height = total_height;
}

bool has_segment_translation(struct ruby_segment *segments) {
    if (!segments) {
        return false;
    }

    struct ruby_segment *seg = segments;
    while (seg) {
        // Check if this is a translation-only segment (empty text and translation exists)
        if (seg->text[0] == '\0' && seg->translation && seg->translation[0] != '\0') {
            return true;
        }
        seg = seg->next;
    }

    return false;
}

// Helper: Render original text segments and return dimensions
// Returns: total_width, updates y_offset and line_count via pointers
static int render_original_text(const struct ruby_render_context *ctx,
                                struct ruby_segment *segments,
                                int *y_offset_out, int *line_count_out) {
    int x_offset = 0;
    int y_offset = *y_offset_out;
    int total_width = 0;
    int line_width = 0;
    int line_count = *line_count_out;

    struct ruby_segment *seg = segments;
    while (seg) {
        // Handle newline
        if (seg->text && strchr(seg->text, '\n')) {
            if (line_width > total_width) {
                total_width = line_width;
            }
            y_offset += ctx->base_text_h + ctx->max_ruby_height;
            x_offset = 0;
            line_width = 0;
            line_count++;
            seg = seg->next;
            continue;
        }

        // Get segment size
        int seg_w;
        int seg_h;
        if (seg->ruby) {
            get_ruby_text_size(ctx->cairo, ctx->font, &seg_w, &seg_h,
                              ctx->scale, seg->text, seg->ruby);
        } else {
            get_text_size(ctx->cairo, ctx->font, &seg_w, &seg_h,
                         NULL, ctx->scale, seg->text);
        }

        // Render segment
        cairo_move_to(ctx->cairo, x_offset, y_offset + ctx->max_ruby_height);
        pango_printf_ruby(ctx->cairo, ctx->font, ctx->scale, seg->text, seg->ruby);

        x_offset += seg_w;
        line_width += seg_w;
        seg = seg->next;
    }

    // Update total width with last line
    if (line_width > total_width) {
        total_width = line_width;
    }

    *y_offset_out = y_offset;
    *line_count_out = line_count;
    return total_width;
}

// Helper: Render translation text and return width
static int render_translation_text(const struct ruby_render_context *ctx,
                                   const char *translation, double translation_scale,
                                   int y_offset) {
    // Use custom opacity from config
    const struct config *cfg = config_get();
    uint32_t dimmed = create_color_with_opacity(ctx->foreground,
                                                cfg->translation.translation_opacity);
    cairo_set_source_u32(ctx->cairo, dimmed);

    int trans_w;
    int trans_h;
    get_text_size(ctx->cairo, ctx->font, &trans_w, &trans_h,
                 NULL, translation_scale, translation);

    // Place translation below main text (adjust spacing based on ruby presence)
    double spacing_factor = (ctx->max_ruby_height > 0) ? 1.5 : 1.0;
    cairo_move_to(ctx->cairo, 0, y_offset + (ctx->base_text_h * spacing_factor));
    pango_printf(ctx->cairo, ctx->font, translation_scale, translation);

    return trans_w;
}

// Helper: Calculate total height based on render mode
static int calculate_total_height(bool show_original, bool show_translation,
                                  int line_count, int base_text_h, int max_ruby_height,
                                  int translation_h) {
    if (show_original && show_translation) {
        int height = line_count * (base_text_h + max_ruby_height);
        return height + translation_h;
    } else if (show_original) {
        return line_count * (base_text_h + max_ruby_height);
    } else {
        return translation_h;
    }
}

void render_ruby_segments_with_translation(const struct translation_params *params) {
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
    struct ruby_segment *segments = params->segments;
    uint32_t foreground = params->base.foreground;
    const char *translation_mode = params->translation_mode;
    const char *translation = params->translation;

    // Determine what to render
    bool show_original = strcmp(translation_mode, "translation_only") != 0;
    bool show_translation = (strcmp(translation_mode, "both") == 0 ||
                            strcmp(translation_mode, "translation_only") == 0) &&
                            translation && translation[0] != '\0';

    cairo_set_source_u32(cairo, foreground);

    // Calculate dimensions
    int max_ruby_height = calculate_max_ruby_height_ruby(cairo, font, segments, scale);
    int base_text_h;
    get_text_size(cairo, font, NULL, &base_text_h, NULL, scale, "A");

    double translation_scale = scale * 0.7;
    int translation_h = 0;
    if (show_translation) {
        get_text_size(cairo, font, NULL, &translation_h, NULL, translation_scale, "A");
    }

    // Build render context
    struct ruby_render_context ctx = {
        .cairo = cairo,
        .font = font,
        .scale = scale,
        .foreground = foreground,
        .max_ruby_height = max_ruby_height,
        .base_text_h = base_text_h,
    };

    // Render and calculate dimensions
    int total_width = 0;
    int y_offset = 0;
    int line_count = 1;

    // Render original text if needed
    if (show_original) {
        total_width = render_original_text(&ctx, segments, &y_offset, &line_count);
    }

    // Render translation text if needed
    if (show_translation) {
        int trans_w = render_translation_text(&ctx, translation,
                                             translation_scale, y_offset);
        if (trans_w > total_width) {
            total_width = trans_w;
        }
    }

    // Calculate total height
    int total_height = calculate_total_height(show_original, show_translation,
                                              line_count, base_text_h,
                                              max_ruby_height, translation_h);

    *params->base.width = total_width;
    *params->base.height = total_height;
}
