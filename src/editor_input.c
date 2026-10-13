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
   ACTION_INSERT_FIRST_NONBLANK,
   ACTION_APPEND_END,
   ACTION_OPEN_BELOW,
   ACTION_OPEN_ABOVE,
   ACTION_DELETE_GRAPHEME,
   ACTION_REPLACE_GRAPHEME,
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
   ACTION_ESCAPE,
   ACTION_NEWLINE,
   ACTION_BACKSPACE,
   ACTION_INDENT,
   ACTION_SNIPPET_NEXT,
   ACTION_SNIPPET_PREVIOUS,
   ACTION_COMMAND_EXECUTE,
   ACTION_LINE_START,
   ACTION_LINE_BACKWARD,
   ACTION_LINE_DELETE,
   ACTION_LINE_END,
   ACTION_LINE_FORWARD,
   ACTION_LINE_KILL_END,
   ACTION_LINE_KILL_WORD,
   ACTION_TEXT_INNER_WORD,
   ACTION_TEXT_AROUND_WORD,
   ACTION_TEXT_INNER_BIG_WORD,
   ACTION_TEXT_AROUND_BIG_WORD,
   ACTION_FILE_SAVE,
   ACTION_QUIT,
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
   bool replace_pending;
   bool snippets_enabled;
};

#define MOTION_ACTION(motion, name, label) \
   { ACTION_MOTION_FIRST + (int)(motion), name, label, \
       "Move the editing cursor" }

static const struct action_definition action_definitions[] = {
   {ACTION_INSERT_BEFORE, "mode.insert-before", "Insert before",
       "Enter Insert mode before the cursor"},
   {ACTION_INSERT_AFTER, "mode.insert-after", "Insert after",
       "Enter Insert mode after the cursor"},
   {ACTION_INSERT_FIRST_NONBLANK, "mode.insert-first-nonblank",
       "Insert at first nonblank", "Enter Insert mode at the first nonblank"},
   {ACTION_APPEND_END, "mode.append-end", "Append at line end",
       "Enter Insert mode at the end of the line"},
   {ACTION_OPEN_BELOW, "mode.open-below", "Open below",
       "Open a new line below in Insert mode"},
   {ACTION_OPEN_ABOVE, "mode.open-above", "Open above",
       "Open a new line above in Insert mode"},
   {ACTION_DELETE_GRAPHEME, "edit.delete-grapheme", "Delete character",
       "Delete one grapheme under the cursor"},
   {ACTION_REPLACE_GRAPHEME, "edit.replace-grapheme", "Replace character",
       "Replace one grapheme under the cursor"},
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
   {ACTION_ESCAPE, "mode.escape", "Escape", "Return to Normal mode"},
   {ACTION_NEWLINE, "edit.newline", "New line",
       "Insert a line break"},
   {ACTION_BACKSPACE, "edit.backspace", "Backspace",
       "Delete the preceding grapheme"},
   {ACTION_INDENT, "edit.indent", "Indent", "Insert to the next tab stop"},
   {ACTION_SNIPPET_NEXT, "snippet.next", "Next snippet stop",
       "Expand an explicit snippet or move to its next stop"},
   {ACTION_SNIPPET_PREVIOUS, "snippet.previous", "Previous snippet stop",
       "Move to the previous snippet stop"},
   {ACTION_COMMAND_EXECUTE, "command.execute", "Execute command",
       "Dispatch the command-line action"},
   {ACTION_LINE_START, "command-line.start", "Line start",
       "Move to the start of the input line"},
   {ACTION_LINE_BACKWARD, "command-line.backward", "Backward",
       "Move one grapheme left in the input line"},
   {ACTION_LINE_DELETE, "command-line.delete", "Delete",
       "Delete one grapheme at the input cursor"},
   {ACTION_LINE_END, "command-line.end", "Line end",
       "Move to the end of the input line"},
   {ACTION_LINE_FORWARD, "command-line.forward", "Forward",
       "Move one grapheme right in the input line"},
   {ACTION_LINE_KILL_END, "command-line.kill-end", "Kill to end",
       "Delete from the input cursor to the end"},
   {ACTION_LINE_KILL_WORD, "command-line.kill-word", "Kill previous word",
       "Delete the previous input word"},
   {ACTION_TEXT_INNER_WORD, "text-object.inner-word", "Inner word",
       "Apply the pending operator to the inner word"},
   {ACTION_TEXT_AROUND_WORD, "text-object.around-word", "Around word",
       "Apply the pending operator around the word"},
   {ACTION_TEXT_INNER_BIG_WORD, "text-object.inner-big-word", "Inner WORD",
       "Apply the pending operator to the inner WORD"},
   {ACTION_TEXT_AROUND_BIG_WORD, "text-object.around-big-word", "Around WORD",
       "Apply the pending operator around the WORD"},
   {ACTION_FILE_SAVE, "file.save", "Save", "Durably save the current note"},
   {ACTION_QUIT, "app.quit", "Quit", "Quit when the note is clean"},
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
   enum cwiki_keymap_mode mode;
   uint32_t first;
   unsigned int first_modifiers;
   uint32_t second;
   unsigned int second_modifiers;
   size_t length;
   const char *action;
};

