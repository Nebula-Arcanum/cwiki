#include "editor_input.h"

#include "snippet.h"
#include "snippet_catalog.h"
#include "unicode.h"

#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <utf8proc.h>

enum editor_action_id {
   ACTION_INSERT_BEFORE,
   ACTION_INSERT_AFTER,
   ACTION_REPLACE,
   ACTION_COMMAND,
   ACTION_DELETE,
   ACTION_CHANGE,
   ACTION_YANK,
   ACTION_PUT_AFTER,
   ACTION_PUT_BEFORE,
   ACTION_UNDO,
   ACTION_REDO,
   ACTION_OLDER,
   ACTION_NEWER,
   ACTION_MOTION_FIRST,
   ACTION_MOTION_LAST = ACTION_MOTION_FIRST +
       (int)CWIKI_MOTION_SENTENCE_FORWARD,
   ACTION_COUNT
};

struct action_definition {
   enum editor_action_id id;
   const char *name;
   const char *label;
   const char *description;
};

struct action_context {
   struct cwiki_editor_input *input;
   enum editor_action_id id;
};

struct cwiki_editor_input {
   struct cwiki_editor *editor;
   struct cwiki_action_registry *actions;
   struct cwiki_keymap *keymap;
   struct cwiki_snippet_registry *snippet_registry;
   struct cwiki_snippet_engine *snippets;
   struct action_context action_contexts[ACTION_COUNT];
   struct cwiki_input_event pending[CWIKI_KEYMAP_MAX_SEQUENCE];
   size_t pending_count;
   struct cwiki_editor_input_context context;
   bool stop_selected;
};

#define MOTION_ACTION(motion, name, label) \
   { ACTION_MOTION_FIRST + (int)(motion), name, label, \
       "Move the editing cursor" }

static const struct action_definition action_definitions[] = {
   {ACTION_INSERT_BEFORE, "mode.insert-before", "Insert before",
       "Enter Insert mode before the cursor"},
   {ACTION_INSERT_AFTER, "mode.insert-after", "Insert after",
       "Enter Insert mode after the cursor"},
   {ACTION_REPLACE, "mode.replace", "Replace",
       "Enter Replace mode"},
   {ACTION_COMMAND, "mode.command", "Command line",
       "Enter Command-line mode"},
   {ACTION_DELETE, "operator.delete", "Delete", "Start the delete operator"},
   {ACTION_CHANGE, "operator.change", "Change", "Start the change operator"},
   {ACTION_YANK, "operator.yank", "Yank", "Start the yank operator"},
   {ACTION_PUT_AFTER, "edit.put-after", "Put after",
       "Put the unnamed yank after the cursor"},
   {ACTION_PUT_BEFORE, "edit.put-before", "Put before",
       "Put the unnamed yank before the cursor"},
   {ACTION_UNDO, "history.undo", "Undo", "Move to the parent undo state"},
   {ACTION_REDO, "history.redo", "Redo", "Redo the first retained branch"},
   {ACTION_OLDER, "history.older", "Older state",
       "Move to the previous chronological undo state"},
   {ACTION_NEWER, "history.newer", "Newer state",
       "Move to the next chronological undo state"},
   MOTION_ACTION(CWIKI_MOTION_LEFT, "motion.left", "Left"),
   MOTION_ACTION(CWIKI_MOTION_RIGHT, "motion.right", "Right"),
   MOTION_ACTION(CWIKI_MOTION_WORD_FORWARD, "motion.word-forward", "Word forward"),
   MOTION_ACTION(CWIKI_MOTION_WORD_FORWARD_BIG, "motion.big-word-forward", "WORD forward"),
   MOTION_ACTION(CWIKI_MOTION_WORD_BACKWARD, "motion.word-backward", "Word backward"),
   MOTION_ACTION(CWIKI_MOTION_WORD_BACKWARD_BIG, "motion.big-word-backward", "WORD backward"),
   MOTION_ACTION(CWIKI_MOTION_WORD_END, "motion.word-end", "Word end"),
   MOTION_ACTION(CWIKI_MOTION_WORD_END_BIG, "motion.big-word-end", "WORD end"),
   MOTION_ACTION(CWIKI_MOTION_DISPLAY_DOWN, "motion.display-down", "Display row down"),
   MOTION_ACTION(CWIKI_MOTION_DISPLAY_UP, "motion.display-up", "Display row up"),
   MOTION_ACTION(CWIKI_MOTION_SOURCE_DOWN, "motion.source-down", "Source line down"),
   MOTION_ACTION(CWIKI_MOTION_SOURCE_UP, "motion.source-up", "Source line up"),
   MOTION_ACTION(CWIKI_MOTION_DOCUMENT_FIRST, "motion.document-first", "First line"),
   MOTION_ACTION(CWIKI_MOTION_DOCUMENT_LAST, "motion.document-last", "Last line"),
   MOTION_ACTION(CWIKI_MOTION_VIEWPORT_HIGH, "motion.viewport-high", "Window top"),
   MOTION_ACTION(CWIKI_MOTION_VIEWPORT_MIDDLE, "motion.viewport-middle", "Window middle"),
   MOTION_ACTION(CWIKI_MOTION_VIEWPORT_LOW, "motion.viewport-low", "Window bottom"),
   MOTION_ACTION(CWIKI_MOTION_LINE_START, "motion.line-start", "Line start"),
   MOTION_ACTION(CWIKI_MOTION_FIRST_NONBLANK, "motion.first-nonblank", "First nonblank"),
   MOTION_ACTION(CWIKI_MOTION_LINE_END, "motion.line-end", "Line end"),
   MOTION_ACTION(CWIKI_MOTION_PREVIOUS_LINE, "motion.previous-line", "Previous line"),
   MOTION_ACTION(CWIKI_MOTION_NEXT_LINE, "motion.next-line", "Next line"),
   MOTION_ACTION(CWIKI_MOTION_LINE_FIRST_NONBLANK, "motion.line-first-nonblank", "Line nonblank"),
   MOTION_ACTION(CWIKI_MOTION_PARAGRAPH_BACKWARD, "motion.paragraph-backward", "Paragraph backward"),
   MOTION_ACTION(CWIKI_MOTION_PARAGRAPH_FORWARD, "motion.paragraph-forward", "Paragraph forward"),
   MOTION_ACTION(CWIKI_MOTION_SENTENCE_BACKWARD, "motion.sentence-backward", "Sentence backward"),
   MOTION_ACTION(CWIKI_MOTION_SENTENCE_FORWARD, "motion.sentence-forward", "Sentence forward")
};

