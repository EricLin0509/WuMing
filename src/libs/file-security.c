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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/random.h>

#include "file-security.h"
#include "path-operations.h"

#ifndef O_PATH // Compatibility for some Linux distributions
#define O_PATH 010000000
#endif

#define SHARED_MEM_FALLBACK_RANDOM_NUM 4519921969881885362 // Fallback random number for shared memory if getrandom() is failed
#define SHARED_MEM_FILE_PATH_LEN 44 // 23 ("/file_security_context") + (20 random number) + (null terminator)

// Use `O_NOFOLLOW` to avoid following symlinks
#define DIRECTORY_OPEN_FLAGS (O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#define FILE_OPEN_FLAGS (O_RDONLY | O_NOFOLLOW | O_CLOEXEC)

typedef struct FileSecurityContext {
    struct stat dir_stat; // Directory status
    struct stat file_stat; // file status

    bool is_shared_memory; // Indicate whether is shared memory
} FileSecurityContext;

/* Take the snapshot of the directory and file status */
static bool file_security_context_take_snapshot(FileSecurityContext *context, const char *path, int *need_dir_fd)
{
    if (context == NULL || path == NULL || *path == '\0') return false;

    char *dir_name = NULL;
    char *file_name = NULL;

    bool is_operation_success = true;

    int dir_fd = -1;
    int file_fd = -1;

    if (is_operation_success && !get_file_dir_name(path, &file_name, &dir_name))
    {
        fprintf(stderr, "[ERROR] Failed to get file and directory name\n");
        is_operation_success = false;
    }

    if (is_operation_success && (dir_fd = open(dir_name, DIRECTORY_OPEN_FLAGS)) == -1)
    {
        fprintf(stderr, "[ERROR] Failed to open directory: %s\n", dir_name);
        is_operation_success = false;
    }
    
    /* Get directory status */
    if (is_operation_success && fstat(dir_fd, &context->dir_stat) == -1)
    {
        fprintf(stderr, "[ERROR] Failed to get directory status: %s\n", dir_name);
        is_operation_success = false;
    }

    /* Get file status */
    if (is_operation_success && (file_fd = openat(dir_fd, file_name, FILE_OPEN_FLAGS)) == -1)
    {
        fprintf(stderr, "[ERROR] Failed to open file: %s\n", file_name);
        is_operation_success = false;
    }

    if (is_operation_success && fstat(file_fd, &context->file_stat) == -1)
    {
        fprintf(stderr, "[ERROR] Failed to get file status: %s\n", file_name);
        is_operation_success = false;
    }

    /* Get the file descriptor if needed */
    if (need_dir_fd != NULL && is_operation_success && dir_fd != -1) *need_dir_fd = dir_fd;
    else if (dir_fd != -1) close(dir_fd);
    dir_fd = -1; // Reset the file descriptor

    /* Clean up */
    if (file_fd != -1)
    {
        close(file_fd);
        file_fd = -1;
    }
    if (dir_name != NULL)
    {
        free(dir_name);
        dir_name = NULL;
    }
    if (file_name != NULL)
    {
        free(file_name);
        file_name = NULL;
    }
    
    return is_operation_success;
}

/* Create a new allocated memory */
static FileSecurityContext *file_security_context_create_memory(void)
{
    FileSecurityContext *context = calloc(1, sizeof(FileSecurityContext));
    if (context == NULL)
    {
        fprintf(stderr, "[ERROR] Failed to allocate memory for file security context\n");
        return NULL;
    }

    context->is_shared_memory = false;

    return context;
}

