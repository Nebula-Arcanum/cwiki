#include "motion.h"

#include "layout.h"
#include "unicode.h"
#include "zone.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <utf8proc.h>

enum word_class {
   WORD_SPACE,
   WORD_KEYWORD,
   WORD_PUNCTUATION
};

static int
position_compare(struct cwiki_position left, struct cwiki_position right)
{
   if (left.line != right.line) {
      return left.line < right.line ? -1 : 1;
   }
   if (left.byte == right.byte) {
      return 0;
   }
   return left.byte < right.byte ? -1 : 1;
}

static bool
valid_position(const struct cwiki_buffer *buffer, struct cwiki_position position)
{
   return buffer != NULL && position.line < buffer->line_count &&
       position.byte <= buffer->lines[position.line].length &&
       cwiki_grapheme_boundary(buffer->lines[position.line].bytes,
       buffer->lines[position.line].length, position.byte);
}

static bool
line_blank(const struct cwiki_line *line)
{
   size_t offset = 0U;

   while (offset < line->length) {
      utf8proc_int32_t codepoint;
      utf8proc_ssize_t used = utf8proc_iterate(
          (const utf8proc_uint8_t *)line->bytes + offset,
          (utf8proc_ssize_t)(line->length - offset < 4U ?
          line->length - offset : 4U), &codepoint);
      utf8proc_category_t category;

      if (used <= 0) {
         return false;
      }
      category = utf8proc_category(codepoint);
      if (codepoint != '\t' && codepoint != '\r' && codepoint != ' ' &&
          category != UTF8PROC_CATEGORY_ZS &&
          category != UTF8PROC_CATEGORY_ZL &&
          category != UTF8PROC_CATEGORY_ZP) {
         return false;
      }
      offset += (size_t)used;
   }
   return true;
}

static size_t
first_nonblank_between(const struct cwiki_line *line, size_t start, size_t end)
{
   size_t offset = start;

   while (offset < end) {
      size_t next = cwiki_grapheme_next(line->bytes, line->length, offset);
      struct cwiki_line cluster;

      if (next == SIZE_MAX) {
         return start;
      }
      cluster = *line;
      cluster.bytes = line->bytes + offset;
      cluster.length = next - offset;
      if (!line_blank(&cluster)) {
         break;
      }
      offset = next;
   }
   return offset;
}

static size_t
first_nonblank(const struct cwiki_line *line)
{
   return first_nonblank_between(line, 0U, line->length);
}

static size_t
last_grapheme(const struct cwiki_line *line)
{
   return line->length == 0U ? 0U :
       cwiki_grapheme_previous(line->bytes, line->length, line->length);
}

static bool
next_position(const struct cwiki_buffer *buffer, struct cwiki_position from,
    struct cwiki_position *to)
{
   const struct cwiki_line *line = &buffer->lines[from.line];

   if (from.byte < line->length) {
      size_t next = cwiki_grapheme_next(line->bytes, line->length, from.byte);

      if (next < line->length) {
         *to = (struct cwiki_position){from.line, next};
         return true;
      }
   }
   if (from.line + 1U < buffer->line_count) {
      *to = (struct cwiki_position){from.line + 1U, 0U};
      return true;
   }
   return false;
}

static bool
previous_position(const struct cwiki_buffer *buffer, struct cwiki_position from,
    struct cwiki_position *to)
{
   if (from.byte != 0U) {
      size_t previous = cwiki_grapheme_previous(buffer->lines[from.line].bytes,
          buffer->lines[from.line].length, from.byte);

      *to = (struct cwiki_position){from.line, previous};
      return true;
   }
   if (from.line != 0U) {
      *to = (struct cwiki_position){from.line - 1U,
          last_grapheme(&buffer->lines[from.line - 1U])};
      return true;
   }
   return false;
}

