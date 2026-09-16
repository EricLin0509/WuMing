/* systemd-control.h
 *
 * Copyright 2025 EricLin
 * Copyright 2026 Dae Euhwa
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
 */

#pragma once

int
is_service_enabled(const char *service_name);

/* Check if clamd.conf has a non-default Threads setting.
 * Returns >1 if Threads is configured and >1, 1 if Threads is 1 or unset, -1 on error. */
int
check_clamd_threads_config(void);