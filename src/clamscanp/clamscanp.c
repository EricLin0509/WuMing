/* clamscanp.c
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

/* Architecture Overview */
/* Example: Two subprocesses for scanning files */
/*

                    Main Process (Parent Process) ------------------> Subprocess 1 (Child Process) -> Submit tasks to the task queue (this will ensure won't block the scanning process)
        /                                                                       \
       /                                                                          \
      /                                                                             \
Subprocess 2 (Child Process)             Subprocess 3 (Child Process)
                |                                                                     |
                |                                                                     |
                | Get tasks                                                     | Get tasks
                |                                                                     |
                |                                                                     |
        Scanning files                                                Scanning files
*/

/*
  * Compare with `clamscan`:
  * Advantages: Using 4 scan processes, scan speed increased by approximately 57% with only a slight decrease in reliability
  * Disadvantages: Only support Unix-like system, no Windows support (due to `fork()` and `wait()`)
*/

/* Bottleneck: It only implments one process to submit tasks, which can block the scanning processes if the task queue is empty */

#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>

#include "manager.h"

/* Use global variables making it easier to pass data between processes */
static struct cl_scan_options options; // Options for scanning
static struct cl_engine *engine; // ClamAV engine
static SharedData *shm; // Shared memory for engine
static pid_t parent_pid; // Parent process id

/* Initialize the resources */
static bool init_resources(void) {
    parent_pid = getpid(); // Get the parent process id

    /* Initialize the shared memory */
    size_t shm_size = sizeof(SharedData);
    shm = mmap (NULL, shm_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (shm == MAP_FAILED) {
        fprintf(stderr, "[ERROR] init_resources: mmap failed: %s\n", strerror(errno));
        return false;
    }
    memset(shm, 0, shm_size);

    /* Initialize the task queue */
    init_task_queue(&shm->scan_tasks);

    return true;
}

/* Clean up the resources */
static void resource_cleanup(void) {
    // Clean up shared memory and semaphores
    if (shm) {
        clear_task_queue(&shm->scan_tasks);
        munmap(shm, sizeof(SharedData));
        shm = NULL;
    }

    // Clean up the engine
    if (engine) {
        cl_engine_free(engine);
        engine = NULL;
    }
}

/* Signal handler for terminating the scan */
void shutdown_handler(int sig) {
    if (getpid() != parent_pid) {
        _exit(EXIT_FAILURE);
    }

    write(STDERR_FILENO, "\n[INFO] Terminating the scan, shutting down...\n", 48);
    atomic_store(&shm->should_exit, true);

    // Clean up shared memory and semaphores
    resource_cleanup();

    exit(EXIT_FAILURE);
}

/* Error function when failed to create a producer process */
static inline void create_producer_error(void *args) {
    fprintf(stderr, "[ERROR] create_producer_error: Failed to spawn a producer process!\n");

    resource_cleanup();
    exit(EXIT_FAILURE);
}

/* Error function when failed to create worker processes */
static inline void create_worker_error(void *args) {
    fprintf(stderr, "[ERROR] create_worker_error: Failed to spawn worker processes!\n");
    size_t num_workers = *(size_t*)args;
    for (size_t i = 0; i < num_workers; i++) {
        kill(getpid(), SIGTERM); // Terminate the parent process
    }

    resource_cleanup();
    exit(EXIT_FAILURE);
}

/* Check whether has remaining tasks in the task queue for worker processes */
static inline bool has_remaining_tasks(TaskQueue *queue, _Atomic bool *is_producer_done) {
    if (atomic_load(is_producer_done)) {
        if (sem_trywait(&queue->mutex) == 0) { // If get the lock failed, assume the queue is not empty
            bool has_tasks = queue->front != queue->rear;
            sem_post(&queue->mutex); // Release the lock
            return has_tasks;
        }
    }
    return true;
}

/* Treverse the directory and add tasks to the task queue */
/*
  * WARNING: This function MUST be called by a producer process
*/
static void producer_main(void *args) {
    const char *path = (const char*)args; // Get the directory path from the argument
    DIR *dir = opendir(path); // Open the directory
    if (dir == NULL) {
        fprintf(stderr, "[ERROR] Failed to open %s: %s\n", path, strerror(errno));
        return;
    }

    struct dirent *entry; // Initialize a directory entry
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue; // Skip current and parent directory

        char fullpath[PATH_MAX]; // Initialize a full path
        snprintf(fullpath, PATH_MAX, "%s/%s", path, entry->d_name); // Build the full path

        if (is_directory(fullpath)) {
            producer_main(fullpath); // Recursively scan the directory if it is a directory
        }
        else if (is_regular_file(fullpath)) {
            Task new_task = build_task(SCAN_FILE, fullpath); // Build a new task for scanning the file
            add_task(&shm->scan_tasks, new_task); // Add the task to the task queue
        }
        else continue; // Skip other types of files
    }
    closedir(dir); // Close the directory
}