static enum word_class
class_at(const struct cwiki_buffer *buffer, struct cwiki_position position,
    bool big)
{
   const struct cwiki_line *line = &buffer->lines[position.line];
   utf8proc_int32_t codepoint;
   utf8proc_category_t category;

   if (line->length == 0U || position.byte >= line->length ||
       utf8proc_iterate((const utf8proc_uint8_t *)line->bytes + position.byte,
       (utf8proc_ssize_t)(line->length - position.byte < 4U ?
       line->length - position.byte : 4U), &codepoint) <= 0) {
      return WORD_SPACE;
   }
   category = utf8proc_category(codepoint);
   if (codepoint == '\t' || codepoint == '\r' || codepoint == ' ' ||
       category == UTF8PROC_CATEGORY_ZS || category == UTF8PROC_CATEGORY_ZL ||
       category == UTF8PROC_CATEGORY_ZP) {
      return WORD_SPACE;
   }
   if (big || codepoint == '_' ||
       (category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_LO) ||
       (category >= UTF8PROC_CATEGORY_ND && category <= UTF8PROC_CATEGORY_NO)) {
      return WORD_KEYWORD;
   }
   return WORD_PUNCTUATION;
}

static struct cwiki_position
word_forward(const struct cwiki_buffer *buffer, struct cwiki_position cursor,
    bool big)
{
   enum word_class initial = class_at(buffer, cursor, big);
   struct cwiki_position next;

   while (next_position(buffer, cursor, &next)) {
      enum word_class current = class_at(buffer, next, big);
      bool crossed_line = next.line != cursor.line;

      cursor = next;
      if (initial == WORD_SPACE) {
         if (current != WORD_SPACE) {
            break;
         }
      } else if (crossed_line) {
         break;
      } else if (current != initial) {
         while (current == WORD_SPACE && next_position(buffer, cursor, &next)) {
            cursor = next;
            current = class_at(buffer, cursor, big);
         }
         break;
      }
   }
   return cursor;
}

static struct cwiki_position
word_backward(const struct cwiki_buffer *buffer, struct cwiki_position cursor,
    bool big)
{
   struct cwiki_position previous;
   enum word_class wanted;

   if (!previous_position(buffer, cursor, &previous)) {
      return cursor;
   }
   cursor = previous;
   while (class_at(buffer, cursor, big) == WORD_SPACE &&
       previous_position(buffer, cursor, &previous)) {
      cursor = previous;
   }
   wanted = class_at(buffer, cursor, big);
   while (previous_position(buffer, cursor, &previous) &&
       previous.line == cursor.line &&
       class_at(buffer, previous, big) == wanted) {
      cursor = previous;
   }
   return cursor;
}

static struct cwiki_position
word_end(const struct cwiki_buffer *buffer, struct cwiki_position cursor,
    bool big)
{
   struct cwiki_position next;
   enum word_class wanted = class_at(buffer, cursor, big);

   if (wanted != WORD_SPACE && next_position(buffer, cursor, &next) &&
       next.line == cursor.line &&
       class_at(buffer, next, big) == wanted) {
      cursor = next;
   } else {
      do {
         if (!next_position(buffer, cursor, &next)) {
            return cursor;
         }
         cursor = next;
      } while (class_at(buffer, cursor, big) == WORD_SPACE);
      wanted = class_at(buffer, cursor, big);
   }
   while (next_position(buffer, cursor, &next) &&
       next.line == cursor.line &&
       class_at(buffer, next, big) == wanted) {
      cursor = next;
   }
   return cursor;
}

static struct cwiki_position
paragraph_forward(const struct cwiki_buffer *buffer,
    struct cwiki_position cursor)
{
   size_t line = cursor.line + (line_blank(&buffer->lines[cursor.line]) ? 1U : 0U);

   while (line < buffer->line_count && !line_blank(&buffer->lines[line])) {
      line++;
   }
   while (line < buffer->line_count && line_blank(&buffer->lines[line])) {
      line++;
   }
   if (line < buffer->line_count) {
      return (struct cwiki_position){line, 0U};
   }
   return (struct cwiki_position){buffer->line_count - 1U,
       last_grapheme(&buffer->lines[buffer->line_count - 1U])};
}

static struct cwiki_position
paragraph_backward(const struct cwiki_buffer *buffer,
    struct cwiki_position cursor)
{
   size_t line = cursor.line;

   if (line != 0U) {
      line--;
   }
   while (line != 0U && line_blank(&buffer->lines[line])) {
      line--;
   }
   while (line != 0U && !line_blank(&buffer->lines[line - 1U])) {
      line--;
   }
   return (struct cwiki_position){line, 0U};
}

