#include "action.h"
#include "config.h"
#include "input.h"
#include "keymap.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
noop(void *context, const struct cwiki_action_argument *argument)
{
   (void)context;
   (void)argument;
   return 0;
}

static void
register_action(struct cwiki_action_registry *actions, const char *name)
{
   const struct cwiki_action_spec spec = {
      name, name, "configuration test action", CWIKI_ACTION_PARAMETER_NONE,
      NULL, noop, NULL, NULL
   };

   assert(cwiki_action_register(actions, &spec) == CWIKI_ACTION_OK);
}

static struct cwiki_key_sequence
sequence1(uint32_t key, unsigned int modifiers)
{
   struct cwiki_key_sequence sequence = {0};

   sequence.keys[0] = (struct cwiki_key){key, modifiers};
   sequence.length = 1U;
   return sequence;
}

static struct cwiki_input_event
event(uint32_t key, unsigned int modifiers)
{
   struct cwiki_input_event result = {0};

   result.kind = CWIKI_INPUT_KEY;
   result.key = key;
   result.modifiers = modifiers;
   result.action = CWIKI_INPUT_PRESS;
   return result;
}

static bool
matches(const struct cwiki_keymap *keymap, enum cwiki_keymap_mode mode,
    const struct cwiki_input_event *events, size_t count, const char *action)
{
   struct cwiki_keymap_match match;

   return cwiki_keymap_match(keymap, mode, events, count, &match) ==
       CWIKI_KEYMAP_OK && match.kind == CWIKI_KEYMAP_COMPLETE &&
       strcmp(match.action.name, action) == 0;
}

static struct cwiki_action_registry *
actions(void)
{
   struct cwiki_action_registry *registry = NULL;

   assert(cwiki_action_registry_init(&registry) == CWIKI_ACTION_OK);
   register_action(registry, "test.one");
   register_action(registry, "test.two");
   return registry;
}

static struct cwiki_keymap *
base_keymap(struct cwiki_action_registry *registry)
{
   struct cwiki_keymap *keymap = NULL;
   struct cwiki_key_sequence x = sequence1('x', 0U);
   struct cwiki_key_sequence y = sequence1('y', 0U);

   assert(cwiki_keymap_init(&keymap, registry) == CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &x, "test.one") ==
       CWIKI_KEYMAP_OK);
   assert(cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &y, "test.two") ==
       CWIKI_KEYMAP_OK);
   return keymap;
}

static void
valid_transaction(void)
{
   static const char yaml[] =
       "keymaps:\n"
       "  normal:\n"
       "    - keys: [x]\n"
       "      action: test.two\n"
       "    - keys: [y]\n"
       "      action: null\n"
       "    - keys: [g, g]\n"
       "      action: test.one\n"
       "    - keys: [C-r]\n"
       "      action: test.two\n"
       "  insert:\n"
       "    - keys: [U+03B1]\n"
       "      action: test.one\n"
       "clue-groups:\n"
       "  normal:\n"
       "    - prefix: [g]\n"
       "      label: Go / history\n";
   struct cwiki_action_registry *registry = actions();
   struct cwiki_keymap *base = base_keymap(registry);
   struct cwiki_keymap *candidate = NULL;
   struct cwiki_config *config = NULL;
   struct cwiki_config_error error = {0};
   struct cwiki_input_event x = event('x', 0U);
   struct cwiki_input_event y = event('y', 0U);
   struct cwiki_input_event gg[] = {event('g', 0U), event('g', 0U)};
   struct cwiki_input_event ctrl_r = event('r', CWIKI_INPUT_CTRL);
   struct cwiki_input_event alpha = event(0x03b1U, 0U);
   struct cwiki_input_event physical_g = event('x', 0U);
   struct cwiki_keymap_match match;

   {
      enum cwiki_config_status status = cwiki_config_parse(&config,
          (const unsigned char *)yaml, sizeof(yaml) - 1U, registry, &error);
      if (status != CWIKI_CONFIG_OK) {
         (void)fprintf(stderr, "config error %d at %zu:%zu: %s\n", status,
             error.line, error.column, error.message);
      }
      assert(status == CWIKI_CONFIG_OK);
   }
   assert(cwiki_config_build_keymap(config, base, &candidate, &error) ==
       CWIKI_CONFIG_OK);
   assert(matches(candidate, CWIKI_KEYMAP_NORMAL, &x, 1U, "test.two"));
   assert(cwiki_keymap_match(candidate, CWIKI_KEYMAP_NORMAL, &y, 1U, &match) ==
       CWIKI_KEYMAP_OK && match.kind == CWIKI_KEYMAP_NO_MATCH);
   assert(matches(candidate, CWIKI_KEYMAP_NORMAL, gg, 2U, "test.one"));
   assert(matches(candidate, CWIKI_KEYMAP_NORMAL, &ctrl_r, 1U, "test.two"));
   assert(matches(candidate, CWIKI_KEYMAP_INSERT, &alpha, 1U, "test.one"));
   assert(matches(base, CWIKI_KEYMAP_NORMAL, &x, 1U, "test.one"));
   assert(matches(base, CWIKI_KEYMAP_NORMAL, &y, 1U, "test.two"));
   physical_g.has_base_layout_key = true;
   physical_g.base_layout_key = 'g';
   assert(strcmp(cwiki_config_clue_group(config, CWIKI_KEYMAP_NORMAL,
       &physical_g, 1U), "Go / history") == 0);
   assert(cwiki_config_clue_group(config, CWIKI_KEYMAP_INSERT, &physical_g,
       1U) == NULL);
   cwiki_keymap_free(candidate);
   cwiki_config_free(config);
   cwiki_keymap_free(base);
   cwiki_action_registry_free(registry);
}

