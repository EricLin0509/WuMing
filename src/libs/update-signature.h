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

#include "wuming-window.h"
#include "security-overview-page.h"
#include "updating-page.h"
#include "update-signature-page.h"

typedef struct UpdateContext UpdateContext;

UpdateContext*
update_context_new(WumingWindow *window, SecurityOverviewPage *security_overview_page, UpdateSignaturePage *update_signature_page, UpdatingPage *updating_page);

void
update_context_clear(UpdateContext **ctx);

void
start_update(UpdateContext *ctx);
