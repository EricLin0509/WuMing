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

#define JITTER_RANGE 30 // Use a random jitter to minimize the contention of the semaphore
#define BASE_TIMEOUT_MS 50 // Base timeout for waiting for the semaphore
#define CALCULATE_WAIT_MS(current_timeout_ms) (current_timeout_ms + (rand() % JITTER_RANGE)) // Calculate the dynamic timeout for waiting for the semaphore

/* Due to macOS doesn't support `sem_timedwait()`, so add a alternative implementation of `sem_timedwait()` */
/*
  * max_timeout_ms: the maximum timeout in milliseconds for waiting the semaphore
*/
static int sem_timewait(sem_t *restrict sem, const size_t max_timeout_ms) {
    size_t current_timeout = BASE_TIMEOUT_MS; // Initialize the current timeout

    while (current_timeout < max_timeout_ms) {
        if (sem_trywait(sem) == 0) return 0; // The semaphore is available, return immediately
        else if (errno != EAGAIN) return -1; // Failed to wait for the semaphore, return with error

        usleep(current_timeout * 1000); // `usleep()` use microseconds as unit, so multiply by 1000 to convert it to milliseconds
        
        current_timeout = CALCULATE_WAIT_MS(current_timeout); // Calculate the next timeout
    }

    errno = ETIMEDOUT;
    return -1; // Timeout exceeded, return with error
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

    int mask = MAX_TASKS - 1; // for and operation
    dest->rear = (dest->rear + 1) & mask; // Update the rear pointer

    sem_post(&dest->mutex); // Unlock the queue
    sem_post(&dest->full); // Signal the full semaphore
}

/* Get a task from the task queue */
/*
  * dest: a pointer to the task to be retrieved
  * source: the task queue to be retrieved from
  * return value: true if a task is retrieved, false if is timeout or error occurred
*/
bool get_task(Task *dest, TaskQueue *source) {
    if (source == NULL || dest == NULL) return false;

    if (sem_timewait(&source->full, 1000000) != 0) return false; // Timeout or error occurred, return

    sem_wait(&source->mutex); // Lock the queue

    *dest = source->tasks[source->front]; // Get the task from the queue

    int mask = MAX_TASKS - 1; // for and operation
    source->front = (source->front + 1) & mask; // Update the front pointer

    sem_post(&source->mutex); // Unlock the queue
    sem_post(&source->empty); // Signal the empty semaphore

    return true;
}

/* Turn user input path into absolute path */
char *get_absolute_path(const char *orignal_path) {
    char *absolute_path = realpath(orignal_path, NULL);
    if (absolute_path == NULL) {
        fprintf(stderr, "[ERROR] get_absolute_path: Failed to get absolute path of %s: %s\n", orignal_path, strerror(errno));
        return NULL;
    }
    return absolute_path;
}

/* Get file stat */
static inline bool get_file_stat(const char *path, struct stat *statbuf) {
	if (stat(path, statbuf) != 0) {
		fprintf(stderr, "[ERROR] get_file_stat: Failed to stat %s: %s\n", path, strerror(errno));
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

/* Check arguments for `spawn_new_process()` and `wait_for_processes()` */
static inline bool check_arguments(pid_t *pid, size_t num_of_process) {
    if (pid == NULL) return false;
    if (num_of_process == 0 || num_of_process > MAX_PROCESSES) return false;
    return true;
}

/* Spawn a new process */
/*
  * pid: a pointer or an array of pid_t to store the pid of the child process
  * num_of_process: the number of processes to be spawned (Maximum is 64)
  * 
  * mission_callback: the function to be executed in the child process
  * mission_callback_args: [OPTIONAL] the arguments to be passed to the `mission_callback`
  *
  * error_callback: [OPTIONAL] the function to be executed if an error occurs when spawning a process
  * error_callback_args: [OPTIONAL] the arguments to be passed to the `error_callback`
*/
void spawn_new_process(pid_t *pid, size_t num_of_process,
                     void (*mission_callback)(void *args), void *mission_callback_args, 
                     void (*error_callback)(void *args), void *error_callback_args) {
    /* Check arguments before spawning the processes */
    if (!check_arguments(pid, num_of_process)) {
        fprintf(stderr, "[ERROR] spawn_new_process: Invalid arguments\n");
        goto error_callback_call;
    }

    if (mission_callback == NULL) {
        fprintf(stderr, "[ERROR] spawn_new_process: mission_callback is NULL\n");
        goto error_callback_call;
    }

    /* Spawn the processes */
    for (size_t i = 0; i < num_of_process; i++) {
        pid_t *current_pid_ptr = pid + i; // Get the current pid pointer
        *current_pid_ptr = fork(); // Fork a new process
        if (*current_pid_ptr == -1) { // Failed to fork
            fprintf(stderr, "[ERROR] spawn_new_process: Failed to fork process: %s\n", strerror(errno));
            goto error_callback_call;
        }

        if (*current_pid_ptr == 0) { // Child process (run the function)
            mission_callback(mission_callback_args);
            _exit(0); // Exit the child process
        }
    }

    return;

error_callback_call:
    if (error_callback != NULL) error_callback(error_callback_args); // Execute the error function if an error occurs
}

/* Wait for processes to finish */
/*
  * Tips: This function SHOULD be called by the parent process after all child processes have been spawned
  
  * pid: a pointer or an array of pid_t to store the pid of the child process
  * num_of_process: the number of processes to be waited for
  * exit_callback: [OPTIONAL] the function to signal the subprocess to exit (e.g. send exit task to all processes)
  * exit_callback_args: [OPTIONAL] the arguments to be passed to the `exit_callback`
*/
int wait_for_processes(pid_t *pid, size_t num_of_process, void (*exit_callback)(void *args), void *exit_callback_args) {
    /* Check arguments before waiting for the processes */
    if (!check_arguments(pid, num_of_process)) {
        fprintf(stderr, "[ERROR] wait_for_processes: Invalid arguments\n");
        return -1;
    }

    int status = -1; // The return value of the function

    for (size_t i = 0; i < num_of_process && exit_callback != NULL; i++) {
        exit_callback(exit_callback_args); // Execute the exit callback function before waiting for the process (If provided)
    }

    for (size_t i = 0; i < num_of_process; i++) {
        pid_t *current_pid_ptr = pid + i; // Get the current pid pointer
        if (*current_pid_ptr <= 0) continue; // Skip invalid pid
        waitpid(*current_pid_ptr, &status, 0); // Wait for the process to finish
    }

    return status;
}

/* Process scan result */
static void process_scan_result(const char *path, cl_error_t result, const char *virname) {
	switch (result) {
		case CL_CLEAN:
			printf("%s: OK\n", path);
			break;
		case CL_VIRUS:
			printf("%s: %s FOUND\n", path, virname);
			break;
		default:
			printf("%s: SCAN ERROR: %s\n", path, cl_strerror(result));
			break;
	}	
}

/* Scan a file */
void process_file(const char *path, struct cl_engine *engine, struct cl_scan_options *scanoptions) {
	cl_error_t result;
    int fd = open(path, O_RDONLY); // Open the file
    if (fd == -1) {
        fprintf(stderr, " [ERROR] process_file: Failed to open %s: %s\n", path, strerror(errno));
        return;
    }
    
    const char *virname = NULL;
    unsigned long scanned = 0;
    result = cl_scandesc(fd, NULL, &virname, &scanned, engine, scanoptions); // Scan the file
    close(fd);

    process_scan_result(path, result, virname);
}
