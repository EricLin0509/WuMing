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

#define PATH_MAX 4096 // maximum length of path
#define MAX_PROCESSES 64 // maximum number of processes can be used for scanning
#define MAX_TASKS 4096 // maximum number of tasks can be added to the task queue

_Static_assert((MAX_TASKS & (MAX_TASKS - 1)) == 0, "MAX_TASKS must be a power of two"); // MAX_TASKS must be a power of two

/* Task type to indicate what kind of task to perform */
typedef enum {
    SCAN_DIR,
    SCAN_FILE,
    EXIT_TASK
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
	Task tasks[MAX_TASKS]; // The task queue
} TaskQueue;

/* The shared data */
typedef struct {
    _Atomic int should_exit; // Flag to indicate if all processes should exit
    TaskQueue scan_tasks; // Task queue for scanning files
} SharedData;

/* Build task from path */
Task build_task(TaskType type, char *path);

/* Initialize the task queue */
void init_task_queue(TaskQueue *queue);

/* Clear the task queue */
void clear_task_queue(TaskQueue *queue);

/* Add a task to the task queue */
/*
  * dest: the task queue to be added
  * source: the task to be added
*/
void add_task(TaskQueue *dest, Task source);

/* Get a task from the task queue */
/*
  * dest: a pointer to the task to be retrieved
  * source: the task queue to be retrieved from
  * should_exit: a atomic integer pointer to indicate if the program should exit
  * return value: true if a task is retrieved, false if is timeout or error occurred
*/
bool get_task(Task *dest, TaskQueue *source, _Atomic int *should_exit);

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
  * pid: a pointer or an array of pid_t to store the pid of the child process
  * num_of_process: the number of processes to be spawned
  * 
  * mission_callback: the function to be executed in the child process
  * mission_callback_args: [OPTIONAL] the arguments to be passed to the `mission_callback`
  *
  * error_callback: [OPTIONAL] the function to be executed if an error occurs when spawning a process
  * error_callback_args: [OPTIONAL] the arguments to be passed to the `error_callback`
*/
void spawn_new_process(pid_t *pid, size_t num_of_process,
                     void (*mission_callback)(void *args), void *mission_callback_args, 
                     void (*error_callback)(void *args), void *error_callback_args);

/* Wait for processes to finish */
/*
  * Tips: This function SHOULD be called by the parent process after all child processes have been spawned
  
  * pid: a pointer or an array of pid_t to store the pid of the child process
  * num_of_process: the number of processes to be waited for
  * exit_callback: [OPTIONAL] the function to signal the subprocess to exit (e.g. send exit task to all processes)
  * exit_callback_args: [OPTIONAL] the arguments to be passed to the `exit_callback`
*/
int wait_for_processes(pid_t *pid, size_t num_of_process, void (*exit_callback)(void *args), void *exit_callback_args);

/* Scan a file */
void process_file(const char *path, struct cl_engine *engine, struct cl_scan_options *scanoptions);

#endif // MANAGER_H