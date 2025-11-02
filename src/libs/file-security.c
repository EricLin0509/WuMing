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
#include <sys/mman.h>
#include <sys/random.h>

#ifndef O_PATH // Compatibility for some Linux distributions
#define O_PATH 010000000
#endif

#define PROTECTED_DIRS_PATTERN "^/(etc|dev|sys|proc|var/log)(/|$)" // Pattern for matching protected directories

// Use `O_NOFOLLOW` to avoid following symlinks
#define DIRECTORY_OPEN_FLAGS (O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#define FILE_OPEN_FLAGS (O_RDONLY | O_NOFOLLOW | O_CLOEXEC)

typedef struct FileSecurityContext {
    struct stat dir_stat; // Directory stat, used for checking whether the directory has been modified
    struct stat file_stat; // File stat, used for checking whether the file has been modified
    gboolean is_shared_memory; // Whether this struct using shared memory
} FileSecurityContext; // File security context

/* Open the file securely store the stat information */
static gboolean
secure_open_and_store_stat(FileSecurityContext *context, const gchar *path)
{
    gboolean result = FALSE;

    // Check if the path is valid
    if (!context || !path || !*path)
    {
       g_critical("[SECURITY] Invalid params: ctx:%p path:%s", context, path ? path : "(null)");
       return result;
    }

    // Initialize the common fields
    g_autofree gchar *dir_path = NULL;
    g_autofree gchar *base_name = NULL;
    int dir_fd = -1;
    int file_fd = -1;

    dir_path = g_path_get_dirname(path); // Get the directory path
    base_name = g_path_get_basename(path); // Get the base name of the file

    // Open parent directory
    dir_fd = open(dir_path, DIRECTORY_OPEN_FLAGS);
    if (dir_fd == -1)
    {
        g_critical("[SECURITY] Failed to open directory: %s", dir_path);
        goto clean_up;
    }

    // Get directory stat
    if (fstat(dir_fd, &context->dir_stat) == -1)
    {
        g_critical("[SECURITY] Failed to get directory stat: %s", dir_path);
        goto clean_up;
    }

    // Check again whether the file is symlink
    struct stat symlink_check_stat;
    if (fstatat(dir_fd, base_name, &symlink_check_stat, AT_SYMLINK_NOFOLLOW) == -1)
    {
        g_critical("[SECURITY] Symlink detected: %s", path);
        goto clean_up;
    }

    // Open file
    file_fd = openat(dir_fd, base_name, FILE_OPEN_FLAGS);
    if (file_fd == -1)
    {
        g_critical("[SECURITY] Failed to open file: %s", path);
        goto clean_up;
    }

    // Get file stat
    if (fstat(file_fd, &context->file_stat) == -1)
    {
        g_critical("[SECURITY] Failed to get file stat: %s", path);
        goto clean_up;
    }

    result = TRUE;

clean_up:
    if (dir_fd != -1) close(dir_fd);
    if (file_fd != -1) close(file_fd);

    return result;
}

/* Create a new allocated memory */
static FileSecurityContext *
file_security_context_create_memory(void)
{
    FileSecurityContext *new_context = g_new0(FileSecurityContext, 1);
    if (!new_context)
    {
        g_critical("[SECURITY] Failed to allocate memory for file security context");
        return NULL;
    }

    new_context->is_shared_memory = FALSE;
    return new_context;
}

