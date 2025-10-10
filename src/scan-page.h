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

G_BEGIN_DECLS

#define SCAN_TYPE_PAGE (scan_page_get_type())

G_DECLARE_FINAL_TYPE (ScanPage, scan_page, SCAN, PAGE, GtkWidget)

/* Set the last scan time to the status page */
/*
  * @param self
  * `ScanPage` object
  * 
  * @param setting [nullable]
  * `GSettings` object to save last scan time, if this is NULL, it will create a new one.
  * 
  * @param timestamp [nullable]
  * Timestamp string to set as last scan time, if this is NULL, it will use the `GSSettings` object to get the last scan time.
  * 
  * @note
  * if `timestamp` is provided, the `setting` parameter will be ignored.
  * 
  * @warning
  * If `GSettings` is not NULL, you need to unref it manually. This allow sharing the same `GSettings` object with other parts of the program.
*/
void
scan_page_set_last_scan_time(ScanPage *self, GSettings *setting, const gchar *timestamp);

GtkWidget *
scan_page_new(void);

G_END_DECLS
