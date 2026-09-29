#include "layout.h"

#include "unicode.h"
#include "zone.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <utf8proc.h>

static bool
add_overflow(size_t left, size_t right, size_t *sum)
{
   if (right > SIZE_MAX - left) {
      return true;
   }
   *sum = left + right;
   return false;
}

static int
reserve(void **items, size_t item_size, size_t needed, size_t *capacity)
{
   size_t next;
   void *grown;

   if (needed <= *capacity) {
      return 0;
   }
   next = *capacity == 0U ? 8U : *capacity;
   while (next < needed) {
      if (next > SIZE_MAX / 2U) {
         errno = ENOMEM;
         return -1;
      }
      next *= 2U;
   }
   if (item_size != 0U && next > SIZE_MAX / item_size) {
      errno = ENOMEM;
      return -1;
   }
   grown = realloc(*items, next * item_size);
   if (grown == NULL) {
      return -1;
   }
   *items = grown;
   *capacity = next;
   return 0;
}

static void
free_line(struct cwiki_layout_line *line)
{
   free(line->source);
   cwiki_conceal_line_free(&line->display);
   free(line->rows);
   (void)memset(line, 0, sizeof(*line));
}

void
cwiki_layout_window_init(struct cwiki_layout_window *window)
{
   if (window != NULL) {
      (void)memset(window, 0, sizeof(*window));
   }
}

void
cwiki_layout_window_free(struct cwiki_layout_window *window)
{
   size_t line;

   if (window == NULL) {
      return;
   }
   for (line = 0U; line < window->line_count; line++) {
      free_line(&window->lines[line]);
   }
   free(window->lines);
   free(window->continuation_marker);
   (void)memset(window, 0, sizeof(*window));
}

static int
marker_width(const char *marker, size_t *width)
{
   size_t length;
   size_t offset = 0U;
   size_t columns = 0U;

   if (marker == NULL) {
      errno = EINVAL;
      return -1;
   }
   length = strlen(marker);
   if (!cwiki_utf8_validate(marker, length)) {
      errno = EINVAL;
      return -1;
   }
   while (offset < length) {
      size_t next = cwiki_grapheme_next(marker, length, offset);
      int cluster_width;

      if (next == SIZE_MAX) {
         errno = EINVAL;
         return -1;
      }
      cluster_width = cwiki_grapheme_width(marker + offset, next - offset);
      if (cluster_width < 0 || add_overflow(columns,
          (size_t)cluster_width, &columns)) {
         errno = EOVERFLOW;
         return -1;
      }
      offset = next;
   }
   *width = columns;
   return 0;
}

static bool
same_options(const struct cwiki_layout_window *window,
    const struct cwiki_layout_options *options)
{
   const char *marker = options->continuation_marker;

   return marker != NULL && window->continuation_marker != NULL &&
       window->content_width == options->content_width &&
       window->wrap == options->wrap &&
       window->break_indent == options->break_indent &&
       strcmp(window->continuation_marker, marker) == 0 &&
       window->conceal_categories == options->conceal_categories &&
       window->mode == options->mode &&
       window->concealcursor_modes == options->concealcursor_modes &&
       window->cursor.line == options->cursor.line &&
       window->cursor.byte == options->cursor.byte &&
       window->reveal_line == options->reveal_line &&
       window->reveal.active == options->reveal.active &&
       window->reveal.source_start == options->reveal.source_start &&
       window->reveal.source_end == options->reveal.source_end;
}

static bool
cache_matches_buffer(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer)
{
   size_t line;

   if (window == NULL || buffer == NULL || !window->valid ||
       window->line_count != buffer->line_count) {
      return false;
   }
   for (line = 0U; line < buffer->line_count; line++) {
      if (buffer->lines[line].zone_dirty ||
          window->lines[line].source_length != buffer->lines[line].length ||
          (buffer->lines[line].length != 0U &&
          memcmp(window->lines[line].source, buffer->lines[line].bytes,
          buffer->lines[line].length) != 0) ||
          window->lines[line].degraded !=
          cwiki_buffer_line_degraded(buffer, line)) {
         return false;
      }
   }
   return true;
}

