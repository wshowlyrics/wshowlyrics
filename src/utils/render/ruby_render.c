#include "ruby_render.h"
#include "render_common.h"
#include "render_params.h"
#include "../pango/pango_utils.h"
#include "../../constants.h"
#include "../../user_experience/config/config.h"
#include <string.h>

int calculate_max_ruby_height_ruby(cairo_t *cairo, const char *font,
                                    struct ruby_segment *segments, int scale) {
    int max_ruby_height = 0;
    struct ruby_segment *seg = segments;

    while (seg) {
        if (seg->ruby) {
            int ruby_w, ruby_h;
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

void render_ruby_segments(const struct ruby_params *params) {
    if (!params || !params->segments) {
        if (params && params->base.width && params->base.height) {
            *params->base.width = 0;
            *params->base.height = 0;
        }
        return;
    }

    // Check if any segment has translation
    bool has_any_translation = false;
    struct ruby_segment *check_seg = params->segments;
    while (check_seg) {
        if (check_seg->text && check_seg->text[0] == '\0' &&
            check_seg->translation && check_seg->translation[0] != '\0') {
            has_any_translation = true;
            break;
        }
        check_seg = check_seg->next;
    }

    // If no translation, use static implementation (eliminates duplication)
    if (!has_any_translation) {
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

    // Calculate maximum ruby height
    int max_ruby_height = calculate_max_ruby_height_ruby(cairo, font, segments, scale);

    // Get base text height
    int base_text_h;
    get_text_size(cairo, font, NULL, &base_text_h, NULL, scale, "A");

    // Calculate translation scale (70% of original)
    double translation_scale = scale * 0.7;

    // Render all segments with translation logic
    int x_offset = 0;
    int y_offset = 0;
    int total_width = 0;
    int line_width = 0;
    int line_count = 1;

    struct ruby_segment *seg = segments;
    while (seg) {
        // Check if this is a translation-only segment
        bool is_translation_seg = (seg->text && seg->text[0] == '\0' && seg->translation && seg->translation[0] != '\0');

        if (is_translation_seg) {
            // Render translation below main text
            if (line_width > total_width) {
                total_width = line_width;
            }

            // Use dimmed color with opacity
            struct config *cfg = config_get();
            uint32_t dimmed = create_color_with_opacity(foreground, cfg->translation.translation_opacity);
            cairo_set_source_u32(cairo, dimmed);

            int trans_w, trans_h;
            get_text_size(cairo, font, &trans_w, &trans_h, NULL, translation_scale, seg->translation);
            // Place translation at same baseline level (text will naturally appear below due to font metrics)
            cairo_move_to(cairo, x_offset, y_offset);
            pango_printf(cairo, font, translation_scale, seg->translation);

            if (trans_w > total_width) {
                total_width = trans_w;
            }

            cairo_set_source_u32(cairo, foreground);  // Restore original color
            seg = seg->next;
            continue;
        }

        if (seg->text && strchr(seg->text, '\n')) {
            if (line_width > total_width) {
                total_width = line_width;
            }
            y_offset += base_text_h + max_ruby_height;
            x_offset = 0;
            line_width = 0;
            line_count++;
            seg = seg->next;
            continue;
        }

        int seg_w, seg_h;
        if (seg->ruby) {
            get_ruby_text_size(cairo, font, &seg_w, &seg_h, scale, seg->text, seg->ruby);
        } else {
            get_text_size(cairo, font, &seg_w, &seg_h, NULL, scale, seg->text);
        }
        // Use pango_printf_ruby for all text - ensures consistent baseline
        cairo_move_to(cairo, x_offset, y_offset + max_ruby_height);
        pango_printf_ruby(cairo, font, scale, seg->text, seg->ruby);

        x_offset += seg_w;
        line_width += seg_w;
        seg = seg->next;
    }

    if (line_width > total_width) {
        total_width = line_width;
    }

    int total_height = line_count * (base_text_h + max_ruby_height);
    // Translation line doesn't need ruby space
    total_height -= max_ruby_height;

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

    // Calculate maximum ruby height for original text
    int max_ruby_height = calculate_max_ruby_height_ruby(cairo, font, segments, scale);

    // Get base text height
    int base_text_h;
    get_text_size(cairo, font, NULL, &base_text_h, NULL, scale, "A");

    // Calculate translation text height (smaller, 70% of original)
    double translation_scale = scale * 0.7;
    int translation_h = 0;
    if (show_translation) {
        get_text_size(cairo, font, NULL, &translation_h, NULL, translation_scale, "A");
    }

    int x_offset = 0;
    int y_offset = 0;
    int total_width = 0;
    int line_width = 0;
    int line_count = 1;

    // Render original text if needed
    if (show_original) {
        struct ruby_segment *seg = segments;
        while (seg) {
            if (seg->text && strchr(seg->text, '\n')) {
                if (line_width > total_width) {
                    total_width = line_width;
                }
                y_offset += base_text_h + max_ruby_height;
                x_offset = 0;
                line_width = 0;
                line_count++;
                seg = seg->next;
                continue;
            }

            int seg_w, seg_h;
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

        if (line_width > total_width) {
            total_width = line_width;
        }
    }

    // Render translation text if needed
    if (show_translation) {
        // Use custom opacity from config
        struct config *cfg = config_get();
        uint32_t dimmed = create_color_with_opacity(foreground, cfg->translation.translation_opacity);
        cairo_set_source_u32(cairo, dimmed);

        int trans_w, trans_h;
        get_text_size(cairo, font, &trans_w, &trans_h, NULL, translation_scale,
                    translation);

        // Place translation below main text (adjust spacing based on ruby presence)
        double spacing_factor = (max_ruby_height > 0) ? 1.5 : 1.0;
        cairo_move_to(cairo, 0, y_offset + (base_text_h * spacing_factor));
        pango_printf(cairo, font, translation_scale, translation);

        // Translation is on a separate line, compare width independently
        if (trans_w > total_width) {
            total_width = trans_w;
        }
    }

    // Calculate total height
    int total_height;
    if (show_original && show_translation) {
        total_height = line_count * (base_text_h + max_ruby_height);
        total_height += translation_h;  // Add translation height
    } else if (show_original) {
        total_height = line_count * (base_text_h + max_ruby_height);
    } else {
        total_height = translation_h;
    }

    *params->base.width = total_width;
    *params->base.height = total_height;
}
