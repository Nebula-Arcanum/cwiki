#include "buffer.h"
#include "zone.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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

static struct cwiki_zone_engine *
builtin_engine(void)
{
   struct cwiki_zone_engine *engine = NULL;
   const struct cwiki_zone_region *regions;
   size_t count;
   uint64_t top_level;

   regions = cwiki_zone_builtin_regions(&count, &top_level);
   check(cwiki_zone_engine_init(&engine, regions, count, top_level) == 0,
       "compile built-in declarative region table");
   return engine;
}

static void
parse(struct cwiki_zone_engine *engine, struct cwiki_buffer *buffer,
    const char *text)
{
   size_t scanned = 0U;

   check(cwiki_buffer_init(buffer) == 0, "initialize zone-test buffer");
   check(cwiki_buffer_load(buffer, text, strlen(text)) == 0,
       "load zone-test fixture");
   check(cwiki_zone_recompute(engine, buffer, 0U, &scanned) == 0 &&
       scanned == buffer->line_count,
       "initial recomputation scans every dirty line");
}

static struct cwiki_zone
zone_at(struct cwiki_zone_engine *engine, const struct cwiki_buffer *buffer,
    size_t line, const char *needle)
{
   struct cwiki_zone zone = {CWIKI_ZONE_CUSTOM, 0U, 0U};
   size_t needle_length = strlen(needle);
   size_t byte;

   for (byte = 0U; byte + needle_length <= buffer->lines[line].length;
       byte++) {
      if (memcmp(buffer->lines[line].bytes + byte, needle, needle_length) ==
          0) {
         break;
      }
   }
   check(byte + needle_length <= buffer->lines[line].length,
       "zone query needle exists in fixture");
   check(cwiki_zone_at(engine, buffer, line, byte, &zone) == 0,
       "innermost-zone query succeeds");
   return zone;
}

static void
check_kind(struct cwiki_zone_engine *engine, const struct cwiki_buffer *buffer,
    size_t line, const char *needle, enum cwiki_zone_kind expected,
    const char *message)
{
   check(zone_at(engine, buffer, line, needle).kind == expected, message);
}

