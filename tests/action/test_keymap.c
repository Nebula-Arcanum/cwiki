#include "action.h"
#include "input.h"
#include "keymap.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static int
noop(void *context, const struct cwiki_action_argument *argument)
{
   (void)context;
   (void)argument;
   return 0;
}

static struct cwiki_action_spec
action_spec(const char *name, const char *label)
{
   struct cwiki_action_spec spec = {
      name, label, "keymap test action", CWIKI_ACTION_PARAMETER_NONE, NULL,
      noop, NULL, NULL
   };

   return spec;
}

static void
register_action(struct cwiki_action_registry *actions, const char *name,
    const char *label)
{
   struct cwiki_action_spec spec = action_spec(name, label);

   check(cwiki_action_register(actions, &spec) == CWIKI_ACTION_OK,
       "register keymap test action");
}

static struct cwiki_key_sequence
sequence1(uint32_t key, unsigned int modifiers)
{
   struct cwiki_key_sequence sequence = {0};

   sequence.keys[0].key = key;
   sequence.keys[0].modifiers = modifiers;
   sequence.length = 1U;
   return sequence;
}

static struct cwiki_key_sequence
sequence2(uint32_t first, unsigned int first_modifiers, uint32_t second,
    unsigned int second_modifiers)
{
   struct cwiki_key_sequence sequence = {0};

   sequence.keys[0].key = first;
   sequence.keys[0].modifiers = first_modifiers;
   sequence.keys[1].key = second;
   sequence.keys[1].modifiers = second_modifiers;
   sequence.length = 2U;
   return sequence;
}

static struct cwiki_key_sequence
sequence3(uint32_t first, uint32_t second, uint32_t third)
{
   struct cwiki_key_sequence sequence = {0};

   sequence.keys[0].key = first;
   sequence.keys[1].key = second;
   sequence.keys[2].key = third;
   sequence.length = 3U;
   return sequence;
}

static struct cwiki_input_event
key_event(uint32_t key, unsigned int modifiers)
{
   struct cwiki_input_event event = {0};

   event.kind = CWIKI_INPUT_KEY;
   event.key = key;
   event.modifiers = modifiers;
   event.action = CWIKI_INPUT_PRESS;
   return event;
}

static struct cwiki_keymap *
new_keymap(struct cwiki_action_registry **actions)
{
   struct cwiki_keymap *keymap = NULL;

   check(cwiki_action_registry_init(actions) == CWIKI_ACTION_OK,
       "initialize action registry");
   register_action(*actions, "cursor.first", "First");
   register_action(*actions, "cursor.second", "Second");
   register_action(*actions, "cursor.third", "Third");
   check(cwiki_keymap_init(&keymap, *actions) == CWIKI_KEYMAP_OK,
       "initialize keymap");
   return keymap;
}

static bool
matches_action(const struct cwiki_keymap *keymap, enum cwiki_keymap_mode mode,
    const struct cwiki_input_event *events, size_t count, const char *name,
    bool has_continuations)
{
   struct cwiki_keymap_match match;

   return cwiki_keymap_match(keymap, mode, events, count, &match) ==
       CWIKI_KEYMAP_OK && match.kind == CWIKI_KEYMAP_COMPLETE &&
       strcmp(match.action.name, name) == 0 &&
       match.has_continuations == has_continuations;
}

static bool
has_match_kind(const struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *events,
    size_t count, enum cwiki_keymap_match_kind kind)
{
   struct cwiki_keymap_match match;

   return cwiki_keymap_match(keymap, mode, events, count, &match) ==
       CWIKI_KEYMAP_OK && match.kind == kind;
}

static void
test_physical_identity_and_disambiguation(void)
{
   struct cwiki_action_registry *actions = NULL;
   struct cwiki_keymap *keymap = new_keymap(&actions);
   struct cwiki_key_sequence physical_q = sequence1('q', 0U);
   struct cwiki_key_sequence ctrl_i = sequence1('i', CWIKI_INPUT_CTRL);
   struct cwiki_key_sequence tab = sequence1(9U, 0U);
   struct cwiki_key_sequence escape = sequence1(27U, 0U);
   struct cwiki_key_sequence alt_escape = sequence1(27U, CWIKI_INPUT_ALT);
   struct cwiki_input_event event;

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &physical_q,
       "cursor.first") == CWIKI_KEYMAP_OK, "bind a physical base-layout key");
   event = key_event('a', 0U);
   event.has_base_layout_key = true;
   event.base_layout_key = 'q';
   check(matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false),
       "non-US produced key dispatches through its base-layout identity");
   event.has_base_layout_key = false;
   check(has_match_kind(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       CWIKI_KEYMAP_NO_MATCH),
       "produced key is not mistaken for a physical key when no base is sent");
   event = key_event('q', 0U);
   check(matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false), "key identity is the fallback without a base key");

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &ctrl_i,
       "cursor.first") == CWIKI_KEYMAP_OK &&
       cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &tab,
       "cursor.second") == CWIKI_KEYMAP_OK,
       "bind Ctrl+i and Tab independently");
   event = key_event('i', CWIKI_INPUT_CTRL);
   check(matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false), "Ctrl+i does not collapse to Tab");
   event = key_event(9U, 0U);
   check(matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.second", false), "Tab does not collapse to Ctrl+i");

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &escape,
       "cursor.first") == CWIKI_KEYMAP_OK &&
       cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &alt_escape,
       "cursor.third") == CWIKI_KEYMAP_OK,
       "bind Escape and Alt+Escape independently");
   event = key_event(27U, 0U);
   check(matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false), "Escape is an exact key event");
   event.modifiers = CWIKI_INPUT_ALT;
   check(matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.third", false), "Alt modifier remains distinct from Escape");

   cwiki_keymap_free(keymap);
   cwiki_action_registry_free(actions);
}

