/* updating-page.h
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

#define UPDATING_TYPE_PAGE (updating_page_get_type ())

G_DECLARE_FINAL_TYPE (UpdatingPage, updating_page, UPDATING, PAGE, GtkWidget)

GtkWidget *
updating_page_new (void);

void
updating_page_reset (UpdatingPage *self);

void
updating_page_set_final_result (UpdatingPage *self, const char *result, const char *icon_name);

G_END_DECLS