static void
test_builtin_contexts(void)
{
   const char text[] =
       "plain 50% prose $x + \\ce{H2O} + \\text{words $y$} + "
       "\\ref{eq:a}$ tail\n"
       "%% note $comment$ %% prose\n"
       "<!-- html $comment$ --> prose\n"
       "```python\n"
       "$literal$ <!-- literal -->\n"
       "```\n"
       "$$\n"
       "math % latex comment\n"
       "50\\% remains math\n"
       "$$\n"
       "\\begin{figure}\n"
       "\\begin{tikzpicture}\n"
       "draw % tikz comment\n"
       "\\end{figure}\n"
       "still tikz\n"
       "\\end{tikzpicture}\n"
       "still latex\n"
       "latex % latex-only comment\n"
       "\\end{figure}\n"
       "prose again\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_zone code;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check_kind(engine, &buffer, 0U, "plain", CWIKI_ZONE_PROSE,
       "ordinary text and percent signs remain prose");
   check_kind(engine, &buffer, 0U, "x +", CWIKI_ZONE_MATH_INLINE,
       "single-dollar content is inline math");
   check_kind(engine, &buffer, 0U, "H2O", CWIKI_ZONE_CHEMISTRY,
       "chemistry is innermost inside math");
   check_kind(engine, &buffer, 0U, "words", CWIKI_ZONE_TEXT,
       "text command creates a non-math hole");
   check_kind(engine, &buffer, 0U, "y", CWIKI_ZONE_MATH_INLINE,
       "a math region may nest inside a text hole");
   check_kind(engine, &buffer, 0U, "eq:a", CWIKI_ZONE_REFERENCE,
       "reference argument is a distinct non-math hole");
   check_kind(engine, &buffer, 0U, "tail", CWIKI_ZONE_PROSE,
       "closing inline delimiter restores prose");
   check_kind(engine, &buffer, 1U, "comment", CWIKI_ZONE_COMMENT_NOTE,
       "paired percent comment is recognized anywhere");
   check_kind(engine, &buffer, 1U, "prose", CWIKI_ZONE_PROSE,
       "inline note comment closes on the same line");
   check_kind(engine, &buffer, 2U, "html", CWIKI_ZONE_COMMENT_HTML,
       "HTML comment is a distinct zone");
   code = zone_at(engine, &buffer, 4U, "literal");
   check(code.kind == CWIKI_ZONE_CODE &&
       strcmp(cwiki_zone_detail(engine, code.detail), "python") == 0,
       "fence language is captured from its own opening line");
   check_kind(engine, &buffer, 7U, "latex", CWIKI_ZONE_COMMENT_PERCENT,
       "unescaped percent starts a line comment in display math");
   check_kind(engine, &buffer, 8U, "remains", CWIKI_ZONE_MATH_DISPLAY,
       "escaped percent does not start a math comment");
   check_kind(engine, &buffer, 12U, "tikz", CWIKI_ZONE_COMMENT_PERCENT,
       "unescaped percent starts a line comment in TikZ");
   check_kind(engine, &buffer, 14U, "still", CWIKI_ZONE_TIKZ,
       "mismatched named environment end cannot close TikZ");
   check_kind(engine, &buffer, 16U, "latex", CWIKI_ZONE_LATEX,
       "matching TikZ end restores its containing LaTeX zone");
   check_kind(engine, &buffer, 17U, "comment", CWIKI_ZONE_COMMENT_PERCENT,
       "unescaped percent starts a line comment in generic LaTeX");
   check_kind(engine, &buffer, 19U, "prose", CWIKI_ZONE_PROSE,
       "matching named LaTeX end restores prose");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_multiline_comments_and_escapes(void)
{
   const char text[] =
       "%%\n"
       "$not math$\n"
       "%%\n"
       "<!--\n"
       "also $not math$\n"
       "-->\n"
       "$one \\$ escaped, two \\\\$ open$ end\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check_kind(engine, &buffer, 1U, "not", CWIKI_ZONE_COMMENT_NOTE,
       "percent-only line opens a multiline note comment");
   check_kind(engine, &buffer, 4U, "also", CWIKI_ZONE_COMMENT_HTML,
       "HTML comments propagate across line caches");
   check_kind(engine, &buffer, 6U, "escaped", CWIKI_ZONE_MATH_INLINE,
       "odd backslash count escapes a dollar delimiter");
   check_kind(engine, &buffer, 6U, "open", CWIKI_ZONE_PROSE,
       "even backslash count activates the dollar and closes math");
   check_kind(engine, &buffer, 6U, "end", CWIKI_ZONE_MATH_INLINE,
       "later delimiter reopens math in asymmetric escape fixture");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_unicode_boundaries(void)
{
   const char text[] =
       "\xcf\x80\xf0\x9f\x98\x80 prose $\xce\xb1 math "
       "\\ce{\xce\xb2 chem} \xce\xb3 tailmath$ \xce\xa9 after\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_zone zone;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check_kind(engine, &buffer, 0U, "prose", CWIKI_ZONE_PROSE,
       "query after two- and four-byte prose remains prose");
   check_kind(engine, &buffer, 0U, "math", CWIKI_ZONE_MATH_INLINE,
       "query after multibyte math content remains math");
   check_kind(engine, &buffer, 0U, "chem", CWIKI_ZONE_CHEMISTRY,
       "query after multibyte chemistry content remains chemistry");
   check_kind(engine, &buffer, 0U, "tailmath", CWIKI_ZONE_MATH_INLINE,
       "query after chemistry and multibyte math restores math");
   check_kind(engine, &buffer, 0U, "after", CWIKI_ZONE_PROSE,
       "query after trailing multibyte prose remains prose");
   errno = 0;
   check(cwiki_zone_at(engine, &buffer, 0U, 1U, &zone) == -1 &&
       errno == EINVAL,
       "zone query rejects a UTF-8 continuation-byte offset");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_declarative_skip_and_containment(void)
{
   const struct cwiki_zone_region regions[] = {
      {CWIKI_ZONE_CUSTOM, "outer", "<", ">", "\\\\.",
          CWIKI_ZONE_REGION_BIT(1), 11U, 0U, 0U, false},
      {CWIKI_ZONE_CHEMISTRY, "inner", "\\[", "\\]", NULL, 0U, 22U,
          0U, 0U, false}
   };
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_buffer buffer;

   check(cwiki_zone_engine_init(&engine, regions, 2U,
       CWIKI_ZONE_REGION_BIT(0)) == 0,
       "caller-provided declarative region table compiles");
   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, "<before \\> [inner] after> prose\n");
   check_kind(engine, &buffer, 0U, "inner", CWIKI_ZONE_CHEMISTRY,
       "parent containment admits configured child");
   check_kind(engine, &buffer, 0U, "after", CWIKI_ZONE_CUSTOM,
       "optional skip suppresses an escaped outer terminator");
   check_kind(engine, &buffer, 0U, "prose", CWIKI_ZONE_PROSE,
       "configured outer terminator closes the region");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_incremental_propagation(void)
{
   const char text[] =
       "$$\n"
       "inside\n"
       "$$\n"
       "$x$\n"
       "$$$\n"
       "still display\n"
       "$$\n"
       "tail\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   size_t scanned = 0U;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check(cwiki_buffer_delete(&buffer, 0U, 0U, 1U) == 0 &&
       buffer.lines[0].zone_dirty, "an edit dirties its line's zone cache");
   check(cwiki_zone_recompute(engine, &buffer, 0U, &scanned) == 0 &&
       scanned == 5U,
       "changed stack propagates until the first equal downstream stack");
   check(!buffer.lines[4U].zone_dirty && !buffer.lines[5U].zone_dirty,
       "propagation leaves the unchanged suffix clean");
   check_kind(engine, &buffer, 3U, "x", CWIKI_ZONE_PROSE,
       "query observes propagated asymmetric nesting");
   check_kind(engine, &buffer, 5U, "still", CWIKI_ZONE_MATH_DISPLAY,
       "unchanged downstream cache remains valid after early stop");
   check(cwiki_buffer_insert(&buffer, 5U, 3U, "x", 1U) == 0 &&
       cwiki_zone_recompute(engine, &buffer, 5U, &scanned) == 0 &&
       scanned == 1U,
       "content-only edit with equal end stack costs one line");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_depth_bound_and_malformed_input(void)
{
   const struct cwiki_zone_region region = {
      CWIKI_ZONE_CUSTOM, "recursive", "<", ">", NULL,
      CWIKI_ZONE_REGION_BIT(0), 0U, 0U, 0U, false
   };
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_zone_stack empty = {{{CWIKI_ZONE_PROSE, 0U, 0U}}, 0U};
   struct cwiki_zone_stack end;
   char malformed[257];
   const char malformed_utf8[] = "\xe2\x28\xa1";
   size_t index;

   check(cwiki_zone_engine_init(&engine, &region, 1U,
       CWIKI_ZONE_REGION_BIT(0)) == 0, "compile recursive depth-bound rule");
   if (engine == NULL) {
      return;
   }
   for (index = 0U; index < 128U; index++) {
      malformed[index] = '<';
      malformed[index + 128U] = '>';
   }
   malformed[256] = '\0';
   check(cwiki_zone_scan_line(engine, malformed, 256U, &empty, &end) == 0 &&
       end.depth <= CWIKI_ZONE_MAX_DEPTH,
       "adversarial nesting is bounded and terminates");
   check(cwiki_zone_scan_line(engine, "<<<<unterminated", 16U, &empty,
       &end) == 0 && end.depth == 4U,
       "unterminated malformed regions remain a bounded end stack");
   errno = 0;
   check(cwiki_zone_scan_line(engine, malformed_utf8,
       sizeof(malformed_utf8) - 1U, &empty, &end) == -1 && errno == EINVAL,
       "direct line scan deterministically rejects malformed UTF-8");
   cwiki_zone_engine_free(engine);
}

