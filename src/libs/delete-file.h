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

/* Create a new delete file data structure */
// Tips: this also creates a new security context for the file
// Warning: the AdwActionRow MUST be added to the GtkListBox before calling this function
DeleteFileData *
delete_file_data_new(GtkWidget *threat_page, GtkWidget *action_row);

/* Prepend a new delete file data structure to the list */
// Tips: You can pass data or threat_page and action_row to this function
// @return the the updated list with the new data structure prepended, or NULL if an error occurred
GList *
delete_file_data_list_prepend(DeleteFileData *data, GtkWidget *threat_page, GtkWidget *action_row);

/* Clear the delete file data structure */
// Tips: this also clears the security context for the file
void
delete_file_data_clear(DeleteFileData **data);

/* Remove a delete file data structure from the list */
// Tips: this also removes the security context for the file
// @return the updated list with the data structure removed, or NULL if the data structure was not found
GList *
delete_file_data_list_remove(DeleteFileData *data);

/* Clear the delete file data structure list */
// Tips: this also clears the DeleteFileData structures and the security contexts in the list
void
delete_file_data_list_clear(void);

/* Delete threat files */
/*
  * this function should pass a AdwActionRow widget to the function
  * because the function needs to remove the row from the GtkListBox
*/
void
delete_threat_file(DeleteFileData *data);