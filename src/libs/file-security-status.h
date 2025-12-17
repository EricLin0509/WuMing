/* file-security-status.h
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

#ifndef FILE_SECURITY_STATUS_H
#define FILE_SECURITY_STATUS_H

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
    FILE_SECURITY_OPERATION_SKIPPED // Operation skipped
} FileSecurityStatus; // File security status code

#endif /* FILE_SECURITY_STATUS_H */