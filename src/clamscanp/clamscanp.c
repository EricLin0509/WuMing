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
static bool init_resources(char *path) {
    parent_pid = getpid(); // Get the parent process id

    /* Initialize the shared memory */
    size_t shm_size = sizeof(SharedData);
    shm = mmap (NULL, shm_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (shm == MAP_FAILED) {
        fprintf(stderr, "[ERROR] init_resources: mmap failed: %s\n", strerror(errno));
        return false;
    }
    memset(shm, 0, shm_size);

    /* Initialize the `CurrentStatus` */
    init_status(&shm->current_status);

    /* Initialize the task queue */
    init_task_queue(&shm->dir_tasks);
    init_task_queue(&shm->file_tasks);

    /* Send initial task to the task queue */
    Task initial_task = build_task(SCAN_DIR, path);
    add_task(&shm->dir_tasks, initial_task);
    free(path); // Free the memory of the path

    return true;
}

/* Clean up the resources */
static void resource_cleanup(void) {
    // Clean up shared memory and semaphores
    if (shm) {
        clear_task_queue(&shm->dir_tasks);
        clear_task_queue(&shm->file_tasks);
        destroy_observer(&shm->producer_observer);
        destroy_observer(&shm->worker_observer);
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
    set_status(&shm->current_status, STATUS_FORCE_QUIT);

    // Clean up shared memory and semaphores
    resource_cleanup();

    exit(EXIT_FAILURE);
}

/* Error callback function when failed to create processes */
static void create_process_error(void *args) {
    fprintf(stderr, "[ERROR] create_process_error: Failed to spawn process!\n");
    Observer *observer = (Observer*)args;
    for (size_t i = 0; i < observer->num_of_processes; i++) {
        kill(observer->pids[i], SIGTERM); // Terminate the rest of the processes
    }

    resource_cleanup();
    exit(EXIT_FAILURE);
}

/* The exit signal handler */
static void exit_signal(int sig) {
    _exit(EXIT_SUCCESS);
}

/* The exit condition for the producer process */
static inline void is_producer_done(TaskQueue *dir_tasks) {
    if (is_task_queue_empty_assumption(dir_tasks)) {
        set_status(&shm->current_status, STATUS_PRODUCER_DONE); // Set the status to producer done
    }
}

/* Sort the paths in the directory and add tasks to the task queue */
static void trverse_directory(char *path, TaskQueue *dir_tasks, TaskQueue *file_tasks) {
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
            Task new_dir_task = build_task(SCAN_DIR, fullpath); // Build a new task for traversing the directory
            add_task(dir_tasks, new_dir_task); // Add the task to the task queue
        }
        else if (is_regular_file(fullpath)) {
            Task new_file_task = build_task(SCAN_FILE, fullpath); // Build a new task for scanning the file
            add_task(file_tasks, new_file_task); // Add the task to the task queue
        }
        else continue; // Skip other types of files
    }
    closedir(dir); // Close the directory
}

/* Treverse the directory and add tasks to the task queue */
/*
  * WARNING: This function MUST be called by a producer process
*/
static void producer_main(void *args) {
    if (args == NULL) return; // Check if the argument is valid
    TaskQueue *queue = (TaskQueue*)args; // Get the task queue from the argument

    Task task; // Initialize a task to get from the task queue
    while (get_status(&shm->current_status) != STATUS_FORCE_QUIT) {
        is_producer_done(queue); // Check if the producer is done

        if (get_task(&task, queue)) { // Get a task from the task queue
            if (task.type != SCAN_DIR) continue; // Skip invalid tasks type

            atomic_fetch_add(&queue->in_progress, 1); // Increment the number of tasks in progress

            // Traverse the directory and add tasks to the task queue
            trverse_directory(task.path, queue, &shm->file_tasks);

            atomic_fetch_sub(&queue->in_progress, 1); // Decrement the number of tasks in progress
        }
    }

}

/* The exit condition for worker processes */
static inline void is_all_task_done(TaskQueue *file_tasks) {
    if (get_status(&shm->current_status) == STATUS_PRODUCER_DONE) { // First check if the producer is done
        if (is_task_queue_empty_assumption(file_tasks)) { // Then check if the task queue is empty and all tasks are done
            set_status(&shm->current_status, STATUS_ALL_TASKS_DONE); // Set the status to all tasks done
        }
    }
}

/* Worker function for scanning files */
/*
  * WARNING: This function MUST be called by comsumer processes
*/
static void worker_main(void *args) {
    if (args == NULL) return; // Check if the argument is valid
    TaskQueue *queue = (TaskQueue*)args; // Get the task queue from the argument

    Task task; // Initialize a task to get from the task queue
    while (get_status(&shm->current_status) != STATUS_FORCE_QUIT) {
        is_all_task_done(queue); // Check if all tasks are done

        if (get_task(&task, queue)) { // Get a task from the task queue
            if (task.type != SCAN_FILE) continue; // Skip invalid tasks type

            atomic_fetch_add(&queue->in_progress, 1); // Increment the number of tasks in progress

            process_file(task.path, engine, &options); // Scan the file

            atomic_fetch_sub(&queue->in_progress, 1); // Decrement the number of tasks in progress
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

    char *real_path = get_absolute_path(argv[1]); // Get the absolute path of the directory to scan
    if (!is_directory(real_path)) { // If the path is not a directory, scan it directly
        printf("[INFO] User pass a file path, try scanning it directly...\n");
        if (!is_regular_file(real_path)) {
            fprintf(stderr, "[ERROR] This IS NOT a regular file, aborting...\n");
            resource_cleanup();
            return EXIT_FAILURE;
        } 
        process_file(real_path, engine, &options);
        resource_cleanup();
        return EXIT_SUCCESS;
    }

    size_t num_workers = argc > 2 ? atoi(argv[2]) : 1; // Get the number of worker processes from the argument or default to 1
    size_t num_producers = num_workers >= 8 ? 4 : 2; // Set the number of producers to 4 if the number of worker processes is greater or equal to 8, otherwise set it to 2

    if (!init_resources(real_path)) { // Initialize the shared resources
        fprintf(stderr, "[ERROR] Failed to initialize shared resources, aborting...\n");
        return EXIT_FAILURE;
    }

    // Add initial tasks to the task queue
    printf("[INFO] Start adding initial tasks to the task queue...\n");
    init_observer(&shm->producer_observer, num_producers, SIGUSR1, exit_signal);
    spawn_new_process(&shm->producer_observer,
                            producer_main, (void*)&shm->dir_tasks,
                            create_process_error, (void*)&shm->producer_observer);

    // Create worker processes
    printf("[INFO] Start creating worker processes...\n");
    init_observer(&shm->worker_observer, num_workers, SIGUSR2, exit_signal);
    spawn_new_process(&shm->worker_observer,
                            worker_main, (void*)&shm->file_tasks,
                            create_process_error, (void*)&shm->worker_observer);

    // Wait for all child processes to exit
    watchdog_main(&shm->producer_observer, &shm->current_status, STATUS_PRODUCER_DONE);
    watchdog_main(&shm->worker_observer, &shm->current_status, STATUS_ALL_TASKS_DONE);
    
    printf("[INFO] Scan completed successfully.\n");

    resource_cleanup();

    return EXIT_SUCCESS;
}