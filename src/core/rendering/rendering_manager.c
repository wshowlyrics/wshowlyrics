#include "rendering_manager.h"
#include "../../lyrics/lyrics_manager.h"
#include "../../user_experience/config/config.h"
#include "../../utils/mpris/mpris.h"
#include "../../utils/shm/shm.h"
#include "../../utils/pango/pango_utils.h"
#include "../../utils/render/render_common.h"
#include "../../utils/render/ruby_render.h"
#include "../../utils/render/word_render.h"
#include "../../events/wayland_events.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <stdlib.h>
#include <strings.h>

// Helper function to create render_base_params (reduces code duplication)
static inline struct render_base_params make_render_base(
    cairo_t *cairo, const char *font, int scale,
    uint32_t foreground, int *width, int *height) {
    return (struct render_base_params){
        .cairo = cairo,
        .font = font,
        .scale = scale,
        .foreground = foreground,
        .width = width,
        .height = height
    };
}

// Forward declaration for helper functions used in rendering
static void render_segments_with_optional_translation(cairo_t *cairo,
                                                     struct lyrics_state *state,
                                                     int scale, int *w, int *h,
                                                     bool has_seg_trans,
                                                     int current_line_index);

// Helper function to render timing offset progress bar
// Shows a horizontal bar below lyrics indicating the current timing offset:
// - Positive offset (advance lyrics): bar extends right from center
// - Negative offset (delay lyrics): bar extends left from center
// - Zero offset: no bar displayed
static int offset_to_bar_width(int offset_ms, int max_offset, int max_bar_width) {
    int abs_offset = abs(offset_ms);
    if (abs_offset > max_offset) abs_offset = max_offset;
    return (int)((double)abs_offset / max_offset * max_bar_width);
}

// Helper: Render a single bar segment
static void render_single_bar(cairo_t *cairo, uint32_t color, int center_x,
                              int bar_y, int bar_height, int width, bool is_positive,
                              int position_offset) {
    if (width <= 0) {
        return;
    }

    cairo_set_source_u32(cairo, color);
    if (is_positive) {
        cairo_rectangle(cairo, center_x + position_offset, bar_y, width, bar_height);
    } else {
        cairo_rectangle(cairo, center_x - position_offset - width, bar_y, width, bar_height);
    }
    cairo_fill(cairo);
}

// Helper: Render bars when both offsets have the same sign
static void render_same_sign_bars(cairo_t *cairo, int center_x, int bar_y, int bar_height,
                                  int global_offset_ms, int session_offset_ms,
                                  int global_width, int session_width,
                                  int total_offset, uint32_t yellow, uint32_t white) {
    bool is_positive = (total_offset >= 0);

    if (is_positive) {
        // Positive: center → right (global then session)
        if (global_offset_ms > 0) {
            render_single_bar(cairo, yellow, center_x, bar_y, bar_height,
                            global_width, true, 0);
        }
        if (session_offset_ms > 0) {
            render_single_bar(cairo, white, center_x, bar_y, bar_height,
                            session_width, true, global_width);
        }
    } else {
        // Negative: center ← left (session then global)
        if (session_offset_ms < 0) {
            render_single_bar(cairo, white, center_x, bar_y, bar_height,
                            session_width, false, global_width);
        }
        if (global_offset_ms < 0) {
            render_single_bar(cairo, yellow, center_x, bar_y, bar_height,
                            global_width, false, 0);
        }
    }
}

// Helper: Render bar when offsets have opposite signs
static void render_opposite_sign_bar(cairo_t *cairo, int center_x, int bar_y, int bar_height,
                                     int global_offset_ms, int session_offset_ms,
                                     int total_offset, int result_width,
                                     uint32_t yellow, uint32_t white) {
    // Use color of the larger offset
    uint32_t color = (abs(global_offset_ms) > abs(session_offset_ms)) ? yellow : white;
    bool is_positive = (total_offset >= 0);

    render_single_bar(cairo, color, center_x, bar_y, bar_height,
                     result_width, is_positive, 0);
}

