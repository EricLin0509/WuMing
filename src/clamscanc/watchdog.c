/* watchdog.c
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

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "watchdog.h"

#define NOTIFY_MSG "done" // the message to be sent to the watchdog
#define NOTIFY_MSG_SIZE (sizeof(NOTIFY_MSG) - 1) // the size of the message to be sent to the watchdog
#define SIGACTION_FLAGS (SA_RESTART | SA_NOCLDSTOP) // restart interrupted system calls

/* Initialize the current status of the scanning process */
void init_status(_Atomic CurrentStatus *status) {
	atomic_init(status, STATUS_UNFINISHED);
}

/* Get the current status of the scanning process */
CurrentStatus get_status(_Atomic CurrentStatus *status) {
	return atomic_load(status);
}

/* Set the current status of the scanning process */
void set_status(_Atomic CurrentStatus *status, CurrentStatus new_status) {
	atomic_store(status, new_status);
}

/* Register the signal handler for the exit condition signal */
/*
  * @param signal
  * The signal to be registered
  * @param handler
  * The signal handler for the signal
*/
void register_signal_handler(int signal, signal_handler handler) {
	if (signal <= 0 || handler == NULL) return; // Skip invalid parameters

	/* Set up the sigaction structure */
	struct sigaction action;
	action.sa_handler = handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SIGACTION_FLAGS; // restart interrupted system calls

	/* Register the signal handler */
	if (sigaction(signal, &action, NULL) < 0) {
		fprintf(stderr, "[ERROR] register_signal_handler: Failed to register the signal handler for signal %d\n", signal);
	}
}

/* Initialize the observer */
bool observer_init(Observer *observer, size_t num_of_processes, int exit_condition_signal, signal_handler condition_signal_handler) {
    if (observer == NULL || num_of_processes > MAX_PROCESSES || num_of_processes == 0) {
        fprintf(stderr, "[ERROR] observer_init: Invalid observer or number of processes\n");
        return false;
    }

    if (pipe(observer->pipe_fd) == -1) {
        fprintf(stderr, "[ERROR] observer_init: Failed to create pipe\n");
        return false;
    }

    observer->pids = calloc(num_of_processes, sizeof(pid_t));

    if (observer->pids == NULL) {
        fprintf(stderr, "[ERROR] observer_init: Failed to allocate memory for pids\n");
        return false;
    }

    observer->num_of_processes = num_of_processes;

    if (condition_signal_handler != NULL && exit_condition_signal != 0) {
        observer->exit_condition_signal = exit_condition_signal;
        observer->condition_signal_handler = condition_signal_handler;
    }

    return true;
}

/* Clear the observer */
void observer_clear(Observer *observer) {
    if (observer == NULL) return;

    free(observer->pids);
    observer->pids = NULL;
    observer->num_of_processes = 0;
    observer->exit_condition_signal = 0;
    observer->condition_signal_handler = NULL;
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
                     mission_callback mission_callback, void *mission_callback_args) {
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

/* Notify the watchdog that the child process has finished */
/*
  * @param observer
  * The observer object to send the notification
  * @warning This function should be called in the child process (producer or worker process)
  * @note This function will decrease the `active_processes` counter in the observer object, the message only send to the watchdog when the `active_processes` counter is 0
*/
void notify_watchdog(Observer *observer) {
	if (observer == NULL || observer->pipe_fd[1] == -1) return; // Skip invalid parameters

    close(observer->pipe_fd[0]); // close the read end of the pipe
    write(observer->pipe_fd[1], NOTIFY_MSG, NOTIFY_MSG_SIZE); // send the message to the watchdog
    close(observer->pipe_fd[1]); // close the write end of the pipe
}

/* Get message from the pipe */
static bool get_message_from_pipe(int *pipe_fd) {
	close(pipe_fd[1]); // close the write end of the pipe
	char msg[NOTIFY_MSG_SIZE + 1];
	int read_size = read(pipe_fd[0], msg, NOTIFY_MSG_SIZE);
	close(pipe_fd[0]); // close the read end of the pipe

	if (read_size != NOTIFY_MSG_SIZE) return false; // failed to read the message from the pipe

	return memcmp(msg, NOTIFY_MSG, NOTIFY_MSG_SIZE) == 0; // check if the message is correct
} 

/* Send the signal to the target process to exit */
static void send_signal_to_all_processes(Observer *observer) {
	if (observer == NULL || observer->exit_condition_signal <= 0) return; // No need to send the signal if there is no exit condition signal

	for (size_t i = 0; i < observer->num_of_processes; i++) {
		if (observer->pids[i] == 0) continue; // Skip the invaild pid (0)

        kill(observer->pids[i], observer->exit_condition_signal); // Send the exit condition signal to the child process
        if (waitpid(observer->pids[i], NULL, WNOHANG) == 0) {
            i -= 1; // The child process is still running, keep this index for the next iteration
            continue;
        }

        observer->pids[i] = 0; // Set the pid to 0 to indicate that the child process has been terminated
	}
}

/* The main function for watchdog */
/*
  * @warning This function should be called in the main process (parent process)
  * @param observer
  * The observer object to be check
  * @param current_status
  * The current status of the scanning process, if `STATUS_FORCE_QUIT` is set, the scanning process will be terminated immediately
*/
void watchdog_main(Observer *observer, _Atomic CurrentStatus *current_status, CurrentStatus target_status) {
    if (observer == NULL || current_status == NULL) return; // Skip invalid parameters

    struct pollfd fds = {
        .fd = observer->pipe_fd[0],
       .events = POLLIN,
    };

    int timeout = 100; // 100ms timeout for the poll function

    while (get_status(current_status) < target_status) {
        int poll_result = poll(&fds, 1, timeout);

        if (poll_result == -1 && errno != EINTR) {
            fprintf(stderr, "[ERROR] watchdog_main: Failed to poll the pipe: %s\n", strerror(errno));
            close(observer->pipe_fd[0]); // close the read end of the pipe
            close(observer->pipe_fd[1]); // close the write end of the pipe
            break;
        }
        else if (poll_result == 0) continue; // Timeout, continue the loop
        else if (poll_result > 0 && get_message_from_pipe(observer->pipe_fd)) break; // Message received, break the loop
    }

    send_signal_to_all_processes(observer); // Send the exit condition signal to all the child processes
}