#define BIND1(mode, key, modifiers, action) \
   {mode, key, modifiers, 0U, 0U, 1U, action}
#define BIND2(mode, first, second, action) \
   {mode, first, 0U, second, 0U, 2U, action}
#define NORMAL1(key, modifiers, action) \
   BIND1(CWIKI_KEYMAP_NORMAL, key, modifiers, action)
#define NORMAL2(first, second, action) \
   BIND2(CWIKI_KEYMAP_NORMAL, first, second, action)

static const struct binding_definition default_bindings[] = {
   NORMAL1('i', 0U, "mode.insert-before"),
   NORMAL1('a', 0U, "mode.insert-after"),
   NORMAL1('i', CWIKI_INPUT_SHIFT, "mode.insert-first-nonblank"),
   NORMAL1('a', CWIKI_INPUT_SHIFT, "mode.append-end"),
   NORMAL1('o', 0U, "mode.open-below"),
   NORMAL1('o', CWIKI_INPUT_SHIFT, "mode.open-above"),
   NORMAL1('x', 0U, "edit.delete-grapheme"),
   NORMAL1('r', 0U, "edit.replace-grapheme"),
   NORMAL1('r', CWIKI_INPUT_SHIFT, "mode.replace"),
   NORMAL1(';', CWIKI_INPUT_SHIFT, "mode.command"),
   NORMAL1('d', 0U, "operator.delete"),
   NORMAL1('c', 0U, "operator.change"),
   NORMAL1('y', 0U, "operator.yank"),
   NORMAL1('p', 0U, "edit.put-after"),
   NORMAL1('p', CWIKI_INPUT_SHIFT, "edit.put-before"),
   NORMAL1('u', 0U, "history.undo"),
   NORMAL1('r', CWIKI_INPUT_CTRL, "history.redo"),
   NORMAL2('g', '-', "history.older"),
   {CWIKI_KEYMAP_NORMAL, 'g', 0U, '=', CWIKI_INPUT_SHIFT, 2U,
       "history.newer"},
   NORMAL1('h', 0U, "motion.left"),
   NORMAL1('l', 0U, "motion.right"),
   NORMAL1('w', 0U, "motion.word-forward"),
   NORMAL1('w', CWIKI_INPUT_SHIFT, "motion.big-word-forward"),
   NORMAL1('b', 0U, "motion.word-backward"),
   NORMAL1('b', CWIKI_INPUT_SHIFT, "motion.big-word-backward"),
   NORMAL1('e', 0U, "motion.word-end"),
   NORMAL1('e', CWIKI_INPUT_SHIFT, "motion.big-word-end"),
   NORMAL1('j', 0U, "motion.display-down"),
   NORMAL1('k', 0U, "motion.display-up"),
   NORMAL2('g', 'j', "motion.source-down"),
   NORMAL2('g', 'k', "motion.source-up"),
   NORMAL2('g', 'g', "motion.document-first"),
   NORMAL1('g', CWIKI_INPUT_SHIFT, "motion.document-last"),
   NORMAL1('h', CWIKI_INPUT_SHIFT, "motion.viewport-high"),
   NORMAL1('m', CWIKI_INPUT_SHIFT, "motion.viewport-middle"),
   NORMAL1('l', CWIKI_INPUT_SHIFT, "motion.viewport-low"),
   NORMAL1('0', 0U, "motion.line-start"),
   NORMAL1('6', CWIKI_INPUT_SHIFT, "motion.first-nonblank"),
   NORMAL1('4', CWIKI_INPUT_SHIFT, "motion.line-end"),
   NORMAL1('-', 0U, "motion.previous-line"),
   NORMAL1('=', CWIKI_INPUT_SHIFT, "motion.next-line"),
   NORMAL1('-', CWIKI_INPUT_SHIFT, "motion.line-first-nonblank"),
   NORMAL1('[', CWIKI_INPUT_SHIFT, "motion.paragraph-backward"),
   NORMAL1(']', CWIKI_INPUT_SHIFT, "motion.paragraph-forward"),
   NORMAL1('9', CWIKI_INPUT_SHIFT, "motion.sentence-backward"),
   NORMAL1('0', CWIKI_INPUT_SHIFT, "motion.sentence-forward"),
   NORMAL1(27U, 0U, "mode.escape"),
   BIND1(CWIKI_KEYMAP_OPERATOR, 27U, 0U, "mode.escape"),
   BIND2(CWIKI_KEYMAP_OPERATOR, 'i', 'w', "text-object.inner-word"),
   {CWIKI_KEYMAP_OPERATOR, 'i', 0U, 'w', CWIKI_INPUT_SHIFT, 2U,
       "text-object.inner-big-word"},
   BIND2(CWIKI_KEYMAP_OPERATOR, 'a', 'w', "text-object.around-word"),
   {CWIKI_KEYMAP_OPERATOR, 'a', 0U, 'w', CWIKI_INPUT_SHIFT, 2U,
       "text-object.around-big-word"},
   BIND1(CWIKI_KEYMAP_REPLACE_ONE, 27U, 0U, "mode.escape"),
   BIND1(CWIKI_KEYMAP_INSERT, 27U, 0U, "mode.escape"),
   BIND1(CWIKI_KEYMAP_INSERT, 'l', CWIKI_INPUT_CTRL, "snippet.next"),
   BIND1(CWIKI_KEYMAP_INSERT, 'h', CWIKI_INPUT_CTRL, "snippet.previous"),
   BIND1(CWIKI_KEYMAP_INSERT, 9U, 0U, "edit.indent"),
   BIND1(CWIKI_KEYMAP_INSERT, 13U, 0U, "edit.newline"),
   BIND1(CWIKI_KEYMAP_INSERT, 10U, 0U, "edit.newline"),
   BIND1(CWIKI_KEYMAP_INSERT, 127U, 0U, "edit.backspace"),
   BIND1(CWIKI_KEYMAP_INSERT, 8U, 0U, "edit.backspace"),
   BIND1(CWIKI_KEYMAP_REPLACE, 27U, 0U, "mode.escape"),
   BIND1(CWIKI_KEYMAP_REPLACE, 9U, 0U, "edit.indent"),
   BIND1(CWIKI_KEYMAP_REPLACE, 13U, 0U, "edit.newline"),
   BIND1(CWIKI_KEYMAP_REPLACE, 10U, 0U, "edit.newline"),
   BIND1(CWIKI_KEYMAP_REPLACE, 127U, 0U, "edit.backspace"),
   BIND1(CWIKI_KEYMAP_REPLACE, 8U, 0U, "edit.backspace"),
   BIND1(CWIKI_KEYMAP_COMMAND, 27U, 0U, "mode.escape"),
   BIND1(CWIKI_KEYMAP_COMMAND, 13U, 0U, "command.execute"),
   BIND1(CWIKI_KEYMAP_COMMAND, 10U, 0U, "command.execute"),
   BIND1(CWIKI_KEYMAP_COMMAND, 127U, 0U, "edit.backspace"),
   BIND1(CWIKI_KEYMAP_COMMAND, 8U, 0U, "edit.backspace"),
   BIND1(CWIKI_KEYMAP_COMMAND, 'a', CWIKI_INPUT_CTRL, "command-line.start"),
   BIND1(CWIKI_KEYMAP_COMMAND, 'b', CWIKI_INPUT_CTRL, "command-line.backward"),
   BIND1(CWIKI_KEYMAP_COMMAND, 'd', CWIKI_INPUT_CTRL, "command-line.delete"),
   BIND1(CWIKI_KEYMAP_COMMAND, 'e', CWIKI_INPUT_CTRL, "command-line.end"),
   BIND1(CWIKI_KEYMAP_COMMAND, 'f', CWIKI_INPUT_CTRL, "command-line.forward"),
   BIND1(CWIKI_KEYMAP_COMMAND, 'h', CWIKI_INPUT_CTRL, "edit.backspace"),
   BIND1(CWIKI_KEYMAP_COMMAND, 'k', CWIKI_INPUT_CTRL, "command-line.kill-end"),
   BIND1(CWIKI_KEYMAP_COMMAND, 'w', CWIKI_INPUT_CTRL, "command-line.kill-word")
};