static void
test_modes_bind_rebind_and_unbind(void)
{
   struct cwiki_action_registry *actions = NULL;
   struct cwiki_keymap *keymap = new_keymap(&actions);
   struct cwiki_key_sequence x = sequence1('x', 0U);
   struct cwiki_key_sequence y = sequence1('y', 0U);
   struct cwiki_input_event event = key_event('x', 0U);

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x,
       "cursor.first") == CWIKI_KEYMAP_OK &&
       cwiki_keymap_bind(keymap, CWIKI_KEYMAP_INSERT, &x,
       "cursor.second") == CWIKI_KEYMAP_OK,
       "the same physical sequence may differ by editor mode");
   check(matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false) &&
       matches_action(keymap, CWIKI_KEYMAP_INSERT, &event, 1U,
       "cursor.second", false) &&
       has_match_kind(keymap, CWIKI_KEYMAP_REPLACE, &event, 1U,
       CWIKI_KEYMAP_NO_MATCH), "mode scope selects only its own binding");

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x,
       "cursor.third") == CWIKI_KEYMAP_CONFLICT &&
       matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false),
       "an exact conflict is rejected without replacing the binding");
   check(cwiki_keymap_rebind(keymap, CWIKI_KEYMAP_NORMAL, &x,
       "cursor.third") == CWIKI_KEYMAP_OK &&
       matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.third", false), "rebind deterministically replaces one binding");
   check(cwiki_keymap_rebind(keymap, CWIKI_KEYMAP_NORMAL, &y,
       "cursor.first") == CWIKI_KEYMAP_NOT_FOUND,
       "rebind does not silently create a binding");
   check(cwiki_keymap_unbind(keymap, CWIKI_KEYMAP_NORMAL, &x) ==
       CWIKI_KEYMAP_OK && has_match_kind(keymap, CWIKI_KEYMAP_NORMAL, &event,
       1U, CWIKI_KEYMAP_NO_MATCH), "unbind removes the exact scoped binding");
   check(cwiki_keymap_unbind(keymap, CWIKI_KEYMAP_NORMAL, &x) ==
       CWIKI_KEYMAP_NOT_FOUND &&
       matches_action(keymap, CWIKI_KEYMAP_INSERT, &event, 1U,
       "cursor.second", false),
       "missing unbind is deterministic and leaves other modes unchanged");

   cwiki_keymap_free(keymap);
   cwiki_action_registry_free(actions);
}

