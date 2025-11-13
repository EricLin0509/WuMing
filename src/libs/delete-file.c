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
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/random.h>

#include "delete-file.h"
#include "file-security.h"
#include "subprocess-components.h"

#include "threat-page.h"

// Warning: this macro should be started and ended with a space, otherwise may cause comparison error
#define SYSTEM_DIRECTORIES " /usr /lib /lib64 /etc /opt /var /sys /proc " // System directories that should be warned before deleting

#define PKEXEC_PATH "/usr/bin/pkexec" // Path to the `pkexec` binary

static GList *delete_file_data_list = NULL; // List of delete file data structures

typedef struct DeleteFileData {
    const char *path;
    GtkWidget *threat_page;
    GtkWidget *action_row;

    FileSecurityContext *security_context; // Security context for the file
} DeleteFileData; // Data structure to store the information of a file to be deleted

/* clear the GList `data` field callback function */
static inline void
clear_list_elements_func(void *data)
{
  g_return_if_fail(data);
  DeleteFileData *delete_data = data; // Cast the data to `DeleteFileData` pointer
  delete_file_data_clear(&delete_data);
}

/* Set file properties */
/*
  * first initialize the file security context using `secure_open_and_verify()`
  * check file whether is a file inside the system directory
  * and set the properties of the AdwActionRow
  * Warning: the AdwActionRow widget MUST have `subtitle` property
*/
gboolean
set_file_properties(DeleteFileData *data)
{
    g_return_val_if_fail(data != NULL, FALSE);
    g_return_val_if_fail(data->security_context != NULL, FALSE);

    if (!data->action_row || !GTK_IS_WIDGET(data->action_row)) return FALSE; // Check if the action row is valid

    const char *path = data->path;

    if (!path || !*path || path[0] != '/') // Check if the path is valid and is absolute
    {
        g_warning("Invalid absolute path: %s", path ? path : "(null)");
        return FALSE;
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
    return TRUE;
}

/* Create a new delete file data structure */
// Tips: this also creates a new security context for the file
// Warning: the AdwActionRow MUST be added to the GtkListBox before calling this function
DeleteFileData *
delete_file_data_new(GtkWidget *threat_page, GtkWidget *action_row)
{
    if (!threat_page || !GTK_IS_WIDGET(threat_page) ||
       !action_row || !GTK_IS_WIDGET(action_row) ||
        !gtk_widget_get_ancestor(action_row, THREAT_TYPE_PAGE) != NULL) // Check if the action_row is a child of the threat_page
    {
        g_critical("[ERROR] Invalid parameters, threat_page: %p, action_row: %p", threat_page, action_row);
        return NULL;
    }

    DeleteFileData *data = g_new0(DeleteFileData, 1);
    data->path = adw_action_row_get_subtitle(ADW_ACTION_ROW(action_row)), // This string SHOULDN'T be freed because it's owned by the action row
    data->threat_page = threat_page;
    data->action_row = action_row;
    data->security_context = file_security_context_new(data->path, FALSE, NULL, NULL);

    if (!set_file_properties(data))
    {
        g_critical("[ERROR] Failed to set file properties");
        delete_file_data_clear(&data);
        return NULL;
    }

    return data;
}

/* Prepend a new delete file data structure to the list */
// Tips: You can pass data or threat_page and action_row to this function
// @return the the updated list with the new data structure prepended, or NULL if an error occurred
GList *
delete_file_data_list_prepend(DeleteFileData *data, GtkWidget *threat_page, GtkWidget *action_row)
{
    gboolean has_data = data != NULL;

    if (!has_data)
    {
        data = delete_file_data_new(threat_page, action_row);
        if (!data)
        {
            g_critical("[ERROR] Failed to create delete file data structure");
            return NULL;
        }
    }
    GList *new_list = g_list_prepend(delete_file_data_list, data);

    if (new_list != NULL) delete_file_data_list = new_list;

    return new_list;
}

/* Clear the delete file data structure */
// Tips: this also clears the security context for the file
void
delete_file_data_clear(DeleteFileData **data)
{
    g_return_if_fail(data != NULL && *data != NULL);

    file_security_context_clear(&(*data)->security_context, NULL, NULL);
    g_free(*data);

    *data = NULL;
}

/* Clear the delete file data structure list */
// Tips: this also clears the DeleteFileData structures and the security contexts in the list
void
delete_file_data_list_clear(void)
{
    if (!delete_file_data_list) return;

    g_list_free_full(delete_file_data_list, clear_list_elements_func);
    delete_file_data_list = NULL;
}

/* Remove a delete file data structure from the list */
// Tips: this also removes the security context for the file
// @return the updated list with the data structure removed, or NULL if the data structure was not found
GList *
delete_file_data_list_remove(DeleteFileData *data)
{
    g_return_val_if_fail(data != NULL, NULL);

    GList *new_list = g_list_remove(delete_file_data_list, data);

    if (new_list != NULL) delete_file_data_list = new_list;

    delete_file_data_clear(&data);

    return new_list;
}

/* Policy forbid the operation to delete the file */
// Use if `validate_path_safety()` returns `FALSE`
static void
policy_forbid_operation(DeleteFileData *data)
{
    gtk_widget_set_sensitive(data->action_row, FALSE);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("Blocked by policy, try removing it manually!"));
}

