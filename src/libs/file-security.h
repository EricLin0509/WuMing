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

#pragma once

#include <sys/stat.h>
#include <glib.h>

typedef struct {
    int dir_fd; // Directory file descriptor
    int file_fd; // Use for checking whether the file is modified
    char *base_name; // Base name of the file used by `unlinkat()`
    struct stat dir_stat; // Directory stat, used for checking whether the directory has been modified
    struct stat file_stat; // File stat, used for checking whether the file has been modified
} FileSecurityContext; // File security context

typedef enum {
    FILE_SECURITY_OK, // File is safe
    FILE_SECURITY_DIR_MODIFIED, // Directory has been modified
    FILE_SECURITY_FILE_MODIFIED, // File has been modified
    FILE_SECURITY_DIR_NOT_FOUND, // Directory not found
    FILE_SECURITY_FILE_NOT_FOUND, // File not found
    FILE_SECURITY_INVALID_PATH, // Invalid path
} FileSecurityStatus; // File security status

/* Initialize the file security context */
FileSecurityContext *
file_security_context_new(const gchar *path);

/* Free the file security context */
void
file_security_context_clear(FileSecurityContext *context);

/* Validate the path safety */
gboolean
validate_path_safety(char *path);

/* Open the file securely and KEEP discriptor open */
gboolean
secure_open_and_verify(FileSecurityContext *context, const gchar *path);

/* Check file integrity using unclosed file descriptor */
FileSecurityStatus
validate_by_fd(FileSecurityContext *context);