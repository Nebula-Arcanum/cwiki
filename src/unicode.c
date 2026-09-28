#include "unicode.h"

#include <stdint.h>

#include <utf8proc.h>

#if UTF8PROC_VERSION_MAJOR < 2 || \
    (UTF8PROC_VERSION_MAJOR == 2 && UTF8PROC_VERSION_MINOR < 9)
#error "cwiki requires utf8proc 2.9 or newer"
#endif

static utf8proc_ssize_t
decode(const char *bytes, size_t length, utf8proc_int32_t *codepoint)
{
   utf8proc_ssize_t available;

   available = (utf8proc_ssize_t)(length < 4U ? length : 4U);
   return utf8proc_iterate((const utf8proc_uint8_t *)bytes, available,
       codepoint);
}

bool
cwiki_utf8_validate(const char *bytes, size_t length)
{
   size_t offset = 0U;

   if (bytes == NULL && length != 0U) {
      return false;
   }
   while (offset < length) {
      utf8proc_int32_t codepoint;
      utf8proc_ssize_t used = decode(bytes + offset, length - offset,
          &codepoint);

      if (used <= 0) {
         return false;
      }
      offset += (size_t)used;
   }
   return true;
}

static size_t
next_boundary(const char *bytes, size_t length, size_t offset)
{
   utf8proc_int32_t previous;
   utf8proc_ssize_t used;
   utf8proc_int32_t state = 0;
   size_t cursor = offset;

   used = decode(bytes + cursor, length - cursor, &previous);
   if (used <= 0) {
      return SIZE_MAX;
   }
   cursor += (size_t)used;
   while (cursor < length) {
      utf8proc_int32_t current;

      used = decode(bytes + cursor, length - cursor, &current);
      if (used <= 0) {
         return SIZE_MAX;
      }
      if (utf8proc_grapheme_break_stateful(previous, current, &state) != 0) {
         return cursor;
      }
      previous = current;
      cursor += (size_t)used;
   }
   return length;
}

bool
cwiki_grapheme_boundary(const char *bytes, size_t length, size_t offset)
{
   size_t cursor = 0U;

   if (offset > length || !cwiki_utf8_validate(bytes, length)) {
      return false;
   }
   while (cursor < offset) {
      cursor = next_boundary(bytes, length, cursor);
      if (cursor == SIZE_MAX || cursor > offset) {
         return false;
      }
   }
   return cursor == offset;
}

size_t
cwiki_grapheme_next(const char *bytes, size_t length, size_t offset)
{
   if (offset > length || !cwiki_grapheme_boundary(bytes, length, offset)) {
      return SIZE_MAX;
   }
   if (offset == length) {
      return length;
   }
   return next_boundary(bytes, length, offset);
}

size_t
cwiki_grapheme_previous(const char *bytes, size_t length, size_t offset)
{
   size_t cursor = 0U;
   size_t previous = 0U;

   if (offset > length || !cwiki_grapheme_boundary(bytes, length, offset)) {
      return SIZE_MAX;
   }
   while (cursor < offset) {
      previous = cursor;
      cursor = next_boundary(bytes, length, cursor);
   }
   return previous;
}

int
cwiki_grapheme_width(const char *bytes, size_t length)
{
   size_t offset = 0U;
   int width = 0;
   bool variation_selector_16 = false;

   if (length == 0U || !cwiki_utf8_validate(bytes, length) ||
       cwiki_grapheme_next(bytes, length, 0U) != length) {
      return -1;
   }
   while (offset < length) {
      utf8proc_int32_t codepoint;
      utf8proc_ssize_t used = decode(bytes + offset, length - offset,
          &codepoint);
      const utf8proc_property_t *property;

      if (used <= 0) {
         return -1;
      }
      property = utf8proc_get_property(codepoint);
      if (codepoint == 0xfe0f) {
         variation_selector_16 = true;
      }
      if (width == 0 && property->ignorable == 0U &&
          property->category != UTF8PROC_CATEGORY_MN &&
          property->category != UTF8PROC_CATEGORY_MC &&
          property->category != UTF8PROC_CATEGORY_ME &&
          property->charwidth != 0U) {
         if (property->charwidth == 2U ||
             property->boundclass == UTF8PROC_BOUNDCLASS_REGIONAL_INDICATOR) {
            width = 2;
         } else {
            width = 1;
         }
      }
      offset += (size_t)used;
   }
   if (width != 0 && variation_selector_16) {
      return 2;
   }
   return width;
}
