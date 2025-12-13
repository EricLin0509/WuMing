/* scanning-page.h
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

G_BEGIN_DECLS

#define SCANNING_TYPE_PAGE (scanning_page_get_type())

G_DECLARE_FINAL_TYPE (ScanningPage, scanning_page, SCANNING, PAGE, GtkWidget)

void
scanning_page_disable_threat_button (ScanningPage *self);

void
scanning_page_reset (ScanningPage *self);

void
scanning_page_set_progress (ScanningPage *self, const char *progress);

void
scanning_page_set_final_result (ScanningPage *self, gboolean has_threat, const char *result, const char *detail, const char *icon_name);

void
scanning_page_set_cancel_signal (ScanningPage *self, GCallback cancel_signal_cb, gpointer user_data);

void
scanning_page_revoke_cancel_signal (ScanningPage *self);

GtkWidget *
scanning_page_new (void);

G_END_DECLS