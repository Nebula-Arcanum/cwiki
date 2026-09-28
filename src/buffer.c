#include "buffer.h"

#include "unicode.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int
reserve(void **memory, size_t element_size, size_t *capacity, size_t needed)
{
   size_t grown;
   void *replacement;

   if (needed <= *capacity) {
      return 0;
   }
   grown = *capacity == 0U ? 4U : *capacity;
   while (grown < needed) {
      if (grown > SIZE_MAX / 2U) {
         grown = needed;
         break;
      }
      grown *= 2U;
   }
   if (element_size != 0U && grown > SIZE_MAX / element_size) {
      errno = ENOMEM;
      return -1;
   }
   replacement = realloc(*memory, grown * element_size);
   if (replacement == NULL) {
      return -1;
   }
   *memory = replacement;
   *capacity = grown;
   return 0;
}

static void
line_free(struct cwiki_line *line)
{
   free(line->bytes);
   line->bytes = NULL;
   line->length = 0U;
   line->capacity = 0U;
}

static bool
valid_position(const struct cwiki_buffer *buffer, size_t line, size_t byte)
{
   unsigned char next;

   if (buffer == NULL || line >= buffer->line_count ||
       byte > buffer->lines[line].length) {
      return false;
   }
   if (byte == buffer->lines[line].length) {
      return true;
   }
   next = (unsigned char)buffer->lines[line].bytes[byte];
   return (next & 0xc0U) != 0x80U;
}

int
cwiki_buffer_init(struct cwiki_buffer *buffer)
{
   if (buffer == NULL) {
      errno = EINVAL;
      return -1;
   }
   memset(buffer, 0, sizeof(*buffer));
   buffer->lines = calloc(1U, sizeof(*buffer->lines));
   if (buffer->lines == NULL) {
      return -1;
   }
   buffer->line_count = 1U;
   buffer->line_capacity = 1U;
   buffer->line_ending = CWIKI_LINE_ENDING_LF;
   return 0;
}

void
cwiki_buffer_free(struct cwiki_buffer *buffer)
{
   size_t index;

   if (buffer == NULL) {
      return;
   }
   for (index = 0U; index < buffer->line_count; index++) {
      line_free(&buffer->lines[index]);
   }
   free(buffer->lines);
   free(buffer->positions);
   memset(buffer, 0, sizeof(*buffer));
}

int
cwiki_buffer_load(struct cwiki_buffer *buffer, const char *bytes, size_t length)
{
   struct cwiki_buffer loaded;
   size_t line_count = 1U;
   size_t start = 0U;
   size_t index;
   bool saw_lf = false;
   bool saw_crlf = false;

   if (buffer == NULL || (bytes == NULL && length != 0U) ||
       !cwiki_utf8_validate(bytes, length)) {
      errno = EINVAL;
      return -1;
   }
   for (index = 0U; index < length; index++) {
      if (bytes[index] == '\n') {
         bool crlf = index > 0U && bytes[index - 1U] == '\r';

         saw_crlf = saw_crlf || crlf;
         saw_lf = saw_lf || !crlf;
         if (line_count == SIZE_MAX) {
            errno = ENOMEM;
            return -1;
         }
         line_count++;
      }
   }
   if (saw_lf && saw_crlf) {
      errno = EINVAL;
      return -1;
   }
   memset(&loaded, 0, sizeof(loaded));
   loaded.lines = calloc(line_count, sizeof(*loaded.lines));
   if (loaded.lines == NULL) {
      return -1;
   }
   loaded.line_count = line_count;
   loaded.line_capacity = line_count;
   loaded.line_ending = saw_crlf ? CWIKI_LINE_ENDING_CRLF :
       CWIKI_LINE_ENDING_LF;
   line_count = 0U;
   for (index = 0U; index <= length; index++) {
      if (index == length || bytes[index] == '\n') {
         size_t end = index;
         struct cwiki_line *line = &loaded.lines[line_count];

         if (saw_crlf && end > start) {
            end--;
         }
         line->length = end - start;
         line->capacity = line->length;
         if (line->length != 0U) {
            line->bytes = malloc(line->length);
            if (line->bytes == NULL) {
               cwiki_buffer_free(&loaded);
               return -1;
            }
            memcpy(line->bytes, bytes + start, line->length);
         }
         line_count++;
         start = index + 1U;
      }
   }
   cwiki_buffer_free(buffer);
   *buffer = loaded;
   return 0;
}