/* Create a new shared memory */
static FileSecurityContext *file_security_context_create_shared_memory(char **shared_mem_filepath)
{
    if (shared_mem_filepath == NULL) return NULL;

    FileSecurityContext *context = NULL;

    /* Create a new random number */
    uint64_t secure_rand;
    if (getrandom(&secure_rand, sizeof(secure_rand), 0) != sizeof(secure_rand)) // Create a random number for the shared memory name
    {
        fprintf(stderr, "[WARNING] getrandom() failed, using fallback random number\n");
        secure_rand = SHARED_MEM_FALLBACK_RANDOM_NUM;
    }

    *shared_mem_filepath = calloc(SHARED_MEM_FILE_PATH_LEN, sizeof(char));
    snprintf(*shared_mem_filepath, SHARED_MEM_FILE_PATH_LEN, "/file_security_context_%" PRIu64, secure_rand);

    /* Create the shared memory */
    int shm_fd = shm_open(*shared_mem_filepath, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd == -1)
    {
        fprintf(stderr, "[ERROR] Failed to create shared memory\n");
        free(*shared_mem_filepath);
        return NULL;
    }

    /* Set the secure flags for shm_fd */
    if (fcntl(shm_fd, F_SETFD, FD_CLOEXEC) == -1)
    {
        fprintf(stderr, "[ERROR] Failed to set secure flags for shared memory\n");
        close(shm_fd);
        shm_unlink(*shared_mem_filepath);
        free(*shared_mem_filepath);
        return NULL;
    }

    /* Set the size of the shared memory */
    if (ftruncate(shm_fd, sizeof(FileSecurityContext)) == -1)
    {
        fprintf(stderr, "[ERROR] Failed to set size of shared memory\n");
        close(shm_fd);
        shm_unlink(*shared_mem_filepath);
        free(*shared_mem_filepath);
        return NULL;
    }

    /* Map the shared memory */
    context = mmap(NULL, sizeof(FileSecurityContext), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (context == MAP_FAILED)
    {
        fprintf(stderr, "[ERROR] Failed to map shared memory\n");
        close(shm_fd);
        shm_unlink(*shared_mem_filepath);
        free(*shared_mem_filepath);
        return NULL;
    }

    /* Mark the context as shared memory */
    context->is_shared_memory = true;

    return context;
}

/* Clear the memory of the file security context */
static void file_security_context_destroy_memory(FileSecurityContext **context)
{
    if (context == NULL || *context == NULL) return; // Check if the context is valid

    if ((*context)->is_shared_memory) return; // Check if the context is using dynamic allocated memory

    free(*context); // Free the memory
    *context = NULL; // Set the context to NULL to indicate that the memory is freed
}

/* Clear the shared memory of the file security context */
static void file_security_context_destroy_shared_memory(FileSecurityContext **context, char **shared_mem_filepath)
{
    if (context == NULL || *context == NULL) return; // Check if the context is valid

    if (!(*context)->is_shared_memory) return; // Check if the context is using shared memory

    file_security_context_close_shared_mem(context);

    if (shared_mem_filepath == NULL || *shared_mem_filepath == NULL) // If the shared memory file path is not provided, do not destroy the shared memory
    {
        fprintf(stderr, "[WARNING] Shared memory file path is not provided, skip destroying shared memory\n");
        return;
    }

    shm_unlink(*shared_mem_filepath); // Destroy the shared memory

    free(*shared_mem_filepath); // Free the shared memory file path
    *shared_mem_filepath = NULL; // Set the shared memory file path to NULL to indicate that the memory is freed
}

/* Initialize the file security context */
/*
  * @param path
  * The path of the file or directory to be checked
  * @param need_shared
  * Whether the returned context should using shared memory or not
  * @param shared_mem_filepath [OUT]
  * If `need_shared` is `TRUE`, the shared memory file path will be returned here
  * @param  need_dir_fd [OUT] [OPTIONAL]
  * If this parameter is not `NULL`, the file descriptor of the file will be returned here, especially for the case that need further operations on the file
  * @return
  * The newly allocated file security context, or `NULL` if an error occurred
*/
FileSecurityContext *file_security_context_new(const char *path, bool need_shared, char **shared_mem_filepath, int *need_dir_fd)
{
    if (path == NULL) return NULL;

    FileSecurityContext *new_context = NULL;

    if (need_shared)
    {
        new_context = file_security_context_create_shared_memory(shared_mem_filepath);
    }
    else
    {
        new_context = file_security_context_create_memory();
    }

    if (new_context == NULL) return NULL; // Check if the context is allocated successfully

    /* Snapshot the directory and file status */
    if (!file_security_context_take_snapshot(new_context, path, need_dir_fd))
    {
        file_security_context_clear(&new_context, shared_mem_filepath, need_dir_fd);
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
FileSecurityContext *file_security_context_copy(FileSecurityContext *context, bool need_shared, char **shared_mem_filepath)
{
    if (context == NULL) return NULL;

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

    /* Copy the data */
    new_context->dir_stat = context->dir_stat;
    new_context->file_stat = context->file_stat;

    return new_context;
}

/* Free the file security context */
/*
  * @param context
  * The file security context to be freed
  * @param shared_mem_filepath [OPTIONAL]
  * The shared memory file path to be freed, if it is not `NULL`
  * @param need_dir_fd [OPTIONAL]
  * The file descriptor to be closed, if it is not `NULL`
*/
void file_security_context_clear(FileSecurityContext **context, char **shared_mem_filepath, int *need_dir_fd)
{
    if (context == NULL || *context == NULL) return; // Check if the context is valid

    if ((*context)->is_shared_memory)
    {
        file_security_context_destroy_shared_memory(context, shared_mem_filepath);
    }
    else
    {
        file_security_context_destroy_memory(context);
    }

    if (need_dir_fd != NULL && *need_dir_fd != -1) // Close the file descriptor if it is provided
    {
        close(*need_dir_fd);
        *need_dir_fd = -1;
    }
}

/* Open a shared memory and get the file security context */
FileSecurityContext *file_security_context_open_shared_mem(const char *shared_mem_filepath)
{
    if (shared_mem_filepath == NULL) return NULL;

    FileSecurityContext *context = NULL;

    int shm_fd = shm_open(shared_mem_filepath, O_RDWR, 0600); // Open shared memory
    if (shm_fd == -1)
    {
        fprintf(stderr, "[SECURITY] Failed to open shared memory: %s", shared_mem_filepath);
        return NULL;
    }

    context = mmap(NULL, sizeof(FileSecurityContext), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // Map the shared memory to the context

    if (context == MAP_FAILED)
    {
        fprintf(stderr, "[SECURITY] Failed to map shared memory: %s", shared_mem_filepath);
        close(shm_fd);
        return NULL;
    }

    close(shm_fd); // Close the shared memory file descriptor
    return context;
}

/* Close the shared memory */
void file_security_context_close_shared_mem(FileSecurityContext **context)
{
    if (context == NULL || *context == NULL) return; // Check if the context is valid

    if (!(*context)->is_shared_memory) return; // Check if the context is using shared memory

    munmap(*context, sizeof(FileSecurityContext)); // Unmap the shared memory

    *context = NULL; // Set the context to NULL to indicate that the shared memory is closed
}

/* Compare the original file stat with the current file stat */
/*
  * If `TRUE`, the file has not been modified since the last time it was opened
  * If `FALSE`, the file has been modified since the last time it was opened or the invalid parameters were provided
*/
static bool
context_compare(const struct stat *original_stat, const struct stat *current_stat, const bool is_check_directory, int flags)
{
    if (original_stat == NULL || current_stat == NULL) return false;

    /* If checking a directory and not use strict mode, only compare the device ID */
    if (is_check_directory && (flags & FILE_SECURITY_VALIDATE_STRICT) == 0)
        return (original_stat->st_dev == current_stat->st_dev);
    
    /* Otherwise, compare all the metadata, content, create time, modification time */

    // Metadata comparison
    const bool descriptor_match = 
        (original_stat->st_dev == current_stat->st_dev &&
         original_stat->st_ino == current_stat->st_ino);

    // Content comparison
    const bool content_match = 
        (original_stat->st_nlink == current_stat->st_nlink &&
         original_stat->st_size == current_stat->st_size);

    // Create time comparison
    const bool create_time_match = 
        (original_stat->st_ctime == current_stat->st_ctime &&
         original_stat->st_ctim.tv_nsec == current_stat->st_ctim.tv_nsec);

    // Modification time comparison
    const bool modification_time_match = 
        (original_stat->st_mtime == current_stat->st_mtime &&
         original_stat->st_mtim.tv_nsec == current_stat->st_mtim.tv_nsec);

    return descriptor_match && content_match && create_time_match && modification_time_match;
}

/* Validate the file integrity */
/*
  * @param orig_context
  * The original file security context
  * @param new_context
  * The new file security context, or `NULL` to create a new context
  * @param path
  * The path of the file or directory to be checked
  * @param flags
  * The validation flags
  * @return
  * The validation status code
  * 
  * @warning
  * if `new_context` is `NULL` and `path` is not a valid, the function will return `FILE_SECURITY_INVALID_PATH`
*/
FileSecurityStatus file_security_validate(FileSecurityContext *orig_context, FileSecurityContext *new_context, const char *path, int flags)
{
    if (orig_context == NULL) return FILE_SECURITY_INVALID_CONTEXT;
    if (flags & FILE_SECURITY_VALIDATE_STRICT)
    {
        fprintf(stdout, "[INFO] Using strict mode for file validation\n");
    }

    bool is_valid = true; // Check if the file is valid
    FileSecurityStatus status = FILE_SECURITY_OK; // The validation status code

    bool has_new_context = (new_context != NULL); // Check if the new context is provided
    new_context = has_new_context ? new_context : file_security_context_new(path, false, NULL, NULL); // Create a new context if needed

    if (new_context == NULL) return FILE_SECURITY_INVALID_PATH; // Failed to take a new snapshot of the directory and file status

    /* Check if the directory and file status has changed */
    // Compare the original directory stat with the current directory stat
    const bool dir_match = context_compare(&orig_context->dir_stat, &new_context->dir_stat, true, flags);
    if (is_valid && !dir_match)
    {
        fprintf(stderr, "[SECURITY] Directory has been modified\n");
        is_valid = false;
        status = FILE_SECURITY_DIR_MODIFIED;
    }

    // Compare the original file stat with the current file stat
    const bool file_match = context_compare(&orig_context->file_stat, &new_context->file_stat, false, flags);
    if (is_valid && !file_match)
    {
        fprintf(stderr, "[SECURITY] File has been modified\n");
        is_valid = false;
        status = FILE_SECURITY_FILE_MODIFIED;
    }

    if (!has_new_context) file_security_context_clear(&new_context, NULL, NULL); // Free the new context if it is not provided
    return status;
}

/* Secure delete the file */
/*
 * @param orig_context
 * The original file security context to be used for validation
 * @param path
 * The path of the file or directory to be deleted
 * @param flags
 * The validation flags
 * @return
 * File security status code
*/
FileSecurityStatus file_security_secure_delete(FileSecurityContext *orig_context, const char *path, int flags)
{
    if (orig_context == NULL || path == NULL || *path == '\0') return FILE_SECURITY_INVALID_CONTEXT;

    bool is_valid = true; // Check if the file is valid
    FileSecurityStatus status = FILE_SECURITY_OK; // The validation status code

    /* Create a new context for the file or directory to be deleted */
    int dir_fd = -1; // Store the file descriptor for `unlinkat`
    FileSecurityContext *new_context = file_security_context_new(path, false, NULL, &dir_fd);

    if (new_context == NULL) return FILE_SECURITY_INVALID_PATH; // Failed to take a new snapshot of the directory and file status

    /* Check if the directory and file status has changed */
    status =  file_security_validate(orig_context, new_context, NULL, flags);
    if (is_valid && status != FILE_SECURITY_OK)
    {
        fprintf(stderr, "[SECURITY] Failed to validate the file: %s\n", path);
        is_valid = false;
    }

    if (is_valid && unlinkat(dir_fd, path, 0) == -1)
    {
        fprintf(stderr, "[SECURITY] Failed to delete the file: %s\n", path);
        status = FILE_SECURITY_OPERATION_FAILED;
        is_valid = false;
    }

    file_security_context_clear(&new_context, NULL, &dir_fd); // Free the new context and the file descriptor
    return status;
}