static bool
ascii_at(const struct cwiki_buffer *buffer, struct cwiki_position position,
    const char *characters)
{
   const struct cwiki_line *line = &buffer->lines[position.line];

   return position.byte < line->length &&
       (unsigned char)line->bytes[position.byte] < 0x80U &&
       strchr(characters, line->bytes[position.byte]) != NULL;
}

static bool
sentence_end(struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *buffer, struct cwiki_position position,
    struct cwiki_position *start)
{
   struct cwiki_position cursor = position;
   struct cwiki_position next;
   struct cwiki_zone zone;

   if (!ascii_at(buffer, position, ".!?") ||
       cwiki_zone_at(zones, buffer, position.line, position.byte, &zone) != 0 ||
       zone.kind != CWIKI_ZONE_PROSE) {
      return false;
   }
   while (next_position(buffer, cursor, &next) && ascii_at(buffer, next, ")]}'\"")) {
      cursor = next;
   }
   if (!next_position(buffer, cursor, &next)) {
      *start = cursor;
      return true;
   }
   if (next.line == cursor.line && class_at(buffer, next, true) != WORD_SPACE) {
      return false;
   }
   cursor = next;
   while (class_at(buffer, cursor, true) == WORD_SPACE &&
       next_position(buffer, cursor, &next)) {
      cursor = next;
   }
   *start = cursor;
   return true;
}

static struct cwiki_position
sentence_forward(struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *buffer, struct cwiki_position cursor)
{
   struct cwiki_position candidate = cursor;
   struct cwiki_position next;

   for (;;) {
      struct cwiki_position start;

      if (sentence_end(zones, buffer, candidate, &start)) {
         return start;
      }
      if (!next_position(buffer, candidate, &next)) {
         break;
      }
      candidate = next;
   }
   return (struct cwiki_position){buffer->line_count - 1U,
       last_grapheme(&buffer->lines[buffer->line_count - 1U])};
}

static struct cwiki_position
sentence_backward(struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *buffer, struct cwiki_position cursor)
{
   struct cwiki_position scan = {0U, 0U};
   struct cwiki_position previous = scan;
   struct cwiki_position next;

   if (position_compare(cursor, scan) <= 0) {
      return scan;
   }
   while (position_compare(scan, cursor) < 0 && next_position(buffer, scan, &next)) {
      struct cwiki_position start;

      if (sentence_end(zones, buffer, scan, &start) &&
          position_compare(start, cursor) < 0) {
         previous = start;
      }
      scan = next;
   }
   return previous;
}

static struct cwiki_position
after_grapheme(const struct cwiki_buffer *buffer, struct cwiki_position position)
{
   const struct cwiki_line *line = &buffer->lines[position.line];

   if (position.byte < line->length) {
      position.byte = cwiki_grapheme_next(line->bytes, line->length,
          position.byte);
   }
   return position;
}

static void
character_range(const struct cwiki_buffer *buffer, struct cwiki_position from,
    struct cwiki_position to, bool inclusive,
    struct cwiki_motion_range *range)
{
   if (position_compare(from, to) <= 0) {
      range->start = from;
      range->end = inclusive ? after_grapheme(buffer, to) : to;
   } else {
      range->start = to;
      range->end = from;
   }
   range->shape = CWIKI_MOTION_CHARACTERWISE;
}

static void
line_range(const struct cwiki_buffer *buffer, size_t first, size_t last,
    struct cwiki_motion_range *range)
{
   range->start = (struct cwiki_position){first, 0U};
   range->end = last + 1U < buffer->line_count ?
       (struct cwiki_position){last + 1U, 0U} :
       (struct cwiki_position){buffer->line_count, 0U};
   range->shape = CWIKI_MOTION_LINEWISE;
}

static void
set_reveal(const struct cwiki_layout_window *layout,
    struct cwiki_position destination, struct cwiki_motion_result *result)
{
   const struct cwiki_layout_line *line;
   size_t run;

   result->reveal_line = destination.line;
   result->reveal = (struct cwiki_conceal_reveal){0U, 0U, false};
   if (layout == NULL || destination.line >= layout->line_count) {
      return;
   }
   if (layout->reveal.active && layout->reveal_line == destination.line &&
       destination.byte >= layout->reveal.source_start &&
       destination.byte < layout->reveal.source_end) {
      result->reveal = layout->reveal;
      return;
   }
   line = &layout->lines[destination.line];
   for (run = 0U; run < line->display.run_count; run++) {
      const struct cwiki_conceal_run *item = &line->display.runs[run];

      if (item->concealed && destination.byte >= item->source_start &&
          destination.byte < item->source_end) {
         result->reveal = (struct cwiki_conceal_reveal){item->source_start,
             item->source_end, true};
         return;
      }
   }
}