#undef BIND1
#undef BIND2
#undef NORMAL1
#undef NORMAL2

static enum cwiki_editor_status snippet_insert(
    struct cwiki_editor_input *input, const char *bytes, size_t length,
    bool auto_expand);
static enum cwiki_editor_status snippet_backspace(
    struct cwiki_editor_input *input);
static enum cwiki_editor_status handle_tab(struct cwiki_editor_input *input,
    bool previous);
static enum cwiki_editor_status insert_indent(
    struct cwiki_editor_input *input);

static bool
action_available(void *context)
{
   const struct action_context *action = context;
   enum cwiki_editor_mode mode = action->input->editor->mode;

   switch (action->id) {
   case ACTION_ESCAPE:
      return true;
   case ACTION_BACKSPACE:
      return mode != CWIKI_EDITOR_NORMAL;
   case ACTION_NEWLINE:
      return mode == CWIKI_EDITOR_INSERT || mode == CWIKI_EDITOR_REPLACE;
   case ACTION_INDENT:
      return mode == CWIKI_EDITOR_INSERT || mode == CWIKI_EDITOR_REPLACE;
   case ACTION_SNIPPET_NEXT:
   case ACTION_SNIPPET_PREVIOUS:
      return mode == CWIKI_EDITOR_INSERT && action->input->snippets_enabled;
   case ACTION_COMMAND_EXECUTE:
   case ACTION_LINE_START:
   case ACTION_LINE_BACKWARD:
   case ACTION_LINE_DELETE:
   case ACTION_LINE_END:
   case ACTION_LINE_FORWARD:
   case ACTION_LINE_KILL_END:
   case ACTION_LINE_KILL_WORD:
      return mode == CWIKI_EDITOR_COMMAND;
   case ACTION_TEXT_INNER_WORD:
   case ACTION_TEXT_AROUND_WORD:
   case ACTION_TEXT_INNER_BIG_WORD:
   case ACTION_TEXT_AROUND_BIG_WORD:
      return mode == CWIKI_EDITOR_NORMAL &&
          action->input->editor->pending_operator != CWIKI_EDITOR_NO_OPERATOR;
   case ACTION_FILE_SAVE:
   case ACTION_QUIT:
      return mode == CWIKI_EDITOR_NORMAL || mode == CWIKI_EDITOR_COMMAND;
   default:
      return mode == CWIKI_EDITOR_NORMAL;
   }
}

