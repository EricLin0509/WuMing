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
#include <sys/mman.h>

#include "file-security.h"

/* Prevent handle `SIGTRAP` (breakpoint) and `SIGILL` (illegal instruction) signal when the program is running */
void
breakpoint_handler(int signal)
{
    g_critical("[ERROR] Breakpoint detected, aborting...");
    exit(FILE_SECURITY_OPERATION_FAILED);
}

gint
command_line_handler(GApplication* self, GApplicationCommandLine* command_line, gpointer user_data)
{
    // Register the breakpoint handler
    signal(SIGTRAP, breakpoint_handler);
    signal(SIGILL, breakpoint_handler);
    
    FileSecurityStatus status = FILE_SECURITY_OK;

    // Check if the program is running as root
    if (getuid() != 0)
    {
        g_critical("This program must be run as root");
        return FILE_SECURITY_PERMISSION_DENIED;
    }

    // Get the command line arguments
    gint argc;
    gchar **argv = g_application_command_line_get_arguments(command_line, &argc); // Get the command line arguments;

    // Check the number of command line arguments
    if (argc != 3)
    {
        g_critical("[ERROR] Invalid arguments");
        g_critical("Usage: %s <shm_name> <file_path>", argv[0]);
        return FILE_SECURITY_INVALID_PATH;
    }

    // Get the shm name and file path from the command line arguments
    const char *shm_name = argv[1];
    const char *file_path = argv[2];

    FileSecurityContext *security_context = file_security_context_open_shared_mem(shm_name);
    if (security_context == NULL)
    {
        g_critical("[ERROR] Failed to open shared memory: %s", shm_name);
        return FILE_SECURITY_OPERATION_FAILED;
    }

    /* Delete file */
    status = file_security_secure_delete(security_context, file_path, 0); // Delete the file with elevated privileges
    file_security_context_close_shared_mem(&security_context); // Close the shared memory
    if (status != FILE_SECURITY_OK)
    {
        g_critical("[ERROR] Failed to unlink the file: %s", file_path);
    }
    else
    {
        g_print("[INFO] The file has been unlinked successfully with elevated privileges.\n");
    }

    return status;
}

int main(int argc, char *argv[]) {
    g_autoptr(GApplication) app = NULL;
    int status;

    app = g_application_new("com.ericlin.wuming.unlinkat-helper", G_APPLICATION_HANDLES_COMMAND_LINE);

    g_signal_connect(app, "command-line", G_CALLBACK(command_line_handler), NULL);

    status = g_application_run (G_APPLICATION (app), argc, argv);

    return status;
}