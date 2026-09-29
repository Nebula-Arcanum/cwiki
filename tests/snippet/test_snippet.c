#include "buffer.h"
#include "snippet.h"
#include "undo.h"
#include "zone.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) { (void)fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

static void
check_transform(enum cwiki_snippet_transform transform, const char *input,
    const char *expected, const char *message)
{
   char *output = NULL;
   size_t length = 0U;
   check(cwiki_snippet_transform_apply(transform, input, strlen(input),
       &output, &length) == CWIKI_SNIPPET_OK && length == strlen(expected) &&
       memcmp(output, expected, length) == 0, message);
   free(output);
}

static struct cwiki_zone_engine *
zones(void)
{
   struct cwiki_zone_engine *engine = NULL;
   const struct cwiki_zone_region *regions;
   size_t count;
   uint64_t top;
   regions = cwiki_zone_builtin_regions(&count, &top);
   check(cwiki_zone_engine_init(&engine, regions, count, top) == 0,
       "initialize zone engine");
   return engine;
}

static void
load(struct cwiki_buffer *buffer, struct cwiki_zone_engine *engine,
    const char *text)
{
   size_t scanned;
   check(cwiki_buffer_init(buffer) == 0 &&
       cwiki_buffer_load(buffer, text, strlen(text)) == 0 &&
       cwiki_zone_recompute(engine, buffer, 0U, &scanned) == 0,
       "load and zone fixture");
}

static void
check_buffer(const struct cwiki_buffer *buffer, const char *expected,
    const char *message)
{
   char *actual = NULL;
   size_t length = 0U;
   check(cwiki_buffer_encode(buffer, &actual, &length) == 0 &&
       length == strlen(expected) && memcmp(actual, expected, length) == 0,
       message);
   free(actual);
}

static enum cwiki_snippet_status
add(struct cwiki_snippet_registry *registry,
    enum cwiki_snippet_trigger_kind kind, const char *trigger,
    const struct cwiki_snippet_body_spec *bodies, size_t body_count,
    const char *subject, uint32_t flags, int priority,
    uint32_t match_limit)
{
   struct cwiki_snippet_spec spec = {
      kind, trigger, strlen(trigger), CWIKI_ZONE_PROSE, bodies, body_count,
      subject, subject == NULL ? 0U : strlen(subject), flags, priority,
      match_limit, CWIKI_SNIPPET_DEFAULT_DEPTH_LIMIT
   };
   return cwiki_snippet_registry_add(registry, &spec);
}

