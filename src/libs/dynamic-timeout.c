/* dynamic-timeout.c
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

#include <glib/gi18n.h>

#include "dynamic-timeout.h"

int
calculate_dynamic_timeout(int *idle_counter, int *current_timeout)
{
    const int jitter = rand() % JITTER_RANGE;

    if (++(*idle_counter) > MAX_IDLE_COUNT)
    {
        *current_timeout = MIN(*current_timeout * 2, MAX_TIMEOUT_MS);
        *idle_counter = 0;
    }

    return CLAMP(*current_timeout + jitter, BASE_TIMEOUT_MS, MAX_TIMEOUT_MS);
}
