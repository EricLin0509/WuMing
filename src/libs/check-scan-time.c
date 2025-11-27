/* check-scan-time.c
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

#include <stdio.h>

#include "date-to-days.h"
#include "check-scan-time.h"

/* Check if the timestamp is expired or not */
static gboolean
get_expired (const char *timestamp)
{
    g_return_val_if_fail(timestamp != NULL, FALSE);

    int year, month, day;

    if (sscanf(timestamp, "%d.%d.%d", &year, &month, &day) != 3)
    {
        g_critical("Invalid timestamp format: %s", timestamp);
        return FALSE;
    }

    /* Get Current Time */
    time_t t = time(NULL);
    struct tm current_time;
    localtime_r(&t, &current_time);

    int day_diff = date_to_days(current_time.tm_year + 1900, current_time.tm_mon + 1, current_time.tm_mday) -
                        date_to_days(year, month, day);

    return day_diff > 7; // If the day difference is greater than 7, it's expired
}

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
is_scan_time_expired (const char *timestamp, GSettings *setting)
{
    g_return_val_if_fail(setting != NULL || timestamp != NULL, FALSE);

    gboolean has_timestamp = timestamp != NULL;

    if (has_timestamp) return get_expired(timestamp); // If timestamp is provided, use it

    g_autofree gchar *last_scan_time_str = g_settings_get_string(setting, "last-scan-time");

    if (last_scan_time_str == NULL) return TRUE; // If last scan time not found, return true

    return get_expired(last_scan_time_str); // If last scan time found, check if it's expired
}