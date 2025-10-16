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

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "manager.h"

#define FILE_OPEN_FLAGS (O_RDONLY | O_NOFOLLOW | O_CLOEXEC) // Secure file open flags
#define MIN(a, b) ((a) < (b) ? (a) : (b)) // For calculating the minimum value

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

/* Build a task from the given path */
Task build_task(TaskType type, const char *path) {
    Task task;
    task.type = type;
    memset(task.path, 0, MAX_PATH);

    if (path != NULL && strlen(path) < MAX_PATH) {
        memcpy(task.path, path, strlen(path) + 1);
    }
    return task;
}

/* Initialize the `cl_scan_options` */
void clamav_options_init(struct cl_scan_options *options) {
    if (options == NULL) return;

	options->heuristic |= CL_SCAN_GENERAL_HEURISTICS;
    options->general |= CL_SCAN_GENERAL_ALLMATCHES;
}

/* Clear the `cl_engine` */
void cl_engine_clear(struct cl_engine **engine) {
    if (engine == NULL || *engine == NULL) return;

    cl_engine_free(*engine);
    *engine = NULL;
}

/* Initialize the `cl_engine` */
void cl_engine_init(struct cl_engine **engine) {
    if (engine == NULL) return;

    unsigned int signatures = 0;
    cl_error_t result; // Initialize result

	// Initialize ClamAV engine
	result = cl_init(CL_INIT_DEFAULT);
	if (result != CL_SUCCESS) {
		fprintf(stderr, "[ERROR] cl_engine_init: cl_init failed: %s\n", cl_strerror(result));
		return;
	}

    *engine = cl_engine_new(); // Create a new ClamAV engine
    if (*engine == NULL) {
        fprintf(stderr, "[ERROR] cl_engine_init: cl_engine_new failed\n");
        return;
    }

    // Load signatures from database directory
	const char *db_dir = cl_retdbdir(); // Get the database directory
    result = cl_load(db_dir, *engine, &signatures, CL_DB_STDOPT);
    if (result != CL_SUCCESS) {
		fprintf(stderr, "[ERROR] cl_engine_init: cl_load failed: %s\n", cl_strerror(result));
        cl_engine_clear(engine);
        return;
	}

    // Compile the signatures
    result = cl_engine_compile(*engine);
    if (result != CL_SUCCESS) {
		fprintf(stderr, "[ERROR] cl_engine_init: cl_engine_compile failed: %s\n", cl_strerror(result));
        cl_engine_clear(engine);
        return;
	}

    printf("[INFO] ClamAV engine initialized with %u signatures\n", signatures);
}

/* Initialize the ClamAV Essentials */
/*
  * @param essentials
  * The ClamAV Essentials to be initialized
  * 
  * @return
  * `true` if the initialization is successful, `false` otherwise.
*/
bool clamav_essentials_init(ClamavEssentials *essentials) {
    if (essentials == NULL) {
        fprintf(stderr, "[ERROR] clamav_essentials_init: Invalid argument\n");
        return false;
    }

    /* Initialize ClamAV engine */
    clamav_options_init(&essentials->scan_options);
    cl_engine_init(&essentials->engine);

    if (essentials->engine == NULL) {
        fprintf(stderr, "[ERROR] clamav_essentials_init: ClamAV engine initialization failed\n");
        return false;
    }
    
    return true;
}

/* Clear the ClamAV Essentials */
void clamav_essentials_clear(ClamavEssentials *essentials) {
    if (essentials == NULL) return;

    cl_engine_clear(&essentials->engine);   
}

/* Initialize the TaskQueue */
void task_queue_init(TaskQueue *queue) {
    if (queue == NULL) return;

    /* Initialize the semaphores */
    sem_init(&queue->mutex, 1, 1);
    sem_init(&queue->empty, 1, QUEUE_SIZE);
    sem_init(&queue->full, 1, 0);

    /* Initialize the tasks */
    memset(queue->tasks, 0, sizeof(queue->tasks));
    atomic_init(&queue->tasks_count, 0);
    atomic_init(&queue->in_progress, 0);
    queue->front = 0;
    queue->rear = 0;
}

/* Clear the TaskQueue */
void task_queue_clear(TaskQueue *queue) {
    /* Only clear the semaphores */
    sem_destroy(&queue->mutex);
    sem_destroy(&queue->empty);
    sem_destroy(&queue->full);
}

/* Check whether the task queue is empty, assume the queue is not empty if failed to get the lock */
bool is_task_queue_empty_assumption(TaskQueue *queue) {
    if (queue == NULL) return true;

    if (sem_trywait(&queue->mutex) == -1 && errno == EAGAIN) return false; // Failed to get the lock, assume the queue is not empty

    bool is_empty = true;
    is_empty &= (atomic_load(&queue->tasks_count)) == 0;
    is_empty &= (atomic_load(&queue->in_progress)) == 0;

    sem_post(&queue->mutex); // Release the lock

    return is_empty;
}

/* Add a task to the TaskQueue */
/*
  * @param queue
  * The TaskQueue to which the task is added
  * 
  * @param task
  * The task to be added to the TaskQueue
*/
void task_queue_add(TaskQueue *queue, Task task) {
    if (queue == NULL && memcmp(task.path, "\0", 1) == 0) return;

    sem_wait(&queue->empty); // Wait for an empty slot
    sem_wait(&queue->mutex);

    queue->tasks[queue->rear] = task; // Add the task to the queue
    queue->rear = (queue->rear + 1) & MASK; // Update the head pointer

    atomic_fetch_add(&queue->tasks_count, 1); // Increment the task count

    sem_post(&queue->full); // Release the full slot
    sem_post(&queue->mutex);
}

