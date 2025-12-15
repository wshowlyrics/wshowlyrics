#ifndef WAYLAND_INIT_H
#define WAYLAND_INIT_H

#include "../../main.h"
#include <stdbool.h>

// Forward declaration
struct lyrics_state;

/**
 * Initialize Wayland surface and connection
 *
 * @param state Lyrics state (will be populated with Wayland objects)
 * @param anchor Layer surface anchor position
 * @param margin Layer surface margin from edge
 * @return true if initialization successful, false otherwise
 */
bool wayland_init_surface(struct lyrics_state *state, unsigned int anchor, int margin);

#endif // WAYLAND_INIT_H
