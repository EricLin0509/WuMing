/* systemd-control.c
 *
 * Copyright 2025 EricLin
 * Copyright 2026 Dae Euhwa
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <dbus/dbus.h>
#include <stdio.h>
#include <string.h>

#include "systemd-control.h"

#define CLAMD_CONF_PATH "/etc/clamav/clamd.conf"

/* Query a single systemd unit's active state via D-Bus.
 * Returns 1 if the unit is in "active" state, 0 otherwise, -1 on error. */
static int
get_unit_active_state(DBusConnection *conn, const char *unit_name)
{
    DBusError err;
    DBusMessage *msg = NULL, *reply = NULL;
    int ret = -1;

    dbus_error_init(&err);

    msg = dbus_message_new_method_call(
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "GetUnit"
    );

    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &unit_name, DBUS_TYPE_INVALID))
    {
        g_warning("Can't add argument to message\n");
        goto cleanup;
    }

    reply = dbus_connection_send_with_reply_and_block(conn, msg, 5000, &err);
    if (dbus_error_is_set(&err))
    {
        /* Unit not found — not an error, just means it doesn't exist */
        dbus_error_free(&err);
        ret = 0;
        goto cleanup;
    }

    /* GetUnit returns an object path; now query its ActiveState property */
    const char *unit_path = NULL;
    if (!dbus_message_get_args(reply, &err, DBUS_TYPE_OBJECT_PATH, &unit_path, DBUS_TYPE_INVALID))
    {
        g_warning("Can't parse GetUnit reply: %s\n", err.message);
        goto cleanup;
    }

    dbus_message_unref(msg);
    msg = NULL;
    dbus_message_unref(reply);
    reply = NULL;

    /* Get the ActiveState property */
    msg = dbus_message_new_method_call(
        "org.freedesktop.systemd1",
        unit_path,
        "org.freedesktop.DBus.Properties",
        "Get"
    );

    const char *interface = "org.freedesktop.systemd1.Unit";
    const char *property = "ActiveState";
    if (!dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &interface,
            DBUS_TYPE_STRING, &property,
            DBUS_TYPE_INVALID))
    {
        g_warning("Can't add arguments to Properties.Get\n");
        goto cleanup;
    }

    reply = dbus_connection_send_with_reply_and_block(conn, msg, 5000, &err);
    if (dbus_error_is_set(&err))
    {
        g_warning("Properties.Get failed: %s\n", err.message);
        goto cleanup;
    }

    /* Parse the variant reply */
    DBusMessageIter iter, variant_iter;
    dbus_message_iter_init(reply, &iter);
    dbus_message_iter_recurse(&iter, &variant_iter);

    const char *active_state = NULL;
    dbus_message_iter_get_basic(&variant_iter, &active_state);

    if (active_state)
        ret = (strcmp(active_state, "active") == 0 ||
               strcmp(active_state, "reloading") == 0) ? 1 : 0;

cleanup:
    if (msg) dbus_message_unref(msg);
    if (reply) dbus_message_unref(reply);
    dbus_error_free(&err);
    return ret;
}

int is_service_enabled(const char *service_name)
{
    DBusError err;
    DBusConnection *conn = NULL;
    int ret = 0;

    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err))
    {
        g_warning("Can't connect to the system bus: %s\n", err.message);
        goto cleanup;
    }

    /* Check if the service unit is actually running (not just enabled).
     * A socket being active doesn't mean the daemon is accepting connections —
     * it may have crashed or failed to start. Only use the daemon-based
     * scanner (clamdscan) when the service is confirmed active. */
    ret = get_unit_active_state(conn, service_name);

cleanup:
    if (conn) dbus_connection_unref(conn);
    dbus_error_free(&err);
    return ret;
}

/* Check if clamd.conf has a non-default Threads setting.
 * Returns >0 if Threads is configured and >1, 1 if Threads is 1 or unset, -1 on error. */
int
check_clamd_threads_config(void)
{
    FILE *f = fopen(CLAMD_CONF_PATH, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        /* Skip comments and empty lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        int threads = 0;
        if (sscanf(p, "Threads %d", &threads) == 1)
        {
            fclose(f);
            return threads;
        }
    }

    fclose(f);
    return 1; /* Threads not found — clamd defaults to 1 */
}