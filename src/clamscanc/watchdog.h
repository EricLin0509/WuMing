/* watchdog.h
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

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>
#include <stdatomic.h>

#define MAX_PROCESSES 64 // maximum number of processes can be used for scanning

typedef void (*mission_callback)(void *args); // The mission callback function type
typedef void (*signal_handler)(int signal); // The signal handler callback function type

/* Current status */
/*
  * `STATUS_UNFINISHED` means the scanning process is still running
  * `STATUS_FORCE_QUIT` means the scanning process should be terminated immediately
*/
typedef enum {
    STATUS_UNFINISHED = 0,
    STATUS_PRODUCER_DONE = 1,
    STATUS_ALL_TASKS_DONE = 2,
    STATUS_FORCE_QUIT = 3
} CurrentStatus;

/* Initialize the current status of the scanning process */
void init_status(_Atomic CurrentStatus *status);

/* Get the current status of the scanning process */
CurrentStatus get_status(_Atomic CurrentStatus *status);

/* Set the current status of the scanning process */
void set_status(_Atomic CurrentStatus *status, CurrentStatus new_status);

/* Register the signal handler for the exit condition signal */
/*
  * @param signal
  * The signal to be registered
  * @param handler
  * The signal handler for the signal
*/
void register_signal_handler(int signal, signal_handler handler);

/* Observer */
/*
  * `num_of_processes` is the number of processes to be created
  * `active_processes` is the number of active processes
  * `pids` is an array of process IDs
  * `pipe_fd` is the pipe file descriptor for watchdog to communicate with the parent process
  * `exit_condition_signal` is the signal to be sent to the processes to exit
  * `condition_signal_handler` is the signal handler for the exit condition signal
*/
typedef struct {
    size_t num_of_processes;
    _Atomic size_t active_processes;
    pid_t *pids;

    /* The exit control components */
    int pipe_fd[2];
    int exit_condition_signal;
    signal_handler condition_signal_handler;
} Observer;

/* Initialize the observer */
/*
  * @param observer
  * The observer object to be initialized
  * @param num_of_processes
  * The number of processes to be created
  * @param exit_condition_signal
  * The signal to be sent to the processes to exit
  * @param condition_signal_handler
  * The signal handler for the exit condition signal
  * @return
  * `true` if the observer is initialized successfully, `false` otherwise
*/
bool observer_init(Observer *observer, size_t num_of_processes, int exit_condition_signal, signal_handler condition_signal_handler);

/* Clear the observer */
void observer_clear(Observer *observer);

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
                    mission_callback mission_callback, void *mission_callback_args);

/* Notify the watchdog that the child process has finished */
/*
  * @param observer
  * The observer object to send the notification
  * @warning This function should be called in the child process (producer or worker process)
*/
void notify_watchdog(Observer *observer);

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
void watchdog_main(Observer *observer, _Atomic CurrentStatus *current_status, CurrentStatus target_status);

#endif /* WATCHDOG_H */