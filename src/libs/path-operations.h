/* path-operation.h
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

#ifndef PATH_OPERATIONS_H
#define PATH_OPERATIONS_H

#include <stdbool.h>

/* Get the file name and dir name */
/*
  * @param path: the path of the file or directory
  * @param file_name: the pointer to store the file name
  * @param dir_name: the pointer to store the directory name
  * 
  * @return: true if success, false if failed
*/
bool get_file_dir_name(const char *path, char **file_name, char **dir_name);

#endif // PATH_OPERATION_H