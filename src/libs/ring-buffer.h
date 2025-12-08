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

typedef struct RingBuffer {
    char data[RING_BUFFER_SIZE];
    size_t head;  // Read
    size_t tail;  // Write
    size_t count; // current data length
} RingBuffer;

/* Initialize the ring buffer */
/*
  * @param ring
  * the ring buffer to be initialized
*/
void
ring_buffer_init(RingBuffer *ring);

/* Get the number of bytes available for reading */
size_t
ring_buffer_available(const RingBuffer *ring);

/* Write data to the ring buffer */
size_t
ring_buffer_write(RingBuffer *ring, const char *src, size_t len);

/* Read data from the ring buffer */
size_t
ring_buffer_read(RingBuffer *ring, char *dest, size_t len);

/* Find a new line in the ring buffer */
/*
  * @param ring
  * the ring buffer to search
  * @return
  * a allocated string containing the new line, or NULL if no new line is found
  * @warning
  * The returned string is allocated on the heap and must be freed by the caller
*/
char *
ring_buffer_find_new_line(RingBuffer *ring);
