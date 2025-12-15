#ifndef RENDERING_MANAGER_H
#define RENDERING_MANAGER_H

#include "../../main.h"
#include "../../utils/wayland/wayland_manager.h"
#include <cairo/cairo.h>
#include <stdint.h>
#include <wayland-client.h>

/**
 * Convert Wayland subpixel order to Cairo subpixel order
 *
 * @param subpixel Wayland subpixel order
 * @return Cairo subpixel order
 */
cairo_subpixel_order_t rendering_manager_to_cairo_subpixel(enum wl_output_subpixel subpixel);

/**
 * Render lyrics state to Cairo context
 * Updates width and height with rendered dimensions
 *
 * @param cairo Cairo context
 * @param state Lyrics state
 * @param scale Output scale factor
 * @param width Output width (updated)
 * @param height Output height (updated)
 */
void rendering_manager_render_to_cairo(cairo_t *cairo, struct lyrics_state *state,
                                       int scale, uint32_t *width, uint32_t *height);

/**
 * Render a transparent frame
 * Used during resize or when no lyrics are displayed
 *
 * @param state Lyrics state
 */
void rendering_manager_render_transparent(struct lyrics_state *state);

/**
 * Render current frame to Wayland surface
 *
 * @param state Lyrics state
 */
void rendering_manager_render_frame(struct lyrics_state *state);

/**
 * Mark state as dirty and schedule re-render
 *
 * @param state Lyrics state
 */
void rendering_manager_set_dirty(struct lyrics_state *state);

#endif // RENDERING_MANAGER_H
