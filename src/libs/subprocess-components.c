/* subprocess-components.c
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

#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#include "subprocess-components.h"
#include "ring-buffer.h"

typedef struct IdleData {
    gpointer context; // context that store some GTKWidgets or other data (e.g. some GTKWidgets you want to control it)
    const char *message; // message to send to the subprocess
} IdleData;

/* Get the context from the `IdleData` */
gpointer
get_idle_context(IdleData *idle_data)
{
    g_return_val_if_fail(idle_data != NULL, NULL);

    return idle_data->context;
}

/* Get the message from the `IdleData` */
const char *
get_idle_message(IdleData *idle_data)
{
    g_return_val_if_fail(idle_data != NULL, NULL);

    return idle_data->message;
}

static IdleData *
idle_data_new(void *context, const char *message)
{
    g_return_val_if_fail(context != NULL, NULL);

    IdleData *data = g_new0(IdleData, 1);
    data->context = context;
    data->message = message;

    return data;
}

static void
idle_data_clear(gpointer user_data)
{
  g_return_if_fail(user_data != NULL);

  IdleData *data = (IdleData *)user_data; // Cast the data to IdleData struct
  g_clear_pointer(&data, g_free);
}

/* Calculate the dynamic timeout based on the idle_counter and current_timeout */
/*
  * ready_status: this is the result of `poll()`, it indicates whether the subprocess is ready to read/write
  * This parameter can be NULL if you don't need to reset the idle_counter
*/
int
calculate_dynamic_timeout(int *idle_counter, int *current_timeout, int *ready_status)
{
    /* Check the input parameters */
    g_return_val_if_fail(idle_counter != NULL, 0);
    g_return_val_if_fail(current_timeout != NULL, 0);

    const int jitter = rand() % JITTER_RANGE; // Add random jitter to the timeout to minimize the wake up of threads at the same time

    if (ready_status != NULL && *ready_status > 0) // Optional parameter, reset the idle_counter if the subprocess is ready to read/write
    {
        *idle_counter = 0; // Reset the idle_counter if the subprocess is ready to read/write
        *current_timeout = BASE_TIMEOUT_MS; // Reset the timeout to the base value
    }

    if (++(*idle_counter) > MAX_IDLE_COUNT)
    {
        *current_timeout = MIN(*current_timeout * 2, MAX_TIMEOUT_MS); // Increase the timeout if the thread is idle for a long time
        *idle_counter = 0;
    }

    return CLAMP(*current_timeout + jitter, BASE_TIMEOUT_MS, MAX_TIMEOUT_MS);
}

/* Wait for the process to finish and return the exit status */
gint
wait_for_process(pid_t pid, int flags)
{
    int status;

    int wait_result = waitpid(pid, &status, flags); // Wait for the process to finish
    switch (wait_result)
    {
        case -1:
            g_critical("Failed to wait for process: %s", strerror(errno));
            return -1; // Return -1 if failed to wait for the process
        case 0: // State not changed, the process is still running (Especially when `WNOHANG` is set)
            return -1;
        default: // The process is finished
            break;
    }

    const gint exit_status = WEXITSTATUS(status);
    g_debug("Process exited with status %d", exit_status);
    
    return exit_status;
}

/* Handle the input/output event */
gboolean
handle_io_event(IOContext *io_ctx)
{
    const size_t free_space = ring_buffer_available(io_ctx->ring_buf);
    const size_t buf_size = CLAMP(buf_size, 512, 4096); // Set the buffer size to 512-4096 bytes

    char read_buf[buf_size];
    ssize_t n = 0;

    if ((n = read(io_ctx->pipefd, read_buf, buf_size)) > 0) // Read from the pipe
    {
        size_t written = ring_buffer_write(io_ctx->ring_buf, read_buf, n); // Write to the ring buffer
        if (written < (size_t)n)
        {
            g_warning("Ring buffer overflow, lost %zd bytes", n - written);
        }
        return TRUE;
    }
    return FALSE;
}

/* Process the subprocess stdout message */
/*
  * io_ctx: the IO context
  * context: the context data for the callback function
  * callback_function: the callback function to process the output lines
  * destroy_notify: the cleanup function for the context data
*/
void
process_output_lines(IOContext *io_ctx, gpointer context,
                      GSourceFunc callback_function)
{
    g_return_if_fail(io_ctx != NULL);
    g_return_if_fail(callback_function != NULL);
    g_return_if_fail(context != NULL);

    char *line;
    while ((line = ring_buffer_find_new_line(io_ctx->ring_buf)) != NULL)
    {
        IdleData *data = idle_data_new(context, line);

        /* Send the message to the main process */
        g_main_context_invoke_full( // Invoke the callback function in the main context
                       g_main_context_default(),
                       G_PRIORITY_HIGH_IDLE,
                       (GSourceFunc) callback_function,
                       data,
                       (GDestroyNotify)idle_data_clear);
    }
}