int
cwiki_buffer_encode(const struct cwiki_buffer *buffer, char **bytes,
    size_t *length)
{
   size_t newline_length;
   size_t total = 0U;
   size_t line;
   size_t offset = 0U;
   char *encoded;

   if (buffer == NULL || bytes == NULL || length == NULL ||
       buffer->line_count == 0U) {
      errno = EINVAL;
      return -1;
   }
   newline_length = buffer->line_ending == CWIKI_LINE_ENDING_CRLF ? 2U : 1U;
   for (line = 0U; line < buffer->line_count; line++) {
      if (buffer->lines[line].length > SIZE_MAX - total) {
         errno = ENOMEM;
         return -1;
      }
      total += buffer->lines[line].length;
      if (line + 1U < buffer->line_count) {
         if (newline_length > SIZE_MAX - total) {
            errno = ENOMEM;
            return -1;
         }
         total += newline_length;
      }
   }
   encoded = malloc(total == 0U ? 1U : total);
   if (encoded == NULL) {
      return -1;
   }
   for (line = 0U; line < buffer->line_count; line++) {
      if (buffer->lines[line].length != 0U) {
         memcpy(encoded + offset, buffer->lines[line].bytes,
             buffer->lines[line].length);
      }
      offset += buffer->lines[line].length;
      if (line + 1U < buffer->line_count) {
         if (newline_length == 2U) {
            encoded[offset++] = '\r';
         }
         encoded[offset++] = '\n';
      }
   }
   *bytes = encoded;
   *length = total;
   return 0;
}

bool
cwiki_buffer_line_degraded(const struct cwiki_buffer *buffer, size_t line)
{
   return buffer != NULL && line < buffer->line_count &&
       buffer->lines[line].length > CWIKI_DEGRADED_LINE_BYTES;
}

int
cwiki_buffer_register_position(struct cwiki_buffer *buffer,
    struct cwiki_position *position)
{
   size_t index;

   if (buffer == NULL || position == NULL ||
       !valid_position(buffer, position->line, position->byte)) {
      errno = EINVAL;
      return -1;
   }
   for (index = 0U; index < buffer->position_count; index++) {
      if (buffer->positions[index] == position) {
         return 0;
      }
   }
   if (reserve((void **)&buffer->positions, sizeof(*buffer->positions),
       &buffer->position_capacity, buffer->position_count + 1U) != 0) {
      return -1;
   }
   buffer->positions[buffer->position_count++] = position;
   return 0;
}

void
cwiki_buffer_unregister_position(struct cwiki_buffer *buffer,
    struct cwiki_position *position)
{
   size_t index;

   if (buffer == NULL || position == NULL) {
      return;
   }
   for (index = 0U; index < buffer->position_count; index++) {
      if (buffer->positions[index] == position) {
         if (index + 1U < buffer->position_count) {
            memmove(&buffer->positions[index], &buffer->positions[index + 1U],
                (buffer->position_count - index - 1U) *
                sizeof(*buffer->positions));
         }
         buffer->position_count--;
         return;
      }
   }
}

int
cwiki_buffer_insert(struct cwiki_buffer *buffer, size_t line, size_t byte,
    const char *bytes, size_t length)
{
   struct cwiki_line *target;
   size_t index;

   if (!valid_position(buffer, line, byte) ||
       (bytes == NULL && length != 0U) || !cwiki_utf8_validate(bytes, length) ||
       (length != 0U && memchr(bytes, '\n', length) != NULL)) {
      errno = EINVAL;
      return -1;
   }
   if (length == 0U) {
      return 0;
   }
   target = &buffer->lines[line];
   if (length > SIZE_MAX - target->length ||
       reserve((void **)&target->bytes, sizeof(*target->bytes),
       &target->capacity, target->length + length) != 0) {
      if (length > SIZE_MAX - target->length) {
         errno = ENOMEM;
      }
      return -1;
   }
   memmove(target->bytes + byte + length, target->bytes + byte,
       target->length - byte);
   memcpy(target->bytes + byte, bytes, length);
   target->length += length;
   for (index = 0U; index < buffer->position_count; index++) {
      struct cwiki_position *position = buffer->positions[index];

      if (position->line == line && position->byte >= byte) {
         position->byte += length;
      }
   }
   return 0;
}

