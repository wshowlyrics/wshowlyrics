#ifndef _LYRICS_DBUS_CONTROL_H
#define _LYRICS_DBUS_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

// Forward declaration
struct lyrics_state;

/**
 * Initialize D-Bus control interface
 *
 * Registers org.wshowlyrics.Control service and exposes control methods.
 *
 * @param state Pointer to lyrics state (for method handlers)
 * @return true on success, false on failure
 */
bool dbus_control_init(struct lyrics_state *state);

/**
 * Cleanup D-Bus control interface
 *
 * Unregisters service and releases resources.
 */
void dbus_control_cleanup(void);

#endif
