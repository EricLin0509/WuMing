/* path-operation.c
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

#include <stdlib.h>
#include <string.h>

#include "path-operations.h"

/* Normalizes the path by removing the trailing slash */
/*
  @warning: the returned string will be allocated dynamically, so you should free it after use.
*/
static char *normalize_path(const char *path)
{
    if (path == NULL) return NULL;

    if (memcmp(path, "/", 2) == 0) return strndup(path, 1); // if the path is the root directory, return it

    char *normalizd_path = calloc(strlen(path) + 1, sizeof(char));
    if (normalizd_path == NULL) return NULL;

    const char *src = path; // the source pointer
    char *dest = normalizd_path; // the destination pointer
    char prev_char = '\0'; // the previous character

    while (*src != '\0')
    {
        if (*src == '/' && prev_char == '/') // skip multiple slashes
        {
            src++;
            continue;
        }

        /* Write the current character to the destination */
        prev_char = *src;
        *dest = *src;

        /* Move to the next character */
        src++;
        dest++;
    }
    *dest = '\0'; // add the null terminator

    /* Remove the trailing slash if is not the root directory */
    size_t length = dest - normalizd_path;
    if (length > 1 && normalizd_path[length - 1] == '/') normalizd_path[length - 1] = '\0'; // remove the trailing slash

    return normalizd_path;
}

/* Get the file name and dir name */
/*
  * @param path: the path of the file or directory
  * @param file_name: the pointer to store the file name
  * @param dir_name: the pointer to store the directory name
  * 
  * @return: true if success, false if failed
*/
bool get_file_dir_name(const char *path, char **file_name, char **dir_name)
{
    if (path == NULL || file_name == NULL || dir_name == NULL) return false;

    char *normalized_path = normalize_path(path); // normalize the path
    if (normalized_path == NULL) return false;

    char *last_slash = strrchr(normalized_path, '/'); // find the last slash
    if (last_slash == NULL) // if there is no slash, it is a file name
    {
        *file_name = normalized_path;
        *dir_name = strdup(".");
        return (*dir_name != NULL && *file_name != NULL);
    }

    if (last_slash == normalized_path) // if the last slash is the first character, it is a root directory
    {
        *file_name = strdup("");
        *dir_name = normalized_path;
        return (*dir_name != NULL && *file_name != NULL);
    }

    *file_name = strndup(last_slash + 1, strlen(normalized_path) - (last_slash - normalized_path)); // get the file name
    if (*file_name == NULL)
    {
        free(normalized_path);
        normalized_path = NULL;
        return false;
    }

    *dir_name = strndup(normalized_path, last_slash - normalized_path + 1); // get the directory name
    if (*dir_name == NULL)
    {
        free(*file_name);
        *file_name = NULL;
        free(normalized_path);
        normalized_path = NULL;
        return false;
    }

    free(normalized_path); // free the normalized path
    normalized_path = NULL;
    return true;
}
