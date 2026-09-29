#include "picker_render.h"

#include "picker.h"
#include "terminal.h"
#include "unicode.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct output {
   char *bytes;
   size_t length;
   int error;
};

static void
emit(struct output *out, const char *bytes, size_t length)
{
   if (out->error != 0) {
      return;
   }
   if (length >= SIZE_MAX - out->length) {
      out->error = EOVERFLOW;
      return;
   }
   if (out->bytes != NULL) {
      (void)memcpy(out->bytes + out->length, bytes, length);
   }
   out->length += length;
}

static void
text(struct output *out, const char *bytes)
{
   emit(out, bytes, strlen(bytes));
}

static void
spaces(struct output *out, size_t count)
{
   while (count-- != 0U) {
      emit(out, " ", 1U);
   }
}

static void
position(struct output *out, size_t row, size_t column)
{
   char sequence[64];
   int length = snprintf(sequence, sizeof(sequence), "\x1b[%zu;%zuH",
       row, column);

   if (length < 0 || (size_t)length >= sizeof(sequence)) {
      out->error = EOVERFLOW;
      return;
   }
   emit(out, sequence, (size_t)length);
}

static size_t
text_width(struct output *out, const char *bytes, size_t length)
{
   size_t offset = 0U;
   size_t width = 0U;

   while (offset < length) {
      size_t next = cwiki_grapheme_next(bytes, length, offset);
      int cells;

      if (next == SIZE_MAX) {
         out->error = EINVAL;
         return 0U;
      }
      cells = cwiki_grapheme_width(bytes + offset, next - offset);
      if (cells < 0 || (size_t)cells > SIZE_MAX - width) {
         out->error = EOVERFLOW;
         return 0U;
      }
      width += (size_t)cells;
      offset = next;
   }
   return width;
}

/* Emit whole graphemes in [left, left + width), padding clipped wide glyphs. */
static size_t
paint(struct output *out, const char *bytes, size_t length, size_t left,
    size_t width)
{
   size_t offset = 0U;
   size_t column = 0U;
   size_t painted = 0U;
   size_t right;

   if (width > SIZE_MAX - left) {
      out->error = EOVERFLOW;
      return 0U;
   }
   right = left + width;

   while (offset < length && column < right && out->error == 0) {
      size_t next = cwiki_grapheme_next(bytes, length, offset);
      int cells;
      size_t next_column;

      if (next == SIZE_MAX) {
         out->error = EINVAL;
         return painted;
      }
      cells = cwiki_grapheme_width(bytes + offset, next - offset);
      if (cells < 0 || (size_t)cells > SIZE_MAX - column) {
         out->error = EOVERFLOW;
         return painted;
      }
      next_column = column + (size_t)cells;
      if (cells > 0 && next_column > left) {
         size_t visible_start = column > left ? column : left;
         size_t visible_end = next_column < right ? next_column : right;

         if (column >= left && next_column <= right) {
            emit(out, bytes + offset, next - offset);
         } else {
            spaces(out, visible_end - visible_start);
         }
         painted += visible_end - visible_start;
      }
      column = next_column;
      offset = next;
   }
   return painted;
}

static void
repeat(struct output *out, const char *glyph, size_t count)
{
   while (count-- != 0U) {
      text(out, glyph);
   }
}

static void
border_row(struct output *out, const struct cwiki_float_area *area,
    size_t row, bool top)
{
   position(out, row, area->left);
   text(out, "\x1b[0;90m");
   text(out, top ? "┌" : "└");
   repeat(out, "─", area->columns - 2U);
   text(out, top ? "┐" : "┘");
   text(out, "\x1b[0m");
}

static void
row_start(struct output *out, const struct cwiki_float_area *area,
    size_t row, bool bordered)
{
   position(out, row, area->left);
   if (bordered) {
      text(out, "\x1b[0;90m│\x1b[0m");
   }
}

static void
row_end(struct output *out, size_t remaining, bool bordered)
{
   spaces(out, remaining);
   text(out, "\x1b[0m");
   if (bordered) {
      text(out, "\x1b[0;90m│\x1b[0m");
   }
}

