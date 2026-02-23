/**
 * D-Bus Control Interface
 *
 * Provides D-Bus methods for controlling wshowlyrics at runtime:
 * - Overlay toggle (show/hide lyrics overlay)
 * - Timing offset adjustment (sync lyrics with audio)
 *
 * Service: org.wshowlyrics.Control
 * Object: /org/wshowlyrics/Control
 * Interface: org.wshowlyrics.Control
 *
 * Methods:
 * - ToggleOverlay() -> void
 * - SetOverlay(enabled: boolean) -> void
 * - SetTimingOffset(offset_ms: int32) -> void
 * - AdjustTimingOffset(delta_ms: int32) -> void
 * - ResetTimingOffset() -> void
 *
 * Benefits over FIFO:
 * - No polling required (event-driven)
 * - No nested main loop issues
 * - Standard Linux IPC mechanism
 * - Integrates with GMainContext
 */

#include "dbus_control.h"
#include "../../main.h"
#include "../../constants.h"
#include "../../core/rendering/rendering_manager.h"
#include "../../user_experience/system_tray/system_tray.h"
#include "../../user_experience/config/config.h"
#include <gio/gio.h>
#include <stdio.h>

// D-Bus service constants
#define CONTROL_BUS_NAME "org.wshowlyrics.Control"
#define CONTROL_OBJECT_PATH "/org/wshowlyrics/Control"
#define CONTROL_INTERFACE "org.wshowlyrics.Control"

// Global state
static GDBusConnection *dbus_connection = NULL;
static guint owner_id = 0;
static guint registration_id = 0;
static struct lyrics_state *g_state = NULL;

// D-Bus introspection XML
static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.wshowlyrics.Control'>"
    "    <method name='ToggleOverlay'/>"
    "    <method name='SetOverlay'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "    </method>"
    "    <method name='SetTimingOffset'>"
    "      <arg type='i' name='offset_ms' direction='in'/>"
    "    </method>"
    "    <method name='AdjustTimingOffset'>"
    "      <arg type='i' name='delta_ms' direction='in'/>"
    "    </method>"
    "    <method name='ResetTimingOffset'>"
    "    </method>"
    "  </interface>"
    "</node>";

// Method call handler
static void handle_method_call(
    GDBusConnection *connection,
    const gchar *sender,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *method_name,
    GVariant *parameters,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
    (void)connection;       // Required by GDBus callback signature
    (void)sender;           // Required by GDBus callback signature
    (void)object_path;      // Required by GDBus callback signature
    (void)interface_name;   // Required by GDBus callback signature
    struct lyrics_state *state = (struct lyrics_state *)user_data;

    if (strcmp(method_name, "ToggleOverlay") == 0) {
        // Toggle overlay state
        state->overlay_enabled = !state->overlay_enabled;
        system_tray_set_overlay_state(state->overlay_enabled);
        rendering_manager_set_dirty(state);

        log_info("D-Bus: Overlay toggled: %s", state->overlay_enabled ? "enabled" : "disabled");
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (strcmp(method_name, "SetOverlay") == 0) {
        // Set overlay to specific state
        gboolean enabled;
        g_variant_get(parameters, "(b)", &enabled);

        state->overlay_enabled = enabled;
        system_tray_set_overlay_state(state->overlay_enabled);
        rendering_manager_set_dirty(state);

        log_info("D-Bus: Overlay set to: %s", enabled ? "enabled" : "disabled");
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (strcmp(method_name, "SetTimingOffset") == 0) {
        // Set timing offset to absolute value
        gint32 offset_ms;
        g_variant_get(parameters, "(i)", &offset_ms);

        // Clamp to reasonable range
        if (offset_ms < -5000) offset_ms = -5000;
        if (offset_ms > 5000) offset_ms = 5000;

        state->timing_offset_ms = offset_ms;
        rendering_manager_set_dirty(state);

        log_info("D-Bus: Timing offset set to: %dms", offset_ms);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (strcmp(method_name, "AdjustTimingOffset") == 0) {
        // Adjust timing offset by delta
        gint32 delta_ms;
        g_variant_get(parameters, "(i)", &delta_ms);

        state->timing_offset_ms += delta_ms;

        // Clamp to reasonable range
        if (state->timing_offset_ms < -5000) state->timing_offset_ms = -5000;
        if (state->timing_offset_ms > 5000) state->timing_offset_ms = 5000;

        rendering_manager_set_dirty(state);

        log_info("D-Bus: Timing offset adjusted by %+dms, now: %dms", delta_ms, state->timing_offset_ms);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (strcmp(method_name, "ResetTimingOffset") == 0) {
        // Reset timing offset to global offset
        state->timing_offset_ms = g_config.lyrics.global_offset_ms;

        rendering_manager_set_dirty(state);

        log_info("D-Bus: Timing offset reset to global offset: %dms", state->timing_offset_ms);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method: %s",
            method_name);
    }
}

// D-Bus interface vtable
static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call,
    NULL, // get_property
    NULL  // set_property
};

// Bus acquired callback
static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)name;  // Required by GDBus callback signature
    GError *error = NULL;
    GDBusNodeInfo *introspection_data;

    dbus_connection = connection;

    // Parse introspection XML
    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (error) {
        log_error("Failed to parse D-Bus introspection XML: %s", error->message);
        g_error_free(error);
        return;
    }

    // Register object
    registration_id = g_dbus_connection_register_object(
        connection,
        CONTROL_OBJECT_PATH,
        introspection_data->interfaces[0],
        &interface_vtable,
        user_data,  // user_data = lyrics_state pointer
        NULL,       // user_data_free_func
        &error);

    g_dbus_node_info_unref(introspection_data);

    if (error) {
        log_error("Failed to register D-Bus object: %s", error->message);
        g_error_free(error);
        return;
    }

    log_info("D-Bus control interface registered at %s", CONTROL_OBJECT_PATH);
}

// Name acquired callback
static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)connection;  // Required by GDBus callback signature
    (void)user_data;   // Required by GDBus callback signature
    log_info("D-Bus name acquired: %s", name);
}

// Name lost callback
static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)user_data;  // Required by GDBus callback signature
    if (connection == NULL) {
        log_error("Failed to connect to D-Bus session bus");
    } else {
        log_warn("D-Bus name lost: %s (another instance might be running)", name);
    }
}

bool dbus_control_init(struct lyrics_state *state) {
    if (!state) {
        log_error("dbus_control_init: state is NULL");
        return false;
    }

    g_state = state;

    // Own the bus name
    owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        CONTROL_BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        state,  // user_data = lyrics_state pointer
        NULL);  // user_data_free_func

    if (owner_id == 0) {
        log_error("Failed to own D-Bus name: %s", CONTROL_BUS_NAME);
        return false;
    }

    log_info("D-Bus control interface initialized");
    return true;
}

void dbus_control_cleanup(void) {
    // Unregister object
    if (registration_id > 0 && dbus_connection) {
        g_dbus_connection_unregister_object(dbus_connection, registration_id);
        registration_id = 0;
    }

    // Release bus name
    if (owner_id > 0) {
        g_bus_unown_name(owner_id);
        owner_id = 0;
    }

    dbus_connection = NULL;
    g_state = NULL;

    log_info("D-Bus control interface cleaned up");
}
