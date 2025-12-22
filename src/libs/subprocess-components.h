/* subproccess-components.h
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

/* This store some functions to control the subprocess */

#pragma once

#include <glib.h>
#include <poll.h>

#include "ring-buffer.h"

#define BASE_TIMEOUT_MS 100

typedef struct IdleData IdleData;

/* Get the context from the `IdleData` */
gpointer
get_idle_context(IdleData *idle_data);

/* Get the message from the `IdleData` */
const char *
get_idle_message(IdleData *idle_data);

/* Get the exit status from the `IdleData` */
int
get_idle_exit_status(IdleData *idle_data);

/* Wait for the process to finish and return the exit status */
gint
wait_for_process(pid_t pid, int flags);

/* Process the subprocess stdout message */
/*
  * ring_buf: the ring buffer to store the output messages
  * pipefd: the pipe file descriptor to read the output messages
  * context: the context data for the callback function
  * callback_function: the callback function to process the output lines
*/
gboolean
process_output_lines(RingBuffer *ring_buf, int pipefd, gpointer context,
                      GSourceFunc callback_function);

/* Send the final message from the subprocess to the main process */
/*
  * context: the context data for the callback function
  * message: the final message from the subprocess
  * is_success: whether the subprocess is exited successfully or not
*/
void
send_final_message(gpointer context, const char *message, gboolean is_success, int exit_status,
                    GSourceFunc callback_function);

/* Spawn a new process */
// path & command: use for `execv()`
// This function MUST end with a NULL argument to indicate the end of the arguments list
gboolean
spawn_new_process(int pipefd[2], pid_t *pid, const char *path, const char *command, ...);

/* Spawn a new process but with no pipes */
// No pipes means you can pass `FIFO` or `Unix Socket` as input/output
// But this function won't provide any parameters to pass `FIFO` or `Unix Socket` , you need to pass directly in the command line
// It might be useful when you design your own programs and communicate each other through `FIFO` or `Unix Socket`
// path & command: use for `execv()`
// This function MUST end with a NULL argument to indicate the end of the arguments list
gboolean
spawn_new_process_no_pipes(pid_t *pid, const char *path, const char *command, ...);
