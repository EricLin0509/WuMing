/* file-security.c
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

#include "file-security.h"

#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <fcntl.h>

#ifndef O_PATH // Compatibility for some Linux distributions
#define O_PATH 010000000
#endif

#define PROTECTED_DIRS_PATTERN "^/(etc|dev|sys|proc|var/log)(/|$)" // Pattern for matching protected directories

// Use `O_NOFOLLOW` to avoid following symlinks
#define DIRECTORY_OPEN_FLAGS (O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#define FILE_OPEN_FLAGS (O_RDONLY | O_NOFOLLOW | O_CLOEXEC)

/* Initialize the file security context */
FileSecurityContext *
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

/* Free the file security context */
void
file_security_context_clear(FileSecurityContext *context)
{
    if (context->dir_fd != -1) close(context->dir_fd);
    if (context->file_fd != -1) close(context->file_fd);

    g_clear_pointer(&context->base_name, g_free);

    g_free(context);
}

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
gboolean
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
// Warning: The file descriptor will remain open until the function `file_security_context_clear` is called
gboolean
secure_open_and_verify(FileSecurityContext *context, const gchar *path)
{
    // Check if the path is valid
    if (!context || !path || !*path || context->dir_fd != -1)
    {
       g_critical("[SECURITY] Invalid params: ctx:%p path:%s", context, path ? path : "(null)");
       return FALSE;
    }

    g_autofree gchar *dir_path = NULL;

    dir_path = g_path_get_dirname(path); // Get the directory path
    context->base_name = g_path_get_basename(path); // Get the base name of the file

    // Open the parent directory
    context->dir_fd = open(dir_path, DIRECTORY_OPEN_FLAGS); // use `O_NOFOLLOW` to avoid following symlinks
    if (context->dir_fd == -1)
    {
        g_critical("[ERROR] Failed to open directory: %s", dir_path);
        g_critical("[SECURITY] open(%s) failed: %s", dir_path, strerror(errno));
        return FALSE;
    }

    // Get the original file stat
    if (fstat(context->dir_fd, &context->dir_stat) == -1) // Get the directory stat
    {
        g_critical("[SECURITY] fstat(%d) failed: %s", context->dir_fd, strerror(errno));
        goto error_clean_up;
    }

    // Check again whether the file is symlink
    struct stat symlink_check_stat;
    if (fstatat(context->dir_fd, context->base_name, &symlink_check_stat, AT_SYMLINK_NOFOLLOW) == -1)
    {
        g_critical("[SECURITY] fstatat(%d, %s) failed: %s", context->dir_fd, context->base_name, strerror(errno));
        goto error_clean_up;
        
        if (S_ISLNK(symlink_check_stat.st_mode)) // Check if the file is a symlink
        {
            g_critical("[SECURITY] Symlink detected after open");
            goto error_clean_up;
        }
    }

    // Open the file
    context->file_fd = openat(context->dir_fd, context->base_name, FILE_OPEN_FLAGS);
    if (context->file_fd == -1)
    {
        g_critical("[ERROR] Failed to open file: %s", path);
        goto error_clean_up;
    }

    // Save the original file stat
    if (fstat(context->file_fd, &context->file_stat) == -1) // Get the file stat
    {
        g_critical("[ERROR] Failed to get file stat");
        goto error_clean_up;
    }

    return TRUE;

error_clean_up:
    if (context->dir_fd != -1)
    {
        close(context->dir_fd);
        context->dir_fd = -1;
    }
    if (context->file_fd != -1)
    {
        close(context->file_fd);
        context->file_fd = -1;
    }
    g_clear_pointer(&context->base_name, g_free);
    return FALSE;
}