/* Worker function for scanning files */
/*
  * WARNING: This function MUST be called by comsumer processes
*/
static void worker_main(void *args) {
    if (args == NULL) return; // Check if the argument is valid
    TaskQueue *queue = (TaskQueue*)args; // Get the task queue from the argument

    Task task; // Initialize a task to get from the task queue
    while (!atomic_load(&shm->should_exit)) {
        if (!has_remaining_tasks(queue, &shm->is_producer_done)) return; // Exit if there are no remaining tasks and the producer has finished adding tasks

        if (get_task(&task, queue)) {
            if (task.type != SCAN_FILE) continue; // Skip other types of tasks

            // Scan the file
            process_file(task.path, engine, &options);
        }
    }
}

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <path> [num of worker processes]\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);

    /* Prepare the `cl_engine` */
    printf("[INFO] Start preparing the engine...\n");
    engine = init_engine(&options);
    if (engine == NULL) {
        fprintf(stderr, "[ERROR] Failed to initialize ClamAV engine.\n");
        return EXIT_FAILURE;
    }
    printf("[INFO] Engine prepared Successfully.\n");

    char *path = get_absolute_path(argv[1]); // Get the absolute path of the directory to scan
    if (!is_directory(path)) { // If the path is not a directory, scan it directly
        printf("[INFO] User pass a file path, try scanning it directly...\n");
        if (!is_regular_file(path)) {
            fprintf(stderr, "[ERROR] This IS NOT a regular file, aborting...\n");
            resource_cleanup();
            return EXIT_FAILURE;
        } 
        process_file(path, engine, &options);
        resource_cleanup();
        return EXIT_SUCCESS;
    }

    size_t num_workers = argc > 2 ? atoi(argv[2]) : 1; // Get the number of worker processes from the argument or default to 1

    if (!init_resources()) { // Initialize the shared resources
        fprintf(stderr, "[ERROR] Failed to initialize shared resources, aborting...\n");
        return EXIT_FAILURE;
    }

    // Add initial tasks to the task queue
    printf("[INFO] Start adding initial tasks to the task queue...\n");
    pid_t producer_pid = 0; // Initialize the producer process id
    spawn_new_process(&producer_pid, 1,
                            producer_main, (void*)path,
                            create_producer_error, NULL);

    // Create worker processes
    printf("[INFO] Start creating worker processes...\n");
    pid_t worker_pids[num_workers];
    memset(worker_pids, 0, sizeof(worker_pids));
    spawn_new_process(worker_pids, num_workers,
                            worker_main, (void*)&shm->scan_tasks,
                            create_worker_error, (void*)&num_workers);

    // Wait for all child processes to exit
    wait_for_processes(&producer_pid, 1); // Producer process
    atomic_store(&shm->is_producer_done, true); // Indicate worker processes the producer has finished adding tasks
    wait_for_processes(worker_pids, num_workers); // Worker processes
    
    printf("[INFO] Scan completed successfully.\n");

    free(path); // Free the memory of the absolute path
    resource_cleanup();

    return EXIT_SUCCESS;
}