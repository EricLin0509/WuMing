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

#define PKEXEC_PATH "/usr/bin/pkexec" // Path to the `pkexec` binary

typedef struct DeleteFileData {
    const char *path;
    GtkWidget *expander_row;

    FileSecurityContext *security_context; // Security context for the file
} DeleteFileData; // Data structure to store the information of a file to be deleted

/* Clear the delete file data structure */
// Tips: this also clears the security context for the file
static void
delete_file_data_clear(DeleteFileData **data)
{
    g_return_if_fail(data != NULL && *data != NULL);

    g_clear_pointer((void **)(&(*data)->path), g_free);
    g_free(*data);

    *data = NULL;
}

/* The GDestroyNotify function to clear the DeleteFileData structure */
static inline void
clear_hash_table_element(gpointer data)
{
    DeleteFileData *delete_file_data = (DeleteFileData *)data;

    if (!delete_file_data) return; // Check if the data is valid

    delete_file_data_clear(&delete_file_data);
}

/* Create a new delete file data structure hash table */
GHashTable *
delete_file_data_table_new(void)
{
        return g_hash_table_new_full(
                            g_direct_hash,
                            g_direct_equal,
                            NULL,
                            (GDestroyNotify) clear_hash_table_element);
}

/* Create a new delete file data structure */
// Tips: this also creates a new security context for the file
// @warning the `path` string must be malloced
static DeleteFileData *
delete_file_data_new(const char *path, GtkWidget *expander_row)
{
    g_return_val_if_fail(path != NULL, NULL);

    DeleteFileData *data = g_new0(DeleteFileData, 1);
    data->path = path;
    data->expander_row = expander_row;
    data->security_context = file_security_context_new(path, FALSE, NULL, NULL);

    if (!data->security_context)
    {
        g_critical("[ERROR] Failed to create new security context for file");
        delete_file_data_clear(&data);
    }

    return data;
}

/* Insert a new delete file data structure to the hash table */
// @return a new created DeleteFileData structure
DeleteFileData *
delete_file_data_table_insert(GHashTable *delete_file_table, const char *path, GtkWidget *expander_row)
{
    g_return_val_if_fail(delete_file_table != NULL, NULL);

    DeleteFileData *data = delete_file_data_new(path, expander_row);
    if (!data)
    {
        g_critical("[ERROR] Failed to create new delete file data structure");
        return data;
    }

    g_hash_table_insert(delete_file_table, data, data); // Insert the data structure to the hash table

    return data;
}

/* Get expander_row from DeleteFileData structure */
GtkWidget *
delete_file_data_get_expander_row(DeleteFileData *data)
{
    g_return_val_if_fail(data != NULL, NULL);

    return data->expander_row;
}

/* Remove a delete file data structure from the hash table */
static void
delete_file_data_table_remove(GHashTable *delete_file_table, DeleteFileData *data)
{
    g_return_if_fail(data != NULL && delete_file_table != NULL);

    g_hash_table_remove(delete_file_table, data); // This also clears the delete file data structure and the security context
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
static FileSecurityStatus
delete_threat_file_elevated(GHashTable *delete_file_table, DeleteFileData *data)
{
    g_return_val_if_fail(delete_file_table, FILE_SECURITY_OPERATION_FAILED);
    g_return_val_if_fail(data && data->security_context, FILE_SECURITY_OPERATION_FAILED);

    char *shm_name = NULL;
    FileSecurityContext *copied_context = file_security_context_copy(data->security_context, TRUE, &shm_name); // Copy the security context to shared memory
    if (!copied_context)
    {
        g_critical("[ERROR] Failed to copy security context to shared memory");
        return FILE_SECURITY_OPERATION_FAILED;
    }

    pid_t pid = 0;

    if (!spawn_new_process_no_pipes(&pid, PKEXEC_PATH, "pkexec", HELPER_PATH, // `HELPER_PATH` is defined in `meson.build`
                                    shm_name, data->path, NULL)) // Spawn the helper process
    {
        /* Process spawn failed */
        g_critical("[ERROR] Failed to spawn helper process");
        file_security_context_clear(&copied_context, &shm_name, NULL);
        return FILE_SECURITY_OPERATION_FAILED;
    }

    int exit_status = wait_for_process(pid, 0); // Wait for the helper process to finish

    if (exit_status == 126) // Pkexec user request dismiss
    {
        g_warning("[WARNING] User dismissed the elevation request");
        file_security_context_clear(&copied_context, &shm_name, NULL);
        return FILE_SECURITY_OPERATION_SKIPPED;
    }

    exit_status = (exit_status < FILE_SECURITY_OK || exit_status > FILE_SECURITY_OPERATION_FAILED) ? FILE_SECURITY_OPERATION_FAILED : exit_status; // Check if the exit status is valid

    if ((FileSecurityStatus)exit_status != FILE_SECURITY_OK) g_critical("[ERROR] Helper process returned error: %d", exit_status);

    // Clean up
    file_security_context_clear(&copied_context, &shm_name, NULL);
    log_deletion_attempt(data->path);
    delete_file_data_table_remove(delete_file_table, data); // Remove the data structure from the list
    return (FileSecurityStatus)exit_status;
}

/* Delete threat files */
/*
  * Delete a threat file and remove the delete file data structure from the hash table
*/
FileSecurityStatus
delete_threat_file(GHashTable *delete_file_table, DeleteFileData *data)
{
    g_return_val_if_fail(data != NULL, FILE_SECURITY_OPERATION_FAILED);
    g_return_val_if_fail(data->security_context != NULL, FILE_SECURITY_OPERATION_FAILED);

    /* Delete file */
    FileSecurityStatus result = file_security_secure_delete(data->security_context, data->path, 0);
    if (result != FILE_SECURITY_OK)
    {
        switch (errno)
        {
            case EACCES:
                g_warning("[WARNING] Permission denied, use elevated mode to delete the file");
                return delete_threat_file_elevated(delete_file_table, data);
                break;
            default:
                break;
        }
    }
    else // If the file is deleted successfully, show the success message and add audit log
    {
        g_print("[INFO] File deleted: %s\n", data->path);
        log_deletion_attempt(data->path);
        delete_file_data_table_remove(delete_file_table, data);
    }

    return result;
}

