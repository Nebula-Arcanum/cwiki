#include "render.h"

#include "editor.h"
#include "highlight.h"
#include "layout.h"
#include "terminal.h"
#include "unicode.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <utf8proc.h>

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

/* Iterate once, rather than repeatedly validating the entire remaining line. */
static size_t
cluster_end(const char *bytes, size_t length, size_t start)
{
   size_t offset = start;
   utf8proc_int32_t previous = 0;
   utf8proc_int32_t state = 0;

   while (offset < length) {
      utf8proc_int32_t current;
      utf8proc_ssize_t used = utf8proc_iterate(
          (const utf8proc_uint8_t *)bytes + offset,
          (utf8proc_ssize_t)(length - offset < 4U ? length - offset : 4U),
          &current);

      if (used <= 0) {
         return SIZE_MAX;
      }
      if (offset != start && utf8proc_grapheme_break_stateful(previous,
          current, &state) != 0) {
         return offset;
      }
      previous = current;
      offset += (size_t)used;
   }
   return offset;
}

static const char *
style(enum cwiki_highlight_role role)
{
   static const char *const styles[CWIKI_HIGHLIGHT_ROLE_COUNT] = {
      "\x1b[0m", "\x1b[0;36m", "\x1b[0;33m", "\x1b[0;33m",
      "\x1b[0;32m", "\x1b[0m", "\x1b[0;34m", "\x1b[0;35m",
      "\x1b[0;35m", "\x1b[0;90m", "\x1b[0;36m", "\x1b[0;31m"
   };

   return styles[role];
}

static enum cwiki_highlight_role
role_at(const struct cwiki_highlight_line *highlight, size_t source,
    size_t *index)
{
   if (highlight == NULL) {
      return CWIKI_HIGHLIGHT_PROSE;
   }
   if (highlight->degraded) {
      return CWIKI_HIGHLIGHT_RAW;
   }
   while (*index < highlight->run_count &&
       highlight->runs[*index].source_end <= source) {
      (*index)++;
   }
   if (*index < highlight->run_count &&
       highlight->runs[*index].source_start <= source) {
      return highlight->runs[*index].role;
   }
   return CWIKI_HIGHLIGHT_PROSE;
}

/* Emit only whole clusters within [left, right); pad clipped wide glyphs. */
static void
paint(struct output *out, const char *bytes, size_t length, size_t left,
    size_t right, size_t *column, size_t *painted,
    const struct cwiki_conceal_line *display,
    const struct cwiki_highlight_line *highlight, bool raw)
{
   size_t offset = 0U;
   size_t run = 0U;
   size_t highlight_run = 0U;
   enum cwiki_highlight_role active = CWIKI_HIGHLIGHT_ROLE_COUNT;

   while (offset < length && *column < right && out->error == 0) {
      size_t end = cluster_end(bytes, length, offset);
      int width;
      size_t next;

      if (end == SIZE_MAX) {
         out->error = EINVAL;
         return;
      }
      width = cwiki_grapheme_width(bytes + offset, end - offset);
      if (width < 0 || (size_t)width > SIZE_MAX - *column) {
         out->error = EOVERFLOW;
         return;
      }
      next = *column + (size_t)width;
      if (width > 0 && next > left) {
         size_t start_visible = *column > left ? *column : left;
         size_t end_visible = next < right ? next : right;

         if (*column >= left && next <= right) {
            if (display != NULL) {
               size_t source;
               enum cwiki_highlight_role role;

               while (run < display->run_count &&
                   display->runs[run].display_end <= offset) {
                  run++;
               }
               if (run == display->run_count) {
                  out->error = EINVAL;
                  return;
               }
               source = display->runs[run].source_start;
               if (!display->runs[run].concealed) {
                  source += offset - display->runs[run].display_start;
               }
               role = raw ? CWIKI_HIGHLIGHT_RAW :
                   role_at(highlight, source, &highlight_run);
               if (role < 0 || role >= CWIKI_HIGHLIGHT_ROLE_COUNT) {
                  out->error = EINVAL;
                  return;
               }
               if (role != active) {
                  text(out, style(role));
                  active = role;
               }
            }
            emit(out, bytes + offset, end - offset);
         } else {
            spaces(out, end_visible - start_visible);
         }
         *painted += end_visible - start_visible;
      }
      *column = next;
      offset = end;
   }
}

