#include "rendering_manager.h"
#include "../../lyrics/lyrics_manager.h"
#include "../../user_experience/config/config.h"
#include "../../utils/mpris/mpris.h"
#include "../../utils/shm/shm.h"
#include "../../utils/pango/pango_utils.h"
#include "../../utils/render/render_common.h"
#include "../../utils/render/ruby_render.h"
#include "../../utils/render/word_render.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <strings.h>

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

void rendering_manager_render_to_cairo(cairo_t *cairo, struct lyrics_state *state,
                                       int scale, uint32_t *width, uint32_t *height) {
    const char *text_to_display = " "; // Default to single space
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
            text_to_display = " "; // Display single space to keep surface visible
        } else {
            text_to_display = text;
            // Karaoke mode is only enabled for LRCX format
            is_karaoke = is_lrcx;
        }
    } else if (has_multiline_context) {
        // During instrumental breaks in LRCX, we still render prev/next lines
        is_karaoke = true;
    }

    // Use transparent background when no lyrics or during instrumental breaks
    // Exception: For LRCX multiline, show background if we have context lines (prev/next)
    bool show_background = (has_lyrics && !is_empty_line) || has_multiline_context;
    uint32_t background_color = show_background ? state->background : 0x00000000;

    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_u32(cairo, background_color);
    cairo_paint(cairo);

    if (is_karaoke) {
        // Karaoke-style rendering with progressive word fill (wipe effect)
        int64_t position_us = mpris_get_position();
        int w, h;

        // Use multi-line rendering if enabled and context lines are available
        if (g_config.display.enable_multiline_lrcx &&
            (state->prev_line || state->next_line)) {
            render_karaoke_multiline(cairo, state->font, scale,
                                    state->prev_line, state->current_line,
                                    state->next_line, state->foreground,
                                    position_us, &w, &h);
        } else {
            // Fallback to single-line karaoke
            render_karaoke_segments(cairo, state->font, scale,
                                   state->current_line->segments,
                                   state->foreground, position_us, &w, &h);
        }

        *width = w;
        *height = h;
    } else {
        // Normal rendering (non-karaoke)
        cairo_set_source_u32(cairo, state->foreground);

        // Check if this line has segments (for ruby text support)
        // Note: LRCX uses word_segment (with karaoke), LRC/SRT use ruby_segment (furigana only)
        bool has_word_segments = (has_lyrics && state->current_line->segments && state->current_line->segment_count > 0);
        bool has_ruby_segments = (has_lyrics && state->current_line->ruby_segments && state->current_line->segment_count > 0);

        if (has_ruby_segments && !has_word_segments) {
            // Render LRC/SRT with ruby_segment (furigana only, no karaoke)
            int w, h;

            // Check if segments have inline translation tags (<sub>)
            bool has_seg_trans = has_segment_translation(state->current_line->ruby_segments);

            if (has_seg_trans) {
                // SRT with <sub> tags - use segment-level translation
                render_ruby_segments(cairo, state->font, scale,
                                    state->current_line->ruby_segments,
                                    state->foreground, &w, &h);
            } else if (g_config.translation.provider && strcmp(g_config.translation.provider, "false") != 0 && state->current_line->translation) {
                // LRC or SRT without <sub> - use line-level translation
                // If translation is still in progress, show hourglass icon
                char translation_text[512];
                if (state->lyrics.translation_in_progress) {
                    // Translation ahead of current line - show hourglass with translated text
                    snprintf(translation_text, sizeof(translation_text),
                            "⏳ %s", state->current_line->translation);
                } else {
                    // Translation complete - just show translation
                    snprintf(translation_text, sizeof(translation_text),
                            "%s", state->current_line->translation);
                }

                render_ruby_segments_with_translation(cairo, state->font, scale,
                                                     state->current_line->ruby_segments,
                                                     state->foreground,
                                                     g_config.translation.translation_display,
                                                     translation_text,
                                                     &w, &h);
            } else if (g_config.translation.provider && strcmp(g_config.translation.provider, "false") != 0 && state->lyrics.translation_in_progress) {
                // Translation in progress - show enhanced progress
                // T: translation_current, C: current line, A: translation_total
                // Format: "⏳ Translating... R% (T/C) [💾]"
                char progress_text[128];
                int T = state->lyrics.translation_current;
                int C = state->current_line_index + 1; // 1-based current line
                int A = state->lyrics.translation_total;
                int R = (A > 0) ? (T * 100 / A) : 0; // Percentage

                // Get threshold from config (comfort: 50%, balanced: 75%, aggressive: 90%)
                float cache_threshold = config_get_cache_threshold(g_config.translation.cache_policy);
                bool show_disk = (A > 0) && ((float)T / A >= cache_threshold);

                // Build progress message
                if (show_disk) {
                    snprintf(progress_text, sizeof(progress_text),
                            "⏳ Translating... %d%% (%d/%d) 💾",
                            R, T, C);
                } else {
                    snprintf(progress_text, sizeof(progress_text),
                            "⏳ Translating... %d%% (%d/%d)",
                            R, T, C);
                }

                render_ruby_segments_with_translation(cairo, state->font, scale,
                                                     state->current_line->ruby_segments,
                                                     state->foreground,
                                                     g_config.translation.translation_display,
                                                     progress_text,
                                                     &w, &h);
            } else {
                // No translation
                render_ruby_segments(cairo, state->font, scale,
                                    state->current_line->ruby_segments,
                                    state->foreground, &w, &h);
            }
            *width = w;
            *height = h;
        } else if (has_lyrics && has_word_segments) {
            // Render LRCX with word_segment but without karaoke fill effect
            // This shouldn't happen normally (LRCX uses karaoke mode above)
            // But we handle it for completeness
            int w, h;
            render_word_segments_static(cairo, state->font, scale,
                                       state->current_line->segments,
                                       state->foreground, &w, &h);
            *width = w;
            *height = h;
        } else {
            // No segments - simple text rendering
            int w, h;
            render_plain_text(cairo, state->font, scale,
                            text_to_display, state->foreground, &w, &h);
            *width = w;
            *height = h;
        }
    }
}

void rendering_manager_render_transparent(struct lyrics_state *state) {
    // Skip rendering if Wayland connection is not available
    if (!state->wl_conn || !state->wl_conn->connected) {
        return;
    }

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
            // Keep a minimal 1x1 surface instead of detaching
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
