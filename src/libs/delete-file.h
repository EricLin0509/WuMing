/* delete-file.h
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

 /* This file contains the function prototypes for deleting a threat file. */

#pragma once

#include <adwaita.h>

#include "file-security-status.h"

typedef struct DeleteFileData DeleteFileData;

/* Create a new delete file data structure hash table */
GHashTable *
delete_file_data_table_new(void);

/* Insert a new delete file data structure to the hash table */
// @return a new created DeleteFileData structure
// @warning the `path` string must be malloced
DeleteFileData *
delete_file_data_table_insert(GHashTable *delete_file_table, const char *path, GtkWidget *expander_row);

/* Get expander_row from DeleteFileData structure */
GtkWidget *
delete_file_data_get_expander_row(DeleteFileData *data);

/* Delete threat files */
/*
  * Delete a threat file and remove the delete file data structure from the hash table
*/
FileSecurityStatus
delete_threat_file(GHashTable *delete_file_table, DeleteFileData *data);