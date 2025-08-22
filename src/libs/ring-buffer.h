/* ring-buffer.h
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

#pragma once


/* the buffer size MUST be a power of 2 */
#define RING_BUFFER_SIZE 8192
#define LINE_ACCUMULATOR_SIZE 16384 // Double buffer for atomicity

typedef struct {
    char data[RING_BUFFER_SIZE];
    size_t head;  // Read
    size_t tail;  // Write
    size_t count; // current data length
} RingBuffer;

typedef struct {
    char buffer[LINE_ACCUMULATOR_SIZE];
    size_t write_pos;
    size_t read_pos;
    size_t current_line_length; // current line length
} LineAccumulator;

void
ring_buffer_init(RingBuffer *ring);

void
line_accumulator_init(LineAccumulator *acc);

size_t
ring_buffer_available(const RingBuffer *ring);

size_t
ring_buffer_write(RingBuffer *ring, const char *src, size_t len);

size_t
ring_buffer_read(RingBuffer *ring, char *dest, size_t len);

gboolean
ring_buffer_read_line(RingBuffer *rb, LineAccumulator *acc, char **output);
