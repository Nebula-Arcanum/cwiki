#include "action.h"
#include "clue_render.h"
#include "float.h"
#include "input.h"
#include "keymap.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIDE_SAVE "\x1b[?25l\x1b[s"
#define RESTORE_SHOW "\x1b[u\x1b[?25h"
#define RESET "\x1b[0m"
#define DEFAULT "\x1b[0;39m"
#define DIM "\x1b[0;90m"
#define CYAN "\x1b[0;36m"

static size_t availability_calls;

static int
noop(void *context, const struct cwiki_action_argument *argument)
{
   (void)context;
   (void)argument;
   return 0;
}

static bool
unavailable(void *context)
{
   (void)context;
   availability_calls++;
   return false;
}

static void
register_action(struct cwiki_action_registry *actions, const char *name,
    const char *label, cwiki_action_available available)
{
   const struct cwiki_action_spec spec = {
      name, label, "clue test action", CWIKI_ACTION_PARAMETER_NONE, NULL,
      noop, available, NULL
   };

   assert(cwiki_action_register(actions, &spec) == CWIKI_ACTION_OK);
}

static struct cwiki_key_sequence
sequence2(uint32_t first, uint32_t second, unsigned int modifiers)
{
   struct cwiki_key_sequence sequence = {0};

   sequence.keys[0].key = first;
   sequence.keys[1].key = second;
   sequence.keys[1].modifiers = modifiers;
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
event(uint32_t key)
{
   struct cwiki_input_event input = {0};

   input.kind = CWIKI_INPUT_KEY;
   input.key = key;
   input.action = CWIKI_INPUT_PRESS;
   return input;
}

static void
expect_overlay(const struct cwiki_keymap *keymap,
    const struct cwiki_action_registry *actions,
    const struct cwiki_input_event *prefix, size_t prefix_count,
    const struct cwiki_float_area *area, enum cwiki_float_border border,
    const char *expected)
{
   char output[4096];
   char before[4096];
   size_t needed = 0U;
   size_t length = 777U;
   size_t capacity;

   assert(cwiki_clue_render_overlay(keymap, actions, CWIKI_KEYMAP_NORMAL,
       prefix, prefix_count, area, border, NULL, NULL, 0U, &needed) == 0);
   assert(needed == strlen(expected));
   (void)memset(output, '!', sizeof(output));
   (void)memcpy(before, output, sizeof(output));
   for (capacity = 0U; capacity <= needed; capacity++) {
      assert(cwiki_clue_render_overlay(keymap, actions, CWIKI_KEYMAP_NORMAL,
          prefix, prefix_count, area, border, NULL, output, capacity, &length) ==
          -1 && errno == ENOSPC);
      assert(length == 777U && memcmp(output, before, sizeof(output)) == 0);
   }
   assert(cwiki_clue_render_overlay(keymap, actions, CWIKI_KEYMAP_NORMAL,
       prefix, prefix_count, area, border, NULL, output, needed + 1U,
       &length) == 0);
   assert(length == needed && strcmp(output, expected) == 0);
   assert(output[length + 1U] == '!');
}

static void
metadata_and_branches(void)
{
   struct cwiki_action_registry *actions = NULL;
   struct cwiki_keymap *keymap = NULL;
   struct cwiki_key_sequence ga = sequence2('g', 'a', 0U);
   struct cwiki_key_sequence gb = sequence2('g', 'b', CWIKI_INPUT_CTRL);
   struct cwiki_key_sequence gcc = sequence3('g', 'c', 'c');
   struct cwiki_key_sequence gd = sequence2('g', 'd', 0U);
   struct cwiki_key_sequence gde = sequence3('g', 'd', 'e');
   struct cwiki_input_event prefix = event('g');
   const struct cwiki_float_area area = {3U, 5U, 6U, 24U};
   const char *expected = HIDE_SAVE
       "\x1b[3;5H" DIM "┌──────────────────────┐" RESET
       "\x1b[4;5H" DIM "│" RESET CYAN "a  " DEFAULT
       "First action       " RESET DIM "│" RESET
       "\x1b[5;5H" DIM "│" RESET DIM "C-b  " DIM
       "Unavailable actio" RESET DIM "│" RESET
       "\x1b[6;5H" DIM "│" RESET CYAN "c  " DEFAULT
       "More…              " RESET DIM "│" RESET
       "\x1b[7;5H" DIM "│" RESET CYAN "d  " DEFAULT
       "Exact and branch … " RESET DIM "│" RESET
       "\x1b[8;5H" DIM "└──────────────────────┘" RESET RESTORE_SHOW;

   assert(cwiki_action_registry_init(&actions) == CWIKI_ACTION_OK);
   register_action(actions, "test.first", "First action", NULL);
   register_action(actions, "test.unavailable", "Unavailable action",
       unavailable);
   register_action(actions, "test.deep", "Deep action", NULL);
   register_action(actions, "test.both", "Exact and branch", NULL);
   assert(cwiki_keymap_init(&keymap, actions) == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &ga,
       "test.first") == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &gb,
       "test.unavailable") == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &gcc,
       "test.deep") == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &gd,
       "test.both") == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &gde,
       "test.deep") == CWIKI_KEYMAP_OK);
   availability_calls = 0U;
   expect_overlay(keymap, actions, &prefix, 1U, &area,
       CWIKI_FLOAT_BORDER_SINGLE, expected);
   assert(availability_calls == 1U);
   cwiki_keymap_free(keymap);
   cwiki_action_registry_free(actions);
}

