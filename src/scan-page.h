/* scan-page.h
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

#include "libs/scan.h"

G_BEGIN_DECLS

#define SCAN_TYPE_PAGE (scan_page_get_type())

G_DECLARE_FINAL_TYPE (ScanPage, scan_page, SCAN, PAGE, GtkWidget)

/* Show the last scan time to the status page */
/*
  * @param self
  * `ScanPage` object
  * 
  * @param setting [OPTIONAL]
  * `GSettings` object to save last scan time, if this is NULL, it will create a new one.
  * 
  * @param timestamp [OPTIONAL]
  * Timestamp string to set as last scan time, if this is NULL, it will use the `GSSettings` object to get the last scan time.
  * 
  * @note
  * if `timestamp` is provided, the `setting` parameter will be ignored.
  * 
  * @warning
  * If `GSettings` is not NULL, you need to unref it manually. This allow sharing the same `GSettings` object with other parts of the program.
*/
void
scan_page_show_last_scan_time (ScanPage *self, GSettings *setting, const gchar *timestamp);

/* Show whether the last scan time is expired or not */
/*
  * @param self
  * `ScanPage` object
  * 
  * @param setting [OPTIONAL]
  * `GSettings` object to save last scan time, if is NULL, it will ignore it and use `is_expired` directly.
  * 
  * @param is_expired [OPTIONAL]
  * Whether the last scan time is expired or not.
  * 
  * @note
  * If `GSettings` is not NULL, the `is_expired` parameter will be ignored.
*/
void
scan_page_show_last_scan_time_status (ScanPage *self, GSettings *setting, gboolean is_expired);

/* Save last scan time to GSettings */
/*
  * @param setting
  * `GSettings` object to save last scan time, if this is NULL, it will create a new one.
  * 
  * @param need_timestamp [optional]
  * If this is true, it will generate a new timestamp and save it to GSettings. otherwise return NULL
  * 
  * @warning
  * If `GSettings` is not NULL, you need to unref it manually. This allow sharing the same `GSettings` object with other parts of the program.
*/
gchar *
save_last_scan_time (GSettings *setting, gboolean need_timestamp);

/* Set the `ScanContext` object to the `ScanPage` object */
void
scan_page_set_scan_context (ScanPage *self, ScanContext *context);

GtkWidget *
scan_page_new(void);

G_END_DECLS