static void
test_body_parser_and_transforms(void)
{
   struct cwiki_snippet_body *body = (void *)(uintptr_t)1U;
   const char invalid_utf8[] = {'$', '0', (char)0xff};

   check(cwiki_snippet_body_compile(&body,
       "$${1:élan}/$1/${stop:1|upper}/${capture:1}/${visual}$0",
       strlen("$${1:élan}/$1/${stop:1|upper}/${capture:1}/${visual}$0"),
       2U) == CWIKI_SNIPPET_OK && cwiki_snippet_body_valid(body, 2U),
       "compile every accepted body spelling and literal dollar");
   cwiki_snippet_body_free(body);
   body = (void *)(uintptr_t)1U;
   check(cwiki_snippet_body_compile(&body, "${1:a}${1:b}$0", 16U, 1U) ==
       CWIKI_SNIPPET_INVALID && body == NULL,
       "reject conflicting defaults without partial output");
   body = (void *)(uintptr_t)1U;
   check(cwiki_snippet_body_compile(&body, "${capture:2}$0", 15U, 2U) ==
       CWIKI_SNIPPET_INVALID && body == NULL,
       "reject impossible capture without partial output");
   body = (void *)(uintptr_t)1U;
   check(cwiki_snippet_body_compile(&body, "${stop:1|shell}$0", 18U, 1U) ==
       CWIKI_SNIPPET_INVALID && body == NULL, "reject unknown transform");
   body = (void *)(uintptr_t)1U;
   check(cwiki_snippet_body_compile(&body, "$1", 2U, 1U) ==
       CWIKI_SNIPPET_INVALID && body == NULL, "require final stop");
   body = (void *)(uintptr_t)1U;
   check(cwiki_snippet_body_compile(&body, invalid_utf8,
       sizeof(invalid_utf8), 1U) == CWIKI_SNIPPET_INVALID && body == NULL,
       "reject malformed UTF-8");

   check_transform(CWIKI_SNIPPET_TRANSFORM_UPPER, "élan Σ", "ÉLAN Σ",
       "upper uses deterministic Unicode simple mapping");
   check_transform(CWIKI_SNIPPET_TRANSFORM_LOWER, "ÉLAN Σ", "élan σ",
       "lower uses deterministic Unicode simple mapping");
   check_transform(CWIKI_SNIPPET_TRANSFORM_CAPITALIZE, "éLAN Σ", "Élan σ",
       "capitalize changes first and subsequent cased codepoints");
   check_transform(CWIKI_SNIPPET_TRANSFORM_MATCH_BRACKET_BACKWARD, "]", "[",
       "bracket transform maps an ASCII closer");
   check_transform(CWIKI_SNIPPET_TRANSFORM_SPACE_UNLESS_PUNCTUATION, "word", " ",
       "space transform adds ASCII space after a word");
   check_transform(CWIKI_SNIPPET_TRANSFORM_SPACE_UNLESS_PUNCTUATION, "word!", "",
       "space transform recognizes final ASCII punctuation only");
   {
      char *output = NULL;
      size_t length = 0U;
      check(cwiki_snippet_transform_apply(
          CWIKI_SNIPPET_TRANSFORM_MATCH_BRACKET_BACKWARD, "）", strlen("）"),
          &output, &length) == CWIKI_SNIPPET_INVALID && output == NULL,
          "bracket transform rejects non-ASCII boundary");
   }
}

