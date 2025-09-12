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

#include <dirent.h>
#include <unistd.h>
#include <sys/shm.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/wait.h>

#include "manager.h"
#include <linux/time.h>

/* Use global variables making it easier to pass data between processes */
static struct cl_scan_options options; // Options for scanning
static struct cl_engine *engine; // ClamAV engine
static SharedData *shm; // Shared memory for engine
static int shm_id; // Shared memory id
static pid_t parent_pid; // Parent process id

/* Signal handler for terminating the scan */
void shutdown_handler(int sig) {
    if (getpid() != parent_pid) {
        _exit(EXIT_SUCCESS);
    }

    printf("\nReceived signal %d, terminating scan...\n", sig);
    atomic_store(&shm->should_exit, 1);
    
    // Clean up shared memory and semaphores
    if (shm) {
        sem_destroy(&shm->mutex);
        sem_destroy(&shm->empty);
        sem_destroy(&shm->full);
        shmdt(shm);
        shmctl(shm_id, IPC_RMID, NULL);
    }
    
    if (engine) {
        cl_engine_free(engine);
        engine = NULL;
    }
    
    exit(EXIT_SUCCESS);
}

/* Add a task to the task queue */
/*
  * WARNING: This function MUST be called by a producer process
*/
static void add_task(ScanTask *task) {
    if (task == NULL || (task->type != SCAN_FILE && task->type != EXIT_TASK)) return; // Check if the task is valid (also allow `EXIT_TASK`)
    if (task->type == SCAN_FILE && strlen(task->path) >= PATH_MAX) return; // Check if the path is too long

    sem_wait(&shm->empty); // Wait for a free slot in the task queue
    sem_wait(&shm->mutex); // Lock the task queue

    shm->tasks[shm->rear] = *task; // Add the task to the task queue
    shm->rear = (shm->rear + 1) % MAX_TASKS; // Update the rear pointer

    sem_post(&shm->mutex); // Unlock the task queue
    sem_post(&shm->full); // Notify the consumer process that a new task is available
}

/* Treverse the directory and add tasks to the task queue */
/*
  * WARNING: This function MUST be called by a producer process
*/
static void producer_main(const char *path) {
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
        } else if (is_regular_file(fullpath)) {
            ScanTask new_task = {SCAN_FILE, {0} }; // Initialize a new task
            strncpy(new_task.path, fullpath, PATH_MAX); // Copy the full path to the task
            add_task(&new_task); // Add the task to the task queue
        } else {
            continue; // Skip if the file is not a regular file or a directory
        }
    }
    closedir(dir); // Close the directory
}