bool
cwiki_layout_needs_rebuild(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer,
    const struct cwiki_layout_options *options)
{
   return options == NULL || !cache_matches_buffer(window, buffer) ||
       !same_options(window, options);
}

static bool
line_is_code(struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *buffer, size_t line)
{
   struct cwiki_zone start;
   struct cwiki_zone end;

   return cwiki_zone_at(zones, buffer, line, 0U, &start) == 0 &&
       cwiki_zone_at(zones, buffer, line, buffer->lines[line].length,
       &end) == 0 &&
       (start.kind == CWIKI_ZONE_CODE || end.kind == CWIKI_ZONE_CODE);
}

static int
build_degraded_display(const struct cwiki_line *source,
    struct cwiki_conceal_line *display)
{
   size_t offset = 0U;
   size_t columns = 0U;
   size_t cluster_width = 0U;
   utf8proc_int32_t previous = 0;
   utf8proc_int32_t state = 0;
   bool have_previous = false;

   (void)memset(display, 0, sizeof(*display));
   if (source->length > SIZE_MAX - 1U) {
      errno = ENOMEM;
      return -1;
   }
   display->display = malloc(source->length + 1U);
   if (display->display == NULL) {
      return -1;
   }
   if (source->length != 0U) {
      display->runs = calloc(1U, sizeof(*display->runs));
      if (display->runs == NULL) {
         cwiki_conceal_line_free(display);
         return -1;
      }
      (void)memcpy(display->display, source->bytes, source->length);
      while (offset < source->length) {
         utf8proc_int32_t codepoint;
         utf8proc_ssize_t used = utf8proc_iterate(
             (const utf8proc_uint8_t *)source->bytes + offset,
             (utf8proc_ssize_t)(source->length - offset < 4U ?
             source->length - offset : 4U), &codepoint);
         const utf8proc_property_t *property;

         if (used <= 0) {
            cwiki_conceal_line_free(display);
            errno = EINVAL;
            return -1;
         }
         if (have_previous && utf8proc_grapheme_break_stateful(previous,
             codepoint, &state) != 0) {
            if (add_overflow(columns, cluster_width, &columns)) {
               cwiki_conceal_line_free(display);
               errno = EOVERFLOW;
               return -1;
            }
            cluster_width = 0U;
            state = 0;
         }
         property = utf8proc_get_property(codepoint);
         if (codepoint == 0xfe0f && cluster_width != 0U) {
            cluster_width = 2U;
         } else if (cluster_width == 0U && property->ignorable == 0U &&
             property->category != UTF8PROC_CATEGORY_MN &&
             property->category != UTF8PROC_CATEGORY_MC &&
             property->category != UTF8PROC_CATEGORY_ME &&
             property->charwidth != 0U) {
            cluster_width = property->charwidth == 2U ||
                property->boundclass == UTF8PROC_BOUNDCLASS_REGIONAL_INDICATOR ?
                2U : 1U;
         }
         previous = codepoint;
         have_previous = true;
         offset += (size_t)used;
      }
      if (add_overflow(columns, cluster_width, &columns)) {
         cwiki_conceal_line_free(display);
         errno = EOVERFLOW;
         return -1;
      }
      display->run_count = 1U;
      display->runs[0].source_end = source->length;
      display->runs[0].display_end = source->length;
      display->runs[0].column_end = columns;
   }
   display->display[source->length] = '\0';
   display->display_length = source->length;
   display->columns = columns;
   return 0;
}

static int
display_byte_at_column(const struct cwiki_conceal_line *display,
    size_t column, size_t *byte)
{
   size_t run;

   if (column > display->columns || byte == NULL) {
      errno = EINVAL;
      return -1;
   }
   if (column == display->columns) {
      *byte = display->display_length;
      return 0;
   }
   for (run = 0U; run < display->run_count; run++) {
      const struct cwiki_conceal_run *item = &display->runs[run];

      if (column < item->column_end) {
         *byte = item->display_start;
         if (!item->concealed && column > item->column_start) {
            size_t offset = item->display_start;
            size_t current = item->column_start;

            while (offset < item->display_end) {
               size_t next = cwiki_grapheme_next(display->display,
                   display->display_length, offset);
               int width;
               size_t next_column;

               if (next == SIZE_MAX) {
                  errno = EINVAL;
                  return -1;
               }
               width = cwiki_grapheme_width(display->display + offset,
                   next - offset);
               if (width < 0 || add_overflow(current, (size_t)width,
                   &next_column)) {
                  errno = EOVERFLOW;
                  return -1;
               }
               if (column < next_column) {
                  break;
               }
               current = next_column;
               offset = next;
            }
            *byte = offset;
         }
         return 0;
      }
   }
   errno = EINVAL;
   return -1;
}

