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
#include <stdatomic.h>

#include "watchdog.h"

#define PATH_MAX 4096 // maximum length of path
#define MAX_PROCESSES 64 // maximum number of processes can be used for scanning

#define MAX_TASKS 4096 // maximum number of tasks can be added to the task queue
#define MASK (MAX_TASKS - 1) // mask to get the index of the task queue
#define MAX_GET_TASKS 20 // maximum number of tasks can be retrieved at once from the task queue

_Static_assert((MAX_TASKS & MASK) == 0, "MAX_TASKS must be a power of two"); // MAX_TASKS must be a power of two

/* Task type to indicate what kind of task to perform */
typedef enum {
    SCAN_DIR,
    SCAN_FILE
} TaskType; 

/* A task to be performed */
typedef struct {
    TaskType type;
    char path[PATH_MAX];
} Task;

/* Task queue */
typedef struct {
	/* Semaphores to control access to the task queue */
	sem_t mutex; // Controls access to the task queue

	sem_t empty; // Consumer waits when queue is empty
	sem_t full; // Producer waits when queue is full

	/* Data fields */
	int front; // Queue front index
	int rear; // Queue rear index
	_Atomic size_t in_progress; // Number of tasks in progress
	_Atomic size_t task_count; // Number of tasks in the queue
	Task tasks[MAX_TASKS]; // The task queue
} TaskQueue;

/* Scan result */
typedef struct {
	_Atomic size_t total_directories; // Total number of directories scanned
	_Atomic size_t total_files; // Total number of files scanned
	_Atomic size_t total_errors; // Total number of errors occurred during scanning
	_Atomic size_t total_found; // Total number of viruses found during scanning
} ScanResult;

/* The shared data */
typedef struct {
	_Atomic CurrentStatus current_status; // Current status of the scanning process

	ScanResult scan_result; // The scanning result

	Observer producer_observer; // Observer for producer process
	TaskQueue dir_tasks; // Task queue for traversing directories

	Observer worker_observer; // Observer for worker processes
	TaskQueue file_tasks; // Task queue for scanning files
} SharedData;

/* Initialize the scan result */
void init_scan_result(ScanResult *result);

/* Print the summary of the scanning result */
void print_summary(ScanResult *result);

/* Build task from path */
Task build_task(TaskType type, char *path);

/* Initialize the task queue */
void init_task_queue(TaskQueue *queue);

/* Clear the task queue */
void clear_task_queue(TaskQueue *queue);

/* Check whether the task queue is empty, assume the queue is not empty if failed to get the lock */
bool is_task_queue_empty_assumption(TaskQueue *queue);

/* Add a task to the task queue */
/*
  * dest: the task queue to be added
  * source: the task to be added
*/
void add_task(TaskQueue *dest, Task source);

/* Get a task from the task queue */
/*
  * @param dest
  * the tasks to be retrieved
  *
  * @param source
  * the task queue to be retrieved from
  *
  * @return
  * Number of tasks retrieved from the queue, 0 if no task is retrieved
  *
  * @warning
  * This function use non-blocking semaphore to avoid busy waiting, so it may need loops to wait for the task to be available
*/
size_t get_task(Task *dest, TaskQueue *source);

/* Turn user input path into absolute path */
char *get_absolute_path(const char *orignal_path);

/* Check if the given path is a directory */
bool is_directory(const char *path);

/* Check if the given path is a regular file */
bool is_regular_file(const char *path);

/* Initialize the ClamAV engine */
struct cl_engine *init_engine(struct cl_scan_options *scanoptions);

/* Spawn a new process */
/*
  * @param observer
  * the observer to be used for spawning the processes
  * 
  * @param mission_callback
  * the function to be executed in the child process
  * @param mission_callback_args
  * the arguments to be passed to the `mission_callback` [OPTIONAL]]
  *
  * @param error_callback
  * the function to be executed if an error occurs when spawning a process [OPTIONAL]
  * @param error_callback_args
  * the arguments to be passed to the `error_callback` [OPTIONAL]
  * 
  * @warning
  * This function will also try to register the signal handler which store in the `observer` structure
*/
void spawn_new_process(Observer *observer,
                     void (*mission_callback)(void *args), void *mission_callback_args, 
                     void (*error_callback)(void *args), void *error_callback_args);

/* Scan a file */
void process_file(const char *path, ScanResult *result, struct cl_engine *engine, struct cl_scan_options *scanoptions);

#endif // MANAGER_H