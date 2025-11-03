/* file-security.h
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

#ifndef FILE_SECURITY_H
#define FILE_SECURITY_H

#include <stdbool.h>
#include <sys/stat.h>

#define FILE_SECURITY_VALIDATE_STRICT 0x10 // Validate the file integrity strictly

typedef struct FileSecurityContext FileSecurityContext;

typedef enum FileSecurityStatus {
    FILE_SECURITY_OK, // File is safe
    FILE_SECURITY_DIR_MODIFIED, // Directory has been modified
    FILE_SECURITY_FILE_MODIFIED, // File has been modified
    FILE_SECURITY_DIR_NOT_FOUND, // Directory not found
    FILE_SECURITY_FILE_NOT_FOUND, // File not found
    FILE_SECURITY_INVALID_PATH, // Invalid path
    FILE_SECURITY_INVALID_CONTEXT, // Invalid file security context
    FILE_SECURITY_PERMISSION_DENIED, // Permission denied
    FILE_SECURITY_OPERATION_FAILED, // Operation failed
    FILE_SECURITY_UNKNOWN_ERROR // Unknown error
} FileSecurityStatus; // File security status code

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
FileSecurityContext *file_security_context_new(const char *path, bool need_shared, char **shared_mem_filepath, int *need_dir_fd);

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
FileSecurityContext *file_security_context_copy(FileSecurityContext *context, bool need_shared, char **shared_mem_filepath);

/* Open a shared memory and get the file security context */
FileSecurityContext *file_security_context_open_shared_mem(const char *shared_mem_filepath);

/* Close the shared memory */
void file_security_context_close_shared_mem(FileSecurityContext **context);

/* Free the file security context */
/*
  * @param context
  * The file security context to be freed
  * @param shared_mem_filepath [OPTIONAL]
  * The shared memory file path to be freed, if it is not `NULL`
  * @param need_dir_fd [OPTIONAL]
  * The file descriptor to be closed, if it is not `NULL`
*/
void file_security_context_clear(FileSecurityContext **context, char **shared_mem_filepath, int *need_dir_fd);

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
FileSecurityStatus file_security_validate(FileSecurityContext *orig_context, FileSecurityContext *new_context, const char *path, int flags);

/* Secure delete the file */
/*
 * @param orig_context
 * The original file security context to be used for validation
 * @param path
 * The path of the file or directory to be deleted
 * @return
 * File security status code
*/
FileSecurityStatus file_security_secure_delete(FileSecurityContext *orig_context, const char *path, int flags);

#endif // FILE_SECURITY_H