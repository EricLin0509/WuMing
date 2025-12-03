/* scan-options-configs.h
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

#define SCAN_OPTIONS_N_ELEMENTS 6

/* The bitmask of scan options */
#define SCAN_OPTIONS_ENABLE_LARGE_FILE 0x01
#define SCAN_OPTIONS_ENABLE_PUA 0x02
#define SCAN_OPTIONS_SCAN_ARCHIVE 0x04
#define SCAN_OPTIONS_SCAN_MAIL 0x08
#define SCAN_OPTIONS_ALERT_EXCEED_MAX 0x10
#define SCAN_OPTIONS_ALERT_ENCRYPTED 0x20