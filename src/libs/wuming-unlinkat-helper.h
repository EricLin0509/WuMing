/* wuming-unlinkat-helper.h
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

/*
  * Due to the limitation of the `pkexec` command, we cannot pass a opened file discriptor to the helper program.
  * So we need to use a FIFO to pass the following `StatData` struct
*/

 #pragma once

#include <stdint.h>
#include <sys/stat.h>

#define STAT_DATA_SIZE (sizeof(StatData))
#define HELPER_DATA_SIZE (sizeof(HelperData))

#define DEFAULT_AUTH_KEY 0xCA12D4E5 // The default authentication key to verify the received data
#define SHM_MAGIC 0x57554D49 // A magic number to verify the shared memory

/* The original data to provide */
typedef struct {
    struct stat dir_stat; // Original directory stat
    struct stat file_stat; // Original file stat
} StatData; // Data struct to pass to helper program

/* The struct need to be passed to the helper program */
typedef struct {
    uint32_t auth_key; // The authentication key to verify the FIFO
    uint32_t shm_magic; // The magic number to verify the shared memory
    StatData data; // The data to provide
} HelperData; // Data struct to pass to helper program