static void
transactional_failure(void)
{
   static const char yaml[] =
       "keymaps:\n  normal:\n    - keys: [z]\n      action: null\n";
   struct cwiki_action_registry *registry = actions();
   struct cwiki_keymap *base = base_keymap(registry);
   struct cwiki_keymap *candidate = (struct cwiki_keymap *)(uintptr_t)1U;
   struct cwiki_config *config = NULL;
   struct cwiki_config_error error = {0};
   struct cwiki_input_event x = event('x', 0U);

   assert(cwiki_config_parse(&config, (const unsigned char *)yaml,
       sizeof(yaml) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   assert(cwiki_config_build_keymap(config, base, &candidate, &error) ==
       CWIKI_CONFIG_SCHEMA_ERROR);
   assert(candidate == NULL && error.line == 3U && error.column == 7U);
   assert(strstr(error.message, "missing") != NULL);
   assert(matches(base, CWIKI_KEYMAP_NORMAL, &x, 1U, "test.one"));
   cwiki_config_free(config);
   cwiki_keymap_free(base);
   cwiki_action_registry_free(registry);
}

static void
expect_error(const char *yaml, enum cwiki_config_status wanted,
    const char *message)
{
   struct cwiki_action_registry *registry = actions();
   struct cwiki_config *config = (struct cwiki_config *)(uintptr_t)1U;
   struct cwiki_config_error error = {0};

   assert(cwiki_config_parse(&config, (const unsigned char *)yaml, strlen(yaml),
       registry, &error) == wanted);
   assert(config == NULL && error.line != 0U && error.column != 0U);
   if (strstr(error.message, message) == NULL) {
      (void)fprintf(stderr, "expected [%s], got [%s] for:\n%s", message,
          error.message, yaml);
   }
   assert(strstr(error.message, message) != NULL);
   cwiki_action_registry_free(registry);
}

static void
strict_schema_errors(void)
{
   expect_error("keymaps: [", CWIKI_CONFIG_YAML_ERROR, "expected");
   expect_error("---\n{}\n---\n{}\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "one YAML document");
   expect_error("unknown: true\n", CWIKI_CONFIG_SCHEMA_ERROR, "unknown");
   expect_error("keymaps: {}\nkeymaps: {}\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "duplicate mapping key");
   expect_error("keymaps:\n  normal: {}\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "binding sequence");
   expect_error("keymaps:\n  normal:\n    - keys: []\n      action: test.one\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "non-empty");
   expect_error("keymaps:\n  normal:\n    - keys: [C-C-x]\n      action: test.one\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "physical key");
   expect_error("keymaps:\n  normal:\n    - keys: [x]\n      action: absent\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "unknown action");
   expect_error("keymaps:\n  normal:\n    - keys: [x]\n      action: 'null'\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "unknown action");
   expect_error("keymaps:\n  normal:\n    - keys: [x]\n      action: test.one\n"
       "    - keys: [x]\n      action: test.two\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "duplicate key binding");
   expect_error("clue-groups:\n  normal:\n    - prefix: [g]\n      label: ''\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "non-empty UTF-8");
   expect_error("clue-groups:\n  normal:\n    - prefix: [g]\n      label: Go\n"
       "    - prefix: [g]\n      label: Again\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "duplicate clue group");
}

static void
bounded_input(void)
{
   struct cwiki_action_registry *registry = actions();
   struct cwiki_config *config = (struct cwiki_config *)(uintptr_t)1U;
   struct cwiki_config_error error = {0};
   unsigned char byte = 0U;

   assert(cwiki_config_parse(&config, &byte, 1024U * 1024U + 1U, registry,
       &error) == CWIKI_CONFIG_SCHEMA_ERROR);
   assert(config == NULL && error.line == 1U && error.column == 1U &&
       strstr(error.message, "1 MiB") != NULL);
   cwiki_action_registry_free(registry);
}

int
main(void)
{
   valid_transaction();
   transactional_failure();
   strict_schema_errors();
   bounded_input();
   (void)puts("config tests: ok");
   return EXIT_SUCCESS;
}
