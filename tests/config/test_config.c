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
       "  operator:\n"
       "    - keys: [i, w]\n"
       "      action: test.two\n"
       "  picker:\n"
       "    - keys: [C-n]\n"
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
   struct cwiki_input_event inner_word[] = {event('i', 0U), event('w', 0U)};
   struct cwiki_input_event ctrl_n = event('n', CWIKI_INPUT_CTRL);
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
   assert(matches(candidate, CWIKI_KEYMAP_OPERATOR, inner_word, 2U,
       "test.two"));
   assert(matches(candidate, CWIKI_KEYMAP_PICKER, &ctrl_n, 1U, "test.one"));
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
   enum cwiki_config_status status;

   status = cwiki_config_parse(&config, (const unsigned char *)yaml,
       strlen(yaml), registry, &error);
   if (status != wanted) {
      (void)fprintf(stderr, "expected status %d, got %d for:\n%s", wanted,
          status, yaml);
   }
   assert(status == wanted);
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
   expect_error("modules: false\n", CWIKI_CONFIG_SCHEMA_ERROR, "mapping");
   expect_error("modules:\n  snippets: 'false'\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "true or false");
   expect_error("modules:\n  unknown: false\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "unknown configuration key");
   expect_error("clue-groups:\n  normal:\n    - prefix: [g]\n      label: ''\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "non-empty UTF-8");
   expect_error("clue-groups:\n  normal:\n    - prefix: [g]\n      label: Go\n"
       "    - prefix: [g]\n      label: Again\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "duplicate clue group");
   expect_error("display: false\n", CWIKI_CONFIG_SCHEMA_ERROR, "mapping");
   expect_error("display:\n  conceal: 'true'\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "true or false");
   expect_error("display:\n  conceal-categories: [greek, greek]\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "duplicate conceal category");
   expect_error("display:\n  conceal-categories: [unknown]\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "unknown conceal category");
   expect_error("display:\n  conceal-cursor: nn\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "duplicate mode");
   expect_error("display:\n  conceal-cursor: nx\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "only n, i, v and c");
   expect_error("display:\n  continuation-marker: \"\\t\"\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "display text");
   expect_error("indentation:\n  shiftwidth: 0\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "integer from 1 to 16");
   expect_error("indentation:\n  tabstop: 17\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "integer from 1 to 16");
   expect_error("indentation:\n  tabstop: '3'\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "integer from 1 to 16");
   expect_error("save-policy: often\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "insert-leave, manual or idle");
   expect_error("snippets: []\n", CWIKI_CONFIG_SCHEMA_ERROR, "mapping");
   expect_error("snippets:\n  Bad Name: null\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "lowercase dot-separated");
   expect_error("snippets:\n  custom.x:\n    trigger: x\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "bodies are required");
   expect_error("snippets:\n  custom.x:\n    trigger: x\n"
       "    kind: guess\n    bodies: {prose: '$0'}\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "literal or regex");
   expect_error("snippets:\n  custom.x:\n    trigger: x\n"
       "    expand: [auto, auto]\n    bodies: {prose: '$0'}\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "duplicate snippet expand");
   expect_error("snippets:\n  custom.x:\n    trigger: x\n"
       "    bodies: {unknown: '$0'}\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "unknown snippet body zone");
   expect_error("snippets:\n  custom.x:\n    trigger: '[invalid'\n"
       "    kind: regex\n    bodies: {prose: '$0'}\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "regex failed to compile");
   expect_error("zones: []\n", CWIKI_CONFIG_SCHEMA_ERROR, "mapping");
   expect_error("zones:\n  code-fence: null\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "builtin zones cannot");
   expect_error("zones:\n  custom.x:\n    kind: latex\n    start: X\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "region pattern is required");
   expect_error("zones:\n  custom.x:\n    kind: latex\n"
       "    start: '[bad'\n    end: X\n", CWIKI_CONFIG_SCHEMA_ERROR,
       "pattern or detail capture");
   expect_error("zones:\n  custom.x:\n    kind: latex\n"
       "    start: '(X)'\n    end: Y\n    start-detail-capture: 2\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "pattern or detail capture");
   expect_error("zones:\n  custom.x:\n    kind: comment-percent\n"
       "    start: X\n    end: Y\n    ends-at-line: true\n",
       CWIKI_CONFIG_SCHEMA_ERROR, "cannot define an end");
}

static void
zone_settings_and_precedence(void)
{
   static const char global[] =
       "zones:\n"
       "  custom.outer:\n"
       "    kind: code\n"
       "    start: 'OLD ([a-z]+)'\n"
       "    end: 'STOP ([a-z]+)'\n"
       "    start-detail-capture: 1\n"
       "    end-detail-capture: 1\n"
       "  custom.disabled:\n"
       "    kind: text\n"
       "    start: OPEN\n"
       "    end: CLOSE\n";
   static const char local[] =
       "zones:\n"
       "  custom.outer:\n"
       "    kind: latex\n"
       "    start: 'BEGIN ([a-z]+)'\n"
       "    end: 'END ([a-z]+)'\n"
       "    start-detail-capture: 1\n"
       "    end-detail-capture: 1\n"
       "    contains: [note-inline-comment]\n"
       "  custom.inner:\n"
       "    kind: chemistry\n"
       "    start: '\\['\n"
       "    end: '\\]'\n"
       "    top-level: false\n"
       "    parents: [custom.outer]\n"
       "  custom.disabled: null\n";
   static const char bad[] =
       "zones:\n"
       "  custom.bad:\n"
       "    kind: custom\n"
       "    start: A\n"
       "    end: B\n"
       "    parents: [missing.parent]\n";
   struct cwiki_action_registry *registry = actions();
   struct cwiki_config *first = NULL;
   struct cwiki_config *second = NULL;
   struct cwiki_config *invalid = NULL;
   const struct cwiki_config *configs[2];
   struct cwiki_config_zone_table table = {0};
   struct cwiki_zone_engine *zones = NULL;
   struct cwiki_buffer buffer;
   struct cwiki_zone zone;
   struct cwiki_config_error error = {0};
   const char text[] = "BEGIN python\n[H2O]\nEND python";

   assert(cwiki_config_parse(&first, (const unsigned char *)global,
       sizeof(global) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   assert(cwiki_config_parse(&second, (const unsigned char *)local,
       sizeof(local) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   configs[0] = first;
   configs[1] = second;
   assert(cwiki_config_build_zones(configs, 2U, &table, &error) ==
       CWIKI_CONFIG_OK);
   {
      size_t builtin_count;

      (void)cwiki_zone_builtin_regions(&builtin_count, NULL);
      assert(table.count == builtin_count + 2U);
   }
   assert(cwiki_zone_engine_init(&zones, table.regions, table.count,
       table.top_level) == 0);
   assert(cwiki_buffer_init(&buffer) == 0 &&
       cwiki_buffer_load(&buffer, text, sizeof(text) - 1U) == 0 &&
       cwiki_zone_recompute(zones, &buffer, 0U, NULL) == 0);
   assert(cwiki_zone_at(zones, &buffer, 0U, buffer.lines[0].length, &zone) == 0 &&
       zone.kind == CWIKI_ZONE_LATEX &&
       strcmp(cwiki_zone_detail(zones, zone.detail), "python") == 0);
   assert(cwiki_zone_at(zones, &buffer, 1U, 2U, &zone) == 0 &&
       zone.kind == CWIKI_ZONE_CHEMISTRY);
   assert(cwiki_zone_at(zones, &buffer, 2U, buffer.lines[2].length, &zone) == 0 &&
       zone.kind == CWIKI_ZONE_PROSE);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(zones);
   cwiki_config_zone_table_free(&table);

   assert(cwiki_config_parse(&invalid, (const unsigned char *)bad,
       sizeof(bad) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   configs[0] = invalid;
   assert(cwiki_config_build_zones(configs, 1U, &table, &error) ==
       CWIKI_CONFIG_SCHEMA_ERROR && table.regions == NULL &&
       strstr(error.message, "unknown parent") != NULL);
   cwiki_config_free(invalid);
   cwiki_config_free(second);
   cwiki_config_free(first);
   cwiki_action_registry_free(registry);
}

static void
note_subject_frontmatter(void)
{
   struct cwiki_buffer buffer;
   struct cwiki_config_subjects subjects = {0};
   struct cwiki_config_error error = {0};
   static const char valid[] =
       "---\n"
       "title: Shared topic\n"
       "subject: [calculus, physics]\n"
       "---\n"
       "Body\n";

   assert(cwiki_buffer_init(&buffer) == 0);
   assert(cwiki_buffer_load(&buffer, valid, sizeof(valid) - 1U) == 0);
   assert(cwiki_config_parse_note_subjects(&buffer, &subjects, &error) ==
       CWIKI_CONFIG_OK && subjects.count == 2U &&
       strcmp(subjects.items[0].value, "calculus") == 0 &&
       strcmp(subjects.items[1].value, "physics") == 0);
   cwiki_config_subjects_free(&subjects);
   cwiki_buffer_free(&buffer);

   assert(cwiki_buffer_init(&buffer) == 0);
   assert(cwiki_buffer_load(&buffer, "subject: [ignored]\n",
       strlen("subject: [ignored]\n")) == 0);
   assert(cwiki_config_parse_note_subjects(&buffer, &subjects, &error) ==
       CWIKI_CONFIG_OK && subjects.count == 0U);
   cwiki_buffer_free(&buffer);

   assert(cwiki_buffer_init(&buffer) == 0);
   assert(cwiki_buffer_load(&buffer, "---\nsubject: physics\n---\n",
       strlen("---\nsubject: physics\n---\n")) == 0);
   assert(cwiki_config_parse_note_subjects(&buffer, &subjects, &error) ==
       CWIKI_CONFIG_SCHEMA_ERROR && error.line == 2U &&
       strstr(error.message, "sequence") != NULL);
   cwiki_buffer_free(&buffer);

   assert(cwiki_buffer_init(&buffer) == 0);
   assert(cwiki_buffer_load(&buffer, "---\nsubject: [physics]\n",
       strlen("---\nsubject: [physics]\n")) == 0);
   assert(cwiki_config_parse_note_subjects(&buffer, &subjects, &error) ==
       CWIKI_CONFIG_SCHEMA_ERROR &&
       strstr(error.message, "closing delimiter") != NULL);
   cwiki_buffer_free(&buffer);
}

static void
parse_tex(const char *text, struct cwiki_config_note_tex *tex,
    struct cwiki_config_error *error, enum cwiki_config_status expected)
{
   struct cwiki_buffer buffer;

   assert(cwiki_buffer_init(&buffer) == 0);
   assert(cwiki_buffer_load(&buffer, text, strlen(text)) == 0);
   assert(cwiki_config_parse_note_tex(&buffer, tex, error) == expected);
   cwiki_buffer_free(&buffer);
}

static void
test_environments(void)
{
   struct cwiki_action_registry *registry = actions();
   struct cwiki_config *a = NULL;
   struct cwiki_config *b = NULL;
   struct cwiki_config *bad = NULL;
   const struct cwiki_config *configs[2];
   struct cwiki_config_environment_table table;
   struct cwiki_config_error error = {0};
   static const char global[] =
       "environments:\n  tikz-cd: [tikzcd]\n  siunitx: [unit, 'unit*']\n"
       "  gone: [x]\n";
   static const char local[] =
       "environments:\n  siunitx: [quantity]\n  gone: null\n";
   static const char *const invalid[] = {
      "environments: [a]\n",
      "environments:\n  p: x\n",
      "environments:\n  p: [[a]]\n",
      "environments:\n  p: [\"a b\"]\n",
      "environments:\n  \"p q\": [a]\n",
      "environments:\n  p: [a]\n  p: [b]\n",
   };
   size_t i;

   assert(cwiki_config_parse(&a, (const unsigned char *)global,
       sizeof(global) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   assert(cwiki_config_parse(&b, (const unsigned char *)local,
       sizeof(local) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   configs[0] = a;
   assert(cwiki_config_build_environments(configs, 1U, &table, &error) ==
       CWIKI_CONFIG_OK && table.count == 3U);
   cwiki_config_environment_table_free(&table);
   configs[1] = b;
   assert(cwiki_config_build_environments(configs, 2U, &table, &error) ==
       CWIKI_CONFIG_OK && table.count == 2U);
   assert(strcmp(table.entries[0].package, "tikz-cd") == 0 &&
       table.entries[0].name_count == 1U &&
       strcmp(table.entries[0].names[0], "tikzcd") == 0);
   assert(strcmp(table.entries[1].package, "siunitx") == 0 &&
       table.entries[1].name_count == 1U &&
       strcmp(table.entries[1].names[0], "quantity") == 0);
   cwiki_config_environment_table_free(&table);
   cwiki_config_free(a);
   cwiki_config_free(b);
   for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
      assert(cwiki_config_parse(&bad, (const unsigned char *)invalid[i],
          strlen(invalid[i]), registry, &error) == CWIKI_CONFIG_SCHEMA_ERROR);
      assert(bad == NULL && error.line >= 1U && error.column >= 1U);
   }
   cwiki_action_registry_free(registry);
}

static void
test_note_tex(void)
{
   struct cwiki_config_note_tex tex;
   struct cwiki_config_error error;

   parse_tex("body\n", &tex, &error, CWIKI_CONFIG_OK);
   assert(tex.engine == CWIKI_TEX_ENGINE_LUALATEX && tex.package_count == 0U &&
       tex.preamble == NULL && tex.body_first_line == 0U);
   cwiki_config_note_tex_free(&tex);

   parse_tex("---\ntitle: x\n---\nbody\n", &tex, &error, CWIKI_CONFIG_OK);
   assert(tex.engine == CWIKI_TEX_ENGINE_LUALATEX && tex.package_count == 0U &&
       tex.preamble == NULL && tex.body_first_line == 3U);
   cwiki_config_note_tex_free(&tex);

   parse_tex("---\nengine: pdflatex\n---\n", &tex, &error, CWIKI_CONFIG_OK);
   assert(tex.engine == CWIKI_TEX_ENGINE_PDFLATEX);
   cwiki_config_note_tex_free(&tex);
   parse_tex("---\nengine: lualatex\n---\n", &tex, &error, CWIKI_CONFIG_OK);
   assert(tex.engine == CWIKI_TEX_ENGINE_LUALATEX);
   cwiki_config_note_tex_free(&tex);
   parse_tex("---\ntitle: x\nengine: LuaTeX\n---\n", &tex, &error,
       CWIKI_CONFIG_SCHEMA_ERROR);
   assert(error.line == 3U && error.column == 9U);

   parse_tex("---\npackages: [tikz-cd, siunitx]\n---\n", &tex, &error,
       CWIKI_CONFIG_OK);
   assert(tex.package_count == 2U && strcmp(tex.packages[0], "tikz-cd") == 0 &&
       strcmp(tex.packages[1], "siunitx") == 0 && tex.package_lines[1] == 2U);
   cwiki_config_note_tex_free(&tex);
   parse_tex("---\npackages: \"x\"\n---\n", &tex, &error,
       CWIKI_CONFIG_SCHEMA_ERROR);
   assert(error.line == 2U);
   parse_tex("---\npackages: [\"a b\"]\n---\n", &tex, &error,
       CWIKI_CONFIG_SCHEMA_ERROR);
   assert(error.line == 2U);

   parse_tex("---\npreamble: [x]\n---\n", &tex, &error,
       CWIKI_CONFIG_SCHEMA_ERROR);
   assert(error.line == 2U);
   {
      static const char *const rejected[] = {
         "---\npreamble: null\n---\n", "---\npreamble: ~\n---\n",
         "---\npreamble:\n---\n", "---\npreamble: !!int 42\n---\n",
         "---\npreamble: 42\n---\n", "---\npreamble: -1.5e3\n---\n",
         "---\npreamble: true\n---\n", "---\npreamble: .inf\n---\n",
         "---\npreamble: !!null x\n---\n"
      };
      static const char *const accepted[] = {
         "---\npreamble: \"42\"\n---\n",
         "---\npreamble: '\\\\newcommand{\\\\x}{1}'\n---\n",
         "---\npreamble: \\\\def\\\\y{2}\n---\n"
      };
      size_t i;

      for (i = 0U; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
         parse_tex(rejected[i], &tex, &error, CWIKI_CONFIG_SCHEMA_ERROR);
         /* An empty value is marked where it would start, after the colon. */
         assert(error.line == 2U &&
             (error.column == 11U || error.column == 10U));
      }
      for (i = 0U; i < sizeof(accepted) / sizeof(accepted[0]); i++) {
         parse_tex(accepted[i], &tex, &error, CWIKI_CONFIG_OK);
         cwiki_config_note_tex_free(&tex);
      }
   }
   parse_tex("---\npreamble: |\n  \\newcommand{\\R}{\\mathbb{R}}\n"
       "  \\usetikzlibrary{arrows.meta}\n---\nbody\n", &tex, &error,
       CWIKI_CONFIG_OK);
   assert(tex.preamble_length ==
       strlen("\\newcommand{\\R}{\\mathbb{R}}\n\\usetikzlibrary{arrows.meta}\n") &&
       strcmp(tex.preamble,
       "\\newcommand{\\R}{\\mathbb{R}}\n\\usetikzlibrary{arrows.meta}\n") == 0 &&
       tex.body_first_line == 5U);
   cwiki_config_note_tex_free(&tex);

   parse_tex("---\nengine: pdflatex\nenvironments:\n  x: [y]\n---\n", &tex,
       &error, CWIKI_CONFIG_SCHEMA_ERROR);
   assert(error.line == 3U && error.column == 1U);
}

static bool
snippet_matches(struct cwiki_snippet_registry *registry, const char *text,
    enum cwiki_snippet_expand_kind kind)
{
   const struct cwiki_zone_region *regions;
   struct cwiki_zone_engine *zones = NULL;
   struct cwiki_buffer buffer;
   struct cwiki_snippet_match match = {0};
   struct cwiki_position cursor;
   enum cwiki_snippet_status status;
   uint64_t top_level;
   size_t count;

   regions = cwiki_zone_builtin_regions(&count, &top_level);
   assert(cwiki_zone_engine_init(&zones, regions, count, top_level) == 0);
   assert(cwiki_buffer_init(&buffer) == 0);
   assert(cwiki_buffer_load(&buffer, text, strlen(text)) == 0);
   assert(cwiki_zone_recompute(zones, &buffer, 0U, NULL) == 0);
   cursor.line = buffer.line_count - 1U;
   cursor.byte = buffer.lines[cursor.line].length;
   status = cwiki_snippet_match(registry, zones, &buffer, cursor, kind,
       CWIKI_SNIPPET_INPUT_NONE, &match);
   cwiki_snippet_match_free(&match);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(zones);
   return status == CWIKI_SNIPPET_OK;
}

static void
snippet_settings_and_precedence(void)
{
   static const char global[] =
       "snippets:\n"
       "  custom.greet:\n"
       "    trigger: qq\n"
       "    expand: [explicit]\n"
       "    word-boundary: true\n"
       "    priority: 7\n"
       "    bodies: {prose: 'GLOBAL$0'}\n"
       "  vimtex.alpha: null\n"
       "  custom.subject:\n"
       "    trigger: sub\n"
       "    subject: chemistry\n"
       "    bodies: {prose: 'SUBJECT$0'}\n";
   static const char local[] =
       "snippets:\n"
       "  custom.greet:\n"
       "    trigger: zz\n"
       "    beginning-of-line: true\n"
       "    bodies: {prose: 'LOCAL$0'}\n"
       "  math.square:\n"
       "    trigger: sq\n"
       "    expand: [auto, explicit]\n"
       "    bodies:\n"
       "      math-inline: '^2$0'\n"
       "      math-display: '^2$0'\n"
       "  custom.regex:\n"
       "    trigger: '(x+)z'\n"
       "    kind: regex\n"
       "    bodies: {prose: '${capture:1}$0'}\n";
   struct cwiki_action_registry *registry = actions();
   struct cwiki_config *first = NULL;
   struct cwiki_config *second = NULL;
   const struct cwiki_config *configs[2];
   struct cwiki_snippet_registry *snippets = NULL;
   struct cwiki_config_error error = {0};

   assert(cwiki_config_parse(&first, (const unsigned char *)global,
       sizeof(global) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   assert(cwiki_config_parse(&second, (const unsigned char *)local,
       sizeof(local) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   configs[0] = first;
   configs[1] = second;
   assert(cwiki_config_build_snippets(configs, 2U, &snippets, &error) ==
       CWIKI_CONFIG_OK);
   assert(!snippet_matches(snippets, "qq", CWIKI_SNIPPET_EXPLICIT));
   assert(snippet_matches(snippets, "zz", CWIKI_SNIPPET_EXPLICIT));
   assert(!snippet_matches(snippets, "$`a", CWIKI_SNIPPET_EXPLICIT));
   assert(snippet_matches(snippets, "$//", CWIKI_SNIPPET_EXPLICIT));
   assert(!snippet_matches(snippets, "$sr", CWIKI_SNIPPET_EXPLICIT));
   assert(snippet_matches(snippets, "$sq", CWIKI_SNIPPET_AUTO));
   assert(snippet_matches(snippets, "xxxz", CWIKI_SNIPPET_EXPLICIT));
   assert(!snippet_matches(snippets, "sub", CWIKI_SNIPPET_EXPLICIT));
   cwiki_snippet_registry_free(snippets);
   cwiki_config_free(second);
   cwiki_config_free(first);
   cwiki_action_registry_free(registry);
}

static void
scalar_settings_and_precedence(void)
{
   static const char global[] =
       "modules:\n"
       "  syntax: false\n"
       "  snippets: false\n"
       "  clues: false\n"
       "  picker: false\n"
       "display:\n"
       "  conceal: false\n"
       "  conceal-categories: [greek, sections]\n"
       "  conceal-cursor: iv\n"
       "  wrap: false\n"
       "  break-indent: false\n"
       "  line-numbers: false\n"
       "  cursorline: false\n"
       "  continuation-marker: '↪ '\n"
       "indentation:\n"
       "  shiftwidth: 4\n"
       "  tabstop: 5\n"
       "  expandtab: false\n"
       "save-policy: manual\n";
   static const char local[] =
       "modules:\n"
       "  syntax: true\n"
       "  picker: true\n"
       "display:\n"
       "  conceal: true\n"
       "  wrap: true\n"
       "  line-numbers: true\n"
       "indentation:\n"
       "  tabstop: 3\n"
       "  expandtab: true\n"
       "save-policy: idle\n";
   struct cwiki_action_registry *registry = actions();
   struct cwiki_config *first = NULL;
   struct cwiki_config *second = NULL;
   struct cwiki_config_error error = {0};
   struct cwiki_config_settings settings;

   cwiki_config_settings_defaults(&settings);
   assert(settings.module_syntax && settings.module_snippets &&
       settings.module_clues && settings.module_picker &&
       settings.conceal && settings.wrap && settings.break_indent &&
       settings.line_numbers && settings.cursorline &&
       settings.conceal_categories == CWIKI_CONCEAL_DEFAULT_MASK &&
       settings.concealcursor_modes == CWIKI_CONCEALCURSOR_DEFAULT &&
       strcmp(settings.continuation_marker, "") == 0 &&
       settings.shiftwidth == 3U && settings.tabstop == 3U &&
       settings.expandtab &&
       settings.save_policy == CWIKI_SAVE_INSERT_LEAVE);
   assert(cwiki_config_parse(&first, (const unsigned char *)global,
       sizeof(global) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   assert(cwiki_config_parse(&second, (const unsigned char *)local,
       sizeof(local) - 1U, registry, &error) == CWIKI_CONFIG_OK);
   cwiki_config_apply_settings(first, &settings);
   assert(!settings.module_syntax && !settings.module_snippets &&
       !settings.module_clues && !settings.module_picker &&
       !settings.conceal && !settings.wrap && !settings.break_indent &&
       !settings.line_numbers && !settings.cursorline &&
       settings.conceal_categories ==
       (CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_GREEK) |
       CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_SECTIONS)) &&
       settings.concealcursor_modes ==
       (CWIKI_CONCEAL_MODE_BIT(CWIKI_CONCEAL_MODE_INSERT) |
       CWIKI_CONCEAL_MODE_BIT(CWIKI_CONCEAL_MODE_VISUAL)) &&
       strcmp(settings.continuation_marker, "↪ ") == 0 &&
       settings.shiftwidth == 4U && settings.tabstop == 5U &&
       !settings.expandtab &&
       settings.save_policy == CWIKI_SAVE_MANUAL);
   cwiki_config_apply_settings(second, &settings);
   assert(settings.module_syntax && !settings.module_snippets &&
       !settings.module_clues && settings.module_picker &&
       settings.conceal && settings.wrap && !settings.break_indent &&
       settings.line_numbers && !settings.cursorline &&
       strcmp(settings.continuation_marker, "↪ ") == 0 &&
       settings.shiftwidth == 4U && settings.tabstop == 3U &&
       settings.expandtab &&
       settings.save_policy == CWIKI_SAVE_IDLE);
   cwiki_config_free(second);
   cwiki_config_free(first);
   cwiki_action_registry_free(registry);
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
   scalar_settings_and_precedence();
   snippet_settings_and_precedence();
   zone_settings_and_precedence();
   note_subject_frontmatter();
   test_note_tex();
   test_environments();
   bounded_input();
   (void)puts("config tests: ok");
   return EXIT_SUCCESS;
}
