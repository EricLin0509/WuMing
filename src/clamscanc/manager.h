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

#ifndef MANAGER_H
#define MANAGER_H

#include <stdbool.h>
#include <stdatomic.h>
#include <semaphore.h>
#include <clamav.h>

#include "watchdog.h"

#ifdef __linux__
#define MAX_PATH 4096
#elif defined(__APPLE__) || defined(__MACH__)
#define MAX_PATH 1024
#else
#define MAX_PATH 255
#endif

#define MAX_PROCESSES 64

#define QUEUE_SIZE 4096
#define MASK (QUEUE_SIZE - 1)
#define MAX_GET_TASKS 20

_Static_assert((QUEUE_SIZE & (MASK)) == 0, "QUEUE_SIZE must be power of 2");

/* ClamAV Essentials */
typedef struct {
	struct cl_engine *engine;
	struct cl_scan_options scan_options;
} ClamavEssentials;

/* Task types */
typedef enum {
	TASK_SCAN_DIR,
	TASK_SCAN_FILE
} TaskType;

/* Task structure */
typedef struct {
	TaskType type;
	char path[MAX_PATH];
} Task;

/* Task queue */
/*
  * `mutex` is a semaphore used to protect the TaskQueue
  * `empty` is a semaphore used to indicate how many empty slots are in the TaskQueue
  * `full` is a semaphore used to indicate how many tasks are in the TaskQueue
*/
typedef struct {
	sem_t mutex;
	sem_t empty;
	sem_t full;

	Task tasks[QUEUE_SIZE];
	_Atomic size_t tasks_count;
	_Atomic size_t in_progress;
	size_t front;
	size_t rear;
} TaskQueue;

/* Shared memory */
typedef struct {
	ClamavEssentials essentials;

  _Atomic CurrentStatus current_status;

  Observer producer_observer;
	TaskQueue dir_tasks;

  Observer worker_observer;
	TaskQueue file_tasks;
} SharedMemory;

/* Check if the given path is a directory */
bool is_directory(const char *path);

/* Check if the given path is a regular file */
bool is_regular_file(const char *path);

/* Build a task from the given path */
Task build_task(TaskType type, const char *path);

/* Initialize the ClamAV Essentials */
/*
  * @param essentials
  * The ClamAV Essentials to be initialized
  * 
  * @return
  * `true` if the initialization is successful, `false` otherwise.
*/
bool clamav_essentials_init(ClamavEssentials *essentials);

/* Clear the ClamAV Essentials */
void clamav_essentials_clear(ClamavEssentials *essentials);

/* Initialize the TaskQueue */
void task_queue_init(TaskQueue *queue);

/* Clear the TaskQueue */
void task_queue_clear(TaskQueue *queue);

/* Check whether the task queue is empty, assume the queue is not empty if failed to get the lock */
bool is_task_queue_empty_assumption(TaskQueue *queue);

/* Add a task to the TaskQueue */
/*
  * @param queue
  * The TaskQueue to which the task is added
  * 
  * @param task
  * The task to be added to the TaskQueue
*/
void task_queue_add(TaskQueue *queue, Task task);

/* Get a group of tasks from the task queue */
/*
  * @param queue
  * the tasks to be retrieved
  *
  * @param tasks
  * the task queue to be retrieved from
  *
  * @return
  * Number of tasks retrieved from the queue, 0 if no task is retrieved
  *
  * @warning
  * This function use non-blocking semaphore to avoid busy waiting, so it may need loops to wait for the task to be available
*/
size_t task_queue_get(TaskQueue *queue, Task *tasks);

/* Initialize the shared memory */
/*
  * @param shared_memory
  * The shared memory to be initialized
  * 
  * @warning
  * This function will use `mmap` to allocate the shared memory
*/
bool shared_memory_init(SharedMemory **shared_memory);

/* Clear the shared memory */
void shared_memory_clear(SharedMemory **shared_memory);

/* Process a file */
void process_file(const char *path, ClamavEssentials *essentials);

/* Process a directory */
/*
  * @param path
  * The directory to be processed
  * 
  * @param dir_tasks
  * The TaskQueue to which the directory tasks are added
  * 
  * @param file_tasks
  * The TaskQueue to which the file tasks are added
*/
void traverse_directory(const char *path, TaskQueue *dir_tasks, TaskQueue *file_tasks);

#endif /* MANAGER_H */