/* error operation */
static void
error_operation(DeleteFileData *data, FileSecurityStatus status)
{
    gtk_widget_set_sensitive(data->action_row, FALSE);
    switch (status)
    {
        case FILE_SECURITY_DIR_MODIFIED:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("Directory modified, try removing it manually!"));
            break;
        case FILE_SECURITY_FILE_MODIFIED:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("File may compromised, try removing it manually!"));
            break;
        case FILE_SECURITY_DIR_NOT_FOUND:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("Directory not found!"));
            break;
        case FILE_SECURITY_FILE_NOT_FOUND:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("File not found!"));
            break;
        case FILE_SECURITY_INVALID_PATH:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("Invalid path!"));
            break;
        case FILE_SECURITY_PERMISSION_DENIED:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("Permission denied!"));
            break;
        default:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(data->action_row), gettext("Unknown error!"));
            break;
    }
}

/* Add audit log for if user attempted to delete a file */
static void 
log_deletion_attempt(const char *path)
{
    gchar *sanitized_path = g_filename_display_name(path); // Sanitize the path to avoid log injection
    gchar *log_entry = g_strdup_printf("[AUDIT] User %s performed delete on %s", 
                                     g_get_user_name(), sanitized_path);
    syslog(LOG_AUTH | LOG_WARNING, "%s", log_entry);
    g_free(sanitized_path);
    g_free(log_entry);
}

/* Delete threat files in elevated mode */
static void
delete_threat_file_elevated(DeleteFileData *data)
{
    g_return_if_fail(data && data->security_context);

    char *shm_name = NULL;
    FileSecurityContext *copied_context = file_security_context_copy(data->security_context, TRUE, &shm_name); // Copy the security context to shared memory
    if (!copied_context)
    {
        g_critical("[ERROR] Failed to copy security context to shared memory");
        error_operation(data, FILE_SECURITY_UNKNOWN_ERROR);
        return;
    }

    pid_t pid;

    spawn_new_process_no_pipes(&pid, PKEXEC_PATH, "pkexec", HELPER_PATH, // `HELPER_PATH` is defined in `meson.build`
                                    shm_name, data->path, NULL); // Spawn the helper process

    int exit_status = pid == -1 ? FILE_SECURITY_UNKNOWN_ERROR : wait_for_process(pid, 0); // Wait for the helper process to finish

    if (exit_status == 126) // Pkexec user request dismiss
    {
        file_security_context_clear(&copied_context, &shm_name, NULL);
        return;
    }

    exit_status = (exit_status < FILE_SECURITY_OK || exit_status > FILE_SECURITY_UNKNOWN_ERROR) ? FILE_SECURITY_UNKNOWN_ERROR : exit_status; // Check if the exit status is valid

    if ((FileSecurityStatus)exit_status != FILE_SECURITY_OK)
    {
        g_critical("[ERROR] Helper process returned error: %d", exit_status);
        error_operation(data, (FileSecurityStatus)exit_status);
        file_security_context_clear(&copied_context, &shm_name, NULL);
        return;
    }

    // Clean up
    file_security_context_clear(&copied_context, &shm_name, NULL);
    threat_page_remove_threat(THREAT_PAGE(data->threat_page), data->action_row); // Remove the action row from the list view
    log_deletion_attempt(data->path);
    return;
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

    /* Delete file */
    FileSecurityStatus result = file_security_secure_delete(data->security_context, data->path, 0);
    if (result != FILE_SECURITY_OK)
    {
        switch (errno)
        {
            case EACCES:
                g_warning("[WARNING] Permission denied, use elevated mode to delete the file");
                delete_threat_file_elevated(data);
                break;
            default:
                error_operation(data, result);
        }
    }
    else // If the file is deleted successfully, show the success message and add audit log
    {
        g_print("[INFO] File deleted: %s\n", data->path);
        threat_page_remove_threat(THREAT_PAGE(data->threat_page), data->action_row); // Remove the action row from the list view
        log_deletion_attempt(data->path);
        if (delete_file_data_list_remove(data) == NULL)
        {
            g_critical("[ERROR] Failed to remove delete file data structure from the list");
        }
    }
}