static void
special_keys_empty_and_errors(void)
{
   struct cwiki_action_registry *actions = NULL;
   struct cwiki_keymap *keymap = NULL;
   struct cwiki_key_sequence tab = sequence2('z', 9U, 0U);
   struct cwiki_input_event z = event('z');
   struct cwiki_input_event x = event('x');
   struct cwiki_float_area area = {1U, 1U, 1U, 18U};
   char output[1024] = "unchanged";
   size_t length = 42U;

   assert(cwiki_action_registry_init(&actions) == CWIKI_ACTION_OK);
   register_action(actions, "test.tab", "Complete", NULL);
   assert(cwiki_keymap_init(&keymap, actions) == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &tab,
       "test.tab") == CWIKI_KEYMAP_OK);
   assert(cwiki_clue_render_overlay(keymap, actions, CWIKI_KEYMAP_NORMAL, &z,
       1U, &area, CWIKI_FLOAT_BORDER_NONE, NULL, output, sizeof(output),
       &length) == 0);
   assert(strstr(output, "Tab  ") != NULL && strstr(output, "Complete") != NULL);
   area = (struct cwiki_float_area){1U, 1U, 3U, 18U};
   assert(cwiki_clue_render_overlay(keymap, actions, CWIKI_KEYMAP_NORMAL, &z,
       1U, &area, CWIKI_FLOAT_BORDER_SINGLE, "Navigation", output,
       sizeof(output), &length) == 0);
   assert(strstr(output, "┌─ Navigation ───┐") != NULL);
   assert(cwiki_clue_render_overlay(keymap, actions, CWIKI_KEYMAP_NORMAL, &x,
       1U, &area, CWIKI_FLOAT_BORDER_NONE, NULL, output, sizeof(output),
       &length) == 0);
   assert(strstr(output, "No continuations") != NULL);
   area = (struct cwiki_float_area){1U, 1U, 2U, 4U};
   (void)strcpy(output, "unchanged");
   length = 42U;
   assert(cwiki_clue_render_overlay(keymap, actions, CWIKI_KEYMAP_NORMAL, &z,
       1U, &area, CWIKI_FLOAT_BORDER_SINGLE, NULL, output, sizeof(output),
       &length) == -1 && errno == EINVAL && strcmp(output, "unchanged") == 0 &&
       length == 42U);
   cwiki_keymap_free(keymap);
   cwiki_action_registry_free(actions);
}

static void
demo(void)
{
   struct cwiki_action_registry *actions = NULL;
   struct cwiki_keymap *keymap = NULL;
   const struct cwiki_float_options options = {
      {1U, 1U, 12U, 60U}, 6U, 32U, CWIKI_FLOAT_SOUTH_EAST, -1, -2
   };
   struct cwiki_float_area area;
   struct cwiki_key_sequence gg = sequence2('g', 'g', 0U);
   struct cwiki_key_sequence ge = sequence2('g', 'e', 0U);
   struct cwiki_key_sequence gk = sequence3('g', 'k', 'n');
   struct cwiki_input_event prefix = event('g');
   char output[4096];
   size_t length;

   assert(cwiki_action_registry_init(&actions) == CWIKI_ACTION_OK);
   register_action(actions, "cursor.document-first", "First line", NULL);
   register_action(actions, "cursor.document-last", "Last line", NULL);
   register_action(actions, "cursor.next", "Next item", NULL);
   assert(cwiki_keymap_init(&keymap, actions) == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &gg,
       "cursor.document-first") == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &ge,
       "cursor.document-last") == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &gk,
       "cursor.next") == CWIKI_KEYMAP_OK);
   assert(cwiki_float_place(&options, &area) == 0);
   (void)fputs("\x1b[2J\x1b[H\x1b[0m# Calculus notes\n\n"
       "For $f(x)=x^2$, the derivative is $2x$.\n\n"
       "Press g and pause to inspect continuations.\n", stdout);
   assert(cwiki_clue_render_overlay(keymap, actions, CWIKI_KEYMAP_NORMAL,
       &prefix, 1U, &area, CWIKI_FLOAT_BORDER_SINGLE, "Go", output,
       sizeof(output), &length) == 0);
   assert(fwrite(output, 1U, length, stdout) == length);
   cwiki_keymap_free(keymap);
   cwiki_action_registry_free(actions);
}

int
main(int argc, char **argv)
{
   if (argc == 2 && strcmp(argv[1], "--demo") == 0) {
      demo();
      return EXIT_SUCCESS;
   }
   metadata_and_branches();
   special_keys_empty_and_errors();
   (void)puts("clue render tests: ok");
   return EXIT_SUCCESS;
}
