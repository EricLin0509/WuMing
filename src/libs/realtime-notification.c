/* realtime-notification.c
 *
 * Copyright 2025 EricLin
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "realtime-notification.h"

static guint32 notify_id = 0; // Notification ID
static guint signal_subscription_id = 0; // Signal subscription ID

static void
on_notification_closed(
    GDBusConnection *connection,
    const gchar *sender,
    const gchar *path,
    const gchar *interface,
    const gchar *signal,
    GVariant *params,
    gpointer user_data)
{
    guint32 closed_id;
    g_variant_get(params, "(uu)", &closed_id);

    if (closed_id == notify_id) // If the closed notification ID matches the current notification ID
    {
        notify_id = 0; // Reset the notification ID
    }
}

/* Init the notification */
GDBusConnection *
realtime_notification_init(void)
{
    GError *error = NULL;

    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error != NULL)
    {
        g_critical("Cannot connect to D-Bus: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    /* Register a signal handler to listen for notification closed signal */
    signal_subscription_id = g_dbus_connection_signal_subscribe (
            connection,
            "org.freedesktop.Notifications",
            "org.freedesktop.Notifications",
            "NotificationClosed",
            "/org/freedesktop/Notifications",
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_notification_closed,
            NULL,
            NULL
    );

    return connection;
}

/* Send a notification */
/*
  * @note
  * This can also be used to update the notification.
*/
void
realtime_notification_send(GDBusConnection *connection, const char *icon_name, const char *app_name, const char *title, const char *body)
{
    g_return_if_fail(connection != NULL && app_name != NULL);

    GError *error = NULL;

    const char *notify_title = title == NULL ? "" : title;
    const char *notify_body = body == NULL ? "" : body;

    /* Send a notification */
    GVariant *reply = g_dbus_connection_call_sync(connection,
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "Notify",
        g_variant_new("(susssasa{sv}i)",
            app_name,
            notify_id,
            icon_name,
            notify_title,
            notify_body,
            NULL,
            NULL,
            -1),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);

    if (error != NULL)
    {
        g_critical("Cannot send notification: %s", error->message);
        g_error_free(error);
        return;
    }

    if (notify_id == 0) g_variant_get(reply, "(u)", &notify_id); // Get the notification ID if it's the first time sending a notification
    
    if (reply != NULL) g_variant_unref(reply); // Release the reply object
}

/* Close the notification */
void
realtime_notification_close(GDBusConnection *connection)
{
    g_return_if_fail(connection != NULL);

    if (notify_id != 0)
    {
        GError *error = NULL;
        
        g_dbus_connection_call_sync(connection,
            "org.freedesktop.Notifications",
            "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications",
            "CloseNotification",
            g_variant_new("(u)", notify_id),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);
            
        if (error != NULL)
        {
            g_warning("Failed to close notification: %s", error->message);
            g_error_free(error);
        }
        
        notify_id = 0;
    }
}

/* Clear the notification */
void
realtime_notification_clear(GDBusConnection **connection)
{
    if (connection == NULL || *connection == NULL) {
        return;
    }

    realtime_notification_close(*connection);

    if (signal_subscription_id != 0)
    {
        g_dbus_connection_signal_unsubscribe(*connection, signal_subscription_id);
        signal_subscription_id = 0;
    }

    g_clear_object(connection);
}