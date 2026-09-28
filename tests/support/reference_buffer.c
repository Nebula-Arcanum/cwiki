#include "reference_buffer.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void
reference_buffer_init(struct reference_buffer *buffer)
{
   buffer->bytes = NULL;
   buffer->length = 0U;
   buffer->capacity = 0U;
}

void
reference_buffer_free(struct reference_buffer *buffer)
{
   free(buffer->bytes);
   reference_buffer_init(buffer);
}

const unsigned char *
reference_buffer_bytes(const struct reference_buffer *buffer)
{
   return buffer->bytes;
}

size_t
reference_buffer_length(const struct reference_buffer *buffer)
{
   return buffer->length;
}

int
reference_buffer_insert(struct reference_buffer *buffer, size_t offset,
    const void *bytes, size_t length)
{
   size_t needed;

   if (buffer == NULL || offset > buffer->length ||
       (bytes == NULL && length != 0U) || length > SIZE_MAX - buffer->length) {
      errno = EINVAL;
      return -1;
   }
   if (length == 0U) {
      return 0;
   }
   needed = buffer->length + length;
   if (needed > buffer->capacity) {
      unsigned char *grown;
      size_t capacity = buffer->capacity == 0U ? 16U : buffer->capacity;

      while (capacity < needed) {
         if (capacity > SIZE_MAX / 2U) {
            capacity = needed;
            break;
         }
         capacity *= 2U;
      }
      grown = realloc(buffer->bytes, capacity);
      if (grown == NULL) {
         return -1;
      }
      buffer->bytes = grown;
      buffer->capacity = capacity;
   }
   memmove(buffer->bytes + offset + length, buffer->bytes + offset,
       buffer->length - offset);
   if (length != 0U) {
      memcpy(buffer->bytes + offset, bytes, length);
   }
   buffer->length = needed;
   return 0;
}

int
reference_buffer_delete(struct reference_buffer *buffer, size_t offset,
    size_t length)
{
   if (buffer == NULL || offset > buffer->length ||
       length > buffer->length - offset) {
      errno = EINVAL;
      return -1;
   }
   if (length == 0U) {
      return 0;
   }
   memmove(buffer->bytes + offset, buffer->bytes + offset + length,
       buffer->length - offset - length);
   buffer->length -= length;
   return 0;
}
