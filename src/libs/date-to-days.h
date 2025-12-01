/* date-to-days.h
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

#include <stdbool.h>

/* Convert date to days since 1970-01-01 */
static inline int
date_to_days(int year, int month, int day)
{
    /* Check is valid date */
    if (year < 1970 || year > 9999) return 0;
    if (month < 1 || month > 12) return 0;
    if (day < 1 || day > 31) return 0;

    /* Turn years to days */
    int year_days = (year - 1970) * 365 +
                    (year - 1970) / 4 -
                    (year - 1970) / 100 +
                    (year - 1970) / 400;

    /* Turn months to days */
    const bool current_year_is_leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);

    static const int cumulative_days[] = { // Use cumulative days to calculate the dates of each month
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
    };

    int month_days = cumulative_days[month - 1];
    if (current_year_is_leap_year && month > 2) month_days += 1; // If current month is greater than February and current year is leap year, add 1 to the month_days

    const int max_day = 
        (month == 2 && current_year_is_leap_year) ? 29 : 
        (cumulative_days[month] - cumulative_days[month - 1]);

    if (day > max_day) return 0; // Check is valid day again

    return year_days + month_days + day;
}