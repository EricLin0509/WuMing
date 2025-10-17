/* signature-status.h
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

/* BIT Mask for indicating signature status */
/*
    @warning These bit masks cannot be set at the same time
*/
#define SIGNATURE_STATUS_UPTODATE 0x10 // Signature is up-to-date
#define SIGNATURE_STATUS_NOT_FOUND 1 // No signature found

typedef struct signature_status signature_status;

signature_status *
signature_status_new(void);

void
signature_status_clear(signature_status **status);

/* Get the status of the signature */
/*
  * @param status
  * The signature status object.
  * 
  * @return
  * The status of the signature.
*/
unsigned short
signature_status_get_status(const signature_status *status);

/* Get the date of the signature */
/*
  * @param status
  * The signature status object.
  * 
  * @param year (out)
  * The year of the signature.
  * 
  * @param month (out)
  * The month of the signature.
  * 
  * @param day (out)
  * The day of the signature.
  * 
  * @param hour (out)
  * The hour of the signature.
  * 
  * @param minute (out)
  * The minute of the signature.
*/
void
signature_status_get_date(const signature_status *status, int *year, int *month, int *day, int *hour, int *minute);
