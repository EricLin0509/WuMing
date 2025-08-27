/* scan.h
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

void
start_scan(AdwDialog *dialog,
             GtkWidget *navigation_view,
             GtkWidget *scan_status,
             GtkWidget *close_button,
             AdwNavigationPage *threat_navigation_page,
             GtkWidget *threat_status,
             GtkWidget *threat_button,
             AdwNavigationPage *cancel_navigation_page,
             GtkWidget *cancel_button,
             char *path);
