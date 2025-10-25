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

#include "wuming-window.h"
#include "security-overview-page.h"
#include "scan-page.h"
#include "scanning-page.h"
#include "threat-page.h"

typedef struct ScanContext ScanContext;

ScanContext *
scan_context_new(WumingWindow *window, SecurityOverviewPage *security_overview_page, ScanPage *scan_page, ScanningPage *scanning_page, ThreatPage *threat_page);

void
scan_context_clear(ScanContext **ctx);

void
start_scan(ScanContext *ctx, const char *path);