// Helper: Render karaoke-style content (multiline or single-line)
static void render_karaoke_content(cairo_t *cairo, struct lyrics_state *state,
                                   int scale, int *width, int *height) {
    int64_t position_us = mpris_get_position();
    int w, h;

    // Use multi-line rendering if enabled and context lines are available
    if (g_config.display.enable_multiline_lrcx &&
        (state->prev_line || state->next_line)) {
        struct multiline_params params = {
            .base = make_render_base(cairo, state->font, scale, state->foreground, &w, &h),
            .prev_line = state->prev_line,
            .current_line = state->current_line,
            .next_line = state->next_line,
            .position_us = position_us
        };
        render_karaoke_multiline(&params);
    } else {
        // Fallback to single-line karaoke
        struct karaoke_params params = {
            .base = make_render_base(cairo, state->font, scale, state->foreground, &w, &h),
            .segments = state->current_line->segments,
            .position_us = position_us
        };
        render_karaoke_segments(&params);
    }

    *width = w;
    *height = h;
}

// Helper: Render normal (non-karaoke) content with segments or plain text
static void render_normal_content(cairo_t *cairo, struct lyrics_state *state,
                                  int scale, const char *text_to_display,
                                  bool has_lyrics, int *width, int *height) {
    cairo_set_source_u32(cairo, state->foreground);

    // Check if this line has segments (for ruby text support)
    bool has_word_segments = (has_lyrics && state->current_line->segments &&
                             state->current_line->segment_count > 0);
    bool has_ruby_segments = (has_lyrics && state->current_line->ruby_segments &&
                             state->current_line->segment_count > 0);

    int w, h;

    if (has_ruby_segments && !has_word_segments) {
        // Render LRC/SRT with ruby_segment (furigana only, no karaoke)
        bool has_seg_trans = has_segment_translation(state->current_line->ruby_segments);
        render_segments_with_optional_translation(cairo, state, scale, &w, &h,
                                                 has_seg_trans, state->current_line_index);
    } else if (has_lyrics && has_word_segments) {
        // Render LRCX with word_segment but without karaoke fill effect
        struct word_static_params params = {
            .base = make_render_base(cairo, state->font, scale, state->foreground, &w, &h),
            .segments = state->current_line->segments
        };
        render_word_segments_static(&params);
    } else {
        // No segments - simple text rendering
        struct render_base_params params = make_render_base(cairo, state->font, scale,
                                                           state->foreground, &w, &h);
        render_plain_text(&params, text_to_display);
    }

    *width = w;
    *height = h;
}

static void render_offset_bar(cairo_t *cairo, int global_offset_ms, int session_offset_ms,
                               int width, int height, uint32_t foreground) {
    const int bar_height = 4; // 4픽셀 높이
    const int bar_y = height; // 텍스트 바로 아래
    const int center_x = width / 2;
    const int max_offset = 5000; // ±5초 (최대 범위)
    const int max_bar_width = width / 2; // 바의 최대 길이 (화면 절반)

    // Hide if both offsets are zero
    if (global_offset_ms == 0 && session_offset_ms == 0) {
        return;
    }

    // Calculate total offset and clamp to max range
    int total_offset = global_offset_ms + session_offset_ms;
    if (total_offset > max_offset) {
        total_offset = max_offset;
    }
    if (total_offset < -max_offset) {
        total_offset = -max_offset;
    }

    // Prepare colors with foreground alpha
    uint32_t alpha = foreground & 0xFF;
    uint32_t yellow = (0xFFFF00 << 8) | alpha; // Global offset color
    uint32_t white = (0xFFFFFF << 8) | alpha;  // Session offset color

    // Render bars based on offset signs
    bool same_sign = ((global_offset_ms >= 0 && session_offset_ms >= 0) ||
                      (global_offset_ms <= 0 && session_offset_ms <= 0));

    if (same_sign) {
        // Same sign: render sequentially (global then session)
        int global_width = offset_to_bar_width(global_offset_ms, max_offset, max_bar_width);
        int session_width = offset_to_bar_width(session_offset_ms, max_offset, max_bar_width);

        render_same_sign_bars(cairo, center_x, bar_y, bar_height,
                             global_offset_ms, session_offset_ms,
                             global_width, session_width,
                             total_offset, yellow, white);
    } else {
        // Opposite signs: render only the net result
        int result_width = offset_to_bar_width(total_offset, max_offset, max_bar_width);

        render_opposite_sign_bar(cairo, center_x, bar_y, bar_height,
                                global_offset_ms, session_offset_ms,
                                total_offset, result_width,
                                yellow, white);
    }
}

