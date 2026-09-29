#include "editor.h"

#include "layout.h"
#include "snippet.h"
#include "unicode.h"
#include "zone.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static enum cwiki_editor_status
error_status(void)
{
   return errno == ENOMEM ? CWIKI_EDITOR_NO_MEMORY : CWIKI_EDITOR_INVALID;
}

static bool
normal_editor(const struct cwiki_editor *editor)
{
   return editor != NULL && editor->document != NULL &&
       editor->mode == CWIKI_EDITOR_NORMAL &&
       !cwiki_undo_transaction_active(&editor->undo);
}

static size_t
last_grapheme(const struct cwiki_line *line)
{
   return line->length == 0U ? 0U :
       cwiki_grapheme_previous(line->bytes, line->length, line->length);
}

static void
reset_goals(struct cwiki_editor *editor)
{
   editor->motion.desired_display_column = SIZE_MAX;
   editor->motion.desired_source_column = SIZE_MAX;
}

static bool
current_sequence(const struct cwiki_undo *undo, uint64_t *sequence)
{
   size_t index;

   for (index = 0U; index < cwiki_undo_state_count(undo); index++) {
      struct cwiki_undo_state_info info;

      if (cwiki_undo_state_info(undo, index, &info) == 0 && info.current) {
         *sequence = info.sequence;
         return true;
      }
   }
   return false;
}

static bool
range_empty(const struct cwiki_motion_range *range)
{
   return range->start.line == range->end.line &&
       range->start.byte == range->end.byte;
}

static int
append_bytes(char **bytes, size_t *length, size_t *capacity,
    const char *addition, size_t addition_length)
{
   size_t required;

   if (addition_length > SIZE_MAX - *length) {
      errno = ENOMEM;
      return -1;
   }
   required = *length + addition_length;
   if (required > *capacity) {
      size_t grown = *capacity == 0U ? 64U : *capacity;
      char *replacement;

      while (grown < required) {
         if (grown > SIZE_MAX / 2U) {
            grown = required;
            break;
         }
         grown *= 2U;
      }
      replacement = realloc(*bytes, grown);
      if (replacement == NULL) {
         return -1;
      }
      *bytes = replacement;
      *capacity = grown;
   }
   if (addition_length != 0U) {
      memcpy(*bytes + *length, addition, addition_length);
   }
   *length = required;
   return 0;
}

static int
capture_range(const struct cwiki_buffer *buffer,
    const struct cwiki_motion_range *range, struct cwiki_editor_yank *yank)
{
   char *bytes = NULL;
   size_t length = 0U;
   size_t capacity = 0U;
   size_t line;

   if (range->shape == CWIKI_MOTION_LINEWISE) {
      if (range->start.byte != 0U || range->end.byte != 0U ||
          range->start.line >= range->end.line ||
          range->end.line > buffer->line_count) {
         errno = EINVAL;
         return -1;
      }
      for (line = range->start.line; line < range->end.line; line++) {
         if (append_bytes(&bytes, &length, &capacity,
             buffer->lines[line].bytes, buffer->lines[line].length) != 0 ||
             append_bytes(&bytes, &length, &capacity, "\n", 1U) != 0) {
            free(bytes);
            return -1;
         }
      }
      yank->bytes = bytes;
      yank->length = length;
      yank->linewise = true;
      return 0;
   }
   if (range->start.line >= buffer->line_count ||
       range->end.line >= buffer->line_count ||
       range->start.line > range->end.line ||
       range->start.byte > buffer->lines[range->start.line].length ||
       range->end.byte > buffer->lines[range->end.line].length) {
      errno = EINVAL;
      return -1;
   }
   if (range->start.line == range->end.line) {
      if (range->start.byte > range->end.byte ||
          append_bytes(&bytes, &length, &capacity,
          buffer->lines[range->start.line].bytes + range->start.byte,
          range->end.byte - range->start.byte) != 0) {
         free(bytes);
         return -1;
      }
   } else {
      const struct cwiki_line *first = &buffer->lines[range->start.line];
      const char *first_bytes = first->length == range->start.byte ? NULL :
          first->bytes + range->start.byte;

      if (append_bytes(&bytes, &length, &capacity,
          first_bytes, first->length - range->start.byte) != 0 ||
          append_bytes(&bytes, &length, &capacity, "\n", 1U) != 0) {
         free(bytes);
         return -1;
      }
      for (line = range->start.line + 1U; line < range->end.line; line++) {
         if (append_bytes(&bytes, &length, &capacity,
             buffer->lines[line].bytes, buffer->lines[line].length) != 0 ||
             append_bytes(&bytes, &length, &capacity, "\n", 1U) != 0) {
            free(bytes);
            return -1;
         }
      }
      if (append_bytes(&bytes, &length, &capacity,
          buffer->lines[range->end.line].bytes, range->end.byte) != 0) {
         free(bytes);
         return -1;
      }
   }
   yank->bytes = bytes;
   yank->length = length;
   yank->linewise = false;
   return 0;
}