int
cwiki_buffer_delete(struct cwiki_buffer *buffer, size_t line, size_t byte,
    size_t length)
{
   struct cwiki_line *target;
   size_t end;
   size_t index;

   if (!valid_position(buffer, line, byte)) {
      errno = EINVAL;
      return -1;
   }
   target = &buffer->lines[line];
   if (length > target->length - byte ||
       !valid_position(buffer, line, byte + length)) {
      errno = EINVAL;
      return -1;
   }
   if (length == 0U) {
      return 0;
   }
   end = byte + length;
   memmove(target->bytes + byte, target->bytes + end,
       target->length - end);
   target->length -= length;
   for (index = 0U; index < buffer->position_count; index++) {
      struct cwiki_position *position = buffer->positions[index];

      if (position->line == line && position->byte > byte) {
         position->byte = position->byte <= end ? byte :
             position->byte - length;
      }
   }
   return 0;
}

int
cwiki_buffer_split(struct cwiki_buffer *buffer, size_t line, size_t byte)
{
   struct cwiki_line tail;
   size_t index;

   if (!valid_position(buffer, line, byte)) {
      errno = EINVAL;
      return -1;
   }
   memset(&tail, 0, sizeof(tail));
   tail.length = buffer->lines[line].length - byte;
   tail.capacity = tail.length;
   if (tail.length != 0U) {
      tail.bytes = malloc(tail.length);
      if (tail.bytes == NULL) {
         return -1;
      }
      memcpy(tail.bytes, buffer->lines[line].bytes + byte, tail.length);
   }
   if (reserve((void **)&buffer->lines, sizeof(*buffer->lines),
       &buffer->line_capacity, buffer->line_count + 1U) != 0) {
      line_free(&tail);
      return -1;
   }
   if (line + 1U < buffer->line_count) {
      memmove(&buffer->lines[line + 2U], &buffer->lines[line + 1U],
          (buffer->line_count - line - 1U) * sizeof(*buffer->lines));
   }
   buffer->lines[line + 1U] = tail;
   buffer->lines[line].length = byte;
   buffer->line_count++;
   for (index = 0U; index < buffer->position_count; index++) {
      struct cwiki_position *position = buffer->positions[index];

      if (position->line > line) {
         position->line++;
      } else if (position->line == line && position->byte >= byte) {
         position->line++;
         position->byte -= byte;
      }
   }
   return 0;
}

int
cwiki_buffer_join(struct cwiki_buffer *buffer, size_t line)
{
   struct cwiki_line *first;
   struct cwiki_line *second;
   size_t first_length;
   size_t index;

   if (buffer == NULL || line >= buffer->line_count ||
       line + 1U >= buffer->line_count) {
      errno = EINVAL;
      return -1;
   }
   first = &buffer->lines[line];
   second = &buffer->lines[line + 1U];
   first_length = first->length;
   if (second->length > SIZE_MAX - first->length ||
       reserve((void **)&first->bytes, sizeof(*first->bytes),
       &first->capacity, first->length + second->length) != 0) {
      if (second->length > SIZE_MAX - first->length) {
         errno = ENOMEM;
      }
      return -1;
   }
   if (second->length != 0U) {
      memcpy(first->bytes + first->length, second->bytes, second->length);
   }
   first->length += second->length;
   line_free(second);
   if (line + 2U < buffer->line_count) {
      memmove(&buffer->lines[line + 1U], &buffer->lines[line + 2U],
          (buffer->line_count - line - 2U) * sizeof(*buffer->lines));
   }
   buffer->line_count--;
   memset(&buffer->lines[buffer->line_count], 0,
       sizeof(*buffer->lines));
   for (index = 0U; index < buffer->position_count; index++) {
      struct cwiki_position *position = buffer->positions[index];

      if (position->line == line + 1U) {
         position->line = line;
         position->byte += first_length;
      } else if (position->line > line + 1U) {
         position->line--;
      }
   }
   return 0;
}