static bool
display_grapheme_space(const char *bytes, size_t length)
{
   utf8proc_int32_t codepoint;
   utf8proc_ssize_t used;
   utf8proc_category_t category;

   if (length == 0U) {
      return false;
   }
   used = utf8proc_iterate((const utf8proc_uint8_t *)bytes,
       (utf8proc_ssize_t)(length < 4U ? length : 4U), &codepoint);
   if (used <= 0) {
      return false;
   }
   category = utf8proc_category(codepoint);
   return category == UTF8PROC_CATEGORY_ZS ||
       category == UTF8PROC_CATEGORY_ZL ||
       category == UTF8PROC_CATEGORY_ZP ||
       (codepoint >= 0x09 && codepoint <= 0x0d) || codepoint == 0x85;
}

static int
source_at_column(const struct cwiki_layout_line *line,
    size_t column, size_t *source)
{
   return cwiki_conceal_display_to_source(&line->display, line->source,
       line->source_length, column, source, NULL);
}

static int
append_row(struct cwiki_layout_line *line, size_t *capacity, size_t line_index,
    size_t column_start, size_t column_end, size_t prefix, bool continuation)
{
   struct cwiki_layout_row *row;

   if (reserve((void **)&line->rows, sizeof(*line->rows),
       line->row_count + 1U, capacity) != 0) {
      return -1;
   }
   row = &line->rows[line->row_count++];
   (void)memset(row, 0, sizeof(*row));
   row->line = line_index;
   row->column_start = column_start;
   row->column_end = column_end;
   row->prefix_columns = prefix;
   row->continuation = continuation;
   row->degraded = line->degraded;
   row->horizontal_extent = line->horizontal_extent;
   if (display_byte_at_column(&line->display, column_start,
       &row->display_start) != 0 ||
       display_byte_at_column(&line->display, column_end,
       &row->display_end) != 0 ||
       source_at_column(line, column_start, &row->source_start) != 0 ||
       source_at_column(line, column_end, &row->source_end) != 0) {
      line->row_count--;
      return -1;
   }
   return 0;
}

static size_t
first_nonspace_column(const struct cwiki_conceal_line *display)
{
   size_t run;

   for (run = 0U; run < display->run_count; run++) {
      const struct cwiki_conceal_run *item = &display->runs[run];

      if (item->concealed || !display_grapheme_space(
          display->display + item->display_start,
          item->display_end - item->display_start)) {
         return item->column_start;
      }
   }
   return 0U;
}

