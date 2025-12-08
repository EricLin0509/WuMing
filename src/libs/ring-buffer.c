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
#define calculate_pos(pos) ((pos) & (RING_BUFFER_SIZE - 1)) // calculate the pointer position with modulo operation

#define RING_DIFF(tail, head) (( (tail) >= (head) ) ? ( (tail) - (head) ) : ( RING_BUFFER_SIZE - ( (head) - (tail) ) )) // Use for assertions

G_STATIC_ASSERT((RING_BUFFER_SIZE & (RING_BUFFER_SIZE - 1)) == 0);

/* Initialize the ring buffer */
/*
  * @param ring
  * the ring buffer to be initialized
*/
RingBuffer
ring_buffer_init(void)
{
    RingBuffer ring;

    memset(ring.data, 0, sizeof(ring.data));
    ring.head = 0;
    ring.tail = 0;
    ring.count = 0;

    return ring;
}

/* Get the number of bytes available for reading */
size_t
ring_buffer_available(const RingBuffer *ring)
{
    assert(ring->count <= RING_BUFFER_SIZE); // check whether the counter is valid

    return RING_BUFFER_SIZE - ring->count;
}

/* Write data to the ring buffer */
size_t
ring_buffer_write(RingBuffer *ring, const char *src, size_t len)
{
    if (!src || len == 0) return 0;

    const size_t available = ring_buffer_available(ring);
    const size_t to_write = MIN(len, available);
    if (to_write == 0) return 0;

    const size_t tail_pos = calculate_pos(ring->tail);

    const size_t wrapped_write = (tail_pos + to_write) & (RING_BUFFER_SIZE - 1);
    if (wrapped_write >= tail_pos)
    {
        memcpy(ring->data + tail_pos, src, to_write);
    }
    else
    {
        size_t first = RING_BUFFER_SIZE - tail_pos;
        memcpy(ring->data + tail_pos, src, first);
        memcpy(ring->data, src + first, to_write - first);
    }

    ring->tail += to_write;
    ring->count += to_write;

    assert(ring->count <= RING_BUFFER_SIZE); // check whether the counter is valid
    assert(ring_buffer_full(ring) || ring_buffer_empty(ring) || 
       ring->count == RING_DIFF(ring->tail, ring->head)); // check whether the correct size is maintained

    return to_write;
}

/* Read data from the ring buffer */
size_t
ring_buffer_read(RingBuffer *ring, char *dest, size_t len)
{
    if (!dest || len == 0) return 0;

    assert(ring->count <= RING_BUFFER_SIZE); // check whether the counter is valid

    const size_t to_read = MIN(len, ring->count);
    if (to_read == 0) return 0;

    const size_t head_pos = calculate_pos(ring->head);
    const size_t first_chunk = MIN(to_read, RING_BUFFER_SIZE - head_pos);

    memcpy(dest, ring->data + head_pos, first_chunk);

    if (to_read > first_chunk)
    {
        memcpy(dest + first_chunk, ring->data, to_read - first_chunk);
    }

    ring->head += to_read;
    ring->count -= to_read;

    assert(ring_buffer_full(ring) || ring_buffer_empty(ring) || 
       ring->count == RING_DIFF(ring->tail, ring->head)); // check whether the correct size is maintained

    return to_read;
}

/* A helper function to find a new line character in the ring buffer (with the support of crossing the buffer boundary) */
/*
  * @param ring
  * the ring buffer to search
  * @param target
  * the character to search for
  * @param max_search_size
  * the maximum size to search for the character
  * @return
  * a pointer to the first occurrence of the character in the ring buffer, or NULL if not found
*/
static char *
ring_buffer_memchr(RingBuffer *ring, char target, size_t max_search_size)
{
    if (ring_buffer_empty(ring) || max_search_size == 0) return NULL;

    const size_t head_pos = calculate_pos(ring->head);
    size_t search_size = MIN(max_search_size, ring->count);
    char *found = NULL;

    /* First search in the first chunk of the ring buffer */
    size_t first_chunk = MIN(RING_BUFFER_SIZE - head_pos, search_size); // Prevent overflow
    found = memchr(ring->data + head_pos, target, first_chunk);
    if (found) return found;

    /* Then search in the remaining part of the ring buffer */
    size_t second_chunk = search_size - first_chunk;
    if (search_size > first_chunk) found = memchr(ring->data, target, second_chunk); // DON'T use calculated `second_chunk` here, it may underflow
    return found;
}

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
ring_buffer_find_new_line(RingBuffer *ring)
{
    g_return_val_if_fail(ring != NULL, NULL);

    /* Initialize some varibles */
    char *line = NULL;
    char *newline_pos = NULL;
    size_t line_length = 0;
    const size_t head_pos = calculate_pos(ring->head);

    /* Find the first newline character in the ring buffer */
    newline_pos = ring_buffer_memchr(ring, '\n', MIN(ring->count, MAX_LINE_LENGTH));

    /* Calculate the actual length of the new line */
    if (!newline_pos) return NULL; // No new line found
    else if (ring->count >= MAX_LINE_LENGTH) line_length = MAX_LINE_LENGTH; // The new line is too long

    /* The new line is not wrapped */
    if (newline_pos >= ring->data + head_pos) line_length = (newline_pos - (ring->data + head_pos)) + 1; // +1 for the newline character
    /* The new line is wrapped */
    else line_length = (RING_BUFFER_SIZE - head_pos) + (newline_pos - ring->data) + 1; // +1 for the newline character

    /* Allocate memory for the new line */
    line = g_new0(char, line_length);
    if (!line) return NULL;

    /* Copy the new line to the allocated memory */
    size_t actual_read = ring_buffer_read(ring, line, line_length);

    if (actual_read != line_length)
    {
        g_free(line);
        return NULL;
    }

    /* Add a null terminator to the new line */
    line[line_length-1] = '\0'; // Replace the newline character with a null terminator

    return line;
}