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

#define PROTECTED_DIRS_PATTERN "^/(etc|dev|sys|proc|var/log)(/|$)" // Pattern for matching protected directories

#ifndef O_PATH // Compatibility for glibc
# define O_PATH 010000000
#endif

static FileSecurityContext *
file_security_context_new(const gchar *path)
{

    if (!path || !*path)
    {
        g_critical("[ERROR] Invalid path provided");
        return NULL;
    }

    FileSecurityContext *context = g_new0(FileSecurityContext, 1);
    context->dir_fd = -1;
    context->file_fd = -1;
    context->base_name = NULL;
    context->dir_stat = (struct stat){0};
    context->file_stat = (struct stat){0};

    return context;
}

static void
file_security_context_clear(FileSecurityContext *context)
{
    if (context->dir_fd != -1) close(context->dir_fd);
    if (context->file_fd != -1) close(context->file_fd);
    g_free(context->base_name);
    g_free(context);
}

DeleteFileData *
delete_file_data_new(GtkWidget *action_row)
{
    DeleteFileData *data = g_new0(DeleteFileData, 1);
    data->path = adw_action_row_get_subtitle(ADW_ACTION_ROW(action_row)),
    data->list_box = gtk_widget_get_parent(action_row),
    data->action_row = action_row;
    data->security_context = file_security_context_new(data->path);

    return data;
}

void
delete_file_data_clear(DeleteFileData *data)
{
    g_print("[INFO] Cleaning DeleteFileData \n");
    file_security_context_clear(data->security_context);
    g_free(data);
}

/* SECURITY */
/*
  * These following functions are used for security checks before deleting the file:
  * - `check_protected_dirs()`: check if the path is in protected directories
  * - `calculate_path_depth()`: calculate the depth of the path
  * - `secure_open_and_verify()`: store the original file stat to check whether the file or directory has been modified, if so, cancel the deletion
  * - `validate_by_fd()`: check file integrity using unclosed file descriptor, if so, cancel the deletion
  * - `error_operation()`: when the operation fails, show the error message and disable the action row
  * - `policy_forbid_operation()`: when the operation is blocked by policy, show the error message and disable the action row
  * - `validate_path_safety()`: check if the path is safe to delete
  * - `log_deletion_attempt()`: add audit log for the deletion attempt
*/

/* Pattern for matching protected directories */
static gboolean
check_protected_dirs(const gchar *path)
{
    GRegex *regex = g_regex_new(PROTECTED_DIRS_PATTERN, 0, 0, NULL);
    gboolean matched = g_regex_match(regex, path, 0, NULL);
    g_regex_unref(regex);
    return matched;
}

/* Calculate the depth of the path */
static gint
calculate_path_depth(const gchar *path)
{
    gchar *temp = g_strdup(path);
    gint depth = g_strrstr(temp, "/") ? (gint)g_strv_length(g_strsplit(temp, "/", 0)) - 1 : 0;
    g_free(temp);
    return depth;
}

/* Validate the path safety before deleting the file */
/*
  * These paths should be blocked:
  * Default policies:
  * - / (root directory)
  * - original path contains ".."
  * - /var/log
  * - /etc
  *  - /sys, /proc and other virtual file systems
  * - symlinks
  *  - paths over 10 depths
  * 
  * User configurations (Later versions will add support for this, use GSettings to manage this)
*/
static gboolean
validate_path_safety(char *path)
{
    if (!path || !*path) return FALSE; // Check if the path is valid

    /* Check if the original path contains ".." */
    if (strstr(path, "..") != NULL)
    {
        g_critical("[ERROR] Path contains '..': %s", path);
        return FALSE;
    }

    /* Normalize the path */
    gchar *normalized_path = g_canonicalize_filename(path, NULL);
    if (!normalized_path)
    {
        g_critical("[ERROR] Failed to normalize path: %s", path);
        return FALSE;
    }

    /* Check if the path is root directory `/` */
    if (strcmp(normalized_path, "/") == 0)
    {
        g_critical("[ERROR] Attempted to delete file in root directory: %s", path);
        goto error_clean_up;
    }

    /* Check if the path is in protected directories */
    if (check_protected_dirs(normalized_path))
    {
        g_critical("[ERROR] Attempted to delete file in protected directory: %s", normalized_path);
        goto error_clean_up;
    }

    /* Check if the path is too deep */
    if (calculate_path_depth(normalized_path) > 10)
    {
        g_critical("[ERROR] Path is too deep: %s", normalized_path);
        goto error_clean_up;
    }

    /* Check if the path is a symlink */
    if (g_file_test(normalized_path, G_FILE_TEST_IS_SYMLINK)) // Check if the path is a symlink
    {
        g_critical("[ERROR] Attempted to delete symlink: %s", normalized_path);
        goto error_clean_up;
    }

    /* TODO: Check if the path is in user configuration blacklist */

    g_free(normalized_path);
    return TRUE;

error_clean_up:
    g_free(normalized_path);
    return FALSE;
}

