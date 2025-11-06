/* realtime-notification.h
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

#pragma once

#include <gio/gio.h>
#include <glib.h>

/* Init the notification */
GDBusConnection *
realtime_notification_init(void);

/* Send a notification */
/*
  * @note
  * This can also be used to update the notification.
*/
void
realtime_notification_send(GDBusConnection *connection, const char *icon_name, const char *app_name, const char *title, const char *body);

/* Close the notification */
void
realtime_notification_close(GDBusConnection *connection);

/* Clear the notification */
void
realtime_notification_clear(GDBusConnection **connection);
