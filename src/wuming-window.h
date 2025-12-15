/* wuming-window.h
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

#include <adwaita.h>

G_BEGIN_DECLS

#define WUMING_TYPE_WINDOW (wuming_window_get_type())

G_DECLARE_FINAL_TYPE (WumingWindow, wuming_window, WUMING, WINDOW, AdwApplicationWindow)

/* Push a page by tag */
void
wuming_window_push_page_by_tag (WumingWindow *self, const char *tag);

/* Pop the current page */
void
wuming_window_pop_page (WumingWindow *self);

/* Connect `popped` signal */
gulong
wuming_window_connect_popped_signal (WumingWindow *self, GCallback callback, gpointer user_data);

/* Revoke the `popped` signal */
void
wuming_window_revoke_popped_signal (WumingWindow *self, gulong signal_id);

/* Get current Page tag */
const char *
wuming_window_get_current_page_tag (WumingWindow *self);

/* Check the AdwNavigation is in the `main_page` */
gboolean
wuming_window_is_in_main_page (WumingWindow *self);

/* Get Widgets from the `WumingWindow` */
void *
wuming_window_get_component(WumingWindow *self, const char *component_name);

/* Get hide the window on close */
gboolean
wuming_window_is_hide (WumingWindow *self);

/* Set hide the window on close */
void
wuming_window_set_hide_on_close (WumingWindow *self, gboolean hide_on_close, const char *message);

/* Send the notification */
void
wuming_window_send_notification (WumingWindow *self, GNotificationPriority priority, const char *title, const char *message);

/* Close the notification */
void
wuming_window_close_notification (WumingWindow *self);

/* Send toast notification */
void
wuming_window_send_toast_notification (WumingWindow *self, const char *message, int timeout);

/* Dismiss toast notification */
void
wuming_window_dismiss_toast_notification (WumingWindow *self);

/* Update the signture status */
void
wuming_window_update_signature_status (WumingWindow *self, gboolean need_rescan_signature, gint signature_expiration_time);

G_END_DECLS