static void
test_prefix_matching_and_clues(void)
{
   struct cwiki_action_registry *actions = NULL;
   struct cwiki_keymap *keymap = new_keymap(&actions);
   struct cwiki_key_sequence g = sequence1('g', 0U);
   struct cwiki_key_sequence ga = sequence2('g', 0U, 'a', 0U);
   struct cwiki_key_sequence gbx = sequence3('g', 'b', 'x');
   struct cwiki_key_sequence gby = sequence3('g', 'b', 'y');
   struct cwiki_input_event events[3];
   struct cwiki_keymap_continuation clue;

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &gbx,
       "cursor.third") == CWIKI_KEYMAP_OK &&
       cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &ga,
       "cursor.second") == CWIKI_KEYMAP_OK &&
       cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &gby,
       "cursor.first") == CWIKI_KEYMAP_OK,
       "bind out-of-order multi-key sequences");
   events[0] = key_event('g', 0U);
   events[1] = key_event('b', 0U);
   events[2] = key_event('x', 0U);
   check(has_match_kind(keymap, CWIKI_KEYMAP_NORMAL, events, 1U,
       CWIKI_KEYMAP_PREFIX) &&
       has_match_kind(keymap, CWIKI_KEYMAP_NORMAL, events, 2U,
       CWIKI_KEYMAP_PREFIX) &&
       matches_action(keymap, CWIKI_KEYMAP_NORMAL, events, 3U,
       "cursor.third", false), "matcher separates prefix from complete");
   events[2] = key_event('z', 0U);
   check(has_match_kind(keymap, CWIKI_KEYMAP_NORMAL, events, 3U,
       CWIKI_KEYMAP_NO_MATCH), "matcher reports a dead sequence as no match");

   check(cwiki_keymap_continuation_count(keymap, CWIKI_KEYMAP_NORMAL, events,
       1U) == 2U, "clue enumeration coalesces immediate continuation keys");
   check(cwiki_keymap_continuation_at(keymap, CWIKI_KEYMAP_NORMAL, events, 1U,
       0U, &clue) == CWIKI_KEYMAP_OK && clue.key.key == 'a' &&
       clue.completes && !clue.has_continuations &&
       strcmp(clue.action.name, "cursor.second") == 0 &&
       strcmp(clue.action.label, "Second") == 0,
       "first clue is deterministic and exposes action metadata");
   check(cwiki_keymap_continuation_at(keymap, CWIKI_KEYMAP_NORMAL, events, 1U,
       1U, &clue) == CWIKI_KEYMAP_OK && clue.key.key == 'b' &&
       !clue.completes && clue.has_continuations,
       "a deeper immediate clue identifies further continuations");
   check(cwiki_keymap_continuation_at(keymap, CWIKI_KEYMAP_NORMAL, events, 1U,
       2U, &clue) == CWIKI_KEYMAP_NOT_FOUND,
       "clue enumeration has a deterministic end");

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &g,
       "cursor.first") == CWIKI_KEYMAP_OK &&
       matches_action(keymap, CWIKI_KEYMAP_NORMAL, events, 1U,
       "cursor.first", true),
       "an exact binding may also prefix longer bindings without hiding either");
   {
      struct cwiki_keymap_binding_info binding;
      size_t count = cwiki_keymap_binding_count(keymap);

      check(count == 4U && cwiki_keymap_binding_at(keymap, 0U,
          &binding) == CWIKI_KEYMAP_OK &&
          strcmp(binding.action_name, "cursor.first") == 0 &&
          binding.mode == CWIKI_KEYMAP_NORMAL && binding.sequence.length == 1U,
          "inspection enumerates stable effective bindings");
      check(cwiki_keymap_binding_at(keymap, count, &binding) ==
          CWIKI_KEYMAP_INVALID, "binding inspection rejects an invalid index");
   }

   cwiki_keymap_free(keymap);
   cwiki_action_registry_free(actions);
}

static void
test_non_key_events_never_dispatch(void)
{
   struct cwiki_action_registry *actions = NULL;
   struct cwiki_keymap *keymap = new_keymap(&actions);
   struct cwiki_key_sequence x = sequence1('x', 0U);
   struct cwiki_input_event event = key_event('x', 0U);

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x,
       "cursor.first") == CWIKI_KEYMAP_OK, "bind event-filter test key");
   event.kind = CWIKI_INPUT_PASTE;
   check(has_match_kind(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       CWIKI_KEYMAP_NO_MATCH), "paste text never dispatches as key bindings");
   event.kind = CWIKI_INPUT_KEY;
   event.action = CWIKI_INPUT_RELEASE;
   check(has_match_kind(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       CWIKI_KEYMAP_NO_MATCH), "release events never dispatch as key bindings");
   event.action = CWIKI_INPUT_REPEAT;
   check(matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false), "repeat events use the same physical binding");

   cwiki_keymap_free(keymap);
   cwiki_action_registry_free(actions);
}

static void
test_invalid_and_unknown_bindings_are_transactional(void)
{
   struct cwiki_action_registry *actions = NULL;
   struct cwiki_keymap *keymap = new_keymap(&actions);
   struct cwiki_key_sequence x = sequence1('x', 0U);
   struct cwiki_key_sequence invalid = sequence1(0U, 0U);
   struct cwiki_input_event event = key_event('x', 0U);

   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x,
       "cursor.first") == CWIKI_KEYMAP_OK, "bind transaction baseline");
   check(cwiki_keymap_rebind(keymap, CWIKI_KEYMAP_NORMAL, &x,
       "missing.action") == CWIKI_KEYMAP_UNKNOWN_ACTION &&
       matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false),
       "unknown rebind action leaves the prior binding active");
   check(cwiki_keymap_rebind(keymap, CWIKI_KEYMAP_NORMAL, &x,
       "Not valid") == CWIKI_KEYMAP_UNKNOWN_ACTION &&
       matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false),
       "invalid action name leaves the prior binding active");
   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &invalid,
       "cursor.second") == CWIKI_KEYMAP_INVALID &&
       cwiki_keymap_unbind(keymap, CWIKI_KEYMAP_MODE_COUNT, &x) ==
       CWIKI_KEYMAP_INVALID &&
       matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
       "cursor.first", false), "invalid binding requests do not mutate state");
   invalid = x;
   invalid.length = CWIKI_KEYMAP_MAX_SEQUENCE + 1U;
   check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &invalid,
       "cursor.second") == CWIKI_KEYMAP_INVALID,
       "over-limit key sequences are rejected before reading their keys");

   cwiki_keymap_free(keymap);
   cwiki_action_registry_free(actions);
}