/* Send the final message from the subprocess to the main process */
/*
  * context: the context data for the callback function
  * message: the final message from the subprocess
  * is_success: whether the subprocess is exited successfully or not
  * callback_function: the callback function to process the final message
  * destroy_notify: the cleanup function for the context data
*/
void
send_final_message(gpointer context, const char *message, gboolean is_success,
                    GSourceFunc callback_function)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(callback_function != NULL);
    g_return_if_fail(context != NULL);

    /* Create final status message */
    IdleData *complete_data = idle_data_new(context, message);

    /* Send the final message to the main process */
    g_main_context_invoke_full( // Invoke the callback function in the main context
                   g_main_context_default(),
                   G_PRIORITY_HIGH_IDLE,
                   (GSourceFunc) callback_function,
                   complete_data,
                   (GDestroyNotify)idle_data_clear);
}

/* Build the command arguments */
static GPtrArray *
build_command_args(const char *command, va_list args)
{
    GPtrArray *argv = g_ptr_array_new();
    g_ptr_array_add(argv, (gpointer)command);

    char *arg;
    while ((arg = va_arg(args, char *)) != NULL)
    {
        g_ptr_array_add(argv, arg);
    }

    // Add a NULL argument to indicate the end of the arguments list
    g_ptr_array_add(argv, NULL);

    return argv;
}

/* Spawn a new process */
// path & command: use for `execv()`
// This function MUST end with a NULL argument to indicate the end of the arguments list
gboolean
spawn_new_process(int pipefd[2], pid_t *pid, const char *path, const char *command, ...)
{
    if (access(path, X_OK) == -1) // First check if the path is valid
    {
        g_critical("[ERROR] Cannot execute %s: %s", path, strerror(errno));
        goto error_clean_up;
    }

    if (pipe(pipefd) == -1) // Create a pipe for communication
    {
        g_critical("[ERROR] Failed to create pipe: %s", strerror(errno));
        goto error_clean_up;
    }

    /* Set the read end of the pipe to non-blocking mode */
    int curr_flags = fcntl(pipefd[0], F_GETFL, 0);
    if (curr_flags == -1 || fcntl(pipefd[0], F_SETFL, curr_flags | O_NONBLOCK) == -1)
    {
        g_critical("[ERROR] Failed to set pipe read end to non-blocking mode: %s", strerror(errno));
        goto error_clean_up;
    }

    if ((*pid = fork()) == -1) // Fork a new process
    {
        g_critical("[ERROR] Failed to fork: %s", strerror(errno));
        goto error_clean_up;
    }

    if (*pid == 0) // Child process
    {
        prctl(PR_SET_PDEATHSIG, SIGTERM); // Ensure the child process can be terminated when the parent process dies
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);

        va_list args;
        va_start(args, command);
        GPtrArray *argv = build_command_args(command, args);
        va_end(args);
        assert(g_ptr_array_index(argv, argv->len-1) == NULL); // Check whether the last argument is NULL

        execv(path, (char **)argv->pdata);

        g_ptr_array_free(argv, TRUE);
        exit(EXIT_FAILURE);
    }
    
    close(pipefd[1]);
    return TRUE;

error_clean_up:
    close(pipefd[0]);
    close(pipefd[1]);
    return FALSE;
}

/* Spawn a new process but with no pipes */
// No pipes means you can pass `FIFO` or `Unix Socket` as input/output
// But this function won't provide any parameters to pass `FIFO` or `Unix Socket` , you need to pass directly in the command line
// It might be useful when you design your own programs and communicate each other through `FIFO` or `Unix Socket`
// path & command: use for `execv()`
// This function MUST end with a NULL argument to indicate the end of the arguments list
gboolean
spawn_new_process_no_pipes(pid_t *pid, const char *path, const char *command, ...)
{
    if (access(path, X_OK) == -1) // First check if the path is valid
    {
        g_critical("[ERROR] Cannot execute %s: %s", path, strerror(errno));
        return FALSE;
    }

    if ((*pid = fork()) == -1) // Fork a new process
    {
        g_critical("[ERROR] Failed to fork: %s", strerror(errno));
        return FALSE;
    }

    if (*pid == 0) // Child process
    {
        prctl(PR_SET_PDEATHSIG, SIGTERM); // Ensure the child process can be terminated when the parent process dies

        va_list args;
        va_start(args, command);
        GPtrArray *argv = build_command_args(command, args);
        va_end(args);
        assert(g_ptr_array_index(argv, argv->len-1) == NULL); // Check whether the last argument is NULL

        execv(path, (char **)argv->pdata);

        g_ptr_array_free(argv, TRUE);
        exit(EXIT_FAILURE);
    }

    return TRUE;
}