static int
viewport_destination(const struct cwiki_buffer *buffer,
    const struct cwiki_layout_window *layout,
    const struct cwiki_motion_viewport *viewport, enum cwiki_motion motion,
    struct cwiki_position *destination)
{
   size_t row;
   const struct cwiki_layout_row *item;

   if (layout == NULL || viewport == NULL ||
       viewport->first_display_row > viewport->last_display_row ||
       viewport->last_display_row >= layout->row_count) {
      errno = EINVAL;
      return -1;
   }
   if (motion == CWIKI_MOTION_VIEWPORT_HIGH) {
      row = viewport->first_display_row;
   } else if (motion == CWIKI_MOTION_VIEWPORT_LOW) {
      row = viewport->last_display_row;
   } else {
      row = viewport->first_display_row +
          (viewport->last_display_row - viewport->first_display_row) / 2U;
   }
   item = cwiki_layout_row_at(layout, row);
   if (item == NULL) {
      errno = EINVAL;
      return -1;
   }
   destination->line = item->line;
   destination->byte = first_nonblank_between(&buffer->lines[item->line],
       item->source_start, item->source_end);
   return 0;
}

int
cwiki_motion_apply(const struct cwiki_buffer *buffer,
    const struct cwiki_layout_window *layout, struct cwiki_zone_engine *zones,
    struct cwiki_motion_state *state, enum cwiki_motion motion,
    const struct cwiki_motion_viewport *viewport,
    struct cwiki_motion_result *result)
{
   struct cwiki_motion_state next_state;
   struct cwiki_position from;
   struct cwiki_position to;
   struct cwiki_layout_range layout_range;
   bool inclusive = false;
   bool linewise = false;
   bool horizontal = false;

