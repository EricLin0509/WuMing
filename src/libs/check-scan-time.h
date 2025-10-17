/* check-scan-time.h
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

/* Check if the last scan time is expired or not */
/*
  * @param timestamp
  * The timestamp of the last scan time.
  * 
  * @param setting
  * The GSettings object that stores the last scan time.
  * 
  * @warning
  * If the timestamp is not null, `setting` parameter will be ignored.
  * 
  * @return
  * If the current time is earlier than the last scan time plus a week, it will return true.
*/
gboolean
is_scan_time_expired (const char *timestamp, GSettings *setting);