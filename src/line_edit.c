#include "line_edit.h"

#include "unicode.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <utf8proc.h>

struct cwiki_line_edit {
   char *bytes;
   size_t length;
   size_t capacity;
   size_t cursor;
};

#ifdef CWIKI_LINE_EDIT_TESTING
static size_t allocations_left;
static bool fail_enabled;

void
cwiki_line_edit_test_fail_allocation_after(size_t successful_allocations)
{
   allocations_left = successful_allocations;
   fail_enabled = true;
}

void
cwiki_line_edit_test_reset_allocation(void)
{
   fail_enabled = false;
}

static bool
allocation_fails(void)
{
   if (!fail_enabled) {
      return false;
   }
   if (allocations_left == 0U) {
      return true;
   }
   allocations_left--;
   return false;
}
#else
static bool
allocation_fails(void)
{
   return false;
}
#endif

static void *
line_edit_malloc(size_t size)
{
   return allocation_fails() ? NULL : malloc(size);
}

static void *
line_edit_realloc(void *pointer, size_t size)
{
   return allocation_fails() ? NULL : realloc(pointer, size);
}

static bool
valid_text(const char *text, size_t length)
{
   size_t i;

   if ((text == NULL && length != 0U) || !cwiki_utf8_validate(text, length)) {
      return false;
   }
   for (i = 0U; i < length; i++) {
      unsigned char byte = (unsigned char)text[i];

      if (byte < 0x20U || byte == 0x7fU) {
         return false;
      }
   }
   return true;
}

enum cwiki_line_edit_status
cwiki_line_edit_init(struct cwiki_line_edit **edit, const char *initial,
    size_t length)
{
   struct cwiki_line_edit *created;

   if (edit == NULL || !valid_text(initial, length) || length == SIZE_MAX) {
      return CWIKI_LINE_EDIT_INVALID;
   }
   *edit = NULL;
   created = line_edit_malloc(sizeof(*created));
   if (created == NULL) {
      return CWIKI_LINE_EDIT_NO_MEMORY;
   }
   created->bytes = line_edit_malloc(length + 1U);
   if (created->bytes == NULL) {
      free(created);
      return CWIKI_LINE_EDIT_NO_MEMORY;
   }
   if (length != 0U) {
      (void)memcpy(created->bytes, initial, length);
   }
   created->bytes[length] = '\0';
   created->length = length;
   created->capacity = length + 1U;
   created->cursor = length;
   *edit = created;
   return CWIKI_LINE_EDIT_OK;
}

void
cwiki_line_edit_free(struct cwiki_line_edit *edit)
{
   if (edit == NULL) {
      return;
   }
   free(edit->bytes);
   free(edit);
}

const char *
cwiki_line_edit_bytes(const struct cwiki_line_edit *edit)
{
   return edit == NULL ? NULL : edit->bytes;
}

size_t
cwiki_line_edit_length(const struct cwiki_line_edit *edit)
{
   return edit == NULL ? 0U : edit->length;
}

size_t
cwiki_line_edit_cursor(const struct cwiki_line_edit *edit)
{
   return edit == NULL ? 0U : edit->cursor;
}

static enum cwiki_line_edit_status
reserve(struct cwiki_line_edit *edit, size_t wanted)
{
   char *grown;
   size_t capacity = edit->capacity;

   if (wanted <= capacity) {
      return CWIKI_LINE_EDIT_OK;
   }
   while (capacity < wanted) {
      if (capacity > SIZE_MAX / 2U) {
         capacity = wanted;
         break;
      }
      capacity *= 2U;
   }
   grown = line_edit_realloc(edit->bytes, capacity);
   if (grown == NULL) {
      return CWIKI_LINE_EDIT_NO_MEMORY;
   }
   edit->bytes = grown;
   edit->capacity = capacity;
   return CWIKI_LINE_EDIT_OK;
}