static void
test_regex_resource_degradation(void)
{
   static const char match_fixture[] =
       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaX\n"
       "neighbor\n"
       "cached\n";
   const struct cwiki_zone_region match_region = {
      CWIKI_ZONE_CUSTOM, "match-bound", "(*NO_AUTO_POSSESS)^(a+)+$", ">",
      NULL, 0U, 0U, 0U, 0U, false
   };
   const struct cwiki_zone_region depth_region = {
      CWIKI_ZONE_CUSTOM, "depth-bound",
      "(*NO_START_OPT)(*NO_AUTO_POSSESS)^a((a)*)*b", ">", NULL, 0U, 0U,
      0U, 0U, false
   };
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_buffer buffer;
   size_t scanned = 0U;

   errno = 0;
   check(cwiki_zone_engine_init_with_limits(&engine, &match_region, 1U,
       CWIKI_ZONE_REGION_BIT(0), 0U, 1U) == -1 && errno == EINVAL,
       "zone regex limits must be explicit nonzero values");
   check(cwiki_zone_engine_init_with_limits(&engine, &match_region, 1U,
       CWIKI_ZONE_REGION_BIT(0), 2U, 1000U) == 0,
       "compile pathological region with a deliberately tiny match limit");
   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, match_fixture);
   check(cwiki_buffer_line_zone_degraded(&buffer, 0U) &&
       cwiki_buffer_line_degraded(&buffer, 0U),
       "match-limit exhaustion visibly degrades only the affected line");
   check(!cwiki_buffer_line_zone_degraded(&buffer, 1U) &&
       !cwiki_buffer_line_zone_degraded(&buffer, 2U),
       "asymmetric safe neighboring lines remain trustworthy");
   check_kind(engine, &buffer, 1U, "neighbor", CWIKI_ZONE_PROSE,
       "a limited pattern remains a no-match for later safe lines");
   check(cwiki_buffer_delete(&buffer, 0U, 0U,
       buffer.lines[0U].length) == 0 &&
       cwiki_buffer_insert(&buffer, 0U, 0U, "safe", 4U) == 0 &&
       cwiki_zone_recompute(engine, &buffer, 0U, &scanned) == 0 &&
       scanned == 2U,
       "clearing degradation propagates through one equal cached neighbor");
   check(!cwiki_buffer_line_zone_degraded(&buffer, 0U) &&
       !cwiki_buffer_line_degraded(&buffer, 0U),
       "successful later recomputation clears visible degradation");
   check(!buffer.lines[2U].zone_dirty &&
       !cwiki_buffer_line_zone_degraded(&buffer, 2U),
       "recomputation preserves the unchanged cached suffix");
   check_kind(engine, &buffer, 2U, "cached", CWIKI_ZONE_PROSE,
       "the untouched cached suffix keeps its zone behavior");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);

   engine = NULL;
   check(cwiki_zone_engine_init_with_limits(&engine, &depth_region, 1U,
       CWIKI_ZONE_REGION_BIT(0), 100000U, 1U) == 0,
       "compile pathological region with a deliberately tiny depth limit");
   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, "aaaa\n");
   check(cwiki_buffer_line_zone_degraded(&buffer, 0U),
       "depth-limit exhaustion degrades the affected line as a no-match");
   check(!cwiki_buffer_line_zone_degraded(&buffer, 1U),
       "depth-limit exhaustion does not degrade an empty neighboring line");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

int
main(void)
{
   test_builtin_contexts();
   test_multiline_comments_and_escapes();
   test_unicode_boundaries();
   test_declarative_skip_and_containment();
   test_incremental_propagation();
   test_depth_bound_and_malformed_input();
   test_regex_resource_degradation();
   if (failures != 0) {
      return 1;
   }
   (void)puts("zone tests: ok");
   return 0;
}