cairo_subpixel_order_t rendering_manager_to_cairo_subpixel(enum wl_output_subpixel subpixel) {
    switch (subpixel) {
    case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
        return CAIRO_SUBPIXEL_ORDER_RGB;
    case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
        return CAIRO_SUBPIXEL_ORDER_BGR;
    case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
        return CAIRO_SUBPIXEL_ORDER_VRGB;
    case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
        return CAIRO_SUBPIXEL_ORDER_VBGR;
    default:
        return CAIRO_SUBPIXEL_ORDER_DEFAULT;
    }
    return CAIRO_SUBPIXEL_ORDER_DEFAULT;
}

// Helper: Build translation progress text with icons
static void build_translation_progress_text(char *buffer, size_t buffer_size,
                                            struct lyrics_state *state,
                                            int current_line_index) {
    int T = state->lyrics.translation_current;
    int C = current_line_index + 1; // 1-based current line
    int A = state->lyrics.translation_total;
    int R = (A > 0) ? (T * 100 / A) : 0; // Percentage

    float cache_threshold = config_get_cache_threshold(g_config.translation.cache_policy);
    bool show_disk = (A > 0) && ((float)T / A >= cache_threshold);

    if (state->lyrics.translation_will_discard) {
        snprintf(buffer, buffer_size, "⏳ Translating... %d%% (%d/%d) 🗑️", R, T, C);
    } else if (show_disk) {
        snprintf(buffer, buffer_size, "⏳ Translating... %d%% (%d/%d) 💾", R, T, C);
    } else {
        snprintf(buffer, buffer_size, "⏳ Translating... %d%% (%d/%d)", R, T, C);
    }
}

// Helper: Build translation display text with icons
static void build_translation_display_text(char *buffer, size_t buffer_size,
                                          struct lyrics_state *state,
                                          const char *translation) {
    if (!state->lyrics.translation_in_progress) {
        snprintf(buffer, buffer_size, "%s", translation);
        return;
    }

    float cache_threshold = config_get_cache_threshold(g_config.translation.cache_policy);
    int T = state->lyrics.translation_current;
    int A = state->lyrics.translation_total;
    bool cached = (A > 0) && ((float)T / A >= cache_threshold);

    if (state->lyrics.translation_will_discard) {
        snprintf(buffer, buffer_size, "🗑️ %s", translation);
    } else if (cached) {
        snprintf(buffer, buffer_size, "💾 %s", translation);
    } else {
        snprintf(buffer, buffer_size, "⏳ %s", translation);
    }
}