static int
build_rows(struct cwiki_layout_line *line, size_t line_index,
    const struct cwiki_layout_options *options, size_t marker_columns)
{
   size_t capacity = 0U;
   size_t start = 0U;
   size_t indent = options->break_indent ?
       first_nonspace_column(&line->display) : 0U;

   line->horizontal_extent = line->display.columns;
   if (!options->wrap || line->code || line->degraded ||
       line->display.columns == 0U) {
      return append_row(line, &capacity, line_index, 0U,
          line->display.columns, 0U, false);
   }
   while (start < line->display.columns) {
      bool continuation = start != 0U;
      size_t prefix = 0U;
      size_t available;
      size_t cursor = start;
      size_t last_space = SIZE_MAX;
      size_t end;

      if (continuation) {
         if (add_overflow(indent, marker_columns, &prefix)) {
            errno = EOVERFLOW;
            return -1;
         }
         if (prefix >= options->content_width) {
            prefix = options->content_width - 1U;
         }
      }
      available = options->content_width - prefix;
      while (cursor < line->display.columns) {
         size_t byte;
         size_t next_byte;
         size_t next_column;
         int width;

         if (display_byte_at_column(&line->display, cursor, &byte) != 0) {
            return -1;
         }
         next_byte = cwiki_grapheme_next(line->display.display,
             line->display.display_length, byte);
         if (next_byte == SIZE_MAX) {
            errno = EINVAL;
            return -1;
         }
         width = cwiki_grapheme_width(line->display.display + byte,
             next_byte - byte);
         if (width < 0 || add_overflow(cursor, (size_t)width,
             &next_column)) {
            errno = EOVERFLOW;
            return -1;
         }
         if (next_column - start > available && cursor != start) {
            break;
         }
         cursor = next_column;
         if (display_grapheme_space(line->display.display + byte,
             next_byte - byte)) {
            last_space = cursor;
         }
         if (cursor - start >= available) {
            break;
         }
      }
      if (cursor <= start) {
         errno = EINVAL;
         return -1;
      }
      end = cursor;
      if (cursor < line->display.columns && last_space != SIZE_MAX &&
          last_space > start) {
         end = last_space;
      }
      if (append_row(line, &capacity, line_index, start, end, prefix,
          continuation) != 0) {
         return -1;
      }
      start = end;
   }
   return 0;
}

static bool
valid_options(const struct cwiki_buffer *buffer,
    const struct cwiki_layout_options *options)
{
   return options != NULL && options->content_width != 0U &&
       options->continuation_marker != NULL &&
       options->mode >= 0 && options->mode < CWIKI_CONCEAL_MODE_COUNT &&
       options->cursor.line < buffer->line_count &&
       options->cursor.byte <= buffer->lines[options->cursor.line].length &&
       cwiki_grapheme_boundary(buffer->lines[options->cursor.line].bytes,
       buffer->lines[options->cursor.line].length, options->cursor.byte) &&
       (!options->reveal.active ||
       (options->reveal_line < buffer->line_count &&
       options->reveal.source_start < options->reveal.source_end &&
       options->reveal.source_end <=
       buffer->lines[options->reveal_line].length));
}

int
cwiki_layout_rebuild(struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, struct cwiki_zone_engine *zones,
    const struct cwiki_conceal_table *conceal,
    const struct cwiki_layout_options *options)
{
   struct cwiki_layout_window built;
   size_t marker_columns;
   size_t line;

   if (window == NULL || buffer == NULL || zones == NULL || conceal == NULL ||
       !valid_options(buffer, options) ||
       marker_width(options->continuation_marker, &marker_columns) != 0) {
      errno = EINVAL;
      return -1;
   }
   for (line = 0U; line < buffer->line_count; line++) {
      if (buffer->lines[line].zone_dirty) {
         errno = EAGAIN;
         return -1;
      }
   }
   cwiki_layout_window_init(&built);
   if (buffer->line_count > SIZE_MAX / sizeof(*built.lines)) {
      errno = ENOMEM;
      return -1;
   }
   built.lines = calloc(buffer->line_count, sizeof(*built.lines));
   built.continuation_marker = strdup(options->continuation_marker);
   if (built.lines == NULL || built.continuation_marker == NULL) {
      cwiki_layout_window_free(&built);
      return -1;
   }
   built.line_count = buffer->line_count;
   built.content_width = options->content_width;
   built.wrap = options->wrap;
   built.break_indent = options->break_indent;
   built.marker_columns = marker_columns;
   built.conceal_categories = options->conceal_categories;
   built.mode = options->mode;
   built.concealcursor_modes = options->concealcursor_modes;
   built.cursor = options->cursor;
   built.reveal_line = options->reveal_line;
   built.reveal = options->reveal;
   for (line = 0U; line < buffer->line_count; line++) {
      struct cwiki_layout_line *item = &built.lines[line];
      const struct cwiki_line *source = &buffer->lines[line];
      const struct cwiki_conceal_reveal *reveal = NULL;

      item->source_length = source->length;
      if (source->length != 0U) {
         item->source = malloc(source->length);
         if (item->source == NULL) {
            cwiki_layout_window_free(&built);
            return -1;
         }
         (void)memcpy(item->source, source->bytes, source->length);
      }
      item->degraded = cwiki_buffer_line_degraded(buffer, line);
      item->code = !item->degraded && line_is_code(zones, buffer, line);
      item->first_row = built.row_count;
      if (options->reveal.active && options->reveal_line == line) {
         reveal = &options->reveal;
      }
      if ((item->degraded ? build_degraded_display(source, &item->display) :
          cwiki_conceal_build_line(conceal, zones, buffer, line,
          source->bytes, source->length, options->conceal_categories,
          line == options->cursor.line, options->mode,
          options->concealcursor_modes, reveal, &item->display)) != 0 ||
          build_rows(item, line, options, marker_columns) != 0 ||
          add_overflow(built.row_count, item->row_count,
          &built.row_count)) {
         cwiki_layout_window_free(&built);
         return -1;
      }
   }
   built.valid = true;
   cwiki_layout_window_free(window);
   *window = built;
   return 0;
}

