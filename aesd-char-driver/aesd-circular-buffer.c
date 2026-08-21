/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#include <stdio.h>
#include <stdbool.h>
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"
#include <ctype.h>

static void print_with_escapes(const char* const str, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        if (isprint(str[i]))
        {
            putc(str[i], stdout);
        }
        else
        {
            switch (str[i])
            {
                case '\n':
                    printf("\\n");
                    break;
                default:
                    printf("%02x", (int)(str[i]));
                    break;
            }
        }
    }
}

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    struct aesd_buffer_entry *result = NULL;

    // Loop through entries.
    // To do this, start at the entry @ out_offs.
    size_t accumulated_size = 0;
    // For each entry,
    bool once_flag = true;
    for (uint8_t i = buffer->out_offs;
            (i != buffer->in_offs) || (buffer->full && i == buffer->in_offs && once_flag);
            i = (i + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        if (buffer->full && buffer->out_offs == buffer->in_offs)
        {
            once_flag = false;
        }

        struct aesd_buffer_entry *entry = &buffer->entry[i];
        // If this entry is too small to cover what we need,
        if (char_offset + 1 <= accumulated_size + entry->size)
        {
            // Set return offset to char_offset - current offset
            *entry_offset_byte_rtn = (char_offset ) - accumulated_size;

            // Return this one.
            return entry;
        }
        else
        {
            // add entry's length to current_offset
            accumulated_size += entry->size;
        }
        
    }

    return result;
}

static void print_circular_buffer(struct aesd_circular_buffer *buffer)
{
    const char *const cell_repr        = " [ %3zu ] ";
    const char *const both_tag         = "    B    ";
    const char *const in_tag           = "    I    ";
    const char *const out_tag          = "    O    ";
    const char *const arrow_stem       = "        ";
    const char *const cell_repr_spaces = "         ";
    printf(
"BUFFER: [full: %s.  in_offs=%u    out_offs=%u ]\n", buffer->full? "Y" : "N",
(unsigned int)buffer->in_offs, (unsigned int)buffer->out_offs);
    // Print I and O for input/output offsets.
    for (size_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i)
    {
        if (i == buffer->in_offs && i == buffer->out_offs)
        {
            printf("%s", both_tag);
        }
        else if (i == buffer->in_offs)
        {
            printf("%s", in_tag);
        }
        else if (i == buffer->out_offs)
        {
            printf("%s", out_tag);
        }
        else
        {
            printf("%s", cell_repr_spaces);
        }
    }
    printf("\n");
    for (size_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i)
    {
        if (i == buffer->in_offs || i == buffer->out_offs)
            printf("%s", arrow_stem); 
        else
            printf("%s", cell_repr_spaces);
    }
    printf("\n");
    for (size_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i)
    {
        printf(cell_repr, buffer->entry[i].size);
    }
    printf("\n");
    printf("BUFFERS:\n---------\n");
    for (size_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i)
    {
        printf("[%02zu]: ", i);
        print_with_escapes(buffer->entry[i].buffptr, buffer->entry[i].size);
        printf("\n");
    }
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // Validate parameters
    if (NULL == buffer || NULL == add_entry)
    {
        fprintf(stderr, "%s:%d: Error: Cannot accept a NULL parameter.",
                __func__, __LINE__);
        return;
    }
    if (add_entry->buffptr == NULL)
    {
        fprintf(stderr, "%s:%d: Error: Entry to be added has NULL buffer.\n",
                __func__, __LINE__);
        return;
    }
    if (buffer->out_offs > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        fprintf(stderr, "%s:%d: Error: Malformed aesd_circular_buffer. out_offs = %u, but max = %d",
                __func__, __LINE__,
                (unsigned int)buffer->out_offs,
                AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
               );
        return;
    }
    if (buffer->in_offs > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        fprintf(stderr, "%s:%d: Error: Malformed aesd_circular_buffer. in_offs = %u, but max = %d",
                __func__, __LINE__,
                (unsigned int)buffer->in_offs,
                AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
               );
        return;
    }
    
    printf("===================================================================\n");
    printf(" Adding entry with size=%zu, buffer=[%s]\n\n", add_entry->size, add_entry->buffptr);
    printf("Circular Buffer Before adding entry:\n");
    print_circular_buffer(buffer);

    // Always write at the input offset
    // If the buffer is full, advance the out_offs, since you want to read the oldest
    // entry, not the newest one (the one that was just added).
    buffer->entry[buffer->in_offs] = *add_entry;
    if (buffer->in_offs == buffer->out_offs)
    {
        if (buffer->full)
        {
            buffer->out_offs++;
            buffer->out_offs %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        }
    }
    buffer->in_offs++;
    buffer->in_offs %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    if (buffer->in_offs == buffer->out_offs)
    {
        buffer->full = true;
    }
    else
    {
        buffer->full = false;
    }

    printf("Circular Buffer After adding entry:\n");
    print_circular_buffer(buffer);
    printf("===================================================================\n");
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