#undef MOTION_ACTION

struct binding_definition {
   uint32_t first;
   unsigned int first_modifiers;
   uint32_t second;
   unsigned int second_modifiers;
   size_t length;
   const char *action;
};

#define BIND1(key, modifiers, action) {key, modifiers, 0U, 0U, 1U, action}
#define BIND2(first, second, action) {first, 0U, second, 0U, 2U, action}

static const struct binding_definition default_bindings[] = {
   BIND1('i', 0U, "mode.insert-before"),
   BIND1('a', 0U, "mode.insert-after"),
   BIND1('r', CWIKI_INPUT_SHIFT, "mode.replace"),
   BIND1(';', CWIKI_INPUT_SHIFT, "mode.command"),
   BIND1('d', 0U, "operator.delete"),
   BIND1('c', 0U, "operator.change"),
   BIND1('y', 0U, "operator.yank"),
   BIND1('p', 0U, "edit.put-after"),
   BIND1('p', CWIKI_INPUT_SHIFT, "edit.put-before"),
   BIND1('u', 0U, "history.undo"),
   BIND1('r', CWIKI_INPUT_CTRL, "history.redo"),
   BIND2('g', '-', "history.older"),
   {'g', 0U, '=', CWIKI_INPUT_SHIFT, 2U, "history.newer"},
   BIND1('h', 0U, "motion.left"),
   BIND1('l', 0U, "motion.right"),
   BIND1('w', 0U, "motion.word-forward"),
   BIND1('w', CWIKI_INPUT_SHIFT, "motion.big-word-forward"),
   BIND1('b', 0U, "motion.word-backward"),
   BIND1('b', CWIKI_INPUT_SHIFT, "motion.big-word-backward"),
   BIND1('e', 0U, "motion.word-end"),
   BIND1('e', CWIKI_INPUT_SHIFT, "motion.big-word-end"),
   BIND1('j', 0U, "motion.display-down"),
   BIND1('k', 0U, "motion.display-up"),
   BIND2('g', 'j', "motion.source-down"),
   BIND2('g', 'k', "motion.source-up"),
   BIND2('g', 'g', "motion.document-first"),
   BIND1('g', CWIKI_INPUT_SHIFT, "motion.document-last"),
   BIND1('h', CWIKI_INPUT_SHIFT, "motion.viewport-high"),
   BIND1('m', CWIKI_INPUT_SHIFT, "motion.viewport-middle"),
   BIND1('l', CWIKI_INPUT_SHIFT, "motion.viewport-low"),
   BIND1('0', 0U, "motion.line-start"),
   BIND1('6', CWIKI_INPUT_SHIFT, "motion.first-nonblank"),
   BIND1('4', CWIKI_INPUT_SHIFT, "motion.line-end"),
   BIND1('-', 0U, "motion.previous-line"),
   BIND1('=', CWIKI_INPUT_SHIFT, "motion.next-line"),
   BIND1('-', CWIKI_INPUT_SHIFT, "motion.line-first-nonblank"),
   BIND1('[', CWIKI_INPUT_SHIFT, "motion.paragraph-backward"),
   BIND1(']', CWIKI_INPUT_SHIFT, "motion.paragraph-forward"),
   BIND1('9', CWIKI_INPUT_SHIFT, "motion.sentence-backward"),
   BIND1('0', CWIKI_INPUT_SHIFT, "motion.sentence-forward")
};

