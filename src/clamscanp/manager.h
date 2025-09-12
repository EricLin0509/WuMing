/* manager.h
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

/* This program is similar to `clamscan` from ClamAV, but with some performance improvements */
/*
  * This use process pool to implement parallel scanning, which can significantly improve the scanning speed.
  * Use shared memory to share the engine between processes, which can reduce the memory usage.
*/

#ifndef MANAGER_H
#define MANAGER_H

#include <stdbool.h>
#include <semaphore.h>
#include <clamav.h>
#include <linux/limits.h>

#define MAX_PROCESSES 64 // maximum number of processes can be used for scanning
#define SHM_KEY 0x12345678 // shared memory key
#define MAX_TASKS 4096 // maximum number of tasks can be added to the task queue

/* Task type to indicate what kind of task to perform */
typedef enum {
    SCAN_DIR,
    SCAN_FILE,
    EXIT_TASK
} TaskType; 

/* The task to be performed */
typedef struct {
    TaskType type;
    char path[PATH_MAX];
} ScanTask;

/* The shared data */
typedef struct {
    sem_t mutex; // Controls process access to shared data

    /* The task queue */
    sem_t empty; // Comsumer waits when queue is empty
    sem_t full; // Producer waits when queue is full
    int front; // Queue front index
    int rear; // Queue rear index
    size_t capacity; // Maximum number of tasks can be added to the queue
    _Atomic int should_exit; // Flag to indicate if all processes should exit
    ScanTask tasks[MAX_TASKS]; // The task queue
} SharedData;

/* Check if the given path is a directory */
bool is_directory(const char *path);

/* Check if the given path is a regular file */
bool is_regular_file(const char *path);

/* Initialize the ClamAV engine */
struct cl_engine *init_engine(struct cl_scan_options *scanoptions);

/* Scan a file */
void process_file(const char *path, struct cl_engine *engine, struct cl_scan_options *scanoptions);

#endif // MANAGER_H