const struct cwiki_layout_row *
cwiki_layout_row_at(const struct cwiki_layout_window *window, size_t row)
{
   size_t line;

   if (window == NULL || !window->valid || row >= window->row_count) {
      return NULL;
   }
   for (line = 0U; line < window->line_count; line++) {
      const struct cwiki_layout_line *item = &window->lines[line];

      if (row >= item->first_row && row - item->first_row < item->row_count) {
         return &item->rows[row - item->first_row];
      }
   }
   return NULL;
}

int
cwiki_layout_source_to_display(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, struct cwiki_position source,
    size_t *display_row, size_t *display_column)
{
   const struct cwiki_layout_line *line;
   size_t column;
   size_t row;

   if (window == NULL || buffer == NULL || display_row == NULL ||
       display_column == NULL || !cache_matches_buffer(window, buffer) ||
       source.line >= window->line_count ||
       source.byte > buffer->lines[source.line].length ||
       !cwiki_grapheme_boundary(buffer->lines[source.line].bytes,
       buffer->lines[source.line].length, source.byte)) {
      errno = EINVAL;
      return -1;
   }
   line = &window->lines[source.line];
   if (cwiki_conceal_source_to_display(&line->display, line->source,
       line->source_length, source.byte, &column) != 0) {
      return -1;
   }
   for (row = 0U; row < line->row_count; row++) {
      const struct cwiki_layout_row *item = &line->rows[row];

      if (column < item->column_end || row + 1U == line->row_count) {
         size_t relative = column - item->column_start;

         if (add_overflow(line->first_row, row, display_row) ||
             add_overflow(item->prefix_columns, relative, display_column)) {
            errno = EOVERFLOW;
            return -1;
         }
         return 0;
      }
   }
   errno = EINVAL;
   return -1;
}

int
cwiki_layout_display_to_source(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, size_t display_row,
    size_t display_column, struct cwiki_position *source)
{
   const struct cwiki_layout_row *row;
   const struct cwiki_layout_line *line;
   size_t content_column;

   if (window == NULL || buffer == NULL || source == NULL ||
       !cache_matches_buffer(window, buffer) ||
       display_row >= window->row_count) {
      errno = EINVAL;
      return -1;
   }
   row = cwiki_layout_row_at(window, display_row);
   if (row == NULL || row->line >= buffer->line_count) {
      errno = EINVAL;
      return -1;
   }
   line = &window->lines[row->line];
   if (display_column <= row->prefix_columns) {
      content_column = row->column_start;
   } else if (display_column - row->prefix_columns >=
       row->column_end - row->column_start) {
      content_column = row->column_end;
   } else {
      content_column = row->column_start + display_column -
          row->prefix_columns;
   }
   source->line = row->line;
   return cwiki_conceal_display_to_source(&line->display, line->source,
       line->source_length, content_column, &source->byte, NULL);
}

static void
display_range(const struct cwiki_layout_window *window, size_t first,
    size_t last, struct cwiki_layout_range *range)
{
   const struct cwiki_layout_row *start = cwiki_layout_row_at(window, first);
   const struct cwiki_layout_row *end = cwiki_layout_row_at(window, last);

   range->start.line = start->line;
   range->start.byte = start->source_start;
   range->end.line = end->line;
   range->end.byte = end->source_end;
   range->kind = CWIKI_LAYOUT_RANGE_DISPLAY_ROWS;
}

