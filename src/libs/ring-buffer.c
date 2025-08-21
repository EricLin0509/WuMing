/* ring-buffer.c
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

#include <glib.h>
#include <assert.h>
#include "ring-buffer.h"

#define RING_DIFF(tail, head) (( (tail) >= (head) ) ? ( (tail) - (head) ) : ( RING_BUFFER_SIZE - ( (head) - (tail) ) )) // use for assertion

G_STATIC_ASSERT((RING_BUFFER_SIZE & (RING_BUFFER_SIZE - 1)) == 0);

void
ring_buffer_init(RingBuffer *ring)
{
    memset(ring->data, 0, sizeof(ring->data));
    ring->head = 0;
    ring->tail = 0;
    ring->count = 0;
}

void line_accumulator_init(LineAccumulator *acc)
{
    memset(acc->buffer, 0, sizeof(acc->buffer));
    acc->write_pos = 0;
    acc->read_pos = 0;
    acc->buffer[0] = '\0'; 
}

size_t
ring_buffer_available(const RingBuffer *ring)
{
    assert(ring->count <= RING_BUFFER_SIZE); // check whether the counter is valid

    return RING_BUFFER_SIZE - ring->count;
}

size_t
ring_buffer_write(RingBuffer *ring, const char *src, size_t len)
{
    if (!src || len == 0) return 0;

    const size_t available = ring_buffer_available(ring);
    const size_t to_write = MIN(len, available);
    if (to_write == 0) return 0;

    const size_t mask = RING_BUFFER_SIZE - 1; // for modulo operation
    const size_t tail_pos = ring->tail & mask;
    const size_t first_chunk = MIN(to_write, RING_BUFFER_SIZE - tail_pos);

    char* dest_ptr = ring->data + tail_pos;
    memcpy(dest_ptr, src, first_chunk);

    if (to_write > first_chunk)
    {
        memcpy(ring->data, src + first_chunk, to_write - first_chunk);
    }

    ring->tail += to_write;
    ring->count += to_write;

    assert(ring->count <= RING_BUFFER_SIZE); // check whether the counter is valid
    assert(ring->count == RING_DIFF(ring->tail, ring->head)); // check whether the correct size is maintained

    return to_write;
}

size_t
ring_buffer_read(RingBuffer *ring, char *dest, size_t len)
{
    if (!dest || len == 0) return 0;

    assert(ring->count <= RING_BUFFER_SIZE); // check whether the counter is valid

    const size_t to_read = MIN(len, ring->count);
    if (to_read == 0) return 0;

    const size_t mask = RING_BUFFER_SIZE - 1; // for modulo operation
    const size_t head_pos = ring->head & mask;
    const size_t first_chunk = MIN(to_read, RING_BUFFER_SIZE - head_pos);

    memcpy(dest, ring->data + head_pos, first_chunk);

    if (to_read > first_chunk)
    {
        memcpy(dest + first_chunk, ring->data, to_read - first_chunk);
    }

    ring->head += to_read;
    ring->count -= to_read;

    ring->head %= RING_BUFFER_SIZE;
    ring->tail %= RING_BUFFER_SIZE;

    assert(ring->count == RING_DIFF(ring->tail, ring->head));  // check whether the correct size is maintained

    return to_read;
}

/* Check the boundary of the buffer and sanitize it if necessary */
static void
line_accumulator_sanitize(LineAccumulator *acc)
{
    if (acc->read_pos > acc->write_pos || acc->write_pos >= sizeof(acc->buffer))
    {
        line_accumulator_init(acc); // Reset the buffer if it is corrupted
        return;
    }
}

gboolean
ring_buffer_read_line(RingBuffer *rb, LineAccumulator *acc, char **output)
{
    /*Try to find a newline in the buffer*/
    for (size_t i = acc->read_pos; i < acc->write_pos; ++i)
    {
        if (acc->buffer[i] == '\n')
        {
            acc->buffer[i] = '\0'; // Replace newline with null terminator
            *output = acc->buffer + acc->read_pos; // Move to right position in buffer
            acc->read_pos = i + 1; // Move read position to next character
            return TRUE;
        }
    }

    /*Read new data from the ring buffer*/
    size_t available = ring_buffer_available(rb);
    if (available == 0) return FALSE;

    /*Calculate the maximum space available for writing (reserve last byte for newline)*/
    size_t remaining_space = sizeof(acc->buffer) - acc->write_pos;
    size_t writable = (remaining_space > 1) ? (remaining_space - 1) : 0;  // Reserve last byte for newline
    size_t read_size = MIN(available, writable);

    /*Perform ring buffer read*/
    size_t actual_read = ring_buffer_read(rb, acc->buffer + acc->write_pos, read_size);
    if (actual_read == 0) return FALSE;

    acc->write_pos += actual_read;
    assert(acc->write_pos < LINE_ACCUMULATOR_SIZE); // Check whether the buffer is overflow
    acc->buffer[acc->write_pos] = '\0';

    /*Try to find a newline in the buffer again*/
    for (size_t i = acc->read_pos; i < acc->write_pos; ++i)
    {
        if (acc->buffer[i] == '\n')
        {
            acc->buffer[i] = '\0';
            *output = acc->buffer + acc->read_pos;
            acc->read_pos = i + 1;
            return TRUE;
        }
    }

    /*If there is not enough space, compress the buffer*/
    if (sizeof(acc->buffer) - acc->write_pos < 128)
    {
        line_accumulator_sanitize(acc); // Sanitize the buffer before compressing it

        if (acc->read_pos < acc->write_pos)
        {
            const size_t pending_data = acc->write_pos - acc->read_pos;
            const size_t safe_pending = MIN(pending_data, sizeof(acc->buffer));

            assert(acc->read_pos + safe_pending <= LINE_ACCUMULATOR_SIZE); // Check whether the buffer is overflow

            if (safe_pending > 0) // If there is any pending data, move it to the beginning of the buffer
            {
                memmove(acc->buffer, acc->buffer + acc->read_pos, safe_pending);
                acc->write_pos = safe_pending;
                acc->read_pos = 0;
                acc->buffer[acc->write_pos] = '\0';
            }
            else
            {
                line_accumulator_init(acc); // Reset the buffer if there is no pending data
            }
        }
    }

    return FALSE;
}