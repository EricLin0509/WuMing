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

#define MAX_LINE_LENGTH 2048 // Maximum length of a line

#define ring_buffer_full(r) ((r)->count == RING_BUFFER_SIZE) // Check whether the buffer is full
#define ring_buffer_empty(r) ((r)->count == 0) // Check whether the buffer is empty
#define RING_DIFF(tail, head) (( (tail) >= (head) ) ? ( (tail) - (head) ) : ( RING_BUFFER_SIZE - ( (head) - (tail) ) )) // Use for assertions

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
    assert(ring_buffer_full(ring) || ring_buffer_empty(ring) || 
       ring->count == RING_DIFF(ring->tail, ring->head)); // check whether the correct size is maintained

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

    assert(ring_buffer_full(ring) || ring_buffer_empty(ring) || 
       ring->count == RING_DIFF(ring->tail, ring->head)); // check whether the correct size is maintained

    return to_read;
}

/* Check the boundary of the buffer and sanitize it if necessary */
static void
line_accumulator_sanitize(LineAccumulator *acc) 
{
    if (acc->read_pos >= LINE_ACCUMULATOR_SIZE || 
        acc->write_pos >= LINE_ACCUMULATOR_SIZE ||
        acc->read_pos > acc->write_pos) 
    {
        line_accumulator_init(acc); // Reset the buffer if it is overflowed or underflowed
    }
}

/*Buffer compression */
/*
    * There are four modes of operation:
    * Emergency: trigger when the remaining space in the buffer is less than 64 bytes
    * Full: trigger when moderate space pressure + long data retention
    * Partial: trigger when moderate free space but memory fragmentation
    * Safe: trigger when the read pointer crosses the midpoint of the buffer
*/
static void
line_accumulator_compress(LineAccumulator *acc) 
{
    line_accumulator_sanitize(acc); // Sanitize the buffer before compressing it

    const size_t pending = acc->write_pos - acc->read_pos;
    const size_t free_space = LINE_ACCUMULATOR_SIZE - acc->write_pos;

    switch (free_space)
    {
        case 0 ... 63: // Emergency mode (0~63 bytes free)
            if (pending > 0)
            {
                memmove(acc->buffer, acc->buffer + acc->read_pos, pending);
                acc->write_pos = pending;
                acc->read_pos = 0;
            }
            break;
        case 64 ... 127: // Full mode (64~127 bytes free)
            if (pending > LINE_ACCUMULATOR_SIZE / 4)
            {
                const size_t movable = pending / 2;
                memmove(acc->buffer, acc->buffer + (acc->read_pos + pending - movable), movable);
                 acc->read_pos = 0;
                 acc->write_pos = movable;
            }
            break;
        case 128 ... 255: // Partial mode (128~255 bytes free)
            if (pending > LINE_ACCUMULATOR_SIZE / 8)
            {
                memmove(acc->buffer, acc->buffer + acc->read_pos, pending);
                acc->write_pos = pending;
                acc->read_pos = 0;
            }
            break;
        default: // Safe mode (256+ bytes free)
            if (acc->read_pos > LINE_ACCUMULATOR_SIZE / 2)
            {
                memmove(acc->buffer, acc->buffer + acc->read_pos, pending);
                acc->write_pos = pending;
                acc->read_pos = 0;
            }
            break;
    }

    /* Final status handling */
    acc->buffer[acc->write_pos] = '\0';
    acc->read_pos = MIN(acc->read_pos, acc->write_pos);
    acc->write_pos = MIN(acc->write_pos, LINE_ACCUMULATOR_SIZE);
}

gboolean
ring_buffer_read_line(RingBuffer *ring, LineAccumulator *acc, char **output)
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
    size_t available = ring_buffer_available(ring);
    if (available == 0) goto compress_and_exit; // No more data to read

    /*Calculate the maximum space available for writing (reserve last byte for newline)*/
    size_t remaining_space = sizeof(acc->buffer) - acc->write_pos;
    size_t writable = (remaining_space > 1) ? (remaining_space - 1) : 0;  // Reserve last byte for newline
    size_t read_size = MIN(available, writable);

    /*Perform ring buffer read*/
    size_t actual_read = ring_buffer_read(ring, acc->buffer + acc->write_pos, read_size);
    if (actual_read == 0) goto compress_and_exit; // No actual data read out

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
            acc->read_pos = i + 1; // Move read position to next character

            /*Compress the buffer if necessary*/
            line_accumulator_compress(acc);
            return TRUE;
        }
    }

compress_and_exit:
    line_accumulator_compress(acc);
    return FALSE; // Default return value
}