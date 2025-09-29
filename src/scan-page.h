/* scan-page.h
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

#define SCAN_TYPE_PAGE (scan_page_get_type())

G_DECLARE_FINAL_TYPE (ScanPage, scan_page, SCAN, PAGE, GtkWidget)

GtkWidget *
scan_page_new(void);

GtkWidget *
threat_page_get_list(ScanPage *self);


void
threat_page_prepend_threat(ScanPage *self, AdwActionRow *row);

void
show_threat_page (ScanPage *self);

void
threat_page_clear_list(ScanPage *self);

void
cancel_signal_bind(ScanPage *self, GCallback cancel_callback, gpointer user_data);

void
cancel_signal_clear(ScanPage *self);

void
scanning_page_set_current_status(ScanPage *self, const char *status);

void
scanning_page_set_final_result(ScanPage *self, const char *result, const char *detail, const char *icon_name);

G_END_DECLS
