/* delete-file.c
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

 /* This file contains the implementation of the delete-file action */

#include <glib/gi18n.h>
#include <syslog.h>
#include <inttypes.h>
#include <sys/types.h>
#include <fcntl.h>

#include "delete-file.h"

// Warning: this macro should be started and ended with a space, otherwise may cause comparison error
#define SYSTEM_DIRECTORIES " /usr /lib /lib64 /etc /opt /var /sys /proc " // System directories that should be warned before deleting

#ifndef O_PATH // Compatibility for some Linux distributions
# define O_PATH 010000000
#endif

DeleteFileData *
delete_file_data_new(GtkWidget *action_row)
{
    DeleteFileData *data = g_new0(DeleteFileData, 1);
    data->path = adw_action_row_get_subtitle(ADW_ACTION_ROW(action_row)), // This string SHOULDN'T be freed because it's owned by the action row
    data->list_box = gtk_widget_get_parent(action_row),
    data->action_row = action_row;
    data->security_context = file_security_context_new(data->path);

    return data;
}

void
delete_file_data_clear(DeleteFileData *data)
{
    file_security_context_clear(data->security_context);
    g_free(data);
}

/* Set file properties */
/*
  * first initialize the file security context using `secure_open_and_verify()`
  * check file whether is a file inside the system directory
  * and set the properties of the AdwActionRow
  * Warning: the AdwActionRow widget MUST have `subtitle` property
*/
void
set_file_properties(DeleteFileData *data)
{
    g_return_if_fail(data != NULL);
    g_return_if_fail(data->security_context != NULL);

    if (!secure_open_and_verify(data->security_context, data->path)) // Initialize the file security context
    {
        g_critical("[ERROR] failed to initialize file security context");
        return;
    }

    if (!data->action_row || !GTK_IS_WIDGET(data->action_row)) return; // Check if the action row is valid

    char *path = data->path;

    if (!path || !*path || path[0] != '/') // Check if the path is valid and is absolute
    {
        g_warning("Invalid absolute path: %s", path ? path : "(null)");
        return;
    }

    /* Get the first directory name in the path */
    // Because all the paths should be absolute, the first character should be a slash
    char *second_slash = strchr(path + 1, '/');
    /*
      * if there is no second slash, copy the whole path
      * else, copy the part of the path before the second slash
    */
    char *path_prefix = second_slash ? g_strndup(path, second_slash - path) : g_strdup(path);

    gchar *query = g_strdup_printf(" %s ", path_prefix); // Use for searching in the `SYSTEM_DIRECTORIES`
    gboolean is_system_direcotry = (strstr(SYSTEM_DIRECTORIES, query) != NULL);

    adw_preferences_row_set_title(
        ADW_PREFERENCES_ROW(data->action_row),
        is_system_direcotry ? 
            gettext("Maybe a system file, delete it with caution!") : 
            gettext("Normal file")
    );

    g_free(query);
    g_free(path_prefix);
}

/* Disable the action row and set the title to "Failed to delete file" */
/*
  * 'error' can be NULL
*/
static void
error_operation(DeleteFileData *data, GError *error)
{
    gtk_widget_set_sensitive(data->action_row, FALSE);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("Delete failed!"));
    if (error) g_error_free(error);
}

/* Policy forbid the operation to delete the file */
// Use if `validate_path_safety()` returns `FALSE`
static void
policy_forbid_operation(DeleteFileData *data)
{
    gtk_widget_set_sensitive(data->action_row, FALSE);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("Blocked by policy, try removing it manually!"));
}

/* file validation fail operation */
static void
file_validation_fail_operation(DeleteFileData *data)
{
    gtk_widget_set_sensitive(data->action_row, FALSE);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("File may compromised, try removing it manually!"));
}

/* Add audit log for if user attempted to delete a file */
static void 
log_deletion_attempt(const char *path)
{
    gchar *sanitized_path = g_filename_display_name(path); // Sanitize the path to avoid log injection
    gchar *log_entry = g_strdup_printf("[AUDIT] User performed %s delete on %s", 
                                     g_get_user_name(), sanitized_path);
    syslog(LOG_AUTH | LOG_WARNING, "%s", log_entry);
    g_free(sanitized_path);
    g_free(log_entry);
}

/* Delete threat files */
/*
  * this function should pass a AdwActionRow widget to the function
  * because the function needs to remove the row from the GtkListBox
*/
void
delete_threat_file(DeleteFileData *data)
{
    g_return_if_fail(data != NULL);
    g_return_if_fail(data->security_context != NULL);

    if (!data->action_row || !GTK_IS_WIDGET(data->action_row)) return; // Check if the action row is valid

    /* First check if the path is safe to delete */
    if (!validate_path_safety(data->path))
    {
        policy_forbid_operation(data);
        return;
    }

    /* Check file integrity using unclosed file descriptor */
    if (!validate_by_fd(data->security_context))
    {
        g_critical("[SECURITY] File integrity check failed!");
        file_validation_fail_operation(data);
        return;
    }

    /* Delete the file */
    if (unlinkat(data->security_context->dir_fd, 
                    data->security_context->base_name, 0) == -1)
    {
        g_critical("[ERROR] Failed to delete file: %s", data->path);
        error_operation(data, NULL); // Show the error message
    }
    else // If the file is deleted successfully, show the success message and add audit log
    {
        g_print("[INFO] File deleted: %s\n", data->path);
        gtk_list_box_remove(GTK_LIST_BOX(data->list_box), data->action_row); // Remove the action row from the list view
        log_deletion_attempt(data->path);
    }

    // Clean up
    delete_file_data_clear(data);
}