int
cwiki_layout_move_display_row(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, struct cwiki_position from, bool down,
    size_t *desired_display_column, struct cwiki_position *to,
    struct cwiki_layout_range *range)
{
   size_t row;
   size_t column;
   size_t target;

   if (desired_display_column == NULL || to == NULL || range == NULL ||
       cwiki_layout_source_to_display(window, buffer, from, &row, &column) != 0) {
      errno = EINVAL;
      return -1;
   }
   if (*desired_display_column == SIZE_MAX) {
      *desired_display_column = column;
   }
   if ((down && row + 1U >= window->row_count) || (!down && row == 0U)) {
      *to = from;
      display_range(window, row, row, range);
      return 0;
   }
   target = down ? row + 1U : row - 1U;
   if (cwiki_layout_display_to_source(window, buffer, target,
       *desired_display_column, to) != 0) {
      return -1;
   }
   display_range(window, down ? row : target, down ? target : row, range);
   return 0;
}

static int
raw_source_to_column(const struct cwiki_line *line, size_t byte,
    size_t *column)
{
   size_t offset = 0U;
   size_t width = 0U;

   if (byte > line->length ||
       !cwiki_grapheme_boundary(line->bytes, line->length, byte)) {
      errno = EINVAL;
      return -1;
   }
   while (offset < byte) {
      size_t next = cwiki_grapheme_next(line->bytes, line->length, offset);
      int cluster_width;

      if (next == SIZE_MAX) {
         errno = EINVAL;
         return -1;
      }
      cluster_width = cwiki_grapheme_width(line->bytes + offset,
          next - offset);
      if (cluster_width < 0 || add_overflow(width, (size_t)cluster_width,
          &width)) {
         errno = EOVERFLOW;
         return -1;
      }
      offset = next;
   }
   *column = width;
   return 0;
}

static int
raw_column_to_source(const struct cwiki_line *line, size_t column,
    size_t *byte)
{
   size_t offset = 0U;
   size_t current = 0U;

   while (offset < line->length) {
      size_t next = cwiki_grapheme_next(line->bytes, line->length, offset);
      int width;
      size_t next_column;

      if (next == SIZE_MAX) {
         errno = EINVAL;
         return -1;
      }
      width = cwiki_grapheme_width(line->bytes + offset, next - offset);
      if (width < 0 || add_overflow(current, (size_t)width, &next_column)) {
         errno = EOVERFLOW;
         return -1;
      }
      if (column < next_column) {
         break;
      }
      current = next_column;
      offset = next;
   }
   *byte = offset;
   return 0;
}

int
cwiki_layout_move_source_line(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, struct cwiki_position from, bool down,
    size_t *desired_source_column, struct cwiki_position *to,
    struct cwiki_layout_range *range)
{
   size_t target;
   size_t low;
   size_t high;

   if (window == NULL || buffer == NULL || desired_source_column == NULL ||
       to == NULL || range == NULL || !cache_matches_buffer(window, buffer) ||
       from.line >= buffer->line_count ||
       raw_source_to_column(&buffer->lines[from.line], from.byte,
       &target) != 0) {
      errno = EINVAL;
      return -1;
   }
   if (*desired_source_column == SIZE_MAX) {
      *desired_source_column = target;
   }
   if ((down && from.line + 1U >= buffer->line_count) ||
       (!down && from.line == 0U)) {
      *to = from;
      low = from.line;
      high = from.line;
   } else {
      target = down ? from.line + 1U : from.line - 1U;
      to->line = target;
      if (raw_column_to_source(&buffer->lines[target],
          *desired_source_column, &to->byte) != 0) {
         return -1;
      }
      low = down ? from.line : target;
      high = down ? target : from.line;
   }
   range->start.line = low;
   range->start.byte = 0U;
   range->end.line = high;
   range->end.byte = buffer->lines[high].length;
   range->kind = CWIKI_LAYOUT_RANGE_SOURCE_LINES;
   return 0;
}
