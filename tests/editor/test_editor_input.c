#include "action.h"
#include "document.h"
#include "editor.h"
#include "editor_input.h"
#include "input.h"
#include "keymap.h"
#include "layout.h"
#include "zone.h"

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

static struct cwiki_zone_engine *
attach_zones(struct fixture *fixture)
{
   struct cwiki_zone_engine *zones = NULL;
   const struct cwiki_zone_region *regions;
   size_t count;
   uint64_t top;

   regions = cwiki_zone_builtin_regions(&count, &top);
   check(cwiki_zone_engine_init(&zones, regions, count, top) == 0,
       "initialize snippet zones");
   fixture->context.zones = zones;
   return zones;
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

static void
test_runtime_snippets_and_undo_boundaries(void)
{
   struct fixture fixture;
   struct cwiki_zone_engine *zones;
   struct cwiki_input_event paste = {0};

   fixture_init(&fixture, "");
   zones = attach_zones(&fixture);
   check(send(&fixture, key('i', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('m', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('k', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "mk"), "explicit trigger remains literal before Tab");
   check(send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK &&
       fixture.editor.motion.cursor.byte == 1U &&
       content_is(&fixture, "$$"), "Tab expands and selects the first stop");
   check(send(&fixture, key('x', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(27U, 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "$x$") &&
       cwiki_undo_state_count(&fixture.editor.undo) == 4U,
       "typed trigger, expansion and stop fill are distinct undo steps");
   check(send(&fixture, key('u', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "$$") &&
       send(&fixture, key('u', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "mk") &&
       send(&fixture, key('u', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, ""),
       "undo reverses fill, expansion and typed trigger independently");
   cwiki_zone_engine_free(zones);
   fixture_free(&fixture);

   fixture_init(&fixture, "$ $");
   zones = attach_zones(&fixture);
   fixture.editor.motion.cursor.byte = 1U;
   check(send(&fixture, key('i', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('`', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('a', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "$\\alpha $"),
       "ordinary VimTeX symbol mapping auto-expands in math");
   check(send(&fixture, key(27U, 0U)) == CWIKI_EDITOR_OK,
       "leave symbol expansion insert session");
   cwiki_zone_engine_free(zones);
   fixture_free(&fixture);

   fixture_init(&fixture, "");
   zones = attach_zones(&fixture);
   check(send(&fixture, key('i', 0U)) == CWIKI_EDITOR_OK,
       "enter insert mode for literal paste");
   paste.kind = CWIKI_INPUT_PASTE;
   paste.text = (const unsigned char *)"mk";
   paste.text_len = 2U;
   check(send(&fixture, paste) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "mk"), "paste never auto-expands snippets");
   check(send(&fixture, key(27U, 0U)) == CWIKI_EDITOR_OK,
       "leave pasted insert session");
   cwiki_zone_engine_free(zones);
   fixture_free(&fixture);
}

static void
test_runtime_snippet_mirrors_and_tabs(void)
{
   struct fixture fixture;
   struct cwiki_zone_engine *zones;
   const char *gather = "gather";
   size_t index;

   fixture_init(&fixture, "");
   zones = attach_zones(&fixture);
   check(send(&fixture, key('i', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('e', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('n', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('v', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK,
       "expand mirrored environment snippet");
   for (index = 0U; index < strlen(gather); index++) {
      check(send(&fixture, key((uint32_t)(unsigned char)gather[index], 0U)) ==
          CWIKI_EDITOR_OK, "fill mirrored environment name");
   }
   check(content_is(&fixture,
       "\\begin{gather}\n\n\\end{gather}"),
       "typing one stop updates its mirror on every edit");
   check(send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('x', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(9U, CWIKI_INPUT_SHIFT)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(27U, 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture,
       "\\begin{gather}\nx\n\\end{gather}"),
       "Tab and Shift-Tab navigate numbered stops and final stop");
   check(cwiki_undo_state_count(&fixture.editor.undo) == 4U &&
       send(&fixture, key('u', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "\\begin{align}\n\n\\end{align}") &&
       send(&fixture, key('u', 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "env"),
       "all stop fills and mirrors share one post-expansion insert step");
   cwiki_zone_engine_free(zones);
   fixture_free(&fixture);
}

static void
test_runtime_nested_snippets(void)
{
   struct fixture fixture;
   struct cwiki_zone_engine *zones;

   fixture_init(&fixture, "$ $");
   zones = attach_zones(&fixture);
   fixture.editor.motion.cursor.byte = 1U;
   check(send(&fixture, key('i', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('/', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('/', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('/', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('/', 0U)) == CWIKI_EDITOR_OK,
       "auto-expand a fraction inside an outer fraction stop");
   check(send(&fixture, key('a', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('b', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(9U, 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key('c', 0U)) == CWIKI_EDITOR_OK &&
       send(&fixture, key(27U, 0U)) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "$\\frac{\\frac{a}{b}}{c} $"),
       "nested session returns from its final stop to the outer denominator");
   cwiki_zone_engine_free(zones);
   fixture_free(&fixture);
}

static void
test_reveal_through_key_dispatch(void)
{
   struct fixture fixture;
   struct cwiki_zone_engine *zones = NULL;
   struct cwiki_conceal_table conceal;
   struct cwiki_layout_window layout;
   struct cwiki_layout_options options = {0};
   size_t count;
   uint64_t top;
   const struct cwiki_zone_region *regions =
       cwiki_zone_builtin_regions(&count, &top);
   static const struct {
      uint32_t key;
      unsigned int modifiers;
      size_t byte;
      bool revealed;
   } steps[] = {
      {'l', 0U, 1U, true}, {'l', 0U, 2U, true},
      {'h', 0U, 1U, true}, {'h', 0U, 0U, false},
      {'l', 0U, 1U, true}, {';', CWIKI_INPUT_SHIFT, 1U, true},
      {27U, 0U, 1U, true}, {'i', 0U, 1U, true},
      {27U, 0U, 1U, true}, {'r', CWIKI_INPUT_SHIFT, 1U, true},
      {27U, 0U, 1U, true},
      {'l', 0U, 2U, true}, {'l', 0U, 3U, true},
      {'l', 0U, 4U, true}, {'l', 0U, 5U, true},
      {'l', 0U, 6U, true}, {'l', 0U, 7U, false},
      {'h', 0U, 6U, true}, {'a', 0U, 7U, false},
      {27U, 0U, 7U, false}, {'h', 0U, 6U, true},
      {'y', 0U, 6U, true}, {'h', 0U, 5U, false},
      {'l', 0U, 6U, true}, {'l', 0U, 7U, false},
      {'l', 0U, 8U, false}, {'l', 0U, 9U, false},
      {'l', 0U, 10U, true}, {'l', 0U, 11U, true},
      {'h', 0U, 10U, true}, {'h', 0U, 9U, false}
   };

   fixture_init(&fixture, "$\\alpha + \\beta$\nnext");
   check(cwiki_zone_engine_init(&zones, regions, count, top) == 0 &&
       cwiki_zone_recompute(zones, &fixture.document.buffer, 0U, NULL) == 0 &&
       cwiki_conceal_table_init_builtin(&conceal) == 0,
       "initialize reveal layout dependencies");
   cwiki_layout_window_init(&layout);
   options.content_width = 40U;
   options.continuation_marker = "";
   options.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   options.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
   fixture.context.layout = &layout;
   fixture.context.zones = zones;
   for (size_t i = 0U; i < sizeof(steps) / sizeof(steps[0]); i++) {
      options.cursor = fixture.editor.motion.cursor;
      options.reveal_line = fixture.editor.reveal_line;
      options.reveal = fixture.editor.reveal;
      options.mode = fixture.editor.mode == CWIKI_EDITOR_NORMAL ?
          CWIKI_CONCEAL_MODE_NORMAL : (fixture.editor.mode ==
          CWIKI_EDITOR_COMMAND ? CWIKI_CONCEAL_MODE_COMMAND :
          CWIKI_CONCEAL_MODE_INSERT);
      check(cwiki_layout_rebuild(&layout, &fixture.document.buffer, zones,
          &conceal, &options) == 0, "rebuild dispatched reveal state");
      check(send(&fixture, key(steps[i].key, steps[i].modifiers)) ==
          CWIKI_EDITOR_OK && fixture.editor.motion.cursor.byte == steps[i].byte &&
          fixture.editor.reveal.active == steps[i].revealed,
          "approach, continuity, departure and mode/operator dispatch");
      if (steps[i].revealed) {
         check(fixture.editor.reveal_line == 0U &&
             fixture.editor.reveal.source_start ==
             (steps[i].byte >= 10U ? 10U : 1U) &&
             fixture.editor.reveal.source_end ==
             (steps[i].byte >= 10U ? 15U : 7U),
             "only the approached run remains revealed");
      }
   }
   cwiki_layout_window_free(&layout);
   cwiki_conceal_table_free(&conceal);
   cwiki_zone_engine_free(zones);
   fixture_free(&fixture);
}

int
main(void)
{
   test_reveal_through_key_dispatch();
   test_named_actions_physical_keys_and_literal_text();
   test_operator_sequences_history_and_rebinding();
   test_command_line_and_unknown_input();
   test_runtime_snippets_and_undo_boundaries();
   test_runtime_snippet_mirrors_and_tabs();
   test_runtime_nested_snippets();
   if (failures != 0) {
      (void)fprintf(stderr, "%d editor input test(s) failed\n", failures);
      return EXIT_FAILURE;
   }
   (void)puts("editor input tests passed");
   return EXIT_SUCCESS;
}
