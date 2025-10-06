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

/* This is the watchdog implementation for clamscanp */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <poll.h>

#include "watchdog.h"

#define NOTIFY_MSG "done" // the message to be sent to the watchdog
#define NOTIFY_MSG_SIZE (sizeof(NOTIFY_MSG) - 1) // the size of the message to be sent to the watchdog
#define SIGACTION_FLAGS (SA_RESTART | SA_NOCLDSTOP) // restart interrupted system calls

#define MIN(a, b) ((a) < (b) ? (a) : (b)) // macro to get the minimum value between two values

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

/* Initialize the observer object */
/*
  * @param dest
  * The observer object to be initialized
  * @param num_of_processes
  * The number of processes to be created
  * @param exit_condition_signal
  * The signal to be sent to the processes to exit [OPTIONAL]
  * @param condition_signal_handler
  * The signal handler for the exit condition signal [OPTIONAL]
*/
void init_observer(Observer *dest, size_t num_of_processes, int exit_condition_signal, signal_handler condition_signal_handler) {
	dest->pids = calloc(num_of_processes, sizeof(pid_t));
	dest->num_of_processes = num_of_processes;
	pipe(dest->pipe_fd);
	
	if (exit_condition_signal > 0 && condition_signal_handler != NULL) {
		dest->exit_condition_signal = exit_condition_signal;
		dest->condition_signal_handler = condition_signal_handler;
	}
}

/* Destroy the observer object */
void destroy_observer(Observer *observer) {
	if (observer == NULL) return; // Skip invalid parameters

	if (observer->pids != NULL) { // Only free the memory if it is not NULL
		free(observer->pids);
		observer->pids = NULL;
	}

	observer->num_of_processes = 0;
	observer->exit_condition_signal = 0;
	observer->condition_signal_handler = NULL;
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

/* Notify the watchdog that the child process has finished */
/*
  * @param observer
  * The observer object to send the notification
  * @warning This function should be called in the child process (producer or worker process)
*/
void notify_watchdog(Observer *observer) {
	if (observer == NULL || observer->pipe_fd[1] == -1) return; // Skip invalid parameters

	close(observer->pipe_fd[0]); // close the read end of the pipe
	write(observer->pipe_fd[1], NOTIFY_MSG, NOTIFY_MSG_SIZE); // send the message to the watchdog
	close(observer->pipe_fd[1]); // close the write end of the pipe
}

/* Wait for the processes in the observer */
/*
  * @param observer
  * The observer to be waited for
  * @param options
  * Use for `waitpid()` options [OPTIONAL]
  * @return
  * `true` if all the processes in the can be waited for, `false` otherwise
*/
static bool wait_for_processes(Observer *observer, int options) {
	if (observer == NULL || observer->pids == NULL) return false; // Skip invalid parameters

	for (size_t i = 0; i < observer->num_of_processes; i++) {
		if (waitpid(observer->pids[i], NULL, options) < 0) {
			fprintf(stderr, "[ERROR] wait_for_processes: Failed to wait for the child process %d\n", observer->pids[i]);
			return false; // failed to wait for the child process
		}
	}

	return true; // all the processes have been waited for
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
		if (kill(observer->pids[i], observer->exit_condition_signal) < 0) {
			fprintf(stderr, "[ERROR] send_exit_signal: Failed to send the exit signal to the process %d\n", observer->pids[i]);
		}
	}
}

/* The main function for watchdog */
/*
  * @warning This function should be called in the main process (parent process)
  * @param observer
  * The observer object to be check
  * @param current_status
  * The current status of the scanning process
  * @param target_status
  * The target status to exit the main loop if `current_status` is greater or equal to this value
*/
void watchdog_main(Observer *observer, _Atomic CurrentStatus *current_status, CurrentStatus target_status) {
	if (observer == NULL || current_status == NULL) return; // Skip invalid parameters
	
	close(observer->pipe_fd[1]); // close the write end of the pipe
	struct pollfd pollfds = {
		.fd = observer->pipe_fd[0], // the file descriptor to be polled
		.events = POLLIN, // the events to be polled for
	};

	CurrentStatus status_copy;
	while ((status_copy = get_status(current_status)) < target_status) { // wait for the process to set the target status (finished)
		int is_ready = poll(&pollfds, 1, 100);

		if (is_ready > 0 && pollfds.revents & POLLIN) {
			if (get_message_from_pipe(observer->pipe_fd)) {
				set_status(current_status, target_status); // Fallback set the status to the target status if the message is received
				break; // exit the loop if the message is received
			}
		}
	}

	send_signal_to_all_processes(observer); // send the exit signal to all the child processes
	if (!wait_for_processes(observer, 0)) { // ensure all the child processes have been exited
		set_status(current_status, STATUS_FORCE_QUIT); // failed to wait for the child process, force quit
	}
}