#undef BIND1
#undef BIND2

static bool
action_available(void *context)
{
   const struct action_context *action = context;

   return action->input->editor->mode == CWIKI_EDITOR_NORMAL;
}

static int
handle_action(void *context, const struct cwiki_action_argument *argument)
{
   struct action_context *action = context;
   struct cwiki_editor_input *input = action->input;
   struct cwiki_editor *editor = input->editor;
   enum cwiki_editor_status status;

   (void)argument;
   if (action->id >= ACTION_MOTION_FIRST && action->id <= ACTION_MOTION_LAST) {
      enum cwiki_motion motion =
          (enum cwiki_motion)(action->id - ACTION_MOTION_FIRST);

      status = cwiki_editor_apply_motion(editor, input->context.layout,
          input->context.zones, motion, input->context.viewport);
      if (status == CWIKI_EDITOR_OK) {
         cwiki_snippet_cursor_moved(input->snippets,
             editor->motion.cursor);
      }
      return (int)status;
   }
   switch (action->id) {
   case ACTION_INSERT_BEFORE:
      status = cwiki_editor_enter_insert(editor, false,
          input->context.timestamp);
      break;
   case ACTION_INSERT_AFTER:
      status = cwiki_editor_enter_insert(editor, true,
          input->context.timestamp);
      break;
   case ACTION_REPLACE:
      status = cwiki_editor_enter_replace(editor, input->context.timestamp);
      break;
   case ACTION_COMMAND:
      status = cwiki_editor_begin_command(editor);
      break;
   case ACTION_DELETE:
   case ACTION_CHANGE:
   case ACTION_YANK:
      status = cwiki_editor_start_operator(editor,
          action->id == ACTION_DELETE ? CWIKI_EDITOR_DELETE :
          action->id == ACTION_CHANGE ? CWIKI_EDITOR_CHANGE :
          CWIKI_EDITOR_YANK, input->context.timestamp);
      break;
   case ACTION_PUT_AFTER:
   case ACTION_PUT_BEFORE:
      status = cwiki_editor_put(editor, action->id == ACTION_PUT_BEFORE,
          input->context.timestamp);
      break;
   case ACTION_UNDO:
      status = cwiki_editor_undo(editor);
      break;
   case ACTION_REDO:
      status = cwiki_editor_redo(editor);
      break;
   case ACTION_OLDER:
      status = cwiki_editor_older(editor);
      break;
   case ACTION_NEWER:
      status = cwiki_editor_newer(editor);
      break;
   default:
      status = CWIKI_EDITOR_INVALID;
      break;
   }
   if (status == CWIKI_EDITOR_OK) {
      if (action->id == ACTION_UNDO || action->id == ACTION_REDO ||
          action->id == ACTION_OLDER || action->id == ACTION_NEWER) {
         cwiki_snippet_clear_sessions(input->snippets);
         input->stop_selected = false;
      } else if (action->id == ACTION_DELETE || action->id == ACTION_CHANGE ||
          action->id == ACTION_PUT_AFTER || action->id == ACTION_PUT_BEFORE) {
         cwiki_snippet_clear_sessions(input->snippets);
         input->stop_selected = false;
      } else if (action->id == ACTION_YANK) {
         cwiki_snippet_cursor_moved(input->snippets,
             editor->motion.cursor);
      }
   }
   return (int)status;
}

