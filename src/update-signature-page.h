/* update-signature-page.c
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
#include "libs/update-signature.h"

G_BEGIN_DECLS

#define UPDATE_SIGNATURE_TYPE_PAGE (update_signature_page_get_type())

G_DECLARE_FINAL_TYPE (UpdateSignaturePage, update_signature_page, UPDATE_SIGNATURE, PAGE, GtkWidget)

GtkWidget *
update_signature_page_new(void);

void
update_signature_page_show_isuptodate(UpdateSignaturePage *self, const signature_status *result);

void
update_signature_page_show_servicestat(UpdateSignaturePage *self);

void
update_signature_page_set_update_context(UpdateSignaturePage *self, UpdateContext *context);

G_END_DECLS