// Helper: Render ruby segments with optional translation
static void render_segments_with_optional_translation(cairo_t *cairo,
                                                     struct lyrics_state *state,
                                                     int scale, int *w, int *h,
                                                     bool has_seg_trans,
                                                     int current_line_index) {
    struct render_base_params base = make_render_base(cairo, state->font, scale,
                                                       state->foreground, w, h);

    // SRT with <sub> tags - use segment-level translation
    if (has_seg_trans) {
        struct ruby_params params = {
            .base = base,
            .segments = state->current_line->ruby_segments
        };
        render_ruby_segments(&params);
        return;
    }

    // Check if translation is enabled
    bool translation_enabled = (g_config.translation.provider &&
                               strcmp(g_config.translation.provider, "false") != 0);

    if (!translation_enabled) {
        // No translation - render plain ruby segments
        struct ruby_params params = {
            .base = base,
            .segments = state->current_line->ruby_segments
        };
        render_ruby_segments(&params);
        return;
    }

    // Translation is enabled
    if (state->current_line->translation) {
        // Line has translation - show it with icon
        char translation_text[512];
        build_translation_display_text(translation_text, sizeof(translation_text),
                                      state, state->current_line->translation);

        struct translation_params params = {
            .base = base,
            .segments = state->current_line->ruby_segments,
            .translation_mode = g_config.translation.translation_display,
            .translation = translation_text
        };
        render_ruby_segments_with_translation(&params);
    } else if (state->lyrics.translation_in_progress) {
        // Translation in progress - show progress
        char progress_text[128];
        build_translation_progress_text(progress_text, sizeof(progress_text),
                                       state, current_line_index);

        struct translation_params params = {
            .base = base,
            .segments = state->current_line->ruby_segments,
            .translation_mode = g_config.translation.translation_display,
            .translation = progress_text
        };
        render_ruby_segments_with_translation(&params);
    } else {
        // No translation available - render plain ruby segments
        struct ruby_params params = {
            .base = base,
            .segments = state->current_line->ruby_segments
        };
        render_ruby_segments(&params);
    }
}

void rendering_manager_render_to_cairo(cairo_t *cairo, struct lyrics_state *state,
                                       int scale, uint32_t *width, uint32_t *height) {
    const char *text_to_display = NULL; // NULL means no content to display
    bool has_lyrics = (state->current_line && state->current_line->text);
    bool is_empty_line = false; // Track if current line is empty (instrumental break)
    bool is_karaoke = false; // Track if this is karaoke-style LRCX

    // Check for LRCX multiline context (for instrumental breaks)
    bool is_lrcx = lyrics_manager_is_format(state, ".lrcx");
    bool has_multiline_context = (is_lrcx && g_config.display.enable_multiline_lrcx &&
                                  (state->prev_line || state->next_line));

    if (has_lyrics) {
        // Check if the text is empty or only whitespace
        const char *text = state->current_line->text;
        if (text[0] == '\0') {
            // Empty string - treat as idle/instrumental break
            is_empty_line = true;
            // text_to_display remains NULL - surface will be transparent
        } else {
            text_to_display = text;
            // Karaoke mode is only enabled for LRCX format
            is_karaoke = is_lrcx;
        }
    } else if (has_multiline_context) {
        // During instrumental breaks in LRCX, we still render prev/next lines
        is_karaoke = true;
    }

    // No content to display - return zero size for transparent surface
    if (!text_to_display && !is_karaoke) {
        *width = 0;
        *height = 0;
        return;
    }

    // Use transparent background when no lyrics or during instrumental breaks
    // Exception: For LRCX multiline, show background if we have context lines (prev/next)
    bool show_background = (has_lyrics && !is_empty_line) || has_multiline_context;
    uint32_t background_color = show_background ? state->background : 0x00000000;

    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_u32(cairo, background_color);
    cairo_paint(cairo);

    // Render lyrics content using appropriate mode
    int w, h;
    if (is_karaoke) {
        render_karaoke_content(cairo, state, scale, &w, &h);
    } else {
        render_normal_content(cairo, state, scale, text_to_display, has_lyrics, &w, &h);
    }
    *width = w;
    *height = h;

    // Render timing offset progress bar (if offset is non-zero and lyrics are shown)
    // Hide progress bar during instrumental breaks (is_empty_line = true)
    // Global offset: persistent across tracks (yellow bar)
    // Session offset: temporary per-track adjustment (white bar)
    int global_offset_ms = g_config.lyrics.global_offset_ms;
    int session_offset_ms = state->timing_offset_ms - global_offset_ms; // Extract session-only offset
    if ((global_offset_ms != 0 || session_offset_ms != 0) && has_lyrics && !is_empty_line) {
        render_offset_bar(cairo, global_offset_ms, session_offset_ms, *width, *height, state->foreground);
        *height += 6; // Progress bar 높이 + 여백 (4px bar + 2px spacing)
    }
}