static int
delete_character_range(struct cwiki_editor *editor,
    const struct cwiki_motion_range *range)
{
   struct cwiki_buffer *buffer = &editor->document->buffer;
   size_t intermediate;

   if (range->start.line == range->end.line) {
      return cwiki_undo_delete(&editor->undo, range->start.line,
          range->start.byte, range->end.byte - range->start.byte);
   }
   if (cwiki_undo_delete(&editor->undo, range->start.line, range->start.byte,
       buffer->lines[range->start.line].length - range->start.byte) != 0) {
      return -1;
   }
   intermediate = range->end.line - range->start.line - 1U;
   while (intermediate != 0U) {
      if (cwiki_undo_delete(&editor->undo, range->start.line + 1U, 0U,
          buffer->lines[range->start.line + 1U].length) != 0 ||
          cwiki_undo_join(&editor->undo, range->start.line) != 0) {
         return -1;
      }
      intermediate--;
   }
   if (cwiki_undo_delete(&editor->undo, range->start.line + 1U, 0U,
       range->end.byte) != 0 ||
       cwiki_undo_join(&editor->undo, range->start.line) != 0) {
      return -1;
   }
   return 0;
}

static int
delete_line_range(struct cwiki_editor *editor,
    const struct cwiki_motion_range *range, bool keep_empty)
{
   struct cwiki_buffer *buffer = &editor->document->buffer;
   size_t selected = range->end.line - range->start.line;
   bool has_following = range->end.line < buffer->line_count;

   if (cwiki_undo_delete(&editor->undo, range->start.line, 0U,
       buffer->lines[range->start.line].length) != 0) {
      return -1;
   }
   while (selected > 1U) {
      if (cwiki_undo_delete(&editor->undo, range->start.line + 1U, 0U,
          buffer->lines[range->start.line + 1U].length) != 0 ||
          cwiki_undo_join(&editor->undo, range->start.line) != 0) {
         return -1;
      }
      selected--;
   }
   if (has_following && cwiki_undo_join(&editor->undo, range->start.line) != 0) {
      return -1;
   }
   if (keep_empty && has_following &&
       cwiki_undo_split(&editor->undo, range->start.line, 0U) != 0) {
      return -1;
   }
   return 0;
}

static enum cwiki_editor_status
apply_operator_range(struct cwiki_editor *editor,
    const struct cwiki_motion_range *range,
    enum cwiki_editor_operator operator_kind, uint64_t timestamp)
{
   struct cwiki_editor_yank captured = {0};
   struct cwiki_position original = editor->motion.cursor;
   int changed;