/* Create a new shared memory */
static FileSecurityContext *
file_security_context_create_shared_memory(char **shared_mem_filepath)
{
    g_return_val_if_fail(shared_mem_filepath, NULL);

    FileSecurityContext *new_context = NULL;

    unsigned long long secure_rand;
    getrandom(&secure_rand, sizeof(secure_rand), 0); // Create a random number for the shared memory name

    *shared_mem_filepath = g_strdup_printf("/file_security_context_%llu", secure_rand); // Create the shared memory file path

    int shm_fd = shm_open(*shared_mem_filepath, O_CREAT | O_EXCL | O_RDWR, 0600); // Create shared memory
    if (shm_fd == -1)
    {
        g_critical("[SECURITY] Failed to create shared memory: %s", *shared_mem_filepath);
        g_clear_pointer(shared_mem_filepath, g_free); // Free the shared memory file path
        return NULL;
    }
    ftruncate(shm_fd, sizeof(FileSecurityContext)); // Resize the shared memory to fit the context

    new_context = mmap(NULL, sizeof(FileSecurityContext), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // Map the shared memory to the context

    if (new_context == MAP_FAILED)
    {
        g_critical("[SECURITY] Failed to map shared memory: %s", *shared_mem_filepath);
        close(shm_fd);
        return NULL;
    }

    close(shm_fd); // Close the shared memory file descriptor
    new_context->is_shared_memory = TRUE; // Set the flag to indicate that this context is using shared memory
    return new_context;
}

static void
file_security_context_destroy_memory(FileSecurityContext **context)
{
    g_return_if_fail(context && *context);

    if ((*context)->is_shared_memory) return; // Check if the context is using shared memory

    g_clear_pointer(context, g_free); // Free the memory
}

static void
file_security_context_destroy_shared_memory(FileSecurityContext **context, char **shared_mem_filepath)
{
    g_return_if_fail(context);

    if (!(*context)->is_shared_memory) return; // Check if the context is using shared memory

    file_security_context_close_shared_mem(context);

    if (shared_mem_filepath == NULL || *shared_mem_filepath == NULL) // If the shared memory file path is not provided, do not destroy the shared memory
    {
        g_warning("[SECURITY] Shared memory file path is not provided, skip destroying shared memory");
        return;
    }

    shm_unlink(*shared_mem_filepath); // Destroy the shared memory
    g_clear_pointer(shared_mem_filepath, g_free); // Free the shared memory file path
}

/* Initialize the file security context */
/*
  * @param path
  * The path of the file or directory to be checked
  * @param need_shared
  * Whether the returned context should using shared memory or not
  * @param shared_mem_filepath [OUT]
  * If `need_shared` is `TRUE`, the shared memory file path will be returned here
  * @return
  * The newly allocated file security context, or `NULL` if an error occurred
*/
FileSecurityContext *
file_security_context_new(const gchar *path, gboolean need_shared, char **shared_mem_filepath)
{
    g_return_val_if_fail(path, NULL);

    FileSecurityContext *new_context = NULL;

    // Allocate memory for the context
    if (need_shared)
    {
        new_context = file_security_context_create_shared_memory(shared_mem_filepath);
    }
    else
    {
        new_context = file_security_context_create_memory();
    }

    if (!new_context) return NULL; // Failed to allocate memory for the context

    if (!secure_open_and_store_stat(new_context, path))
    {
        g_critical("[SECURITY] Failed to initialize file security context");
        file_security_context_clear(&new_context, shared_mem_filepath);
        return NULL;
    }
    
    return new_context;
}

/* Copy the file security context to a new space */
/*
  * @param context
  * The file security context to be copied
  * @param need_shared
  * Whether the returned context should using shared memory or not
  * @param shared_mem_filepath [OUT]
  * If `need_shared` is `TRUE`, the shared memory file path will be returned here
  * @return
  * The newly allocated file security context, or `NULL` if an error occurred
*/
FileSecurityContext *
file_security_context_copy(FileSecurityContext *context, gboolean need_shared, char **shared_mem_filepath)
{
    g_return_val_if_fail(context, NULL);

    FileSecurityContext *new_context = NULL;

    if (need_shared)
    {
        new_context = file_security_context_create_shared_memory(shared_mem_filepath);
    }
    else
    {
        new_context = file_security_context_create_memory();
    }

    if (!new_context) return NULL; // Failed to allocate memory for the context

    // Copy the data
    new_context->dir_stat = context->dir_stat;
    new_context->file_stat = context->file_stat;

    return new_context;
}

/* Open a shared memory and get the file security context */
FileSecurityContext *
file_security_context_open_shared_mem(const gchar *shared_mem_filepath)
{
    g_return_val_if_fail(shared_mem_filepath, NULL);

    FileSecurityContext *new_context = NULL;

    int shm_fd = shm_open(shared_mem_filepath, O_RDWR, 0600); // Open shared memory
    if (shm_fd == -1)
    {
        g_critical("[SECURITY] Failed to open shared memory: %s", shared_mem_filepath);
        return NULL;
    }

    new_context = mmap(NULL, sizeof(FileSecurityContext), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // Map the shared memory to the context

    if (new_context == MAP_FAILED)
    {
        g_critical("[SECURITY] Failed to map shared memory: %s", shared_mem_filepath);
        close(shm_fd);
        return NULL;
    }

    close(shm_fd); // Close the shared memory file descriptor
    return new_context;
}

/* Close the shared memory */
void
file_security_context_close_shared_mem(FileSecurityContext **context)
{
    g_return_if_fail(context && *context);

    if (!(*context)->is_shared_memory) return; // Check if the context is using shared memory

    munmap(*context, sizeof(FileSecurityContext)); // Unmap the shared memory

    *context = NULL; // Set the context to NULL to indicate that the shared memory is closed
}

/* Free the file security context */
void
file_security_context_clear(FileSecurityContext **context, char **shared_mem_filepath)
{
    g_return_if_fail(context && *context);

    if ((*context)->is_shared_memory)
    {
        file_security_context_destroy_shared_memory(context, shared_mem_filepath);
    }
    else
    {
        file_security_context_destroy_memory(context);
    }
}

/* Pattern for matching protected directories */
static gboolean
check_protected_dirs(const gchar *path)
{
    GRegex *regex = g_regex_new(PROTECTED_DIRS_PATTERN, G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT, NULL);
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

/* Validate the path safety */
/*
  * @param path
  * The path to be validated
  * @return
  * `TRUE` if the path is safe, `FALSE` otherwise
*/
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
validate_path_safety(const char *path)
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

/* Check file integrity by comparing the original file stat with the current file stat */
static FileSecurityStatus
validate_file_integrity(FileSecurityContext *orig_context, const gchar *path)
{
    // Create a new context for the file
    FileSecurityContext *new_context = file_security_context_new(path, FALSE, NULL);
    if (!new_context) return FILE_SECURITY_INVALID_PATH;

    // Check if the context is valid
    g_return_val_if_fail(orig_context && new_context, FILE_SECURITY_UNKNOWN_ERROR);

    // Compare the original directory stat with the current directory stat
    const gboolean dir_match = context_compare(&orig_context->dir_stat, &new_context->dir_stat, TRUE);
    if (!dir_match)
    {
        g_critical("[SECURITY] Directory has been modified");
        return FILE_SECURITY_DIR_MODIFIED;
    }

    // Compare the original file stat with the current file stat
    const gboolean file_match = context_compare(&orig_context->file_stat, &new_context->file_stat, FALSE);
    if (!file_match)
    {
        g_critical("[SECURITY] File has been modified");
        return FILE_SECURITY_FILE_MODIFIED;
    }

    return FILE_SECURITY_OK;
}

/* Delete file securely */
/*
  * @param orig_context
  * The `FileSecurityContext` you store before deleting the file
  * @param path
  * The path of the file to be deleted
  * @return
  * the security status code
*/
FileSecurityStatus
delete_file_securely(FileSecurityContext *orig_context, const gchar *path)
{
    FileSecurityStatus status = FILE_SECURITY_OK;

    g_return_val_if_fail(orig_context && path, FILE_SECURITY_INVALID_CONTEXT);

    /* Check if the file has been modified since the last time it was opened */
    status = validate_file_integrity(orig_context, path);
    if (status != FILE_SECURITY_OK)
    {
        g_critical("[SECURITY] Validation failed: %s", path);
        return status;
    }

    /* Delete the file */
    if (unlink(path) == -1)
    {
        g_critical("[SECURITY] Failed to delete %s: %s", path, strerror(errno));
        status = FILE_SECURITY_UNKNOWN_ERROR;
        return status;
    }

    return status;
}