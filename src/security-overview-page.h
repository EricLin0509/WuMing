/* security-overview-page.h
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

#include "libs/signature-status.h"

G_BEGIN_DECLS

#define SECURITY_OVERVIEW_TYPE_PAGE (security_overview_page_get_type ())

G_DECLARE_FINAL_TYPE (SecurityOverviewPage, security_overview_page, SECURITY_OVERVIEW, PAGE, GtkWidget)

/* Let `scan_overview_button` connect signal to `clicked` signal of goto scan page */
/*
  * @warning
  * This function should be called when `wuming-window` initialized.
*/
void
security_overview_page_connect_goto_scan_page_signal (SecurityOverviewPage *self);

/* Show the last scan time status on the security overview page. */
/*
  * @param self
  * `SecurityOverviewPage` object.
  * 
  * @param is_expired
  * Whether the last scan time is expired or not.
  * 
  * @note
  * If `GSettings` is not NULL, the `is_expired` parameter will be ignored.
*/
void
security_overview_page_show_last_scan_time_status (SecurityOverviewPage *self, gboolean is_expired);

/* Show the signature status on the security overview page. */
/*
  * @param self
  * `SecurityOverviewPage` object.
  * 
  * @param result
  * `scan_result` object to get signature status.
*/
void
security_overview_page_show_signature_status (SecurityOverviewPage *self, const signature_status *result);

/* Show the freshclam serivce is enabled or not */
void
security_overview_page_show_servicestat (SecurityOverviewPage *self, int service_status);

/* Show the health level on the security overview page. */
void
security_overview_page_show_health_level (SecurityOverviewPage *self);

GtkWidget *
security_overview_page_new (void);

G_END_DECLS