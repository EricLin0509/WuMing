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

#define RING_BUFFER_SIZE 8192

typedef struct {
    char data[RING_BUFFER_SIZE];
    size_t head;  // Read
    size_t tail;  // Write
    size_t count; // current data length
} RingBuffer;

typedef struct {
    char buffer[RING_BUFFER_SIZE * 2];  // Double buffer for atomicity
    size_t write_pos;
    size_t read_pos;
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

char *
ring_buffer_find_newline(const RingBuffer *ring);

gboolean
ring_buffer_read_line(RingBuffer *rb, LineAccumulator *acc, char **output);
