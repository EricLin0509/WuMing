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

#define JITTER_RANGE 30
#define MAX_IDLE_COUNT 5 // If idle_counter greater than this, use MAX_TIMEOUT_MS
#define BASE_TIMEOUT_MS 50
#define MAX_TIMEOUT_MS 1000

typedef struct {
    int pipefd;
    RingBuffer ring_buf;

    struct pollfd fds;
    int idle_counter;
    int dynamic_timeout;
} IOContext;

typedef struct IdleData IdleData;

/* Initialize the IO context */
IOContext
io_context_init(int pipefd, int poll_events, int idle_counter, int dynamic_timeout);

/* IO context handle poll events */
int
io_context_handle_poll_events(IOContext *io_ctx);

/* Calculate the dynamic timeout based on the idle_counter and current_timeout */
/*
  * ready_status: this is the result of `poll()`, it indicates whether the subprocess is ready to read/write
  * This parameter can be NULL if you don't need to reset the idle_counter
*/
int
calculate_dynamic_timeout(IOContext *io_ctx, int *ready_status);

/* Get the context from the `IdleData` */
gpointer
get_idle_context(IdleData *idle_data);

/* Get the message from the `IdleData` */
const char *
get_idle_message(IdleData *idle_data);

/* Wait for the process to finish and return the exit status */
gint
wait_for_process(pid_t pid, int flags);

/* Handle the input/output event */
gboolean
handle_input_event(IOContext *io_ctx);

/* Process the subprocess stdout message */
/*
  * io_ctx: the IO context
  * context: the context data for the callback function
  * callback_function: the callback function to process the output lines
*/
void
process_output_lines(IOContext *io_ctx, gpointer context,
                      GSourceFunc callback_function);

/* Send the final message from the subprocess to the main process */
/*
  * context: the context data for the callback function
  * message: the final message from the subprocess
  * is_success: whether the subprocess is exited successfully or not
*/
void
send_final_message(gpointer context, const char *message, gboolean is_success,
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
