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
#include <sys/wait.h>
#include <sys/random.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "delete-file.h"
#include "wuming-unlinkat-helper.h"
#include "subprocess-components.h"

// Warning: this macro should be started and ended with a space, otherwise may cause comparison error
#define SYSTEM_DIRECTORIES " /usr /lib /lib64 /etc /opt /var /sys /proc " // System directories that should be warned before deleting

#define CLAMP(x, low, high)( (x) < (low) ? (low) : ((x) > (high) ? (high) : (x)) );

#define PKEXEC_PATH "/usr/bin/pkexec" // Path to the `pkexec` binary

/* Create a new delete file data structure */
// Tips: this also creates a new security context for the file
// Warning: the AdwActionRow MUST be added to the GtkListBox before calling this function
DeleteFileData *
delete_file_data_new(GtkWidget *list_box, GtkWidget *action_row)
{
    if (!list_box || !GTK_IS_WIDGET(list_box) ||
       !action_row || !GTK_IS_WIDGET(action_row) || 
       gtk_widget_get_parent(action_row) != list_box) // Check if the action_row is a child of the list_box
    {
        g_critical("[ERROR] Invalid parameters, list_box: %p, action_row: %p", list_box, action_row);
        return NULL;
    }

    DeleteFileData *data = g_new0(DeleteFileData, 1);
    data->path = adw_action_row_get_subtitle(ADW_ACTION_ROW(action_row)), // This string SHOULDN'T be freed because it's owned by the action row
    data->list_box = list_box;
    data->action_row = action_row;
    data->security_context = file_security_context_new(data->path);

    return data;
}

/* Clear the delete file data structure */
// Tips: this also clears the security context for the file
void
delete_file_data_clear(DeleteFileData *data)
{
    g_return_if_fail(data != NULL);

    file_security_context_clear(data->security_context);
    g_free(data);

    data = NULL;
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

/* Generate a random key for authentication */
static uint32_t
generate_auth_key(void)
{
    uint32_t key = 0;
    if (getrandom(&key, sizeof(key), 0) != sizeof(key))
    {
        g_warning("[WARNING] Failed to generate random key, using default key!");
        return DEFAULT_AUTH_KEY;
    }
    return key;
}

/* Delete threat files in elevated mode */
static void
delete_threat_file_elevated(DeleteFileData *data)
{
    g_return_if_fail(data && data->security_context);

    // Generate the auth key for authentication
    uint32_t gen_key = generate_auth_key();

    char key_str[16]; // Use for passing the key through the command line
    snprintf(key_str, sizeof(key_str), "%" PRIu32, gen_key);

    /* Prepare the data to send */
    HelperData helper_data = {
        .auth_key = gen_key,
        .shm_magic = SHM_MAGIC,

        .security_context = *(data->security_context),
    };

    // Create a shared memory for passing data between the helper process and the main process
    unsigned short secure_rand;
    getrandom(&secure_rand, sizeof(secure_rand), 0); // Create a random number for the shared memory name

    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/wuming_shm_%d_%d", getpid(), secure_rand);

    // Configure the shared memory
    int shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd == -1)
    {
        g_critical("[ERROR] Failed to create shared memory: %s", strerror(errno));
        error_operation(data, FILE_SECURITY_UNKNOWN_ERROR);
        return;
    }
    ftruncate(shm_fd, HELPER_DATA_SIZE); // Set the size of the shared memory

    HelperData *shm_data = mmap(NULL, HELPER_DATA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // Map the shared memory
    if (shm_data == MAP_FAILED) // Check if the mapping is successful
    {
        g_critical("[ERROR] Failed to map shared memory: %s", strerror(errno));
        error_operation(data, FILE_SECURITY_UNKNOWN_ERROR);
        shm_unlink(shm_name);
        return;
    }

    *shm_data = helper_data; // Copy the helper data to the shared memory
    munmap(shm_data, HELPER_DATA_SIZE); // Unmap the shared memory
    close(shm_fd);

    pid_t pid;

    spawn_new_process_no_pipes(&pid, PKEXEC_PATH, "pkexec", HELPER_PATH, // `HELPER_PATH` is defined in `meson.build`
                                    shm_name, data->path, key_str, NULL); // Spawn the helper process

    int exit_status = pid == -1 ? FILE_SECURITY_UNKNOWN_ERROR : wait_for_process(pid); // Wait for the helper process to finish
    exit_status = CLAMP(exit_status, FILE_SECURITY_OK, FILE_SECURITY_UNKNOWN_ERROR); // Clamp the exit status to the valid range

    if ((FileSecurityStatus)exit_status != FILE_SECURITY_OK)
    {
        g_critical("[ERROR] Helper process returned error: %d", exit_status);
        goto error_clean_up;
    }

    // Clean up
    shm_unlink(shm_name);
    gtk_list_box_remove(GTK_LIST_BOX(data->list_box), data->action_row); // Remove the action row from the list view
    log_deletion_attempt(data->path);
    return;

error_clean_up:
    shm_unlink(shm_name);
    error_operation(data, (FileSecurityStatus)exit_status);
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

    /* First check if the path is safe to delete */
    if (!validate_path_safety(data->path))
    {
        policy_forbid_operation(data);
        return;
    }

    /* Delete file */
    FileSecurityStatus result = delete_file_securely(data->security_context, data->path);
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
        gtk_list_box_remove(GTK_LIST_BOX(data->list_box), data->action_row); // Remove the action row from the list view
        log_deletion_attempt(data->path);
    }
}