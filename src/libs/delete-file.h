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

typedef struct DeleteFileData DeleteFileData;

/* Insert a new delete file data structure to the hash table */
// @return a new created DeleteFileData structure
DeleteFileData *
delete_file_data_table_insert(GtkWidget *threat_page, const char *path, GtkWidget *expander_row);

/* Remove a delete file data structure from the hash table */
// @warning this also clear the DeleteFileData structure and the security context in the hash table
void
delete_file_data_table_remove(DeleteFileData *data);

/* Clear the delete file data structure hash table */
// Tips: this also clears the DeleteFileData structures and the security contexts in the hash table
void
delete_file_data_table_clear(void);

/* Delete threat files */
/*
  * this function should pass a AdwExpanderRow widget to the function
  * because the function needs to remove the row from the GtkListBox
*/
void
delete_threat_file(DeleteFileData *data);

/* Delete all threat files in the threat page */
void
delete_all_threat_files(void);