   editor->reveal.active = false;
   if (range_empty(range)) {
      editor->pending_operator = CWIKI_EDITOR_NO_OPERATOR;
      return CWIKI_EDITOR_NOTHING;
   }
   if (capture_range(&editor->document->buffer, range, &captured) != 0) {
      return error_status();
   }
   if (operator_kind == CWIKI_EDITOR_YANK) {
      free(editor->yank.bytes);
      editor->yank = captured;
      editor->motion.cursor = range->start;
      editor->pending_operator = CWIKI_EDITOR_NO_OPERATOR;
      reset_goals(editor);
      return CWIKI_EDITOR_OK;
   }
   if (cwiki_undo_begin(&editor->undo, timestamp) != 0) {
      free(captured.bytes);
      return error_status();
   }
   if (range->shape == CWIKI_MOTION_LINEWISE) {
      changed = delete_line_range(editor, range,
          operator_kind == CWIKI_EDITOR_CHANGE);
   } else {
      changed = delete_character_range(editor, range);
   }
   if (changed != 0) {
      (void)cwiki_undo_cancel(&editor->undo);
      editor->motion.cursor = original;
      free(captured.bytes);
      return error_status();
   }
   editor->motion.cursor = range->start;
   free(editor->yank.bytes);
   editor->yank = captured;
   editor->pending_operator = CWIKI_EDITOR_NO_OPERATOR;
   reset_goals(editor);
   cwiki_document_mark_dirty(editor->document);
   if (operator_kind == CWIKI_EDITOR_CHANGE) {
      editor->mode = CWIKI_EDITOR_INSERT;
      return CWIKI_EDITOR_OK;
   }
   if (cwiki_undo_commit(&editor->undo) != 0) {
      return error_status();
   }
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_init(struct cwiki_editor *editor, struct cwiki_document *document)
{
   if (editor == NULL || document == NULL || document->buffer.line_count == 0U) {
      return CWIKI_EDITOR_INVALID;
   }
   memset(editor, 0, sizeof(*editor));
   editor->document = document;
   editor->motion.desired_display_column = SIZE_MAX;
   editor->motion.desired_source_column = SIZE_MAX;
   if (cwiki_undo_init(&editor->undo, &document->buffer) != 0) {
      memset(editor, 0, sizeof(*editor));
      return error_status();
   }
   if (cwiki_buffer_register_position(&document->buffer,
       &editor->motion.cursor) != 0) {
      cwiki_undo_free(&editor->undo);
      memset(editor, 0, sizeof(*editor));
      return error_status();
   }
   editor->savepoint_valid = !document->dirty;
   editor->saved_sequence = 0U;
   return CWIKI_EDITOR_OK;
}

void
cwiki_editor_free(struct cwiki_editor *editor)
{
   if (editor == NULL) {
      return;
   }
   if (editor->document != NULL) {
      cwiki_buffer_unregister_position(&editor->document->buffer,
          &editor->motion.cursor);
   }
   if (cwiki_undo_transaction_active(&editor->undo)) {
      (void)cwiki_undo_cancel(&editor->undo);
   }
   cwiki_undo_free(&editor->undo);
   free(editor->yank.bytes);
   free(editor->command);
   memset(editor, 0, sizeof(*editor));
}

enum cwiki_editor_status
cwiki_editor_enter_insert(struct cwiki_editor *editor, bool append,
    uint64_t timestamp)
{
   struct cwiki_line *line;

   if (!normal_editor(editor)) {
      return CWIKI_EDITOR_INVALID;
   }
   if (cwiki_undo_begin(&editor->undo, timestamp) != 0) {
      return error_status();
   }
   if (append) {
      line = &editor->document->buffer.lines[editor->motion.cursor.line];
      if (editor->motion.cursor.byte < line->length) {
         editor->motion.cursor.byte = cwiki_grapheme_next(line->bytes,
             line->length, editor->motion.cursor.byte);
      }
      if (editor->reveal.active &&
          editor->motion.cursor.byte >= editor->reveal.source_end) {
         editor->reveal.active = false;
      }
   }
   editor->mode = CWIKI_EDITOR_INSERT;
   editor->pending_operator = CWIKI_EDITOR_NO_OPERATOR;
   reset_goals(editor);
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_enter_replace(struct cwiki_editor *editor, uint64_t timestamp)
{
   if (!normal_editor(editor)) {
      return CWIKI_EDITOR_INVALID;
   }
   if (cwiki_undo_begin(&editor->undo, timestamp) != 0) {
      return error_status();
   }
   editor->mode = CWIKI_EDITOR_REPLACE;
   editor->pending_operator = CWIKI_EDITOR_NO_OPERATOR;
   reset_goals(editor);
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_escape(struct cwiki_editor *editor)
{
   if (editor == NULL) {
      return CWIKI_EDITOR_INVALID;
   }
   if (editor->mode == CWIKI_EDITOR_COMMAND) {
      editor->command_length = 0U;
      editor->mode = CWIKI_EDITOR_NORMAL;
      return CWIKI_EDITOR_OK;
   }
   if (editor->mode == CWIKI_EDITOR_NORMAL) {
      editor->pending_operator = CWIKI_EDITOR_NO_OPERATOR;
      return CWIKI_EDITOR_OK;
   }
   if ((editor->mode != CWIKI_EDITOR_INSERT &&
       editor->mode != CWIKI_EDITOR_REPLACE) ||
       !cwiki_undo_transaction_active(&editor->undo)) {
      return CWIKI_EDITOR_INVALID;
   }
   if (cwiki_undo_commit(&editor->undo) != 0) {
      return error_status();
   }
   editor->mode = CWIKI_EDITOR_NORMAL;
   if (editor->motion.cursor.byte ==
       editor->document->buffer.lines[editor->motion.cursor.line].length &&
       editor->motion.cursor.byte != 0U) {
      editor->motion.cursor.byte = last_grapheme(
          &editor->document->buffer.lines[editor->motion.cursor.line]);
   }
   reset_goals(editor);
   return CWIKI_EDITOR_OK;
}

static int
insert_one(struct cwiki_editor *editor, const char *bytes, size_t length)
{
   struct cwiki_line *line =
       &editor->document->buffer.lines[editor->motion.cursor.line];

   if (editor->mode == CWIKI_EDITOR_REPLACE &&
       editor->motion.cursor.byte < line->length) {
      size_t next = cwiki_grapheme_next(line->bytes, line->length,
          editor->motion.cursor.byte);

      if (cwiki_undo_delete(&editor->undo, editor->motion.cursor.line,
          editor->motion.cursor.byte, next - editor->motion.cursor.byte) != 0) {
         return -1;
      }
   }
   return cwiki_undo_insert(&editor->undo, editor->motion.cursor.line,
       editor->motion.cursor.byte, bytes, length);
}

enum cwiki_editor_status
cwiki_editor_insert(struct cwiki_editor *editor, const char *bytes,
    size_t length)
{
   size_t offset = 0U;

   if (editor == NULL || (editor->mode != CWIKI_EDITOR_INSERT &&
       editor->mode != CWIKI_EDITOR_REPLACE) ||
       (bytes == NULL && length != 0U) || !cwiki_utf8_validate(bytes, length)) {
      return CWIKI_EDITOR_INVALID;
   }
   if (length != 0U) {
      editor->reveal.active = false;
   }
   while (offset < length) {
      if (bytes[offset] == '\n') {
         if (cwiki_undo_split(&editor->undo, editor->motion.cursor.line,
             editor->motion.cursor.byte) != 0) {
            return error_status();
         }
         offset++;
      } else {
         size_t next = cwiki_grapheme_next(bytes, length, offset);

         if (next == SIZE_MAX || insert_one(editor, bytes + offset,
             next - offset) != 0) {
            return error_status();
         }
         offset = next;
      }
   }
   if (length != 0U) {
      cwiki_document_mark_dirty(editor->document);
      reset_goals(editor);
   }
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_enter(struct cwiki_editor *editor)
{
   static const char newline = '\n';

   return cwiki_editor_insert(editor, &newline, 1U);
}

enum cwiki_editor_status
cwiki_editor_backspace(struct cwiki_editor *editor)
{
   struct cwiki_position cursor;

   if (editor == NULL || (editor->mode != CWIKI_EDITOR_INSERT &&
       editor->mode != CWIKI_EDITOR_REPLACE)) {
      return CWIKI_EDITOR_INVALID;
   }
   cursor = editor->motion.cursor;
   if (cursor.byte != 0U || cursor.line != 0U) {
      editor->reveal.active = false;
   }
   if (cursor.byte != 0U) {
      size_t previous = cwiki_grapheme_previous(
          editor->document->buffer.lines[cursor.line].bytes,
          editor->document->buffer.lines[cursor.line].length, cursor.byte);

      if (cwiki_undo_delete(&editor->undo, cursor.line, previous,
          cursor.byte - previous) != 0) {
         return error_status();
      }
   } else if (cursor.line != 0U) {
      if (cwiki_undo_join(&editor->undo, cursor.line - 1U) != 0) {
         return error_status();
      }
   } else {
      return CWIKI_EDITOR_NOTHING;
   }
   cwiki_document_mark_dirty(editor->document);
   reset_goals(editor);
   return CWIKI_EDITOR_OK;
}

static enum cwiki_editor_status
snippet_status(enum cwiki_snippet_status status)
{
   if (status == CWIKI_SNIPPET_OK) {
      return CWIKI_EDITOR_OK;
   }
   return status == CWIKI_SNIPPET_NO_MEMORY ? CWIKI_EDITOR_NO_MEMORY :
       CWIKI_EDITOR_INVALID;
}

enum cwiki_editor_status
cwiki_editor_snippet_expand(struct cwiki_editor *editor,
    struct cwiki_snippet_engine *engine,
    const struct cwiki_snippet_match *match, uint64_t timestamp)
{
   enum cwiki_snippet_status status;

   if (editor == NULL || engine == NULL || match == NULL ||
       editor->mode != CWIKI_EDITOR_INSERT ||
       !cwiki_undo_transaction_active(&editor->undo)) {
      return CWIKI_EDITOR_INVALID;
   }
   if (cwiki_undo_commit(&editor->undo) != 0) {
      return error_status();
   }
   status = cwiki_snippet_expand(engine, match, NULL, 0U, timestamp,
       &editor->motion.cursor);
   if (status == CWIKI_SNIPPET_OK) {
      editor->reveal.active = false;
      cwiki_document_mark_dirty(editor->document);
      reset_goals(editor);
   }
   if (cwiki_undo_begin(&editor->undo, timestamp) != 0) {
      return error_status();
   }
   return snippet_status(status);
}

enum cwiki_editor_status
cwiki_editor_snippet_edit(struct cwiki_editor *editor,
    struct cwiki_snippet_engine *engine, struct cwiki_position start,
    struct cwiki_position end, const char *bytes, size_t length)
{
   enum cwiki_snippet_status status;

   if (editor == NULL || engine == NULL || editor->mode != CWIKI_EDITOR_INSERT ||
       !cwiki_undo_transaction_active(&editor->undo)) {
      return CWIKI_EDITOR_INVALID;
   }
   status = cwiki_snippet_edit_pending(engine, start, end, bytes, length,
       &editor->motion.cursor);
   if (status == CWIKI_SNIPPET_OK &&
       (start.line != end.line || start.byte != end.byte || length != 0U)) {
      editor->reveal.active = false;
      cwiki_document_mark_dirty(editor->document);
      reset_goals(editor);
   }
   return snippet_status(status);
}

enum cwiki_editor_status
cwiki_editor_snippet_move(struct cwiki_editor *editor,
    struct cwiki_position cursor)
{
   if (editor == NULL || editor->mode != CWIKI_EDITOR_INSERT ||
       cursor.line >= editor->document->buffer.line_count ||
       cursor.byte > editor->document->buffer.lines[cursor.line].length) {
      return CWIKI_EDITOR_INVALID;
   }
   editor->motion.cursor = cursor;
   reset_goals(editor);
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_start_operator(struct cwiki_editor *editor,
    enum cwiki_editor_operator operator_kind, uint64_t timestamp)
{
   struct cwiki_motion_range range;

   if (!normal_editor(editor) || operator_kind < CWIKI_EDITOR_DELETE ||
       operator_kind > CWIKI_EDITOR_YANK) {
      return CWIKI_EDITOR_INVALID;
   }
   if (editor->pending_operator == CWIKI_EDITOR_NO_OPERATOR) {
      editor->pending_operator = operator_kind;
      editor->pending_timestamp = timestamp;
      return CWIKI_EDITOR_OK;
   }
   if (editor->pending_operator != operator_kind) {
      editor->pending_operator = operator_kind;
      editor->pending_timestamp = timestamp;
      return CWIKI_EDITOR_OK;
   }
   range.start = (struct cwiki_position){editor->motion.cursor.line, 0U};
   range.end = editor->motion.cursor.line + 1U <
       editor->document->buffer.line_count ?
       (struct cwiki_position){editor->motion.cursor.line + 1U, 0U} :
       (struct cwiki_position){editor->document->buffer.line_count, 0U};
   range.shape = CWIKI_MOTION_LINEWISE;
   return apply_operator_range(editor, &range, operator_kind,
       editor->pending_timestamp);
}

enum cwiki_editor_status
cwiki_editor_apply_motion(struct cwiki_editor *editor,
    const struct cwiki_layout_window *layout, struct cwiki_zone_engine *zones,
    enum cwiki_motion motion, const struct cwiki_motion_viewport *viewport)
{
   struct cwiki_motion_state original;
   struct cwiki_motion_result result;
   enum cwiki_editor_status status;

   if (!normal_editor(editor)) {
      return CWIKI_EDITOR_INVALID;
   }
   original = editor->motion;
   if (cwiki_motion_apply(&editor->document->buffer, layout, zones,
       &editor->motion, motion, viewport, &result) != 0) {
      return error_status();
   }
   if (editor->pending_operator == CWIKI_EDITOR_NO_OPERATOR) {
      editor->reveal_line = result.reveal_line;
      editor->reveal = result.reveal;
      return CWIKI_EDITOR_OK;
   }
   status = apply_operator_range(editor, &result.range,
       editor->pending_operator, editor->pending_timestamp);
   if (status != CWIKI_EDITOR_OK && status != CWIKI_EDITOR_NOTHING) {
      editor->motion = original;
   }
   return status;
}

static int
insert_characterwise(struct cwiki_editor *editor, size_t line, size_t byte,
    const char *bytes, size_t length, struct cwiki_position *end)
{
   size_t offset = 0U;

   editor->motion.cursor = (struct cwiki_position){line, byte};
   while (offset < length) {
      const char *newline = memchr(bytes + offset, '\n', length - offset);
      size_t segment = newline == NULL ? length - offset :
          (size_t)(newline - (bytes + offset));

      if (segment != 0U && cwiki_undo_insert(&editor->undo,
          editor->motion.cursor.line, editor->motion.cursor.byte,
          bytes + offset, segment) != 0) {
         return -1;
      }
      offset += segment;
      if (offset < length) {
         if (cwiki_undo_split(&editor->undo, editor->motion.cursor.line,
             editor->motion.cursor.byte) != 0) {
            return -1;
         }
         offset++;
      }
   }
   *end = editor->motion.cursor;
   return 0;
}

static int
insert_linewise(struct cwiki_editor *editor, size_t before_line,
    const char *bytes, size_t length)
{
   struct cwiki_buffer *buffer = &editor->document->buffer;
   size_t line;
   size_t offset = 0U;

   if (before_line == buffer->line_count) {
      line = buffer->line_count - 1U;
      if (cwiki_undo_split(&editor->undo, line,
          buffer->lines[line].length) != 0) {
         return -1;
      }
      line++;
   } else {
      line = before_line;
      if (cwiki_undo_split(&editor->undo, line, 0U) != 0) {
         return -1;
      }
   }
   editor->motion.cursor = (struct cwiki_position){line, 0U};
   while (offset < length) {
      const char *newline = memchr(bytes + offset, '\n', length - offset);
      size_t segment;

      if (newline == NULL) {
         errno = EINVAL;
         return -1;
      }
      segment = (size_t)(newline - (bytes + offset));
      if (segment != 0U && cwiki_undo_insert(&editor->undo, line, 0U,
          bytes + offset, segment) != 0) {
         return -1;
      }
      offset += segment + 1U;
      if (offset < length) {
         if (cwiki_undo_split(&editor->undo, line,
             buffer->lines[line].length) != 0) {
            return -1;
         }
         line++;
      }
   }
   return 0;
}

enum cwiki_editor_status
cwiki_editor_put(struct cwiki_editor *editor, bool before, uint64_t timestamp)
{
   struct cwiki_position original;
   struct cwiki_position end;
   int inserted;

   if (!normal_editor(editor)) {
      return CWIKI_EDITOR_INVALID;
   }
   if (editor->yank.length == 0U) {
      return CWIKI_EDITOR_NOTHING;
   }
   original = editor->motion.cursor;
   if (cwiki_undo_begin(&editor->undo, timestamp) != 0) {
      return error_status();
   }
   editor->reveal.active = false;
   if (editor->yank.linewise) {
      size_t target = before ? original.line : original.line + 1U;

      inserted = insert_linewise(editor, target, editor->yank.bytes,
          editor->yank.length);
   } else {
      size_t byte = original.byte;
      const struct cwiki_line *line =
          &editor->document->buffer.lines[original.line];

      if (!before && byte < line->length) {
         byte = cwiki_grapheme_next(line->bytes, line->length, byte);
      }
      inserted = insert_characterwise(editor, original.line, byte,
          editor->yank.bytes, editor->yank.length, &end);
      if (inserted == 0) {
         if (end.byte != 0U) {
            editor->motion.cursor.byte = cwiki_grapheme_previous(
                editor->document->buffer.lines[end.line].bytes,
                editor->document->buffer.lines[end.line].length, end.byte);
         } else {
            editor->motion.cursor = end;
         }
      }
   }
   if (inserted != 0) {
      (void)cwiki_undo_cancel(&editor->undo);
      editor->motion.cursor = original;
      return error_status();
   }
   if (cwiki_undo_commit(&editor->undo) != 0) {
      return error_status();
   }
   cwiki_document_mark_dirty(editor->document);
   reset_goals(editor);
   return CWIKI_EDITOR_OK;
}

static enum cwiki_editor_status
history_result(struct cwiki_editor *editor, int result)
{
   uint64_t sequence;

   editor->reveal.active = false;
   if (result != 0) {
      return errno == ENOENT ? CWIKI_EDITOR_NOTHING : error_status();
   }
   if (editor->savepoint_valid && current_sequence(&editor->undo, &sequence) &&
       sequence == editor->saved_sequence) {
      editor->document->dirty = false;
   } else {
      cwiki_document_mark_dirty(editor->document);
   }
   editor->pending_operator = CWIKI_EDITOR_NO_OPERATOR;
   reset_goals(editor);
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_undo(struct cwiki_editor *editor)
{
   return normal_editor(editor) ?
       history_result(editor, cwiki_undo_to_parent(&editor->undo)) :
       CWIKI_EDITOR_INVALID;
}

enum cwiki_editor_status
cwiki_editor_redo(struct cwiki_editor *editor)
{
   return normal_editor(editor) ?
       history_result(editor, cwiki_undo_redo_child(&editor->undo, 0U)) :
       CWIKI_EDITOR_INVALID;
}

enum cwiki_editor_status
cwiki_editor_older(struct cwiki_editor *editor)
{
   return normal_editor(editor) ?
       history_result(editor, cwiki_undo_older(&editor->undo)) :
       CWIKI_EDITOR_INVALID;
}

enum cwiki_editor_status
cwiki_editor_newer(struct cwiki_editor *editor)
{
   return normal_editor(editor) ?
       history_result(editor, cwiki_undo_newer(&editor->undo)) :
       CWIKI_EDITOR_INVALID;
}

enum cwiki_editor_status
cwiki_editor_begin_command(struct cwiki_editor *editor)
{
   if (!normal_editor(editor)) {
      return CWIKI_EDITOR_INVALID;
   }
   editor->pending_operator = CWIKI_EDITOR_NO_OPERATOR;
   editor->command_length = 0U;
   editor->mode = CWIKI_EDITOR_COMMAND;
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_command_insert(struct cwiki_editor *editor, const char *bytes,
    size_t length)
{
   if (editor == NULL || editor->mode != CWIKI_EDITOR_COMMAND ||
       (bytes == NULL && length != 0U) ||
       (length != 0U && memchr(bytes, '\n', length) != NULL) ||
       !cwiki_utf8_validate(bytes, length)) {
      return CWIKI_EDITOR_INVALID;
   }
   if (append_bytes(&editor->command, &editor->command_length,
       &editor->command_capacity, bytes, length) != 0) {
      return error_status();
   }
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_command_backspace(struct cwiki_editor *editor)
{
   if (editor == NULL || editor->mode != CWIKI_EDITOR_COMMAND) {
      return CWIKI_EDITOR_INVALID;
   }
   if (editor->command_length == 0U) {
      return CWIKI_EDITOR_NOTHING;
   }
   editor->command_length = cwiki_grapheme_previous(editor->command,
       editor->command_length, editor->command_length);
   return CWIKI_EDITOR_OK;
}

static bool
command_is(const struct cwiki_editor *editor, const char *command)
{
   size_t length = strlen(command);

   return editor->command_length == length &&
       memcmp(editor->command, command, length) == 0;
}

enum cwiki_editor_status
cwiki_editor_execute_command(struct cwiki_editor *editor)
{
   bool save;
   bool quit;

   if (editor == NULL || editor->mode != CWIKI_EDITOR_COMMAND) {
      return CWIKI_EDITOR_INVALID;
   }
   save = command_is(editor, "w") || command_is(editor, "wq");
   quit = command_is(editor, "q") || command_is(editor, "wq");
   if (!save && !quit) {
      return CWIKI_EDITOR_INVALID;
   }
   if (save && cwiki_document_save(editor->document).status !=
       CWIKI_DURABLE_WRITE_SUCCESS) {
      return CWIKI_EDITOR_SAVE_FAILED;
   }
   if (save) {
      uint64_t sequence;

      if (!current_sequence(&editor->undo, &sequence)) {
         return CWIKI_EDITOR_INVALID;
      }
      editor->saved_sequence = sequence;
      editor->savepoint_valid = true;
   }
   if (quit && editor->document->dirty) {
      return CWIKI_EDITOR_DIRTY;
   }
   editor->quit_requested = quit;
   editor->command_length = 0U;
   editor->mode = CWIKI_EDITOR_NORMAL;
   return CWIKI_EDITOR_OK;
}
