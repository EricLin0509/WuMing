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

/* Get current Page tag */
const char *
wuming_window_get_current_page_tag (WumingWindow *self);

/* Check the AdwNavigation is in the `main_page` */
gboolean
wuming_window_is_in_main_page (WumingWindow *self);

/* Get `UpdateContext` */
/*
  * @warning
  * This return a void pointer, which need to be cast to the `UpdateContext` pointer type.
*/
void *
wuming_window_get_update_context (WumingWindow *self);

/* Get `ScanContext` */
/*
  * @warning
  * This return a void pointer, which need to be cast to the `ScanContext` pointer type.
*/
void *
wuming_window_get_scan_context (WumingWindow *self);

/* Get hide the window on close */
gboolean
wuming_window_is_active (WumingWindow *self);

/* Set hide the window on close */
void
wuming_window_set_hide_on_close (WumingWindow *self, gboolean hide_on_close);

/* Set the notification body */
void
wuming_window_set_notification_body (WumingWindow *self, const char *body);

/* Send the notification */
void
wuming_window_send_notification (WumingWindow *self, GNotificationPriority priority, const char *title, const char *message);

G_END_DECLS
