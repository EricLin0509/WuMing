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
#include <sys/stat.h>

#include "file-security.h"

typedef struct {
    const char *path;
    GtkWidget *list_box;
    GtkWidget *action_row;

    FileSecurityContext *security_context; // Security context for the file
} DeleteFileData; // Data structure to store the information of a file to be deleted

/* Create a new delete file data structure */
// Tips: this also creates a new security context for the file
// Warning: the AdwActionRow MUST be added to the GtkListBox before calling this function
DeleteFileData *
delete_file_data_new(GtkWidget *list_box, GtkWidget *action_row);

/* Clear the delete file data structure */
// Tips: this also clears the security context for the file
void
delete_file_data_clear(DeleteFileData *data);

/* Set file properties */
/*
  * first initialize the file security context using `secure_open_and_verify()`
  * check file whether is a file inside the system directory
  * and set the properties of the AdwActionRow
  * Warning: the AdwActionRow widget MUST have `subtitle` property
*/
gboolean
set_file_properties(DeleteFileData *data);

/* Delete threat files */
/*
  * this function should pass a AdwActionRow widget to the function
  * because the function needs to remove the row from the GtkListBox
*/
void
delete_threat_file(DeleteFileData *data);