static bool
command_is(const struct cwiki_editor *editor, const char *command)
{
   size_t length = strlen(command);

   return editor->command_length == length &&
       memcmp(editor->command, command, length) == 0;
}

static enum cwiki_editor_status
dispatch_named(struct cwiki_editor_input *input, const char *name)
{
   int result = (int)CWIKI_EDITOR_INVALID;
   enum cwiki_action_status status = cwiki_action_dispatch(input->actions,
       name, NULL, &result);

   if (status == CWIKI_ACTION_NO_MEMORY) {
      return CWIKI_EDITOR_NO_MEMORY;
   }
   if (status != CWIKI_ACTION_OK || result < (int)CWIKI_EDITOR_OK ||
       result > (int)CWIKI_EDITOR_DURABILITY_UNCERTAIN) {
      return CWIKI_EDITOR_INVALID;
   }
   return (enum cwiki_editor_status)result;
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
   case ACTION_INSERT_FIRST_NONBLANK:
   case ACTION_APPEND_END:
      status = cwiki_editor_apply_motion(editor, input->context.layout,
          input->context.zones,
          action->id == ACTION_INSERT_FIRST_NONBLANK ?
          CWIKI_MOTION_FIRST_NONBLANK : CWIKI_MOTION_LINE_END,
          input->context.viewport);
      if (status == CWIKI_EDITOR_OK) {
         status = cwiki_editor_enter_insert(editor,
             action->id == ACTION_APPEND_END, input->context.timestamp);
      }
      break;
   case ACTION_OPEN_BELOW:
   case ACTION_OPEN_ABOVE:
      status = cwiki_editor_open_line(editor,
          action->id == ACTION_OPEN_BELOW, input->context.timestamp);
      break;
   case ACTION_DELETE_GRAPHEME:
      status = cwiki_editor_delete_grapheme(editor, input->context.timestamp);
      break;
   case ACTION_REPLACE_GRAPHEME:
      input->replace_pending = true;
      status = CWIKI_EDITOR_OK;
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
   case ACTION_ESCAPE:
      input->pending_count = 0U;
      input->replace_pending = false;
      status = cwiki_editor_escape(editor);
      break;
   case ACTION_NEWLINE:
      status = editor->mode == CWIKI_EDITOR_INSERT ?
          snippet_insert(input, "\n", 1U, true) : cwiki_editor_enter(editor);
      break;
   case ACTION_BACKSPACE:
      status = editor->mode == CWIKI_EDITOR_COMMAND ?
          cwiki_editor_command_backspace(editor) : snippet_backspace(input);
      break;
   case ACTION_INDENT:
      status = insert_indent(input);
      break;
   case ACTION_SNIPPET_NEXT:
   case ACTION_SNIPPET_PREVIOUS:
      status = handle_tab(input, action->id == ACTION_SNIPPET_PREVIOUS);
      break;
   case ACTION_COMMAND_EXECUTE:
      if (!command_is(editor, "w") && !command_is(editor, "q") &&
          !command_is(editor, "wq")) {
         status = CWIKI_EDITOR_INVALID;
         break;
      }
      status = CWIKI_EDITOR_OK;
      if (command_is(editor, "w") || command_is(editor, "wq")) {
         status = dispatch_named(input, "file.save");
      }
      if (status != CWIKI_EDITOR_OK) {
         (void)cwiki_editor_escape(editor);
         break;
      }
      if (status == CWIKI_EDITOR_OK &&
          (command_is(editor, "q") || command_is(editor, "wq"))) {
         status = dispatch_named(input, "app.quit");
      }
      if (status == CWIKI_EDITOR_OK) {
         status = cwiki_editor_escape(editor);
      }
      break;
   case ACTION_LINE_START:
      status = cwiki_editor_command_control(editor, 1U);
      break;
   case ACTION_LINE_BACKWARD:
      status = cwiki_editor_command_control(editor, 2U);
      break;
   case ACTION_LINE_DELETE:
      status = cwiki_editor_command_control(editor, 4U);
      break;
   case ACTION_LINE_END:
      status = cwiki_editor_command_control(editor, 5U);
      break;
   case ACTION_LINE_FORWARD:
      status = cwiki_editor_command_control(editor, 6U);
      break;
   case ACTION_LINE_KILL_END:
      status = cwiki_editor_command_control(editor, 11U);
      break;
   case ACTION_LINE_KILL_WORD:
      status = cwiki_editor_command_control(editor, 23U);
      break;
   case ACTION_TEXT_INNER_WORD:
   case ACTION_TEXT_AROUND_WORD:
   case ACTION_TEXT_INNER_BIG_WORD:
   case ACTION_TEXT_AROUND_BIG_WORD:
      status = cwiki_editor_apply_word_object(editor,
          action->id == ACTION_TEXT_INNER_BIG_WORD ||
          action->id == ACTION_TEXT_AROUND_BIG_WORD,
          action->id == ACTION_TEXT_AROUND_WORD ||
          action->id == ACTION_TEXT_AROUND_BIG_WORD);
      break;
   case ACTION_FILE_SAVE:
      status = cwiki_editor_save(editor);
      break;
   case ACTION_QUIT:
      if (editor->document->dirty) {
         status = CWIKI_EDITOR_DIRTY;
      } else {
         editor->quit_requested = true;
         status = CWIKI_EDITOR_OK;
      }
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
      if (cwiki_keymap_bind(input->keymap, binding->mode, &sequence,
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
   created->snippets_enabled = true;
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

void
cwiki_editor_input_set_snippets_enabled(struct cwiki_editor_input *input,
    bool enabled)
{
   if (input == NULL) {
      return;
   }
   input->snippets_enabled = enabled;
   if (!enabled) {
      cwiki_snippet_clear_sessions(input->snippets);
      input->stop_selected = false;
   }
}

static enum cwiki_editor_status
dispatch_match(struct cwiki_editor_input *input,
    const struct cwiki_keymap_match *match)
{
   return dispatch_named(input, match->action.name);
}

static enum cwiki_editor_status
handle_keymap(struct cwiki_editor_input *input, enum cwiki_keymap_mode mode,
    bool normal_fallback, const struct cwiki_input_event *event, bool *handled)
{
   struct cwiki_keymap_match match;
   bool had_prefix;

   *handled = false;
   if (event->kind != CWIKI_INPUT_KEY ||
       event->action == CWIKI_INPUT_RELEASE) {
      return CWIKI_EDITOR_NOTHING;
   }
   if (input->pending_count == CWIKI_KEYMAP_MAX_SEQUENCE) {
      input->pending_count = 0U;
      *handled = true;
      return CWIKI_EDITOR_INVALID;
   }
   had_prefix = input->pending_count != 0U;
   input->pending[input->pending_count] = *event;
   input->pending[input->pending_count].text = NULL;
   input->pending[input->pending_count].text_len = 0U;
   input->pending_count++;
   if (cwiki_keymap_match(input->keymap, mode, input->pending,
       input->pending_count, &match) != CWIKI_KEYMAP_OK) {
      input->pending_count = 0U;
      *handled = true;
      return CWIKI_EDITOR_INVALID;
   }
   if (match.kind == CWIKI_KEYMAP_NO_MATCH && normal_fallback &&
       cwiki_keymap_match(input->keymap, CWIKI_KEYMAP_NORMAL, input->pending,
       input->pending_count, &match) != CWIKI_KEYMAP_OK) {
      input->pending_count = 0U;
      *handled = true;
      return CWIKI_EDITOR_INVALID;
   }
   if (match.kind == CWIKI_KEYMAP_PREFIX) {
      *handled = true;
      return CWIKI_EDITOR_OK;
   }
   if (match.kind == CWIKI_KEYMAP_NO_MATCH) {
      if (had_prefix) {
         input->pending[0] = *event;
         input->pending[0].text = NULL;
         input->pending[0].text_len = 0U;
         input->pending_count = 1U;
         if (cwiki_keymap_match(input->keymap, mode, input->pending, 1U,
             &match) != CWIKI_KEYMAP_OK) {
            input->pending_count = 0U;
            *handled = true;
            return CWIKI_EDITOR_INVALID;
         }
         if (match.kind == CWIKI_KEYMAP_PREFIX) {
            *handled = true;
            return CWIKI_EDITOR_OK;
         }
         if (match.kind == CWIKI_KEYMAP_COMPLETE) {
            input->pending_count = 0U;
            *handled = true;
            return dispatch_match(input, &match);
         }
      }
      input->pending_count = 0U;
      *handled = had_prefix;
      return had_prefix ? CWIKI_EDITOR_INVALID : CWIKI_EDITOR_NOTHING;
   }
   input->pending_count = 0U;
   *handled = true;
   return dispatch_match(input, &match);
}

static enum cwiki_editor_status
replace_one(struct cwiki_editor_input *input,
    const struct cwiki_input_event *event)
{
   unsigned char encoded[4];
   utf8proc_ssize_t length;

   if (event->text_len != 0U) {
      input->replace_pending = false;
      return cwiki_editor_replace_grapheme(input->editor,
          (const char *)event->text, event->text_len, input->context.timestamp);
   }
   if (event->key >= UINT32_C(57344) && event->key <= UINT32_C(63743)) {
      return CWIKI_EDITOR_NOTHING;
   }
   input->replace_pending = false;
   if ((event->modifiers & ~(unsigned int)CWIKI_INPUT_SHIFT) != 0U ||
       event->key < UINT32_C(0x20) || event->key > UINT32_C(0x10ffff) ||
       (event->key >= UINT32_C(0xd800) && event->key <= UINT32_C(0xdfff))) {
      return CWIKI_EDITOR_INVALID;
   }
   length = utf8proc_encode_char((utf8proc_int32_t)event->key, encoded);
   return length <= 0 ? CWIKI_EDITOR_INVALID :
       cwiki_editor_replace_grapheme(input->editor, (const char *)encoded,
       (size_t)length, input->context.timestamp);
}

static enum cwiki_editor_status
handle_normal(struct cwiki_editor_input *input,
    const struct cwiki_input_event *event)
{
   enum cwiki_editor_status status;
   enum cwiki_keymap_mode mode;
   bool handled;

   mode = input->replace_pending ? CWIKI_KEYMAP_REPLACE_ONE :
       (input->editor->pending_operator == CWIKI_EDITOR_NO_OPERATOR ?
       CWIKI_KEYMAP_NORMAL : CWIKI_KEYMAP_OPERATOR);
   status = handle_keymap(input, mode, mode == CWIKI_KEYMAP_OPERATOR, event,
       &handled);
   if (handled) {
      if (status == CWIKI_EDITOR_INVALID && mode == CWIKI_KEYMAP_OPERATOR) {
         (void)cwiki_editor_escape(input->editor);
      }
      return status;
   }
   return input->replace_pending ? replace_one(input, event) :
       CWIKI_EDITOR_NOTHING;
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

   if (!input->snippets_enabled) {
      return CWIKI_EDITOR_NOTHING;
   }
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
       (event->key >= UINT32_C(57344) && event->key <= UINT32_C(63743)) ||
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
insert_indent(struct cwiki_editor_input *input)
{
   static const char spaces[] = "                ";
   const struct cwiki_line *line =
       &input->editor->document->buffer.lines[input->editor->motion.cursor.line];
   size_t width = input->context.tabstop == 0U ? 3U : input->context.tabstop;
   size_t byte = 0U;
   size_t column = 0U;
   size_t count;

   if (!input->context.expandtab) {
      return snippet_insert(input, "\t", 1U, true);
   }
   if (width > sizeof(spaces) - 1U) {
      return CWIKI_EDITOR_INVALID;
   }
   while (byte < input->editor->motion.cursor.byte) {
      size_t next = cwiki_grapheme_next_unchecked(line->bytes, line->length,
          byte);

      if (next == byte + 1U && line->bytes[byte] == '\t') {
         column += width - column % width;
      } else {
         int cells = cwiki_grapheme_width(line->bytes + byte, next - byte);

         if (cells > 0) {
            column += (size_t)cells;
         }
      }
      byte = next;
   }
   count = width - column % width;
   return snippet_insert(input, spaces, count, true);
}

static enum cwiki_editor_status
handle_editing(struct cwiki_editor_input *input,
    const struct cwiki_input_event *event)
{
   struct cwiki_editor *editor = input->editor;
   enum cwiki_keymap_mode mode = editor->mode == CWIKI_EDITOR_INSERT ?
       CWIKI_KEYMAP_INSERT : (editor->mode == CWIKI_EDITOR_REPLACE ?
       CWIKI_KEYMAP_REPLACE : CWIKI_KEYMAP_COMMAND);
   enum cwiki_editor_status status;
   bool handled;

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
   status = handle_keymap(input, mode, false, event, &handled);
   if (handled) {
      return status;
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

enum cwiki_snippet_status
cwiki_editor_input_list_active_snippets(const struct cwiki_editor_input *input,
    struct cwiki_zone_engine *zones, struct cwiki_snippet_info *infos,
    size_t capacity, size_t *count)
{
   struct cwiki_zone zone;

   if (input == NULL || count == NULL) {
      return CWIKI_SNIPPET_INVALID;
   }
   if (!input->snippets_enabled) {
      *count = 0U;
      return CWIKI_SNIPPET_OK;
   }
   if (zones == NULL || input->editor == NULL ||
       input->editor->document == NULL ||
       cwiki_zone_at(zones, &input->editor->document->buffer,
       input->editor->motion.cursor.line, input->editor->motion.cursor.byte,
       &zone) != 0) {
      return CWIKI_SNIPPET_INVALID;
   }
   return cwiki_snippet_registry_list_active(input->snippet_registry, zone.kind,
       infos, capacity, count);
}