static size_t
cell_width(struct output *out, const char *bytes, size_t length)
{
   size_t offset = 0U;
   size_t columns = 0U;

   while (offset < length) {
      size_t next = cluster_end(bytes, length, offset);
      int width;

      if (next == SIZE_MAX) {
         out->error = EINVAL;
         return 0U;
      }
      width = cwiki_grapheme_width(bytes + offset, next - offset);
      if (width < 0 || (size_t)width >= SIZE_MAX - columns) {
         out->error = EOVERFLOW;
         return 0U;
      }
      columns += (size_t)width;
      offset = next;
   }
   return columns;
}

static void
source_row(struct output *out, const struct cwiki_layout_window *layout,
    const struct cwiki_highlight_line *highlights,
    const struct cwiki_layout_row *row, size_t left, size_t width)
{
   size_t painted = 0U;

   if (row != NULL) {
      const struct cwiki_layout_line *line = &layout->lines[row->line];
      size_t column = 0U;
      size_t prefix = row->prefix_columns;
      size_t ignored = 0U;
      size_t right;

      if (layout->wrap && !line->code && !line->degraded) {
         left = 0U;
      }
      right = left + width;
      if (prefix > 0U) {
         size_t indent = prefix > layout->marker_columns ?
             prefix - layout->marker_columns : 0U;

         text(out, "\x1b[0;90m");
         spaces(out, indent);
         painted += indent;
         paint(out, layout->continuation_marker,
             strlen(layout->continuation_marker), 0U, prefix - indent,
             &column, &painted, NULL, NULL, false);
         spaces(out, prefix - painted);
         painted = prefix;
      }
      column = 0U;
      /* Scan the full display to preserve its original run/source offsets. */
      paint(out, line->display.display, row->display_end,
          row->column_start + left,
          row->column_start + right - prefix,
          &column, &ignored, &line->display,
          highlights == NULL ? NULL : &highlights[row->line], row->degraded);
      painted += ignored;
   }
   text(out, "\x1b[0m");
   spaces(out, width - painted);
}

static size_t
status_row(struct output *out, const struct cwiki_editor *editor,
    size_t width)
{
   static const char *const modes[] = { "NORMAL", "INSERT", "REPLACE" };
   size_t column = 0U;
   size_t painted = 0U;
   size_t cursor = 0U;

   text(out, "\x1b[0;7m");
   if (editor->mode == CWIKI_EDITOR_COMMAND) {
      size_t command_width = cell_width(out, editor->command,
          editor->command_length);
      size_t available = width - 1U;
      size_t left = command_width >= available ?
          command_width - available + 1U : 0U;

      text(out, ":");
      painted = 1U;
      if (available > 0U) {
         paint(out, editor->command, editor->command_length,
             left, left + available, &column, &painted, NULL, NULL, false);
      }
      cursor = painted < width ? painted : width - 1U;
   } else {
      const char *path = editor->document->path;
      const char *base = path == NULL ? "[No Name]" : strrchr(path, '/');
      const struct cwiki_position *at = &editor->motion.cursor;
      const struct cwiki_line *line = &editor->document->buffer.lines[at->line];
      size_t source_column = cell_width(out, line->bytes, at->byte);
      char coordinates[64];
      int length;

      base = path == NULL ? base : (base == NULL ? path : base + 1);
      paint(out, modes[editor->mode], strlen(modes[editor->mode]),
          0U, width, &column, &painted, NULL, NULL, false);
      paint(out, " ", 1U, 0U, width, &column, &painted, NULL, NULL, false);
      paint(out, base, strlen(base), 0U, width, &column, &painted,
          NULL, NULL, false);
      if (editor->document->dirty) {
         paint(out, " [+]", 4U, 0U, width, &column, &painted,
             NULL, NULL, false);
      }
      length = snprintf(coordinates, sizeof(coordinates), " %zu:%zu",
          at->line + 1U, source_column + 1U);
      if (length < 0 || (size_t)length >= sizeof(coordinates)) {
         out->error = EOVERFLOW;
         return 0U;
      }
      paint(out, coordinates, (size_t)length, 0U, width, &column,
          &painted, NULL, NULL, false);
   }
   spaces(out, width - painted);
   text(out, "\x1b[0m");
   return cursor;
}

