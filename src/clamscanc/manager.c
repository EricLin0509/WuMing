/* manager.c
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

/* This is the implementation of the `clamscanp` program */

#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>

#include "manager.h"

#define FILE_OPEN_FLAGS (O_RDONLY | O_NOFOLLOW | O_CLOEXEC) // Secure file open flags

#define JITTER_RANGE 30 // Use a random jitter to minimize the contention of the semaphore
#define BASE_TIMEOUT_MS 50 // Base timeout for waiting for the semaphore
#define CALCULATE_WAIT_MS(current_timeout_ms) (current_timeout_ms + (rand() % JITTER_RANGE)) // Calculate the dynamic timeout for waiting for the semaphore

/* Initialize the scan result */
void init_scan_result(ScanResult *result) {
    atomic_init(&result->total_directories, 0);
    atomic_init(&result->total_files, 0);
    atomic_init(&result->total_errors, 0);
    atomic_init(&result->total_found, 0);
}

/* Print the summary of the scanning result */
void print_summary(ScanResult *result) {
    printf("\n----------- SCAN SUMMARY -----------\n");
    printf("Scanned files: %zu\n", atomic_load(&result->total_files));
    printf("Infected files: %zu\n", atomic_load(&result->total_found));
    printf("Errors: %zu\n", atomic_load(&result->total_errors));
}

/* Build task from path */
Task build_task(TaskType type, char *path) {
    Task task;
    task.type = type;
    if (path) strncpy(task.path, path, PATH_MAX - 1); // -1 for null terminator
    return task;
}

/* Initialize the task queue */
void init_task_queue(TaskQueue *queue) {
    /* Initialize the semaphore */
    sem_init(&queue->mutex, 1, 1);
    sem_init(&queue->empty, 1, MAX_TASKS);
    sem_init(&queue->full, 1, 0);

    /* Initialize the data fields of the task queue */
    queue->front = 0;
    queue->rear = 0;
    atomic_init(&queue->in_progress, 0);
    atomic_init(&queue->task_count, 0);
    memset(queue->tasks, 0, sizeof(Task) * MAX_TASKS);
}

/* Clear the task queue */
void clear_task_queue(TaskQueue *queue) {
    // Because the data fields of the task queue are store in stack, we don't need to clear them

    // Clear the semaphore
    sem_destroy(&queue->mutex);
    sem_destroy(&queue->empty);
    sem_destroy(&queue->full);
}

/* Check whether the task queue is empty, assume the queue is not empty if failed to get the lock */
bool is_task_queue_empty_assumption(TaskQueue *queue) {
    if (queue == NULL) return true;

    if (sem_trywait(&queue->mutex) == 0) {
        bool is_empty = (atomic_load(&queue->task_count) == 0); // Check if the task count is zero
        bool has_tasks_in_progress = (atomic_load(&queue->in_progress) > 0); // Check if there are tasks in progress
        sem_post(&queue->mutex);
        return is_empty && !has_tasks_in_progress; // If the task count is zero and there are no tasks in progress
    }
    return false; // Failed to get the lock, assume the queue is not empty
}

/* Add a task to the task queue */
/*
  * dest: the task queue to be added
  * source: the task to be added
*/
void add_task(TaskQueue *dest, Task source) {
    if (dest == NULL) return;

    sem_wait(&dest->empty); // Wait for the empty semaphore
    sem_wait(&dest->mutex); // Lock the queue

    dest->tasks[dest->rear] = source; // Add the task to the queue

    dest->rear = (dest->rear + 1) & MASK; // Update the rear pointer

    atomic_fetch_add(&dest->task_count, 1); // Increment the task count

    sem_post(&dest->mutex); // Unlock the queue
    sem_post(&dest->full); // Signal the full semaphore
}

/* Calculate the task to get number */
static inline size_t calculate_task_to_get_number(TaskQueue *queue) {
    if (queue == NULL) return 0; // Invalid arguments

    size_t available_tasks = atomic_load(&queue->task_count); // Get the available tasks
    if (available_tasks == 0) return 0; // No task is available

    return (available_tasks < MAX_GET_TASKS) ? available_tasks : MAX_GET_TASKS; // Calculate the number of tasks to get
}

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
size_t get_task(Task *dest, TaskQueue *source) {
    if (dest == NULL || source == NULL) return 0; // Invalid arguments

    if (sem_trywait(&source->mutex) != 0) return 0; // Failed to get the lock, assume no task is available

    size_t tasks_to_get = calculate_task_to_get_number(source); // Calculate the number of tasks to get
    if (tasks_to_get == 0) {
        sem_post(&source->mutex); // Unlock the queue
        return 0; // No task is available
    }

    for (size_t i = 0; i < tasks_to_get; i++) {
        // Try to decrement the `full` semaphore, if failed, exit the loop
        if (sem_trywait(&source->full) != 0) {
            tasks_to_get = i; // Update the number of tasks to get
            break;
        }

        // Get the task from the queue
        dest[i] = source->tasks[source->front];

        source->front = (source->front + 1) & MASK; // Update the front pointer

        atomic_fetch_sub(&source->task_count, 1); // Decrement the task count

        sem_post(&source->empty); // Signal the empty semaphore
    }
    sem_post(&source->mutex); // Unlock the queue

    return tasks_to_get;
}

/* Get file stat */
static inline bool get_file_stat(const char *path, struct stat *statbuf) {
	if (lstat(path, statbuf) != 0) {
		fprintf(stderr, "[ERROR] get_file_stat: Failed to lstat %s: %s\n", path, strerror(errno));
		return false;
	}
	return true;
}