   if (state == NULL || result == NULL || !valid_position(buffer, state->cursor) ||
       motion < CWIKI_MOTION_LEFT || motion > CWIKI_MOTION_SENTENCE_FORWARD) {
      errno = EINVAL;
      return -1;
   }
   next_state = *state;
   from = state->cursor;
   to = from;
   switch (motion) {
   case CWIKI_MOTION_LEFT:
      horizontal = true;
      if (to.byte != 0U) {
         to.byte = cwiki_grapheme_previous(buffer->lines[to.line].bytes,
             buffer->lines[to.line].length, to.byte);
      }
      break;
   case CWIKI_MOTION_RIGHT:
      horizontal = true;
      if (to.byte < last_grapheme(&buffer->lines[to.line])) {
         to.byte = cwiki_grapheme_next(buffer->lines[to.line].bytes,
             buffer->lines[to.line].length, to.byte);
      }
      break;
   case CWIKI_MOTION_WORD_FORWARD:
   case CWIKI_MOTION_WORD_FORWARD_BIG:
      to = word_forward(buffer, to, motion == CWIKI_MOTION_WORD_FORWARD_BIG);
      break;
   case CWIKI_MOTION_WORD_BACKWARD:
   case CWIKI_MOTION_WORD_BACKWARD_BIG:
      to = word_backward(buffer, to, motion == CWIKI_MOTION_WORD_BACKWARD_BIG);
      break;
   case CWIKI_MOTION_WORD_END:
   case CWIKI_MOTION_WORD_END_BIG:
      to = word_end(buffer, to, motion == CWIKI_MOTION_WORD_END_BIG);
      inclusive = true;
      break;
   case CWIKI_MOTION_DISPLAY_DOWN:
   case CWIKI_MOTION_DISPLAY_UP:
      next_state.desired_source_column = SIZE_MAX;
      if (cwiki_layout_move_display_row(layout, buffer, from,
          motion == CWIKI_MOTION_DISPLAY_DOWN,
          &next_state.desired_display_column, &to, &layout_range) != 0) {
         return -1;
      }
      if (position_compare(from, to) == 0) {
         result->range.start = from;
         result->range.end = from;
      } else {
         result->range.start = layout_range.start;
         result->range.end = layout_range.end;
      }
      result->range.shape = CWIKI_MOTION_DISPLAY_ROWS;
      break;
   case CWIKI_MOTION_SOURCE_DOWN:
   case CWIKI_MOTION_SOURCE_UP:
      next_state.desired_display_column = SIZE_MAX;
      if (cwiki_layout_move_source_line(layout, buffer, from,
          motion == CWIKI_MOTION_SOURCE_DOWN,
          &next_state.desired_source_column, &to, &layout_range) != 0) {
         return -1;
      }
      linewise = true;
      break;
   case CWIKI_MOTION_DOCUMENT_FIRST:
      to = (struct cwiki_position){0U, first_nonblank(&buffer->lines[0])};
      linewise = true;
      break;
   case CWIKI_MOTION_DOCUMENT_LAST:
      to.line = buffer->line_count - 1U;
      to.byte = first_nonblank(&buffer->lines[to.line]);
      linewise = true;
      break;
   case CWIKI_MOTION_VIEWPORT_HIGH:
   case CWIKI_MOTION_VIEWPORT_MIDDLE:
   case CWIKI_MOTION_VIEWPORT_LOW:
      if (viewport_destination(buffer, layout, viewport, motion, &to) != 0) {
         return -1;
      }
      linewise = true;
      break;
   case CWIKI_MOTION_LINE_START:
      to.byte = 0U;
      break;
   case CWIKI_MOTION_FIRST_NONBLANK:
   case CWIKI_MOTION_LINE_FIRST_NONBLANK:
      to.byte = first_nonblank(&buffer->lines[to.line]);
      break;
   case CWIKI_MOTION_LINE_END:
      to.byte = last_grapheme(&buffer->lines[to.line]);
      inclusive = true;
      break;
   case CWIKI_MOTION_PREVIOUS_LINE:
   case CWIKI_MOTION_NEXT_LINE:
      if (motion == CWIKI_MOTION_PREVIOUS_LINE && to.line != 0U) {
         to.line--;
      } else if (motion == CWIKI_MOTION_NEXT_LINE &&
          to.line + 1U < buffer->line_count) {
         to.line++;
      }
      to.byte = first_nonblank(&buffer->lines[to.line]);
      linewise = true;
      break;
   case CWIKI_MOTION_PARAGRAPH_BACKWARD:
      to = paragraph_backward(buffer, to);
      break;
   case CWIKI_MOTION_PARAGRAPH_FORWARD:
      to = paragraph_forward(buffer, to);
      break;
   case CWIKI_MOTION_SENTENCE_BACKWARD:
   case CWIKI_MOTION_SENTENCE_FORWARD:
      if (zones == NULL) {
         errno = EINVAL;
         return -1;
      }
      to = motion == CWIKI_MOTION_SENTENCE_BACKWARD ?
          sentence_backward(zones, buffer, to) :
          sentence_forward(zones, buffer, to);
      break;
   }
   if (motion < CWIKI_MOTION_DISPLAY_DOWN ||
       motion > CWIKI_MOTION_SOURCE_UP) {
      next_state.desired_display_column = SIZE_MAX;
      next_state.desired_source_column = SIZE_MAX;
   }
   if (linewise) {
      size_t first = from.line < to.line ? from.line : to.line;
      size_t last = from.line < to.line ? to.line : from.line;

      if ((motion == CWIKI_MOTION_SOURCE_DOWN ||
          motion == CWIKI_MOTION_SOURCE_UP) &&
          position_compare(from, to) == 0) {
         result->range.start = (struct cwiki_position){from.line, 0U};
         result->range.end = result->range.start;
         result->range.shape = CWIKI_MOTION_LINEWISE;
      } else {
         line_range(buffer, first, last, &result->range);
      }
   } else if (motion < CWIKI_MOTION_DISPLAY_DOWN ||
       motion > CWIKI_MOTION_DISPLAY_UP) {
      character_range(buffer, from, to, inclusive, &result->range);
   }
   next_state.cursor = to;
   result->destination = to;
   if (horizontal) {
      set_reveal(layout, to, result);
   } else {
      result->reveal_line = to.line;
      result->reveal = (struct cwiki_conceal_reveal){0U, 0U, false};
   }
   *state = next_state;
   return 0;
}