static void
frame(struct output *out, const struct cwiki_layout_window *layout,
    const struct cwiki_highlight_line *highlights,
    const struct cwiki_editor *editor,
    const struct cwiki_render_viewport *view, size_t cursor_row,
    size_t cursor_column)
{
   size_t row;
   size_t command_cursor;
   const struct cwiki_layout_line *cursor_line =
       &layout->lines[editor->motion.cursor.line];
   size_t left = layout->wrap && !cursor_line->code && !cursor_line->degraded ?
       0U : view->horizontal_offset;

   text(out, CWIKI_TERMINAL_CURSOR_HIDE "\x1b[0m\x1b[2J\x1b[H");
   for (row = 0U; row + 1U < view->rows; row++) {
      position(out, row + 1U, 1U);
      source_row(out, layout, highlights,
          cwiki_layout_row_at(layout, view->first_row + row),
          view->horizontal_offset, view->columns);
   }
   position(out, view->rows, 1U);
   command_cursor = status_row(out, editor, view->columns);
   if (editor->mode == CWIKI_EDITOR_COMMAND) {
      position(out, view->rows, command_cursor + 1U);
      text(out, CWIKI_TERMINAL_CURSOR_SHOW);
   } else if (cursor_row >= view->first_row &&
       cursor_row - view->first_row < view->rows - 1U &&
       cursor_column >= left &&
       (cursor_column - left < view->columns ||
       (editor->motion.cursor.byte == cursor_line->source_length &&
       cursor_column - left == view->columns))) {
      size_t column = cursor_column - left;

      position(out, cursor_row - view->first_row + 1U,
          column < view->columns ? column + 1U : view->columns);
      text(out, CWIKI_TERMINAL_CURSOR_SHOW);
   }
}

int
cwiki_render_frame(const struct cwiki_layout_window *layout,
    const struct cwiki_highlight_line *highlights,
    const struct cwiki_editor *editor,
    const struct cwiki_render_viewport *viewport,
    char *bytes, size_t capacity, size_t *length)
{
   struct output out = { NULL, 0U, 0 };
   size_t cursor_row;
   size_t cursor_column;

   if (layout == NULL || editor == NULL || editor->document == NULL ||
       viewport == NULL || length == NULL ||
       (bytes == NULL && capacity != 0U) ||
       viewport->rows == 0U || viewport->rows > 4096U ||
       viewport->columns == 0U || viewport->columns > 4096U ||
       viewport->columns != layout->content_width ||
       viewport->first_row > SIZE_MAX - viewport->rows ||
       viewport->horizontal_offset > SIZE_MAX - viewport->columns ||
       editor->mode < CWIKI_EDITOR_NORMAL ||
       editor->mode > CWIKI_EDITOR_COMMAND ||
       (editor->command == NULL && editor->command_length != 0U) ||
       cwiki_layout_source_to_display(layout, &editor->document->buffer,
       editor->motion.cursor, &cursor_row, &cursor_column) != 0) {
      errno = EINVAL;
      return -1;
   }
   frame(&out, layout, highlights, editor, viewport, cursor_row, cursor_column);
   if (out.error != 0 || (bytes != NULL && capacity <= out.length)) {
      errno = out.error != 0 ? out.error : ENOSPC;
      return -1;
   }
   if (bytes != NULL) {
      out.bytes = bytes;
      out.length = 0U;
      frame(&out, layout, highlights, editor, viewport, cursor_row, cursor_column);
      bytes[out.length] = '\0';
   }
   *length = out.length;
   return 0;
}