/* Check if the given path is a directory */
bool is_directory(const char *path) {
    struct stat status;
    return get_file_stat(path, &status) && S_ISDIR(status.st_mode);
}

/* Check if the given path is a regular file */
bool is_regular_file(const char *path) {
    struct stat status;
    return get_file_stat(path, &status) && S_ISREG(status.st_mode);
}

/* Set scan options */
static void set_scan_options(struct cl_scan_options *scanoptions) {
	scanoptions->heuristic |= CL_SCAN_GENERAL_HEURISTICS;
    scanoptions->general |= CL_SCAN_GENERAL_ALLMATCHES;
}

/* Initialize the ClamAV engine */
struct cl_engine *init_engine(struct cl_scan_options *scanoptions) {
	set_scan_options(scanoptions);

    struct cl_engine *engine;
    unsigned int signatures = 0;
    cl_error_t result; // Initialize result

	// Initialize ClamAV engine
	result = cl_init(CL_INIT_DEFAULT);
	if (result != CL_SUCCESS) {
		fprintf(stderr, "[ERROR] init_engine: cl_init failed: %s\n", cl_strerror(result));
		return NULL;
	}

    engine = cl_engine_new(); // Create a new ClamAV engine
    if (engine == NULL) {
        fprintf(stderr, "[ERROR] init_engine: cl_engine_new failed\n");
        return NULL;
    }

    // Load signatures from database directory
	const char *db_dir = cl_retdbdir(); // Get the database directory
    result = cl_load(db_dir, engine, &signatures, CL_DB_STDOPT);
    if (result != CL_SUCCESS) {
		fprintf(stderr, "[ERROR] init_engine: cl_load failed: %s\n", cl_strerror(result));
        cl_engine_free(engine);
        return NULL;
	}

    // Compile the signatures
    result = cl_engine_compile(engine);
    if (result != CL_SUCCESS) {
		fprintf(stderr, "[ERROR] init_engine: cl_engine_compile failed: %s\n", cl_strerror(result));
        cl_engine_free(engine);
        return NULL;
	}

    printf("[INFO] ClamAV engine initialized with %u signatures\n", signatures);
    return engine;
}

/* Spawn a new process */
/*
  * @param observer
  * the observer to be used for spawning the processes
  * 
  * @param mission_callback
  * the function to be executed in the child process
  * @param mission_callback_args
  * the arguments to be passed to the `mission_callback` [OPTIONAL]
  * 
  * @return
  * `true` if the process is spawned successfully, `false` otherwise
  * 
  * @warning
  * This function will also try to register the signal handler which store in the `observer` struct
*/
bool spawn_new_process(Observer *observer,
                     void (*mission_callback)(void *args), void *mission_callback_args) {
    if (mission_callback == NULL || observer == NULL) {
        fprintf(stderr, "[ERROR] spawn_new_process: Invalid arguments mission_callback=%p observer=%p\n", mission_callback, observer);
        return false;
    }

    register_signal_handler(observer->exit_condition_signal, SIG_IGN); // Ignore the exit condition signal in the parent process

    if (observer->num_of_processes == 0 || observer->num_of_processes > MAX_PROCESSES) {
        fprintf(stderr, "[ERROR] spawn_new_process: Invalid number of processes: %zu of %d\n", observer->num_of_processes, MAX_PROCESSES);
        return false;
    }

    /* Spawn the processes */
    for (size_t i = 0; i < observer->num_of_processes; i++) {
        pid_t *current_pid_ptr = observer->pids + i; // Get the current pid pointer
        *current_pid_ptr = fork(); // Fork a new process
        if (*current_pid_ptr == -1) { // Failed to fork
            fprintf(stderr, "[ERROR] spawn_new_process: Failed to fork process: %s\n", strerror(errno));
            return false;
        }

        if (*current_pid_ptr == 0) { // Child process (run the function)
            register_signal_handler(observer->exit_condition_signal, observer->condition_signal_handler); // Register the signal handler for the exit condition signal
            mission_callback(mission_callback_args);
            _exit(0); // Exit the child process
        }
    }

    return true;
}

/* Process scan result */
static void process_scan_result(const char *path, ScanResult *result, cl_error_t error, const char *virname) {
	switch (error) {
		case CL_CLEAN:
			printf("%s: OK\n", path);
			break;
		case CL_VIRUS:
            atomic_fetch_add(&result->total_found, 1); // Increment the total found count
			printf("%s: %s FOUND\n", path, virname);
			break;
		default:
            atomic_fetch_add(&result->total_errors, 1); // Increment the total error count
			printf("%s: SCAN ERROR: %s\n", path, cl_strerror(error));
			return; // Stop incrementing the total scanned count
	}

    atomic_fetch_add(&result->total_files, 1); // Increment the total scanned count
}

/* Scan a file */
void process_file(const char *path, ScanResult *result, struct cl_engine *engine, struct cl_scan_options *scanoptions) {
	cl_error_t error;
    int fd = open(path, FILE_OPEN_FLAGS); // Open the file
    if (fd == -1) {
        fprintf(stderr, "[ERROR] process_file: Failed to open %s: %s\n", path, strerror(errno));
        return;
    }
    
    const char *virname = NULL;
    unsigned long scanned = 0;
    error = cl_scandesc(fd, NULL, &virname, &scanned, engine, scanoptions); // Scan the file
    close(fd);

    process_scan_result(path, result, error, virname);
}