/* Open the file securely and KEEP discriptor open */
static gboolean
secure_open_and_verify(FileSecurityContext *context, const gchar *path)
{
    if (!context || !path)
    {
        g_critical("[ERROR] Invalid file security context or path");
        return FALSE;
    }

    // Separate the file name and directory
    gchar *dir_path = g_path_get_dirname(path); // Get the directory path
    context->base_name = g_path_get_basename(path); // Get the base name of the file

    // Open the parent directory
    context->dir_fd = open(dir_path, O_PATH | O_DIRECTORY | O_NOFOLLOW); // use `O_NOFOLLOW` to avoid following symlinks
    g_free(dir_path);
    if (context->dir_fd == -1)
    {
        g_critical("[ERROR] Failed to open directory: %s", path);
        return FALSE;
    }

    // Get the original file stat
    if (fstat(context->dir_fd, &context->dir_stat) == -1) // Get the directory stat
    {
        g_critical("[ERROR] Failed to get directory stat");
        close(context->dir_fd);
        return FALSE;
    }

    // Open the file
    context->file_fd = openat(context->dir_fd, context->base_name, O_RDONLY | O_NOFOLLOW); // use `O_NOFOLLOW` to avoid following symlinks
    if (context->file_fd == -1)
    {
        g_critical("[ERROR] Failed to open file: %s", path);
        close(context->dir_fd);
        return FALSE;
    }

    // Save the original file stat
    if (fstat(context->file_fd, &context->file_stat) == -1) // Get the file stat
    {
        g_critical("[ERROR] Failed to get file stat");
        close(context->file_fd);
        close(context->dir_fd);
        return FALSE;
    }

    return TRUE;
}

/* Check file integrity using unclosed file descriptor */
static gboolean
validate_by_fd(FileSecurityContext *context)
{
    if (!context)
    {
        g_critical("[ERROR] Invalid file security context");
        return FALSE;
    }

    if (context->dir_fd == -1 || context->file_fd == -1)
    {
        g_critical("[ERROR] Invalid file descriptors");
        return FALSE;
    }

    struct stat current_dir_stat, current_file_stat;
    int new_fd = -1; // New file descriptor to reopen the file, use for comparison

    // Check whether the directory integrity
    if (fstat(context->dir_fd, &current_dir_stat) == -1 || 
        current_dir_stat.st_dev != context->dir_stat.st_dev ||
        current_dir_stat.st_ino != context->dir_stat.st_ino) 
    {
        g_critical("[ERROR] Directory has been modified!");
        goto validation_failed;
    }

    // Reopen the file and get the current stat
    if ((new_fd = openat(context->dir_fd, context->base_name, 
                       O_RDONLY | O_NOFOLLOW)) == -1) 
    {
        g_critical("[ERROR] File reopened failed: %s", strerror(errno));
        goto validation_failed;
    }

    // Get new file stat
    if (fstat(new_fd, &current_file_stat) == -1)
    {
        g_critical("[ERROR] New fd stat failed");
        close(new_fd);
        goto validation_failed;
    }

    // Metadata comparison
    const gboolean descriptor_match = 
        (current_file_stat.st_ino == context->file_stat.st_ino) &&
        (current_file_stat.st_dev == context->file_stat.st_dev);

    // Content comparison
    const gboolean content_match = 
        (current_file_stat.st_size == context->file_stat.st_size) &&
        (current_file_stat.st_mtime == context->file_stat.st_mtime);

    close (new_fd);

    if (!descriptor_match || !content_match)
    {
        g_critical("[SECURITY] File replaced or modified!");
        return FALSE;
    }

    // Final verification of the original descriptor
    if (fstat(context->file_fd, &current_file_stat) == -1 ||
        current_file_stat.st_ino != context->file_stat.st_ino ||
        current_file_stat.st_size != context->file_stat.st_size) 
    {
        g_critical("[SECURITY] File descriptor compromised!");
        return FALSE;
    }

    return TRUE;

validation_failed:
    if (new_fd != -1) close(new_fd);
    return FALSE;
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