static void
test_allocation_failures_are_transactional(void)
{
   size_t failure_point;

   for (failure_point = 0U; failure_point < 2U; failure_point++) {
      struct cwiki_action_registry *actions = NULL;
      struct cwiki_keymap *keymap = new_keymap(&actions);
      struct cwiki_key_sequence x = sequence1('x', 0U);
      struct cwiki_input_event event = key_event('x', 0U);

      cwiki_keymap_test_fail_allocation_after(failure_point);
      check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x,
          "cursor.first") == CWIKI_KEYMAP_NO_MEMORY &&
          has_match_kind(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
          CWIKI_KEYMAP_NO_MATCH),
          "failed bind allocation leaves no partial binding");
      cwiki_keymap_test_reset_allocation();
      check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x,
          "cursor.first") == CWIKI_KEYMAP_OK,
          "keymap remains usable after a failed bind allocation");
      cwiki_keymap_free(keymap);
      cwiki_action_registry_free(actions);
   }
   {
      struct cwiki_action_registry *actions = NULL;
      struct cwiki_keymap *keymap = new_keymap(&actions);
      struct cwiki_key_sequence x = sequence1('x', 0U);
      struct cwiki_input_event event = key_event('x', 0U);

      check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x,
          "cursor.first") == CWIKI_KEYMAP_OK, "bind reallocation baseline");
      cwiki_keymap_test_fail_allocation_after(0U);
      check(cwiki_keymap_rebind(keymap, CWIKI_KEYMAP_NORMAL, &x,
          "cursor.second") == CWIKI_KEYMAP_NO_MEMORY &&
          matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event, 1U,
          "cursor.first", false),
          "failed rebind allocation preserves the old action");
      cwiki_keymap_test_reset_allocation();
      cwiki_keymap_free(keymap);
      cwiki_action_registry_free(actions);
   }
   {
      struct cwiki_action_registry *actions = NULL;
      struct cwiki_keymap *keymap = (void *)(uintptr_t)1U;

      check(cwiki_action_registry_init(&actions) == CWIKI_ACTION_OK,
          "initialize actions for keymap init failure");
      cwiki_keymap_test_fail_allocation_after(0U);
      check(cwiki_keymap_init(&keymap, actions) == CWIKI_KEYMAP_NO_MEMORY &&
          keymap == NULL, "failed initialization leaves no partial keymap");
      cwiki_keymap_test_reset_allocation();
      cwiki_action_registry_free(actions);
   }
   for (failure_point = 0U; failure_point < 3U; failure_point++) {
      struct cwiki_action_registry *actions = NULL;
      struct cwiki_keymap *keymap = new_keymap(&actions);
      struct cwiki_keymap *copy = (void *)(uintptr_t)1U;
      struct cwiki_key_sequence x = sequence1('x', 0U);
      struct cwiki_input_event event = key_event('x', 0U);

      check(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x,
          "cursor.first") == CWIKI_KEYMAP_OK, "bind clone baseline");
      cwiki_keymap_test_fail_allocation_after(failure_point);
      check(cwiki_keymap_clone(&copy, keymap) == CWIKI_KEYMAP_NO_MEMORY &&
          copy == NULL && matches_action(keymap, CWIKI_KEYMAP_NORMAL, &event,
          1U, "cursor.first", false),
          "failed clone allocation publishes nothing and preserves source");
      cwiki_keymap_test_reset_allocation();
      check(cwiki_keymap_clone(&copy, keymap) == CWIKI_KEYMAP_OK &&
          matches_action(copy, CWIKI_KEYMAP_NORMAL, &event, 1U,
          "cursor.first", false), "successful clone preserves bindings");
      cwiki_keymap_free(copy);
      cwiki_keymap_free(keymap);
      cwiki_action_registry_free(actions);
   }
}

int
main(void)
{
   test_physical_identity_and_disambiguation();
   test_modes_bind_rebind_and_unbind();
   test_prefix_matching_and_clues();
   test_non_key_events_never_dispatch();
   test_invalid_and_unknown_bindings_are_transactional();
   test_allocation_failures_are_transactional();
   if (failures != 0) {
      (void)fprintf(stderr, "%d keymap test(s) failed\n", failures);
      return 1;
   }
   (void)puts("keymap tests passed");
   return 0;
}
