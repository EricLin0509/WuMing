/* clamscanc.c
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

#include <unistd.h>

#include "manager.h"

#define CLAMP(x, low, high) ((x) < (low) ? (low) : ((x) > (high) ? (high) : (x))) // Use for clamping the number of processes

SharedMemory *shm;
pid_t parent_pid;

/* Signal handler for terminating the scan */
void shutdown_handler(int sig) {
    if (getpid() != parent_pid) return; // Only the parent process can handle the signal

    write(STDERR_FILENO, "\n[INFO] Terminating the scan, shutting down...\n", 48);
    set_status(&shm->current_status, STATUS_FORCE_QUIT);
}

/* The exit signal handler */
static void exit_signal(int sig) {
    _exit(EXIT_SUCCESS);
}

/* The exit condition for the producer process */
static inline void is_producer_done(TaskQueue *dir_tasks) {
    if (is_task_queue_empty_assumption(dir_tasks)) {
        notify_watchdog(&shm->producer_observer); // Notify the watchdog that the producer is done
        set_status(&shm->current_status, STATUS_PRODUCER_DONE); // Set the status to producer done
    }
}

/* Treverse the directory and add tasks to the task queue */
/*
  * WARNING: This function MUST be called by a producer process
*/
static void producer_main(void *args) {
    if (args == NULL) return; // Check if the argument is valid
    TaskQueue *queue = (TaskQueue*)args; // Get the task queue from the argument

    Task task[MAX_GET_TASKS]; // Initialize tasks array to get tasks from the task queue
    while (get_status(&shm->current_status) != STATUS_FORCE_QUIT) {
        size_t tasks_to_get = task_queue_get(queue, task); // Get tasks from the task queue
        if (tasks_to_get == 0) {
            is_producer_done(queue); // Check if the producer is done
            continue; // If there is no task to get, continue
        }

        for (size_t i = 0; i < tasks_to_get; i++) {
            if (task[i].type != TASK_SCAN_DIR) continue; // Skip invalid tasks type
            traverse_directory(task[i].path, &shm->dir_tasks, &shm->file_tasks); // Traverse the directory and add tasks to the task queue
        }
    }
}

/* The exit condition for worker processes */
static inline void is_all_task_done(TaskQueue *file_tasks) {
    if (get_status(&shm->current_status) == STATUS_PRODUCER_DONE) { // First check if the producer is done
        if (is_task_queue_empty_assumption(file_tasks)) { // Then check if the task queue is empty and all tasks are done
            notify_watchdog(&shm->worker_observer); // Notify the watchdog that all tasks are done
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

    Task task[MAX_GET_TASKS]; // Initialize task array to get tasks from the task queue
    while (get_status(&shm->current_status) != STATUS_FORCE_QUIT) {
        size_t tasks_to_get = task_queue_get(queue, task); // Get tasks from the task queue
        if (tasks_to_get == 0) {
            is_all_task_done(queue); // Check if all tasks are done
            continue; // If there is no task to get, continue
        }

        for (size_t i = 0; i < tasks_to_get; i++) {
            if (task[i].type != TASK_SCAN_FILE) continue; // Skip invalid tasks type
            process_file(task[i].path, &shm->essentials); // Scan the file
        }
    }
}

/* Scan a single file directly without creating a task queue */
static void scan_file_directly(const char *path) {
    ClamavEssentials essentials;
    if (!clamav_essentials_init(&essentials)) {
        fprintf(stderr, "Failed to initialize ClamAV essentials\n");
        return;
    }
    process_file(path, &essentials);
    clamav_essentials_clear(&essentials);
}

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <directory> [num_of_processes]\n", argv[0]);
        return 1;
    }

    char *real_path = realpath(argv[1], NULL);
    if (real_path == NULL) {
        fprintf(stderr, "Failed to get real path of %s\n", argv[1]);
        return 1;
    }

    if (!is_directory(real_path)) {
        if (!is_regular_file(real_path)) {
            fprintf(stderr, "%s is not a directory or a regular file\n", real_path);
            free(real_path);
            return 1;
        }
        // process single file
        printf("%s is a regular file, try scanning it directly\n", real_path);
        scan_file_directly(real_path);
        free(real_path);
        return 0;
    }

    size_t num_workers = argc > 2 ? CLAMP(atoi(argv[2]), 1, MAX_PROCESSES) : 1; // Get the number of worker processes from the argument or default to 1
    size_t num_producers = num_workers >= 8 ? 4 : 2; // Set the number of producers to 4 if the number of worker processes is greater or equal to 8, otherwise set it to 2

    if (!shared_memory_init(&shm)) {
        fprintf(stderr, "Failed to initialize shared memory\n");
        free(real_path);
        return 1;
    }

    // Set the signal handlers
    register_signal_handler(SIGINT, shutdown_handler);
    register_signal_handler(SIGTERM, shutdown_handler);

    /* Add initial tasks to the task queue */
    Task task = build_task(TASK_SCAN_DIR, real_path);
    task_queue_add(&shm->dir_tasks, task);
    free(real_path);
    parent_pid = getpid();

    /* Spawn the producer and worker processes */
    bool spawn_result = true;
    observer_init(&shm->producer_observer, num_producers, SIGUSR1, exit_signal);
    observer_init(&shm->worker_observer, num_workers, SIGUSR2, exit_signal);

    spawn_result &= spawn_new_process(&shm->producer_observer,
                            producer_main, (void*)&shm->dir_tasks);

    spawn_result &= spawn_new_process(&shm->worker_observer,
                            worker_main, (void*)&shm->file_tasks);

    if (!spawn_result) {
        fprintf(stderr, "[ERROR] Failed to spawn processes, aborting...\n");
        set_status(&shm->current_status, STATUS_FORCE_QUIT);
    }

    // Wait for all child processes to exit
    watchdog_main(&shm->producer_observer, &shm->current_status, STATUS_PRODUCER_DONE);
    watchdog_main(&shm->worker_observer, &shm->current_status, STATUS_ALL_TASKS_DONE);
}