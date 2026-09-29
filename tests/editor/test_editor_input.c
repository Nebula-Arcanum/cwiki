#include "action.h"
#include "document.h"
#include "editor.h"
#include "editor_input.h"
#include "input.h"
#include "keymap.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

struct fixture {
   struct cwiki_document document;
   struct cwiki_editor editor;
   struct cwiki_editor_input *input;
   struct cwiki_editor_input_context context;
   char path[64];
};

static void
fixture_init(struct fixture *fixture, const char *text)
{
   int descriptor;

   (void)memset(fixture, 0, sizeof(*fixture));
   (void)strcpy(fixture->path, "/tmp/cwiki-editor-input-XXXXXX");
   descriptor = mkstemp(fixture->path);
   check(descriptor >= 0, "create input fixture");
   if (descriptor >= 0) {
      check(write(descriptor, text, strlen(text)) == (ssize_t)strlen(text),
          "write input fixture");
      check(close(descriptor) == 0, "close input fixture");
   }
   check(cwiki_document_load(&fixture->document, fixture->path) == 0,
       "load input fixture");
   check(cwiki_editor_init(&fixture->editor, &fixture->document) ==
       CWIKI_EDITOR_OK, "initialize input editor");
   check(cwiki_editor_input_init(&fixture->input, &fixture->editor) ==
       CWIKI_EDITOR_OK, "initialize editor input controller");
   fixture->context.timestamp = 100U;
}

static void
fixture_free(struct fixture *fixture)
{
   cwiki_editor_input_free(fixture->input);
   cwiki_editor_free(&fixture->editor);
   cwiki_document_free(&fixture->document);
   (void)unlink(fixture->path);
}

static bool
content_is(struct fixture *fixture, const char *expected)
{
   char *bytes = NULL;
   size_t length = 0U;
   size_t expected_length = strlen(expected);
   bool equal;

   if (cwiki_buffer_encode(&fixture->document.buffer, &bytes, &length) != 0) {
      return false;
   }
   equal = length == expected_length &&
       memcmp(bytes, expected, expected_length) == 0;
   free(bytes);
   return equal;
}

static struct cwiki_input_event
key(uint32_t value, unsigned int modifiers)
{
   struct cwiki_input_event event = {0};

   event.kind = CWIKI_INPUT_KEY;
   event.key = value;
   event.modifiers = modifiers;
   event.action = CWIKI_INPUT_PRESS;
   return event;
}

static enum cwiki_editor_status
send(struct fixture *fixture, struct cwiki_input_event event)
{
   fixture->context.timestamp++;
   return cwiki_editor_input_handle(fixture->input, &event,
       &fixture->context);
}

static void
test_named_actions_physical_keys_and_literal_text(void)
{
   struct fixture fixture;
   struct cwiki_input_event event;
   struct cwiki_action_info info;
   static const unsigned char associated[] = "界";
   static const unsigned char pasted[] = {'\n', 'P'};

   fixture_init(&fixture, "ab");
   check(cwiki_action_lookup(cwiki_editor_input_actions(fixture.input),
       "operator.delete", &info) == CWIKI_ACTION_OK &&
       strcmp(info.label, "Delete") == 0,
       "controller exposes builtin operations through the named registry");

   event = key('x', 0U);
   event.has_base_layout_key = true;
   event.base_layout_key = 'i';
   check(send(&fixture, event) == CWIKI_EDITOR_OK &&
       fixture.editor.mode == CWIKI_EDITOR_INSERT,
       "default bindings use the physical base-layout identity");
   event = key(0U, 0U);
   event.text = associated;
   event.text_len = sizeof(associated) - 1U;
   check(send(&fixture, event) == CWIKI_EDITOR_OK,
       "associated text inserts as literal UTF-8");
   event = key('x', 0U);
   event.action = CWIKI_INPUT_RELEASE;
   check(send(&fixture, event) == CWIKI_EDITOR_NOTHING,
       "key releases do not insert text");
   (void)memset(&event, 0, sizeof(event));
   event.kind = CWIKI_INPUT_PASTE;
   event.text = pasted;
   event.text_len = sizeof(pasted);
   check(send(&fixture, event) == CWIKI_EDITOR_OK,
       "bracketed paste inserts literally without normal dispatch");
   check(send(&fixture, key(27U, 0U)) == CWIKI_EDITOR_OK &&
       fixture.editor.mode == CWIKI_EDITOR_NORMAL &&
       content_is(&fixture, "界\nPab"),
       "Escape commits the parser-driven insert session");
   fixture_free(&fixture);
}

static void
test_operator_sequences_history_and_rebinding(void)
{
   struct fixture fixture;
   struct cwiki_key_sequence z = {0};

   fixture_init(&fixture, "alpha beta");
   check(send(&fixture, key('d', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('w', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "beta"),
       "d followed by w composes through named operator and motion actions");
   check(send(&fixture, key('p', CWIKI_INPUT_SHIFT)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "alpha beta"),
       "P dispatches the unnamed characterwise put");
   check(send(&fixture, key('u', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "beta"), "u dispatches one undo state");
   check(send(&fixture, key('r', CWIKI_INPUT_CTRL)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "alpha beta"),
       "Ctrl-r remains distinct and dispatches redo");

   fixture.editor.motion.cursor = (struct cwiki_position){0U, 0U};
   check(send(&fixture, key('g', 0U)) == CWIKI_EDITOR_OK &&
       fixture.editor.motion.cursor.byte == 0U &&
       send(&fixture, key('g', 0U)) == CWIKI_EDITOR_OK,
       "a g prefix waits and gg dispatches as one sequence");

   z.keys[0].key = 'z';
   z.length = 1U;
   check(cwiki_keymap_bind(cwiki_editor_input_keymap(fixture.input),
       CWIKI_KEYMAP_NORMAL, &z, "motion.line-end") == CWIKI_KEYMAP_OK &&
       send(&fixture, key('z', 0U)) == CWIKI_EDITOR_OK &&
       fixture.editor.motion.cursor.byte == strlen("alpha beta") - 1U,
       "a runtime binding uses the same named-action dispatch path");
   fixture_free(&fixture);
}

static void
test_command_line_and_unknown_input(void)
{
   struct fixture fixture;

   fixture_init(&fixture, "note");
   check(send(&fixture, key('?', 0U)) == CWIKI_EDITOR_NOTHING &&
       fixture.editor.mode == CWIKI_EDITOR_NORMAL,
       "an unbound Normal-mode key is a deterministic no-op");
   check(send(&fixture, key(';', CWIKI_INPUT_SHIFT)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('w', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('q', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(13U, 0U)) == CWIKI_EDITOR_OK &&
       fixture.editor.quit_requested,
       "typed :wq executes through Command-line mode on Enter");
   fixture_free(&fixture);
}

int
main(void)
{
   test_named_actions_physical_keys_and_literal_text();
   test_operator_sequences_history_and_rebinding();
   test_command_line_and_unknown_input();
   if (failures != 0) {
      (void)fprintf(stderr, "%d editor input test(s) failed\n", failures);
      return EXIT_FAILURE;
   }
   (void)puts("editor input tests passed");
   return EXIT_SUCCESS;
}