static void
query_row(struct output *out, const struct cwiki_picker *picker,
    const struct cwiki_float_area *area, size_t row, size_t width,
    bool bordered, size_t *cursor_column)
{
   const char *query = cwiki_picker_query(picker);
   size_t length = cwiki_picker_query_length(picker);
   size_t cursor = cwiki_picker_query_cursor(picker);
   size_t cursor_width = text_width(out, query, cursor);
   size_t available = width - 2U;
   size_t left = cursor_width >= available ?
       cursor_width - available + 1U : 0U;
   size_t painted;

   row_start(out, area, row, bordered);
   text(out, "\x1b[0;36m> \x1b[0m");
   painted = paint(out, query, length, left, available);
   row_end(out, available - painted, bordered);
   *cursor_column = area->left + (bordered ? 1U : 0U) + 2U +
       cursor_width - left;
}

static void
item_row(struct output *out, const struct cwiki_picker *picker,
    const struct cwiki_float_area *area, size_t row, size_t width,
    bool bordered, size_t index)
{
   struct cwiki_picker_item item;
   size_t painted = 0U;
   size_t selected = cwiki_picker_selected(picker);

   row_start(out, area, row, bordered);
   text(out, index == selected ? "\x1b[0;7m" : "\x1b[0m");
   if (cwiki_picker_at(picker, index, &item) == CWIKI_PICKER_OK) {
      painted = paint(out, item.label, strlen(item.label), 0U, width);
      if (item.detail[0] != '\0' && painted + 2U < width) {
         spaces(out, 2U);
         painted += 2U;
         painted += paint(out, item.detail, strlen(item.detail), 0U,
             width - painted);
      }
   }
   row_end(out, width - painted, bordered);
}

static void
empty_row(struct output *out, const struct cwiki_float_area *area, size_t row,
    size_t width, bool bordered)
{
   static const char message[] = "No matches";
   size_t painted;

   row_start(out, area, row, bordered);
   text(out, "\x1b[0;90m");
   painted = paint(out, message, sizeof(message) - 1U, 0U, width);
   row_end(out, width - painted, bordered);
}

static void
overlay(struct output *out, const struct cwiki_picker *picker,
    const struct cwiki_float_area *area, bool bordered)
{
   size_t inset = bordered ? 1U : 0U;
   size_t width = area->columns - inset * 2U;
   size_t inner_rows = area->rows - inset * 2U;
   size_t list_rows = inner_rows - 1U;
   size_t count = cwiki_picker_count(picker);
   size_t selected = cwiki_picker_selected(picker);
   size_t first = selected != SIZE_MAX && selected >= list_rows ?
       selected - list_rows + 1U : 0U;
   size_t query_cursor = 0U;
   size_t i;

   text(out, CWIKI_TERMINAL_CURSOR_HIDE);
   if (bordered) {
      border_row(out, area, area->top, true);
   }
   query_row(out, picker, area, area->top + inset, width, bordered,
       &query_cursor);
   for (i = 0U; i < list_rows; i++) {
      size_t index = first + i;
      size_t row = area->top + inset + 1U + i;

      if (count == 0U && i == 0U) {
         empty_row(out, area, row, width, bordered);
      } else {
         item_row(out, picker, area, row, width, bordered, index);
      }
   }
   if (bordered) {
      border_row(out, area, area->top + area->rows - 1U, false);
   }
   position(out, area->top + inset, query_cursor);
   text(out, CWIKI_TERMINAL_CURSOR_SHOW);
}

int
cwiki_picker_render_overlay(const struct cwiki_picker *picker,
    const struct cwiki_float_area *area, enum cwiki_float_border border,
    char *bytes, size_t capacity, size_t *length)
{
   struct output out = { NULL, 0U, 0 };
   bool bordered = border == CWIKI_FLOAT_BORDER_SINGLE;
   size_t minimum_rows = bordered ? 4U : 2U;
   size_t minimum_columns = bordered ? 5U : 3U;

   if (picker == NULL || area == NULL || length == NULL ||
       (bytes == NULL && capacity != 0U) ||
       (border != CWIKI_FLOAT_BORDER_NONE &&
       border != CWIKI_FLOAT_BORDER_SINGLE) || area->top == 0U ||
       area->left == 0U || area->rows < minimum_rows ||
       area->columns < minimum_columns || area->rows > 4096U ||
       area->columns > 4096U || area->top > 4097U - area->rows ||
       area->left > 4097U - area->columns) {
      errno = EINVAL;
      return -1;
   }
   overlay(&out, picker, area, bordered);
   if (out.error != 0 || (bytes != NULL && capacity <= out.length)) {
      errno = out.error != 0 ? out.error : ENOSPC;
      return -1;
   }
   if (bytes != NULL) {
      out.bytes = bytes;
      out.length = 0U;
      overlay(&out, picker, area, bordered);
      bytes[out.length] = '\0';
   }
   *length = out.length;
   return 0;
}