void rendering_manager_render_transparent(struct lyrics_state *state) {
    // Skip rendering if Wayland connection is not available
    if (!state->wl_conn || !state->wl_conn->connected) {
        return;
    }

    if (state->no_buffer_detach) {
        // Some compositors (e.g., KDE) reset surface position on buffer detach
        // Use transparent buffer instead
        const int scale = state->output ? state->output->scale : 1;
        state->current_buffer = get_next_buffer(state->shm,
                state->buffers, state->width * scale, state->height * scale);
        if (state->current_buffer) {
            cairo_t *shm = state->current_buffer->cairo;
            cairo_save(shm);
            cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
            cairo_paint(shm);
            cairo_restore(shm);

            wl_surface_set_buffer_scale(state->surface, scale);
            wl_surface_attach(state->surface, state->current_buffer->buffer, 0, 0);
            wl_surface_damage_buffer(state->surface, 0, 0, state->width, state->height);
            wl_surface_commit(state->surface);
        }
    } else {
        // Detach buffer to make surface fully transparent
        // This avoids buffer/surface size mismatch during resize
        wl_surface_attach(state->surface, NULL, 0, 0);
        wl_surface_commit(state->surface);
    }
}

void rendering_manager_render_frame(struct lyrics_state *state) {
    // Skip rendering if Wayland connection is not available
    if (!state->wl_conn || !state->wl_conn->connected) {
        return;
    }

    cairo_surface_t *recorder = cairo_recording_surface_create(
            CAIRO_CONTENT_COLOR_ALPHA, NULL);
    cairo_t *cairo = cairo_create(recorder);
    cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
    if (state->output) {
        cairo_font_options_set_subpixel_order(
                fo, rendering_manager_to_cairo_subpixel(state->output->subpixel));
    }
    cairo_set_font_options(cairo, fo);
    cairo_font_options_destroy(fo);
    cairo_save(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cairo);
    cairo_restore(cairo);

    const int scale = state->output ? state->output->scale : 1;
    uint32_t width = 0, height = 0;
    rendering_manager_render_to_cairo(cairo, state, scale, &width, &height);

    if (height / scale != state->height
            || width / scale != state->width
            || state->width == 0) {
        // Size change detected - make overlay transparent during resize
        rendering_manager_render_transparent(state);

        // Reconfigure surface size
        if (width == 0 || height == 0) {
            // No content - keep minimal 1x1 surface (buffer already detached above)
            zwlr_layer_surface_v1_set_size(state->layer_surface, 1, 1);
        } else {
            zwlr_layer_surface_v1_set_size(
                    state->layer_surface, width / scale, height / scale);
        }

        wl_surface_commit(state->surface);
    } else if (height > 0) {
        // Replay recording into shm and send it off
        state->current_buffer = get_next_buffer(state->shm,
                state->buffers, state->width * scale, state->height * scale);
        if (!state->current_buffer) {
            cairo_surface_destroy(recorder);
            cairo_destroy(cairo);
            return;
        }
        cairo_t *shm = state->current_buffer->cairo;

        cairo_save(shm);
        cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
        cairo_paint(shm);
        cairo_restore(shm);

        cairo_set_source_surface(shm, recorder, 0.0, 0.0);
        cairo_paint(shm);

        wl_surface_set_buffer_scale(state->surface, scale);
        wl_surface_attach(state->surface,
                state->current_buffer->buffer, 0, 0);
        wl_surface_damage_buffer(state->surface, 0, 0,
                state->width, state->height);

        // Request frame callback for vsync
        state->frame_callback = wl_surface_frame(state->surface);
        wl_callback_add_listener(state->frame_callback,
                wayland_events_get_frame_listener(), state);
        state->frame_scheduled = true;

        wl_surface_commit(state->surface);
    }

    cairo_surface_destroy(recorder);
    cairo_destroy(cairo);
}

void rendering_manager_set_dirty(struct lyrics_state *state) {
    // Skip if Wayland connection is not available
    if (!state->wl_conn || !state->wl_conn->connected) {
        return;
    }

    if (state->frame_scheduled) {
        state->dirty = true;
    } else if (state->surface) {
        rendering_manager_render_frame(state);
    }
}