/* Calculate the task to get number */
static inline size_t calculate_task_to_get_number(TaskQueue *queue) {
    if (queue == NULL) return 0; // Invalid arguments

    size_t available_tasks = atomic_load(&queue->tasks_count); // Get the available tasks

    return MIN(available_tasks, MAX_GET_TASKS); // Calculate the number of tasks to get
}

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
size_t task_queue_get(TaskQueue *queue, Task *tasks) {
    if (queue == NULL || tasks == NULL) return 0; // Invalid arguments

    if (sem_trywait(&queue->mutex) != 0) return 0; // Failed to get the lock, assume no task is available

    size_t tasks_to_get = calculate_task_to_get_number(queue); // Calculate the number of tasks to get
    if (tasks_to_get == 0) {
        sem_post(&queue->mutex); // Unlock the queue
        return 0; // No task is available
    }

    for (size_t i = 0; i < tasks_to_get; i++) {
        // Try to decrement the `full` semaphore, if failed, exit the loop
        if (sem_trywait(&queue->full) != 0) {
            tasks_to_get = i; // Update the number of tasks to get
            break;
        }

        // Get the task from the queue
        tasks[i] = queue->tasks[queue->front];

        queue->front = (queue->front + 1) & MASK; // Update the front pointer

        atomic_fetch_sub(&queue->tasks_count, 1); // Decrement the task count

        sem_post(&queue->empty); // Signal the empty semaphore
    }
    sem_post(&queue->mutex); // Unlock the queue

    return tasks_to_get;
}

/* Initialize the shared memory */
/*
  * @param shared_memory
  * The shared memory to be initialized
  * 
  * @warning
  * This function will use `mmap` to allocate the shared memory
*/
bool shared_memory_init(SharedMemory **shared_memory) {
    if (shared_memory == NULL || *shared_memory != NULL) return false; // Invalid arguments or already initialized

    *shared_memory = mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (*shared_memory == MAP_FAILED) {
        fprintf(stderr, "[ERROR] shared_memory_init: Failed to allocate shared memory: %s\n", strerror(errno));
        return false;
    }

    /* Initialize the ClamAV Essentials */
    if (!clamav_essentials_init(&(*shared_memory)->essentials)) {
        fprintf(stderr, "[ERROR] shared_memory_init: ClamAV Essentials initialization failed\n");
        munmap(*shared_memory, sizeof(SharedMemory));
        *shared_memory = NULL;
        return false;
    }

    /* Initialize the TaskQueues */
    task_queue_init(&(*shared_memory)->dir_tasks);
    task_queue_init(&(*shared_memory)->file_tasks);

    return true;
}

/* Clear the shared memory */
void shared_memory_clear(SharedMemory **shared_memory) {
    if (shared_memory == NULL || *shared_memory == NULL) return;

    /* Clear the ClamAV Essentials */
    clamav_essentials_clear(&(*shared_memory)->essentials);

    /* Clear the TaskQueues */
    task_queue_clear(&(*shared_memory)->dir_tasks);
    task_queue_clear(&(*shared_memory)->file_tasks);

    /* Unmap the shared memory */
    munmap(*shared_memory, sizeof(SharedMemory));
    *shared_memory = NULL;
}

/* Process scan result */
static void process_scan_result(const char *path, cl_error_t error, const char *virname) {
	switch (error) {
		case CL_CLEAN:
			printf("%s: OK\n", path);
			break;
		case CL_VIRUS:
			printf("%s: %s FOUND\n", path, virname);
			break;
		default:
			printf("%s: SCAN ERROR: %s\n", path, cl_strerror(error));
            break;
	}
}

/* Process a file */
void process_file(const char *path, ClamavEssentials *essentials) {
    if (path == NULL || essentials == NULL) return; // Invalid arguments

	cl_error_t error;
    int fd = open(path, FILE_OPEN_FLAGS); // Open the file
    if (fd == -1) {
        fprintf(stderr, "[ERROR] process_file: Failed to open %s: %s\n", path, strerror(errno));
        return;
    }
    
    const char *virname = NULL;
    unsigned long scanned = 0;
    error = cl_scandesc(fd, NULL, &virname, &scanned, essentials->engine, &essentials->scan_options); // Scan the file
    close(fd);

    process_scan_result(path, error, virname);
}

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
void traverse_directory(const char *path, TaskQueue *dir_tasks, TaskQueue *file_tasks) {
    if (path == NULL || dir_tasks == NULL || file_tasks == NULL) return; // Invalid arguments

    DIR *dir = opendir(path); // Open the directory
    if (dir == NULL) {
        fprintf(stderr, "[ERROR] traverse_directory: Failed to open %s: %s\n", path, strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir))) { // Traverse the directory
        if (memcmp(entry->d_name, ".", MIN(strlen(entry->d_name), 1)) == 0 ||
            memcmp(entry->d_name, "..", MIN(strlen(entry->d_name), 2)) == 0) continue; // Skip the current and parent directory

        char fullpath[MAX_PATH]; // Initialize a full path
        snprintf(fullpath, MAX_PATH, "%s/%s", path, entry->d_name); // Build the full path

        if (is_directory(fullpath)) {
            Task new_dir_task = build_task(TASK_SCAN_DIR, fullpath); // Build a new task for traversing the directory
            task_queue_add(dir_tasks, new_dir_task); // Add the task to the task queue
        }
        else if (is_regular_file(fullpath)) {
            Task new_file_task = build_task(TASK_SCAN_FILE, fullpath); // Build a new task for scanning the file
            task_queue_add(file_tasks, new_file_task); // Add the task to the task queue
        }
        else continue; // Skip other types of files
    }
    closedir(dir); // Close the directory
}