/* Worker function for scanning files */
static void worker_main() {
    while (!atomic_load(&shm->should_exit)) {
        // Wait for a task to be available with timeout
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // 1 second timeout
        
        if (sem_timedwait(&shm->full, &ts) != 0) {
            // Timeout or error, check if we should exit
            if (atomic_load(&shm->should_exit)) {
                break;
            }
            continue;
        }

        sem_wait(&shm->mutex); // Lock the task queue

        ScanTask get_task = shm->tasks[shm->front]; // Get the task from the task queue
        shm->front = (shm->front + 1) % MAX_TASKS;

        sem_post(&shm->mutex); // Unlock the task queue
        sem_post(&shm->empty); // Notify the producer process that a task is processed

        if (get_task.type != SCAN_FILE) return; // Check if the task is valid

        process_file(get_task.path, engine, &options); // Scan the file
    }
}

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <path> [num of worker processes]\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Prepare the `cl_engine` */
    printf("[INFO] Start preparing the engine...\n");
    engine = init_engine(&options);
    if (engine == NULL) {
        fprintf(stderr, "[ERROR] Failed to initialize ClamAV engine.\n");
        return EXIT_FAILURE;
    }
    printf("[INFO] Engine prepared Successfully.\n");

    const char *path = argv[1];
    if (!is_directory(path)) { // If the path is not a directory, scan it directly
        printf("[INFO] User pass a file path, try scanning it directly...\n");
        if (!is_regular_file(path)) {
            fprintf(stderr, "[ERROR] This IS NOT a regular file, aborting...\n");
            cl_engine_free(engine);
            return 1;
        } 
        process_file(path, engine, &options);
        cl_engine_free(engine); // Free the engine
        return EXIT_SUCCESS;
    }

    int num_workers = 0;
    if (argc < 3) {
        printf("[WARNING] User did not specify the number of worker processes, use default value 1...\n");
        num_workers = 1;
    }
    else num_workers = atoi(argv[2]);

    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);

    /* Initialize the shared memory */
    size_t shm_size = sizeof(SharedData);
    shm_id = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | 0666);
    if (shm_id == -1) {
        fprintf(stderr, "[ERROR] Failed to create shared memory!\n");
        cl_engine_free(engine);
        return EXIT_FAILURE;
    }
    
    shm = shmat(shm_id, NULL, 0);
    if (shm == (void*)-1) {
        fprintf(stderr, "[ERROR] Failed to attach shared memory!\n");
        cl_engine_free(engine);
        return 1;
    }
    memset(shm, 0, shm_size);

    /* Initialize the semaphore */
    if (sem_init(&shm->mutex, 1, 1) != 0 ||
        sem_init(&shm->empty, 1, MAX_TASKS) != 0 ||
        sem_init(&shm->full, 1, 0) != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize semaphore!\n");
        shmdt(shm);
        shmctl(shm_id, IPC_RMID, NULL);
        cl_engine_free(engine);
        return 1;
    }

    // Add initial tasks to the task queue
    printf("[INFO] Start adding initial tasks to the task queue...\n");
    pid_t producer_pid = fork(); // Fork a producer process
    if (producer_pid == 0) { // If the child process is the producer process
        producer_main(path); // Scan the directory and add tasks to the task queue
        exit(EXIT_SUCCESS);
    }
    else if (producer_pid < 0) { // If fork failed
        fprintf(stderr, "[ERROR] Failed to fork a producer process!\n");
        shmdt(shm);
        shmctl(shm_id, IPC_RMID, NULL);
        cl_engine_free(engine);
        return EXIT_FAILURE;
    }

    // Create worker processes
    printf("[INFO] Start creating worker processes...\n");
    pid_t worker_pids[num_workers];
    for (int i = 0; i < num_workers; i++) {
        worker_pids[i] = fork(); // Fork a worker process
        if (worker_pids[i] == 0) { // If the child process is a worker process
            worker_main(); // Scan files from the task queue
            exit(EXIT_SUCCESS);
        } else if (worker_pids[i] < 0) {
            fprintf(stderr, "[ERROR] Failed to fork a worker process!\n");
            // If fork fails, terminate all previously created processes
            atomic_store(&shm->should_exit, 1);
            for (int j = 0; j < i; j++) {
                kill(worker_pids[j], SIGTERM);
            }
            kill(producer_pid, SIGTERM);
            break;
        }
    }

    // Wait for all child processes to exit
    int status;
    waitpid(producer_pid, &status, 0);

    // Send exit tasks to the task queue
    for (int i = 0; i < num_workers; i++) {
        ScanTask exit_task = {EXIT_TASK, {0} };
        add_task(&exit_task);
    }
    
    // Wait for workers to finish
    for (int i = 0; i < num_workers; i++) {
        if (worker_pids[i] > 0) {
            waitpid(worker_pids[i], &status, 0);
        }
    }

    // Clean up
    sem_destroy(&shm->mutex);
    sem_destroy(&shm->empty);
    sem_destroy(&shm->full);
    shmdt(shm);
    shmctl(shm_id, IPC_RMID, NULL);
    cl_engine_free(engine); // Free the engine
    
    printf("[INFO] Scan completed successfully.\n");
}