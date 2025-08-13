#include <glib.h>
#include <dbus/dbus.h>

#include "systemd-control.h"

int is_service_enabled(const char *service_name)
{
    DBusError err;
    DBusConnection *conn = NULL;
    DBusMessage *msg = NULL, *reply = NULL;
    int ret = -1;

    dbus_error_init(&err);

    /*Connect to the system bus and request a name on it*/
    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err))
    {
        g_warning("Can't connect to the system bus: %s\n", err.message);
        goto cleanup;
    }

    /*Create a method call message*/
    msg = dbus_message_new_method_call(
        "org.freedesktop.systemd1",           // Target service
        "/org/freedesktop/systemd1",          // Object path
        "org.freedesktop.systemd1.Manager",   // Interface name
        "GetUnitFileState"                    // Method name
    );

    /*Add the service name argument*/
    const char *unit = service_name;
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &unit, DBUS_TYPE_INVALID))
    {
        g_warning("Can't add argument to message\n");
        goto cleanup;
    }

    /*Send the message and wait for a reply (5000ms timeout)*/
    reply = dbus_connection_send_with_reply_and_block(conn, msg, 5000, &err);
    if (dbus_error_is_set(&err))
    {
        g_warning("Method call failed: %s\n", err.message);
        goto cleanup;
    }

    /*Parse the returned string*/
    const char *state;
    if (!dbus_message_get_args(reply, &err, DBUS_TYPE_STRING, &state, DBUS_TYPE_INVALID))
    {
        g_warning("Can't parse arguments: %s\n", err.message);
        goto cleanup;
    }

    /*Check the state and return the result*/
    ret = strcmp(state, "enabled") == 0 ? 1 : 0;

cleanup:
    if (msg) dbus_message_unref(msg);
    if (reply) dbus_message_unref(reply);
    if (conn) dbus_connection_unref(conn);
    dbus_error_free(&err);
    return ret;
}
