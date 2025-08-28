/* thread-components.h
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

/* This store some essential thread components for this program */

#pragma once

#include <glib.h>

#include "ring-buffer.h"

#define JITTER_RANGE 30
#define MAX_IDLE_COUNT 5 // If idle_counter greater than this, use MAX_TIMEOUT_MS
#define BASE_TIMEOUT_MS 50
#define MAX_TIMEOUT_MS 1000

typedef struct {
    int pipefd;
    RingBuffer *ring_buf;
    LineAccumulator *acc;
} IOContext;

/* Calculate the dynamic timeout based on the idle_counter and current_timeout */
int
calculate_dynamic_timeout(int *idle_counter, int *current_timeout);

/* Wait for the process to finish and return the exit status */
/*
 * is_scan_process: if true, the process is a scan process, and it should have three exit status:
 * 0: no threat found
 * 1: threats found
 * else: error occurred
*/
gboolean
wait_for_process(pid_t pid, gboolean is_scan_process);

/* Handle the input/output event */
gboolean
handle_io_event(IOContext *io_ctx);

/* Spawn a new process */
// path & command: use for `execv()`
// This function MUST end with a NULL argument to indicate the end of the arguments list
gboolean
spawn_new_process(int pipefd[2], pid_t *pid, const char *path, const char *command, ...);