enum cwiki_line_edit_status
cwiki_line_edit_insert(struct cwiki_line_edit *edit, const char *text,
    size_t length)
{
   enum cwiki_line_edit_status status;

   if (edit == NULL || !valid_text(text, length) ||
       length > SIZE_MAX - edit->length - 1U) {
      return CWIKI_LINE_EDIT_INVALID;
   }
   if (length == 0U) {
      return CWIKI_LINE_EDIT_OK;
   }
   status = reserve(edit, edit->length + length + 1U);
   if (status != CWIKI_LINE_EDIT_OK) {
      return status;
   }
   (void)memmove(edit->bytes + edit->cursor + length,
       edit->bytes + edit->cursor, edit->length - edit->cursor + 1U);
   (void)memcpy(edit->bytes + edit->cursor, text, length);
   edit->length += length;
   edit->cursor += length;
   return CWIKI_LINE_EDIT_OK;
}

static void
delete_range(struct cwiki_line_edit *edit, size_t start, size_t end)
{
   (void)memmove(edit->bytes + start, edit->bytes + end,
       edit->length - end + 1U);
   edit->length -= end - start;
   edit->cursor = start;
}

static bool
grapheme_is_space(const struct cwiki_line_edit *edit, size_t offset)
{
   utf8proc_int32_t codepoint;
   utf8proc_ssize_t available;
   const utf8proc_property_t *property;

   available = (utf8proc_ssize_t)((edit->length - offset) < 4U ?
       (edit->length - offset) : 4U);
   if (utf8proc_iterate((const utf8proc_uint8_t *)edit->bytes + offset,
       available, &codepoint) <= 0) {
      return false;
   }
   property = utf8proc_get_property(codepoint);
   return property->category == UTF8PROC_CATEGORY_ZS ||
       property->category == UTF8PROC_CATEGORY_ZL ||
       property->category == UTF8PROC_CATEGORY_ZP;
}

static void
kill_previous_word(struct cwiki_line_edit *edit)
{
   size_t start = edit->cursor;

   while (start != 0U) {
      size_t previous = cwiki_grapheme_previous(edit->bytes, edit->length,
          start);

      if (!grapheme_is_space(edit, previous)) {
         break;
      }
      start = previous;
   }
   while (start != 0U) {
      size_t previous = cwiki_grapheme_previous(edit->bytes, edit->length,
          start);

      if (grapheme_is_space(edit, previous)) {
         break;
      }
      start = previous;
   }
   delete_range(edit, start, edit->cursor);
}

enum cwiki_line_edit_status
cwiki_line_edit_control(struct cwiki_line_edit *edit, uint32_t key)
{
   size_t next;
   size_t previous;

   if (edit == NULL) {
      return CWIKI_LINE_EDIT_INVALID;
   }
   switch (key) {
   case 1U: /* C-a */
      edit->cursor = 0U;
      return CWIKI_LINE_EDIT_OK;
   case 2U: /* C-b */
      edit->cursor = cwiki_grapheme_previous(edit->bytes, edit->length,
          edit->cursor);
      return CWIKI_LINE_EDIT_OK;
   case 4U: /* C-d */
      if (edit->cursor != edit->length) {
         next = cwiki_grapheme_next(edit->bytes, edit->length, edit->cursor);
         delete_range(edit, edit->cursor, next);
      }
      return CWIKI_LINE_EDIT_OK;
   case 5U: /* C-e */
      edit->cursor = edit->length;
      return CWIKI_LINE_EDIT_OK;
   case 6U: /* C-f */
      edit->cursor = cwiki_grapheme_next(edit->bytes, edit->length,
          edit->cursor);
      return CWIKI_LINE_EDIT_OK;
   case 8U: /* C-h */
      if (edit->cursor != 0U) {
         previous = cwiki_grapheme_previous(edit->bytes, edit->length,
             edit->cursor);
         delete_range(edit, previous, edit->cursor);
      }
      return CWIKI_LINE_EDIT_OK;
   case 11U: /* C-k */
      delete_range(edit, edit->cursor, edit->length);
      return CWIKI_LINE_EDIT_OK;
   case 23U: /* C-w */
      kill_previous_word(edit);
      return CWIKI_LINE_EDIT_OK;
   default:
      return CWIKI_LINE_EDIT_NOT_HANDLED;
   }
}
