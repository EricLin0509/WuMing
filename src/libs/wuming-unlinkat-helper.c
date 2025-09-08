/* wuming-unlinkat-helper.c
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


/* This is the elevated helper program for `unlinkat()` operation */

#include <gio/gio.h>
#include <stdio.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>

#include "wuming-unlinkat-helper.h"
#include "file-security.h"

/* Verify the authentication key */
static inline gboolean 
verify_key(uint32_t expected, uint32_t received)
{
    return expected == received;
}

/* Get the authentication key from the command line argument */
static uint32_t
get_auth_key(const char *auth_key_str)
{
    uint32_t auth_key = 0;
    if (sscanf(auth_key_str, "%" PRIu32, &auth_key) != 1)
    {
        g_critical("Invalid authentication key: %s", auth_key_str);
        return 0;
    }
    return auth_key;
}

/* Prevent handle `SIGTRAP` (breakpoint) and `SIGILL` (illegal instruction) signal when the program is running */
void
breakpoint_handler(int signal)
{
    g_critical("[ERROR] Breakpoint detected, aborting...");
    exit(EXIT_FAILURE);
}

/* Similiar to `validate_by_fd` in `file-security.c`, but this function use struct `stat` to validate the file security status. */
static gboolean
validate_by_stat(FileSecurityContext *context, const struct stat *stat_data, const gboolean is_check_directory)
{
    if (!context || !stat_data)
    {
        g_critical("Invalid arguments.");
        return FALSE;
    }

    /* If check directory, only check the directory device */
    if (is_check_directory)
    {
        return (stat_data->st_dev == context->dir_stat.st_dev);
    }

    /* Otherwise, compare all the metadata, content, create time, modification time */

    // Metadata comparison
    const gboolean descriptor_match = 
        (stat_data->st_dev == context->file_stat.st_dev &&
         stat_data->st_ino == context->file_stat.st_ino);

    // Content comparison
    const gboolean content_match = 
        (stat_data->st_nlink == context->file_stat.st_nlink &&
         stat_data->st_size == context->file_stat.st_size);
    
    // Create time comparison
    const gboolean create_time_match = 
        (stat_data->st_ctime == context->file_stat.st_ctime &&
        stat_data->st_ctim.tv_nsec == context->file_stat.st_ctim.tv_nsec);

    // Modification time comparison
    const gboolean modification_time_match = 
        (stat_data->st_mtime == context->file_stat.st_mtime &&
        stat_data->st_mtim.tv_nsec == context->file_stat.st_mtim.tv_nsec);

    return descriptor_match && content_match && create_time_match && modification_time_match;
}

gint
command_line_handler(GApplication* self, GApplicationCommandLine* command_line, gpointer user_data)
{
    // Register the breakpoint handler
    signal(SIGTRAP, breakpoint_handler);
    signal(SIGILL, breakpoint_handler);
    
    // Check if the program is running as root
    if (getuid() != 0)
    {
        g_critical("This program must be run as root");
        return EXIT_FAILURE;
    }

    // Get the command line arguments
    gint argc;
    gchar **argv = g_application_command_line_get_arguments(command_line, &argc); // Get the command line arguments;

    // Check the number of command line arguments
    if (argc != 4)
    {
        g_critical("Invalid arguments");
        g_critical("Usage: %s <fifo_path> <file_path> <auth_key>", argv[0]);
        return EXIT_FAILURE;
    }

    // Get the fifo path and file path from the command line arguments
    const char *fifo_path = argv[1];
    const char *file_path = argv[2];

    int fifo_fd = open(fifo_path, O_RDONLY);
    if (fifo_fd < 0)
    {
        g_critical("Failed to open the fifo: %s", fifo_path);
        return EXIT_FAILURE;
    }

    HelperData helper_data = {0};
    ssize_t read_bytes = read(fifo_fd, &helper_data, HELPER_DATA_SIZE);
    if (read_bytes != HELPER_DATA_SIZE)
    {
        g_critical("Data read incomplete: %zd of %lu bytes received!\n", 
               read_bytes, HELPER_DATA_SIZE);
        close(fifo_fd);
        return EXIT_FAILURE;
    }
    g_print("[INFO] HelperData received!\n");

    // Get authentication key from the command line argument
    const uint32_t auth_key = get_auth_key(argv[3]);
    if (!verify_key(auth_key, helper_data.auth_key))
    {
        g_critical("Authentication key mismatch, aborting...");
        close(fifo_fd);
        return EXIT_FAILURE;
    }

    FileSecurityContext *context = file_security_context_new(file_path); // Create the file security context

    secure_open_and_verify(context, file_path); // Take snapshot of the file and directory

    // Check directory security status
    if (!validate_by_stat(context, &helper_data.data.dir_stat, TRUE) ||
        !validate_by_stat(context, &helper_data.data.file_stat, FALSE)) // Check the file security status
    {
        g_critical("The directory or file has been modified");
        goto error_clean_up;
    }

    // Unlink the file
    if (unlinkat(context->dir_fd, 
                    context->base_name, 0) == -1)
    {
        g_critical("Failed to unlink the file: %s", g_strerror(errno));
        goto error_clean_up;
    }

    g_print("[INFO] The file has been unlinked successfully with elevated privileges.\n");

    file_security_context_clear(context); // Free the file security context
    g_strfreev (argv); // Free the command line arguments
    close(fifo_fd);
    return EXIT_SUCCESS;

error_clean_up:
    file_security_context_clear(context); // Free the file security context
    g_strfreev (argv); // Free the command line arguments
    close(fifo_fd);
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    g_autoptr(GApplication) app = NULL;
    int status;

    app = g_application_new("com.ericlin.wuming.unlinkat-helper", G_APPLICATION_HANDLES_COMMAND_LINE);

    g_signal_connect(app, "command-line", G_CALLBACK(command_line_handler), NULL);

    status = g_application_run (G_APPLICATION (app), argc, argv);

    return status;
}