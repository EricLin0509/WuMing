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

#include "check-scan-time.h"

/* Check if the timestamp is expired or not */
static gboolean
get_expired (const char *timestamp)
{
    struct tm tm = {0};
    int year, month, day, hour, min, sec;
    time_t scan_time, now;
    double diff_seconds;

    /* Parse timestamp */
    if (sscanf(timestamp, "%d.%d.%d %d:%d:%d",
               &year, &month, &day, &hour, &min, &sec) != 6)
    {
        return FALSE; // Format error
    }

    /* Store timestamp to tm struct */
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;

    if ((scan_time = mktime(&tm)) == (time_t)-1)
    {
        return FALSE; // Invaild time value
    }

    time(&now);

    diff_seconds = difftime(now, scan_time);

    return diff_seconds > 604800; // 7 days
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