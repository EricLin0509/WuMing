/* update-signature.h
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

/* BIT Mask for indicating signature status */
/*
    @warning These bit masks cannot be set at the same time
*/
#define SIGNATURE_STATUS_UPTODATE 0x10 // Signature is up-to-date
#define SIGNATURE_STATUS_NOT_FOUND 1 // No signature found

typedef struct _UpdateSignaturePage UpdateSignaturePage;

typedef struct scan_result {
    int year;
    int month;
    int day;
    int time;
    unsigned short status;
} scan_result;

void
scan_signature_date(scan_result *result);

void
is_signature_uptodate(scan_result *result);

void
start_update(UpdateSignaturePage *page);