static void
test_matching_layers_flags_and_zones(void)
{
   struct cwiki_snippet_registry *registry = NULL;
   struct cwiki_zone_engine *engine = zones();
   struct cwiki_buffer buffer;
   struct cwiki_snippet_match match = {0};
   const struct cwiki_snippet_body_spec global[] = {{CWIKI_ZONE_PROSE, "G$0", 3U}};
   const struct cwiki_snippet_body_spec subject[] = {{CWIKI_ZONE_PROSE, "S$0", 3U}};
   const struct cwiki_snippet_body_spec longest[] = {{CWIKI_ZONE_PROSE, "L$0", 3U}};
   const struct cwiki_snippet_body_spec zones_body[] = {
      {CWIKI_ZONE_PROSE, "text$0", 6U},
      {CWIKI_ZONE_MATH_INLINE, "math$0", 6U}
   };
   check(cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK,
       "initialize registry");
   check(add(registry, CWIKI_SNIPPET_LITERAL, "x", global, 1U, NULL,
       CWIKI_SNIPPET_TRIGGER_AUTO | CWIKI_SNIPPET_TRIGGER_EXPLICIT, 1, 0U) == CWIKI_SNIPPET_OK &&
       add(registry, CWIKI_SNIPPET_LITERAL, "x", subject, 1U, "calculus",
       CWIKI_SNIPPET_TRIGGER_AUTO, 1, 0U) == CWIKI_SNIPPET_OK &&
       add(registry, CWIKI_SNIPPET_LITERAL, "ax", longest, 1U, NULL,
       CWIKI_SNIPPET_TRIGGER_AUTO, 1, 0U) == CWIKI_SNIPPET_OK,
       "register global subject and longer literal competitors");
   check(cwiki_snippet_registry_set_subject(registry, "calculus", 8U) ==
       CWIKI_SNIPPET_OK, "activate subject layer");
   load(&buffer, engine, "ax");
   check(cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){0U, 2U}, CWIKI_SNIPPET_AUTO,
       CWIKI_SNIPPET_INPUT_NONE, &match) == CWIKI_SNIPPET_OK &&
       match.start_byte == 0U, "longest consumed source wins after priority");
   cwiki_snippet_match_free(&match); cwiki_buffer_free(&buffer);
   load(&buffer, engine, "x");
   check(cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){0U, 1U}, CWIKI_SNIPPET_AUTO, 0U, &match) ==
       CWIKI_SNIPPET_OK, "subject layer wins equal global match");
   cwiki_snippet_match_free(&match);
   check(cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){0U, 1U}, CWIKI_SNIPPET_AUTO,
       CWIKI_SNIPPET_INPUT_PASTE, &match) == CWIKI_SNIPPET_NO_MATCH,
       "paste suppresses all matching");
   cwiki_buffer_free(&buffer);

   check(add(registry, CWIKI_SNIPPET_LITERAL, "zz", zones_body, 2U, NULL,
       CWIKI_SNIPPET_TRIGGER_AUTO, 9, 0U) == CWIKI_SNIPPET_OK,
       "one trigger owns zone-dependent bodies");
   load(&buffer, engine, "zz $zz$\n```\nzz\n```\n%% zz %%");
   check(cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){0U, 2U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_OK &&
       match.zone == CWIKI_ZONE_PROSE, "zone-dependent trigger matches prose body");
   cwiki_snippet_match_free(&match);
   check(cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){0U, 6U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_OK &&
       match.zone == CWIKI_ZONE_MATH_INLINE, "zone-dependent trigger matches math body");
   cwiki_snippet_match_free(&match);
   check(cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){2U, 2U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_NO_MATCH,
       "code innermost zone excludes trigger");
   check(cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){4U, 5U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_NO_MATCH,
       "comment innermost zone excludes trigger");
   cwiki_buffer_free(&buffer);

   load(&buffer, engine, "$zz \\ce{zz} \\text{zz}$");
   check(cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){0U, 10U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_NO_MATCH &&
       cwiki_snippet_match(registry, engine, &buffer,
       (struct cwiki_position){0U, 20U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_NO_MATCH,
       "chemistry and text-hole innermost zones exclude math/prose bodies");
   cwiki_buffer_free(&buffer);

   {
      const struct cwiki_snippet_body_spec flags_body = {CWIKI_ZONE_PROSE, "ok$0", 4U};
      check(add(registry, CWIKI_SNIPPET_LITERAL, "bo", &flags_body, 1U, NULL,
          CWIKI_SNIPPET_TRIGGER_AUTO | CWIKI_SNIPPET_TRIGGER_BEGINNING_OF_LINE,
          20, 0U) == CWIKI_SNIPPET_OK &&
          add(registry, CWIKI_SNIPPET_LITERAL, "wb", &flags_body, 1U, NULL,
          CWIKI_SNIPPET_TRIGGER_AUTO | CWIKI_SNIPPET_TRIGGER_WORD_BOUNDARY,
          20, 0U) == CWIKI_SNIPPET_OK &&
          add(registry, CWIKI_SNIPPET_LITERAL, "ee", &flags_body, 1U, NULL,
          CWIKI_SNIPPET_TRIGGER_EXPLICIT, 20, 0U) == CWIKI_SNIPPET_OK,
          "register every trigger flag");
      load(&buffer, engine, "xbo");
      check(cwiki_snippet_match(registry, engine, &buffer,
          (struct cwiki_position){0U, 3U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_NO_MATCH,
          "beginning-of-line flag rejects nonzero source start");
      cwiki_buffer_free(&buffer); load(&buffer, engine, "awb");
      check(cwiki_snippet_match(registry, engine, &buffer,
          (struct cwiki_position){0U, 3U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_NO_MATCH,
          "word-boundary flag rejects preceding Unicode-word context");
      cwiki_buffer_free(&buffer); load(&buffer, engine, "ee");
      check(cwiki_snippet_match(registry, engine, &buffer,
          (struct cwiki_position){0U, 2U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_NO_MATCH &&
          cwiki_snippet_match(registry, engine, &buffer,
          (struct cwiki_position){0U, 2U}, CWIKI_SNIPPET_EXPLICIT, 0U, &match) == CWIKI_SNIPPET_OK,
          "explicit-only trigger is absent from automatic matching");
      cwiki_snippet_match_free(&match); cwiki_buffer_free(&buffer);
   }
   cwiki_snippet_registry_free(registry); cwiki_zone_engine_free(engine);
}

static void
test_nested_sessions_and_position_fixup(void)
{
   struct cwiki_snippet_registry *registry = NULL;
   struct cwiki_zone_engine *zone_engine = zones();
   struct cwiki_buffer buffer;
   struct cwiki_undo undo;
   struct cwiki_snippet_engine *engine = NULL;
   struct cwiki_snippet_match outer_match = {0}, inner_match = {0};
   struct cwiki_position cursor, start, end;
   const struct cwiki_snippet_body_spec outer = {CWIKI_ZONE_PROSE, "${1:in}$0", 10U};
   const struct cwiki_snippet_body_spec inner = {CWIKI_ZONE_PROSE, "[$0]", 4U};
   check(cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK &&
       add(registry, CWIKI_SNIPPET_LITERAL, "out", &outer, 1U, NULL,
       CWIKI_SNIPPET_TRIGGER_AUTO, 0, 0U) == CWIKI_SNIPPET_OK &&
       add(registry, CWIKI_SNIPPET_LITERAL, "in", &inner, 1U, NULL,
       CWIKI_SNIPPET_TRIGGER_AUTO, 0, 0U) == CWIKI_SNIPPET_OK,
       "register nested snippets");
   load(&buffer, zone_engine, "out tail");
   check(cwiki_undo_init(&undo, &buffer) == 0 &&
       cwiki_snippet_engine_init(&engine, &buffer, &undo) == CWIKI_SNIPPET_OK &&
       cwiki_snippet_match(registry, zone_engine, &buffer,
       (struct cwiki_position){0U, 3U}, CWIKI_SNIPPET_AUTO, 0U, &outer_match) == CWIKI_SNIPPET_OK &&
       cwiki_snippet_expand(engine, &outer_match, NULL, 0U, 1U, &cursor) == CWIKI_SNIPPET_OK,
       "expand outer session");
   {
      size_t scanned;
      check(cwiki_zone_recompute(zone_engine, &buffer, 0U, &scanned) == 0 &&
          cwiki_snippet_match(registry, zone_engine, &buffer,
          (struct cwiki_position){0U, 2U}, CWIKI_SNIPPET_AUTO, 0U, &inner_match) == CWIKI_SNIPPET_OK &&
          cwiki_snippet_expand(engine, &inner_match, NULL, 0U, 2U, &cursor) == CWIKI_SNIPPET_OK,
          "nested expansion pushes a second session");
   }
   check(cwiki_snippet_session_depth(engine) == 2U, "session stack depth is two");
   cwiki_snippet_cursor_moved(engine, (struct cwiki_position){0U, buffer.lines[0].length});
   check(cwiki_snippet_session_depth(engine) == 1U,
       "leaving nested extent pops only the innermost session");
   check(cwiki_snippet_current_stop(engine, &start, &end) == 0 && start.byte == 0U && end.byte == 2U,
       "outer registered stop survives nested position fixup");
   check(cwiki_buffer_insert(&buffer, 0U, 0U, "λ", strlen("λ")) == 0 &&
       cwiki_snippet_current_stop(engine, &start, &end) == 0 &&
       start.byte == strlen("λ") && end.byte == strlen("λ") + 2U,
       "ordinary buffer position fixup moves live stop boundaries");
   cwiki_snippet_cursor_moved(engine, (struct cwiki_position){0U, buffer.lines[0].length});
   check(cwiki_snippet_session_depth(engine) == 0U,
       "moving beyond final stop pops only the innermost remaining session");
   cwiki_snippet_match_free(&outer_match); cwiki_snippet_match_free(&inner_match);
   cwiki_snippet_engine_free(engine); cwiki_undo_free(&undo); cwiki_buffer_free(&buffer);
   cwiki_snippet_registry_free(registry); cwiki_zone_engine_free(zone_engine);
}

static void
test_expansion_mirrors_undo_and_sessions(void)
{
   struct cwiki_snippet_registry *registry = NULL;
   struct cwiki_zone_engine *zone_engine = zones();
   struct cwiki_buffer buffer;
   struct cwiki_undo undo;
   struct cwiki_snippet_engine *engine = NULL;
   struct cwiki_snippet_match match = {0};
   struct cwiki_position cursor, start, end;
   const char *body_text = "${1:éx}-$1-${stop:1|upper}-${visual}-$0";
   struct cwiki_snippet_body_spec body = {CWIKI_ZONE_PROSE, body_text, 0U};
   body.body_length = strlen(body_text);
   check(cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK &&
       add(registry, CWIKI_SNIPPET_LITERAL, "go", &body, 1U, NULL,
       CWIKI_SNIPPET_TRIGGER_AUTO, 0, 0U) == CWIKI_SNIPPET_OK,
       "register mirror expansion");
   load(&buffer, zone_engine, "go tail");
   check(cwiki_undo_init(&undo, &buffer) == 0 &&
       cwiki_snippet_engine_init(&engine, &buffer, &undo) == CWIKI_SNIPPET_OK,
       "initialize session engine");
   check(cwiki_snippet_match(registry, zone_engine, &buffer,
       (struct cwiki_position){0U, 2U}, CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_OK &&
       true, "match rollback fixture");
   cwiki_undo_test_fail_allocation_after(0U);
   check(cwiki_snippet_expand(engine, &match, "V", 1U, UINT64_C(1), &cursor) != CWIKI_SNIPPET_OK,
       "injected undo allocation failure rejects expansion");
   cwiki_undo_test_reset_allocation();
   check_buffer(&buffer, "go tail", "failed expansion rolls buffer back exactly");
   check(buffer.position_count == 0U && cwiki_snippet_session_depth(engine) == 0U,
       "failed expansion leaves no registered positions or partial session");
   check(cwiki_snippet_expand(engine, &match, "V", 1U, UINT64_C(1), &cursor) == CWIKI_SNIPPET_OK,
       "expand defaults mirrors transforms visual and final stop");
   check_buffer(&buffer, "éx-éx-ÉX-V- tail", "expansion bytes are independently expected");
   check(cwiki_snippet_session_depth(engine) == 1U &&
       cwiki_snippet_current_stop(engine, &start, &end) == 0 &&
       start.byte == 0U && end.byte == strlen("éx"),
       "first stop positions include multibyte byte length");
   check(cwiki_snippet_edit(engine, (struct cwiki_position){0U, 0U},
       (struct cwiki_position){0U, strlen("é")}, "åβ", strlen("åβ"),
       UINT64_C(2), &cursor) == CWIKI_SNIPPET_OK,
       "asymmetric multibyte edit updates mirrors in one API");
   check_buffer(&buffer, "åβx-åβx-ÅΒX-V- tail",
       "raw and transformed mirrors receive edited stop value");
   check(cwiki_undo_to_parent(&undo) == 0, "one undo reverses edit and mirrors");
   check_buffer(&buffer, "éx-éx-ÉX-V- tail",
       "mirror edit is one undo transaction");
   check(cwiki_undo_to_parent(&undo) == 0, "one undo reverses expansion");
   cwiki_snippet_expansion_undone(engine);
   check_buffer(&buffer, "go tail", "expansion undo restores trigger exactly");
   check(cwiki_snippet_session_depth(engine) == 0U,
       "explicit undo hook drops invalid session");

   cwiki_snippet_match_free(&match); cwiki_snippet_engine_free(engine);
   cwiki_undo_free(&undo); cwiki_buffer_free(&buffer);
   cwiki_snippet_registry_free(registry); cwiki_zone_engine_free(zone_engine);
}

static void
test_regex_captures_limits_and_rollback(void)
{
   struct cwiki_snippet_registry *registry = NULL;
   struct cwiki_zone_engine *zone_engine = zones();
   struct cwiki_buffer buffer;
   struct cwiki_snippet_match match = {0};
   const char *capture_text = "${capture:1|upper}:$0";
   struct cwiki_snippet_body_spec capture = {CWIKI_ZONE_PROSE, capture_text, 0U};
   const struct cwiki_snippet_body_spec limit = {CWIKI_ZONE_PROSE, "bad$0", 5U};
   capture.body_length = strlen(capture_text);
   check(cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK &&
       add(registry, CWIKI_SNIPPET_REGEX, "([[:alpha:]]+)@", &capture, 1U,
       NULL, CWIKI_SNIPPET_TRIGGER_AUTO, 0, 0U) == CWIKI_SNIPPET_OK,
       "compile regex once with legal final literal");
   check(add(registry, CWIKI_SNIPPET_REGEX, "(a+)+!", &limit, 1U, NULL,
       CWIKI_SNIPPET_TRIGGER_AUTO, 0, 2U) == CWIKI_SNIPPET_OK,
       "register tightly bounded regex");
   check(add(registry, CWIKI_SNIPPET_REGEX, "x+", &limit, 1U, NULL,
       CWIKI_SNIPPET_TRIGGER_AUTO, 0, 0U) == CWIKI_SNIPPET_INVALID,
       "reject regex without final literal codepoint");
   load(&buffer, zone_engine, "élan@");
   check(cwiki_snippet_match(registry, zone_engine, &buffer,
       (struct cwiki_position){0U, strlen("élan@")}, CWIKI_SNIPPET_AUTO, 0U,
       &match) == CWIKI_SNIPPET_OK && match.capture_count == 2U &&
       match.captures[1].start == 0U && match.captures[1].end == strlen("élan"),
       "regex matching preserves UTF-8 capture byte offsets");
   {
      struct cwiki_undo undo;
      struct cwiki_snippet_engine *snippet_engine = NULL;
      struct cwiki_position cursor;
      check(cwiki_undo_init(&undo, &buffer) == 0 &&
          cwiki_snippet_engine_init(&snippet_engine, &buffer, &undo) == CWIKI_SNIPPET_OK &&
          cwiki_snippet_expand(snippet_engine, &match, NULL, 0U, 1U, &cursor) == CWIKI_SNIPPET_OK,
          "expand regex capture reference");
      check_buffer(&buffer, "ÉLAN:", "capture transform supplies exact expansion bytes");
      cwiki_snippet_engine_free(snippet_engine); cwiki_undo_free(&undo);
   }
   cwiki_snippet_match_free(&match); cwiki_buffer_free(&buffer);
   load(&buffer, zone_engine, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaab!");
   check(cwiki_snippet_match(registry, zone_engine, &buffer,
       (struct cwiki_position){0U, strlen("aaaaaaaaaaaaaaaaaaaaaaaaaaaaab!")},
       CWIKI_SNIPPET_AUTO, 0U, &match) == CWIKI_SNIPPET_NO_MATCH &&
       cwiki_snippet_take_limit_diagnostic(registry) &&
       !cwiki_snippet_take_limit_diagnostic(registry),
       "resource-limited regex disables and reports exactly once");
   cwiki_buffer_free(&buffer);

   {
      struct cwiki_snippet_body *compiled = NULL;
      cwiki_snippet_test_fail_allocation_after(0U);
      check(cwiki_snippet_body_compile(&compiled, "x$0", 3U, 1U) ==
          CWIKI_SNIPPET_NO_MEMORY && compiled == NULL,
          "allocation failure leaves no partial body");
      cwiki_snippet_test_reset_allocation();
   }
   cwiki_snippet_registry_free(registry); cwiki_zone_engine_free(zone_engine);
}

int
main(void)
{
   test_body_parser_and_transforms();
   test_matching_layers_flags_and_zones();
   test_nested_sessions_and_position_fixup();
   test_expansion_mirrors_undo_and_sessions();
   test_regex_captures_limits_and_rollback();
   if (failures != 0) { (void)fprintf(stderr, "%d snippet test(s) failed\n", failures); return EXIT_FAILURE; }
   (void)puts("snippet tests passed"); return EXIT_SUCCESS;
}