/* Compare the original file stat with the current file stat */
/*
  * If `TRUE`, the file has not been modified since the last time it was opened
  * If `FALSE`, the file has been modified since the last time it was opened or the invalid parameters were provided
*/
static gboolean
context_compare(const struct stat *original_stat, const struct stat *current_stat, const gboolean is_check_directory)
{
    if (!original_stat || !current_stat)
    {
        g_critical("[SECURITY] Invalid params! original_stat:%p current_stat:%p", original_stat, current_stat);
        return FALSE;
    }

    /* If checking a directory, only compare the device ID */
    if (is_check_directory) return (original_stat->st_dev == current_stat->st_dev);

    /* Otherwise, compare all the metadata, content, create time, modification time */

    // Metadata comparison
    const gboolean descriptor_match = 
        (original_stat->st_dev == current_stat->st_dev &&
         original_stat->st_ino == current_stat->st_ino);

    // Content comparison
    const gboolean content_match = 
        (original_stat->st_nlink == current_stat->st_nlink &&
         original_stat->st_size == current_stat->st_size);

    // Create time comparison
    const gboolean create_time_match = 
        (original_stat->st_ctime == current_stat->st_ctime &&
         original_stat->st_ctim.tv_nsec == current_stat->st_ctim.tv_nsec);

    // Modification time comparison
    const gboolean modification_time_match = 
        (original_stat->st_mtime == current_stat->st_mtime &&
         original_stat->st_mtim.tv_nsec == current_stat->st_mtim.tv_nsec);

    return descriptor_match && content_match && create_time_match && modification_time_match;
}

static void
close_new_fd(int *new_dir_fd, int *new_file_fd)
{
    if (*new_dir_fd != -1) close(*new_dir_fd);
    if (*new_file_fd != -1) close(*new_file_fd);
}

/* Check file integrity using unclosed file descriptor */
FileSecurityStatus
validate_by_fd(FileSecurityContext *context)
{
    // Check if the context is valid
    if (!context || context->dir_fd == -1 || context->file_fd == -1)
    {
        g_critical("[SECURITY] Invalid context: dir_fd=%d file_fd=%d", 
                    context ? context->dir_fd : -1, 
                    context ? context->file_fd : -1);
        return FILE_SECURITY_INVALID_PATH;
    }

    struct stat current_stat; // Store the current file stat
    int new_dir_fd = -1; // Store the new directory fd
    int new_file_fd = -1; // Store the new file fd

    // Reopen the directory
    new_dir_fd = openat(context->dir_fd, ".", DIRECTORY_OPEN_FLAGS); // "." means the current directory
    if (new_dir_fd == -1)
    {
        g_critical("[SECURITY] Failed to reopen directory: %s", strerror(errno));
        close_new_fd(&new_dir_fd, &new_file_fd);
        return FILE_SECURITY_DIR_NOT_FOUND;
    }

    if (fstat(new_dir_fd, &current_stat) == -1
        || !context_compare(&context->dir_stat, &current_stat, TRUE)) // Check if the directory has been modified
    {
        g_critical("[SECURITY] Directory has been modified");
        close_new_fd(&new_dir_fd, &new_file_fd);
        return FILE_SECURITY_DIR_MODIFIED;
    }

    // Reopen the file
    new_file_fd = openat(new_dir_fd, context->base_name, FILE_OPEN_FLAGS);
    if (new_file_fd == -1)
    {
        g_critical("[SECURITY] Failed to reopen file: %s", strerror(errno));
        close_new_fd(&new_dir_fd, &new_file_fd);
        return FILE_SECURITY_FILE_NOT_FOUND;
    }

    if (fstat(new_file_fd, &current_stat) == -1
        || !context_compare(&context->file_stat, &current_stat, FALSE)) // Check if the file has been modified
    {
        g_critical("[SECURITY] File has been modified");
        close_new_fd(&new_dir_fd, &new_file_fd);
        return FILE_SECURITY_FILE_MODIFIED;
    }


    // Close the new file and directory fd
    close_new_fd(&new_dir_fd, &new_file_fd);

    return FILE_SECURITY_OK;
}