static enum cwiki_editor_status
register_actions(struct cwiki_editor_input *input)
{
   size_t index;

   for (index = 0U; index < sizeof(action_definitions) /
       sizeof(action_definitions[0]); index++) {
      const struct action_definition *definition = &action_definitions[index];
      struct cwiki_action_spec spec;

      input->action_contexts[definition->id].input = input;
      input->action_contexts[definition->id].id = definition->id;
      spec.name = definition->name;
      spec.label = definition->label;
      spec.description = definition->description;
      spec.parameter_type = CWIKI_ACTION_PARAMETER_NONE;
      spec.parameter_name = NULL;
      spec.handler = handle_action;
      spec.available = action_available;
      spec.context = &input->action_contexts[definition->id];
      if (cwiki_action_register(input->actions, &spec) != CWIKI_ACTION_OK) {
         return CWIKI_EDITOR_NO_MEMORY;
      }
   }
   return CWIKI_EDITOR_OK;
}

static enum cwiki_editor_status
bind_defaults(struct cwiki_editor_input *input)
{
   size_t index;

   for (index = 0U; index < sizeof(default_bindings) /
       sizeof(default_bindings[0]); index++) {
      const struct binding_definition *binding = &default_bindings[index];
      struct cwiki_key_sequence sequence = {0};

      sequence.keys[0].key = binding->first;
      sequence.keys[0].modifiers = binding->first_modifiers;
      if (binding->length == 2U) {
         sequence.keys[1].key = binding->second;
         sequence.keys[1].modifiers = binding->second_modifiers;
      }
      sequence.length = binding->length;
      if (cwiki_keymap_bind(input->keymap, CWIKI_KEYMAP_NORMAL, &sequence,
          binding->action) != CWIKI_KEYMAP_OK) {
         return CWIKI_EDITOR_NO_MEMORY;
      }
   }
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_input_init(struct cwiki_editor_input **input,
    struct cwiki_editor *editor)
{
   struct cwiki_editor_input *created;
   enum cwiki_editor_status status;

   if (input == NULL || editor == NULL || editor->document == NULL) {
      return CWIKI_EDITOR_INVALID;
   }
   *input = NULL;
   created = calloc(1U, sizeof(*created));
   if (created == NULL) {
      return CWIKI_EDITOR_NO_MEMORY;
   }
   created->editor = editor;
   if (cwiki_action_registry_init(&created->actions) != CWIKI_ACTION_OK) {
      free(created);
      return CWIKI_EDITOR_NO_MEMORY;
   }
   if (cwiki_snippet_registry_init(&created->snippet_registry) !=
       CWIKI_SNIPPET_OK ||
       cwiki_snippet_catalog_install(created->snippet_registry, NULL, 0U) !=
       CWIKI_SNIPPET_OK ||
       cwiki_snippet_engine_init(&created->snippets,
       &editor->document->buffer, &editor->undo) != CWIKI_SNIPPET_OK) {
      cwiki_editor_input_free(created);
      return CWIKI_EDITOR_NO_MEMORY;
   }
   status = register_actions(created);
   if (status != CWIKI_EDITOR_OK ||
       cwiki_keymap_init(&created->keymap, created->actions) !=
       CWIKI_KEYMAP_OK) {
      cwiki_editor_input_free(created);
      return status == CWIKI_EDITOR_OK ? CWIKI_EDITOR_NO_MEMORY : status;
   }
   status = bind_defaults(created);
   if (status != CWIKI_EDITOR_OK) {
      cwiki_editor_input_free(created);
      return status;
   }
   *input = created;
   return CWIKI_EDITOR_OK;
}

void
cwiki_editor_input_free(struct cwiki_editor_input *input)
{
   if (input == NULL) {
      return;
   }
   cwiki_snippet_engine_free(input->snippets);
   cwiki_snippet_registry_free(input->snippet_registry);
   cwiki_keymap_free(input->keymap);
   cwiki_action_registry_free(input->actions);
   free(input);
}

enum cwiki_editor_status
cwiki_editor_input_replace_keymap(struct cwiki_editor_input *input,
    struct cwiki_keymap *candidate)
{
   if (input == NULL || candidate == NULL) {
      return CWIKI_EDITOR_INVALID;
   }
   cwiki_keymap_free(input->keymap);
   input->keymap = candidate;
   input->pending_count = 0U;
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_input_replace_snippets(struct cwiki_editor_input *input,
    struct cwiki_snippet_registry *candidate)
{
   if (input == NULL || candidate == NULL ||
       cwiki_snippet_session_depth(input->snippets) != 0U) {
      return CWIKI_EDITOR_INVALID;
   }
   cwiki_snippet_registry_free(input->snippet_registry);
   input->snippet_registry = candidate;
   return CWIKI_EDITOR_OK;
}

enum cwiki_editor_status
cwiki_editor_input_set_snippet_subjects(struct cwiki_editor_input *input,
    const struct cwiki_snippet_subject *subjects, size_t subject_count)
{
   enum cwiki_snippet_status status;

   if (input == NULL || cwiki_snippet_session_depth(input->snippets) != 0U) {
      return CWIKI_EDITOR_INVALID;
   }
   status = cwiki_snippet_registry_set_subjects(input->snippet_registry,
       subjects, subject_count);
   return status == CWIKI_SNIPPET_OK ? CWIKI_EDITOR_OK :
       (status == CWIKI_SNIPPET_NO_MEMORY ? CWIKI_EDITOR_NO_MEMORY :
       CWIKI_EDITOR_INVALID);
}

static enum cwiki_editor_status
dispatch_match(struct cwiki_editor_input *input,
    const struct cwiki_keymap_match *match)
{
   int result = (int)CWIKI_EDITOR_INVALID;
   enum cwiki_action_status status;

   status = cwiki_action_dispatch(input->actions, match->action.name, NULL,
       &result);
   if (status == CWIKI_ACTION_NO_MEMORY) {
      return CWIKI_EDITOR_NO_MEMORY;
   }
   if (status != CWIKI_ACTION_OK || result < (int)CWIKI_EDITOR_OK ||
       result > (int)CWIKI_EDITOR_SAVE_FAILED) {
      return CWIKI_EDITOR_INVALID;
   }
   return (enum cwiki_editor_status)result;
}

static enum cwiki_editor_status
handle_normal(struct cwiki_editor_input *input,
    const struct cwiki_input_event *event)
{
   struct cwiki_keymap_match match;

   if (event->kind != CWIKI_INPUT_KEY ||
       event->action == CWIKI_INPUT_RELEASE) {
      return CWIKI_EDITOR_NOTHING;
   }
   if (event->key == 27U && event->modifiers == 0U) {
      input->pending_count = 0U;
      return cwiki_editor_escape(input->editor);
   }
   if (input->pending_count == CWIKI_KEYMAP_MAX_SEQUENCE) {
      input->pending_count = 0U;
      return CWIKI_EDITOR_INVALID;
   }
   input->pending[input->pending_count] = *event;
   input->pending[input->pending_count].text = NULL;
   input->pending[input->pending_count].text_len = 0U;
   input->pending_count++;
   if (cwiki_keymap_match(input->keymap, CWIKI_KEYMAP_NORMAL, input->pending,
       input->pending_count, &match) != CWIKI_KEYMAP_OK) {
      input->pending_count = 0U;
      return CWIKI_EDITOR_INVALID;
   }
   if (match.kind == CWIKI_KEYMAP_PREFIX) {
      return CWIKI_EDITOR_OK;
   }
   input->pending_count = 0U;
   if (match.kind == CWIKI_KEYMAP_NO_MATCH) {
      return CWIKI_EDITOR_NOTHING;
   }
   return dispatch_match(input, &match);
}

static enum cwiki_editor_status
recompute_zones(struct cwiki_editor_input *input)
{
   struct cwiki_buffer *buffer = &input->editor->document->buffer;
   size_t line;

   if (input->context.zones == NULL) {
      return CWIKI_EDITOR_NOTHING;
   }
   for (line = 0U; line < buffer->line_count; line++) {
      if (buffer->lines[line].zone_dirty &&
          cwiki_zone_recompute(input->context.zones, buffer, line, NULL) != 0) {
         return errno == ENOMEM ? CWIKI_EDITOR_NO_MEMORY :
             CWIKI_EDITOR_INVALID;
      }
   }
   return CWIKI_EDITOR_OK;
}

static enum cwiki_editor_status
try_expand(struct cwiki_editor_input *input,
    enum cwiki_snippet_expand_kind kind)
{
   struct cwiki_snippet_match match = {0};
   enum cwiki_snippet_status matched;
   enum cwiki_editor_status status;

   if (cwiki_snippet_session_depth(input->snippets) >=
       CWIKI_SNIPPET_SESSION_MAX_DEPTH) {
      return CWIKI_EDITOR_NOTHING;
   }
   status = recompute_zones(input);
   if (status != CWIKI_EDITOR_OK) {
      return status;
   }
   matched = cwiki_snippet_match(input->snippet_registry,
       input->context.zones, &input->editor->document->buffer,
       input->editor->motion.cursor, kind, CWIKI_SNIPPET_INPUT_NONE, &match);
   if (matched == CWIKI_SNIPPET_NO_MATCH ||
       matched == CWIKI_SNIPPET_LIMIT_DISABLED) {
      return CWIKI_EDITOR_NOTHING;
   }
   if (matched != CWIKI_SNIPPET_OK) {
      return matched == CWIKI_SNIPPET_NO_MEMORY ? CWIKI_EDITOR_NO_MEMORY :
          CWIKI_EDITOR_INVALID;
   }
   status = cwiki_editor_snippet_expand(input->editor, input->snippets,
       &match, input->context.timestamp);
   cwiki_snippet_match_free(&match);
   if (status == CWIKI_EDITOR_OK) {
      input->stop_selected = true;
   }
   return status;
}

static enum cwiki_editor_status
snippet_insert(struct cwiki_editor_input *input, const char *bytes,
    size_t length, bool auto_expand)
{
   struct cwiki_editor *editor = input->editor;
   enum cwiki_editor_status status;

   if (editor->mode != CWIKI_EDITOR_INSERT ||
       cwiki_snippet_session_depth(input->snippets) == 0U) {
      status = cwiki_editor_insert(editor, bytes, length);
   } else {
      struct cwiki_position start = editor->motion.cursor;
      struct cwiki_position end = start;

      if (input->stop_selected &&
          cwiki_snippet_current_stop(input->snippets, &start, &end) != 0) {
         return CWIKI_EDITOR_INVALID;
      }
      status = cwiki_editor_snippet_edit(editor, input->snippets, start, end,
          bytes, length);
      input->stop_selected = false;
   }
   if (status != CWIKI_EDITOR_OK || !auto_expand ||
       editor->mode != CWIKI_EDITOR_INSERT) {
      return status;
   }
   status = try_expand(input, CWIKI_SNIPPET_AUTO);
   return status == CWIKI_EDITOR_NOTHING ? CWIKI_EDITOR_OK : status;
}

static enum cwiki_editor_status
insert_key_text(struct cwiki_editor_input *input,
    const struct cwiki_input_event *event)
{
   struct cwiki_editor *editor = input->editor;
   utf8proc_uint8_t encoded[4];
   utf8proc_ssize_t length;

   if (event->text_len != 0U) {
      return editor->mode == CWIKI_EDITOR_COMMAND ?
          cwiki_editor_command_insert(editor, (const char *)event->text,
          event->text_len) :
          snippet_insert(input, (const char *)event->text, event->text_len,
          true);
   }
   if ((event->modifiers & ~(unsigned int)CWIKI_INPUT_SHIFT) != 0U ||
       event->key > UINT32_C(0x10ffff) ||
       (event->key >= UINT32_C(0xd800) && event->key <= UINT32_C(0xdfff))) {
      return CWIKI_EDITOR_NOTHING;
   }
   length = utf8proc_encode_char((utf8proc_int32_t)event->key, encoded);
   if (length <= 0) {
      return CWIKI_EDITOR_INVALID;
   }
   return editor->mode == CWIKI_EDITOR_COMMAND ?
       cwiki_editor_command_insert(editor, (const char *)encoded,
       (size_t)length) :
       snippet_insert(input, (const char *)encoded, (size_t)length, true);
}

static enum cwiki_editor_status
snippet_backspace(struct cwiki_editor_input *input)
{
   struct cwiki_editor *editor = input->editor;
   struct cwiki_position start = editor->motion.cursor;
   struct cwiki_position end = start;

   if (editor->mode != CWIKI_EDITOR_INSERT ||
       cwiki_snippet_session_depth(input->snippets) == 0U) {
      return cwiki_editor_backspace(editor);
   }
   if (input->stop_selected) {
      if (cwiki_snippet_current_stop(input->snippets, &start, &end) != 0) {
         return CWIKI_EDITOR_INVALID;
      }
   } else if (start.byte != 0U) {
      start.byte = cwiki_grapheme_previous(
          editor->document->buffer.lines[start.line].bytes,
          editor->document->buffer.lines[start.line].length, start.byte);
   } else if (start.line != 0U) {
      start.line--;
      start.byte = editor->document->buffer.lines[start.line].length;
   } else {
      return CWIKI_EDITOR_NOTHING;
   }
   input->stop_selected = false;
   return cwiki_editor_snippet_edit(editor, input->snippets, start, end, NULL,
       0U);
}

static enum cwiki_editor_status
handle_tab(struct cwiki_editor_input *input, bool previous)
{
   struct cwiki_position cursor;
   int moved;

   if (cwiki_snippet_session_depth(input->snippets) == 0U) {
      if (previous) {
         return CWIKI_EDITOR_NOTHING;
      }
      return try_expand(input, CWIKI_SNIPPET_EXPLICIT);
   }
   cursor = input->editor->motion.cursor;
   moved = previous ? cwiki_snippet_previous_stop(input->snippets, &cursor) :
       cwiki_snippet_next_stop(input->snippets, &cursor);
   if (moved < 0) {
      return CWIKI_EDITOR_INVALID;
   }
   input->stop_selected = moved == 0;
   return moved == 0 ? cwiki_editor_snippet_move(input->editor, cursor) :
       CWIKI_EDITOR_OK;
}

static enum cwiki_editor_status
handle_editing(struct cwiki_editor_input *input,
    const struct cwiki_input_event *event)
{
   struct cwiki_editor *editor = input->editor;

   if (event->kind == CWIKI_INPUT_PASTE) {
      return editor->mode == CWIKI_EDITOR_COMMAND ?
          cwiki_editor_command_insert(editor, (const char *)event->text,
          event->text_len) :
          snippet_insert(input, (const char *)event->text, event->text_len,
          false);
   }
   if (event->action == CWIKI_INPUT_RELEASE) {
      return CWIKI_EDITOR_NOTHING;
   }
   if (event->key == 27U && event->modifiers == 0U) {
      return cwiki_editor_escape(editor);
   }
   if (editor->mode == CWIKI_EDITOR_INSERT && event->key == 9U &&
       (event->modifiers == 0U || event->modifiers == CWIKI_INPUT_SHIFT)) {
      return handle_tab(input, event->modifiers == CWIKI_INPUT_SHIFT);
   }
   if ((event->key == 13U || event->key == 10U) &&
       event->modifiers == 0U) {
      return editor->mode == CWIKI_EDITOR_COMMAND ?
          cwiki_editor_execute_command(editor) :
          (editor->mode == CWIKI_EDITOR_INSERT ?
          snippet_insert(input, "\n", 1U, true) :
          cwiki_editor_enter(editor));
   }
   if ((event->key == 127U || event->key == 8U) &&
       event->modifiers == 0U) {
      return editor->mode == CWIKI_EDITOR_COMMAND ?
          cwiki_editor_command_backspace(editor) :
          snippet_backspace(input);
   }
   return insert_key_text(input, event);
}

enum cwiki_editor_status
cwiki_editor_input_handle(struct cwiki_editor_input *input,
    const struct cwiki_input_event *event,
    const struct cwiki_editor_input_context *context)
{
   if (input == NULL || event == NULL || context == NULL) {
      return CWIKI_EDITOR_INVALID;
   }
   input->context = *context;
   if (input->editor->mode == CWIKI_EDITOR_NORMAL) {
      return handle_normal(input, event);
   }
   input->pending_count = 0U;
   return handle_editing(input, event);
}

struct cwiki_action_registry *
cwiki_editor_input_actions(struct cwiki_editor_input *input)
{
   return input == NULL ? NULL : input->actions;
}

struct cwiki_keymap *
cwiki_editor_input_keymap(struct cwiki_editor_input *input)
{
   return input == NULL ? NULL : input->keymap;
}

const struct cwiki_input_event *
cwiki_editor_input_pending(const struct cwiki_editor_input *input,
    size_t *count)
{
   if (count == NULL) {
      return NULL;
   }
   *count = input == NULL ? 0U : input->pending_count;
   return input == NULL || input->pending_count == 0U ? NULL : input->pending;
}
