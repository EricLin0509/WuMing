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

#include "wuming-unlinkat-helper.h"

/* Verify the authentication key */
static inline gboolean
verify_key(uint32_t expected, uint32_t received)
{
    return expected == received;
}

/* Verify the magic number of the shared memory */
static inline gboolean 
verify_magic(uint32_t magic)
{
    return magic == SHM_MAGIC;
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
    exit(FILE_SECURITY_UNKNOWN_ERROR);
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
    if (argc != 4)
    {
        g_critical("[ERROR] Invalid arguments");
        g_critical("Usage: %s <shm_name> <file_path> <auth_key>", argv[0]);
        return FILE_SECURITY_INVALID_PATH;
    }

    // Get the shm name and file path from the command line arguments
    const char *shm_name = argv[1];
    const char *file_path = argv[2];

    int shm_fd = shm_open(shm_name, O_RDONLY, 0); // Open the shared memory
    if (shm_fd < 0)
    {
        g_critical("[ERROR] Failed to open the shared memory: %s", shm_name);
        return FILE_SECURITY_INVALID_PATH;
    }

    HelperData *helper_data = mmap(NULL, HELPER_DATA_SIZE, PROT_READ, MAP_SHARED, shm_fd, 0); // Map the shared memory
    if (helper_data == MAP_FAILED)
    {
        g_critical("[ERROR] Failed to map the shared memory: %s", shm_name);
        close(shm_fd);
        return FILE_SECURITY_INVALID_PATH;
    }
    g_print("[INFO] HelperData received!\n");

    // Verify the magic number of the shared memory
    if (!verify_magic(helper_data->shm_magic))
    {
        g_critical("[ERROR] Invalid magic number of the shared memory: %s", shm_name);
        status = FILE_SECURITY_INVALID_CONTEXT;
        goto clean_up;
    }

    // Get authentication key from the command line argument
    const uint32_t auth_key = get_auth_key(argv[3]);
    if (!verify_key(auth_key, helper_data->auth_key))
    {
        g_critical("[ERROR] Authentication key mismatch, aborting...");
        status = FILE_SECURITY_INVALID_CONTEXT;
        goto clean_up;
    }

    /* Delete file */
    status = delete_file_securely(&helper_data->security_context, file_path);
    if (status != FILE_SECURITY_OK)
    {
        g_critical("[ERROR] Failed to unlink the file: %s", file_path);
        goto clean_up;
    }

    g_print("[INFO] The file has been unlinked successfully with elevated privileges.\n");

clean_up:
    g_strfreev (argv);
    munmap(helper_data, HELPER_DATA_SIZE);
    close(shm_fd);
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