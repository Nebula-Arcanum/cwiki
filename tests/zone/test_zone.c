#include "buffer.h"
#include "unicode.h"
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
test_prose_chemistry(void)
{
   const char text[] = "a \\ce{H2O} b \\$x \\\\ce{no} c \\ce";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check_kind(engine, &buffer, 0U, "H2O", CWIKI_ZONE_CHEMISTRY,
       "prose ce command opens a chemistry zone");
   check_kind(engine, &buffer, 0U, " b", CWIKI_ZONE_PROSE,
       "closing brace returns to prose");
   check_kind(engine, &buffer, 0U, "x ", CWIKI_ZONE_PROSE,
       "escaped dollar stays prose");
   check_kind(engine, &buffer, 0U, "no}", CWIKI_ZONE_PROSE,
       "escaped backslash before ce is not a chemistry command");
   check(buffer.lines[0].end_zones.depth == 0U, "no zone left open");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_balanced_braces(void)
{
   const char text[] =
       "a \\ce{SO4^{2-}} tail1\n"
       "$x + \\ce{Fe^{3+}} + y1$ tail2\n"
       "b \\ce{a \\{ c} tail3\n"
       "c \\ce{a \\} c} tail4\n"
       "$\\text{a {b} c} m1 \\label{q{r}} m2$ tail5\n"
       "d \\ce{a{b}\n"
       "still";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check_kind(engine, &buffer, 0U, "2-", CWIKI_ZONE_CHEMISTRY,
       "nested brace group in prose ce stays chemistry");
   check_kind(engine, &buffer, 0U, "} tail1", CWIKI_ZONE_CHEMISTRY,
       "inner close brace does not end prose ce");
   check_kind(engine, &buffer, 0U, " tail1", CWIKI_ZONE_PROSE,
       "outer close brace returns to prose");
   check_kind(engine, &buffer, 1U, "} + y1", CWIKI_ZONE_CHEMISTRY,
       "inner close brace does not end math ce");
   check_kind(engine, &buffer, 1U, " + y1", CWIKI_ZONE_MATH_INLINE,
       "outer close brace returns to math");
   check_kind(engine, &buffer, 1U, " tail2", CWIKI_ZONE_PROSE,
       "math closes after balanced ce");
   check_kind(engine, &buffer, 2U, " c}", CWIKI_ZONE_CHEMISTRY,
       "escaped open brace is not counted");
   check_kind(engine, &buffer, 2U, " tail3", CWIKI_ZONE_PROSE,
       "ce with escaped open brace closes at its brace");
   check_kind(engine, &buffer, 3U, " c}", CWIKI_ZONE_CHEMISTRY,
       "escaped close brace does not end ce");
   check_kind(engine, &buffer, 3U, " tail4", CWIKI_ZONE_PROSE,
       "ce with escaped close brace closes at its brace");
   check_kind(engine, &buffer, 4U, " c} m1", CWIKI_ZONE_TEXT,
       "nested brace group does not end text hole");
   check_kind(engine, &buffer, 4U, " m1", CWIKI_ZONE_MATH_INLINE,
       "text hole closes at its balanced brace");
   check_kind(engine, &buffer, 4U, "} m2", CWIKI_ZONE_REFERENCE,
       "nested brace group does not end reference argument");
   check_kind(engine, &buffer, 4U, " m2", CWIKI_ZONE_MATH_INLINE,
       "reference argument closes at its balanced brace");
   check_kind(engine, &buffer, 4U, " tail5", CWIKI_ZONE_PROSE,
       "math with balanced holes closes");
   check_kind(engine, &buffer, 6U, "still", CWIKI_ZONE_CHEMISTRY,
       "unbalanced ce stays open past its inner group");
   check(buffer.lines[6].end_zones.depth == 1U &&
       buffer.lines[6].end_zones.zones[0].kind == CWIKI_ZONE_CHEMISTRY,
       "unbalanced ce leaves only the chemistry zone open");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_synignore_escape_hatch(void)
{
   const char text[] =
       "$unterminated\n"
       " \t% VimTeX: SynIgnore on\t\n"
       "π $not math \\ce{not chemistry} <!-- not comment\n"
       "```broken }}} % %% \\begin{tikzpicture}\n"
       "% VimTeX: SynIgnore off\n"
       "after $math$ prose\n"
       "% vimtex: SynIgnore on\n"
       "% VimTeX: SynIgnore ON\n"
       "% VimTeX: SynIgnore on trailing\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_zone_line opening = {0};
   struct cwiki_zone_line saved = {0};
   struct cwiki_zone zone;
   size_t scanned = 0U;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check_kind(engine, &buffer, 2U, "not math", CWIKI_ZONE_IGNORE,
       "SynIgnore suppresses nested math and chemistry parsing");
   check_kind(engine, &buffer, 3U, "broken", CWIKI_ZONE_IGNORE,
       "pathological delimiters remain inert in the ignored region");
   check_kind(engine, &buffer, 5U, "after", CWIKI_ZONE_PROSE,
       "off directive discards pre-ignore corruption and resumes in prose");
   check_kind(engine, &buffer, 5U, "math", CWIKI_ZONE_MATH_INLINE,
       "ordinary parsing resumes immediately after the off directive");
   check_kind(engine, &buffer, 6U, "vimtex", CWIKI_ZONE_PROSE,
       "directive tokens are case-sensitive");
   check_kind(engine, &buffer, 7U, "ON", CWIKI_ZONE_PROSE,
       "directive values are case-sensitive");
   check_kind(engine, &buffer, 8U, "trailing", CWIKI_ZONE_PROSE,
       "trailing non-whitespace prevents directive recognition");

   check(cwiki_zone_at(engine, &buffer, 1U, 1U, &zone) == 0 &&
       zone.kind == CWIKI_ZONE_MATH_INLINE,
       "cursor prefix before a complete on directive keeps prior context");
   check(cwiki_zone_at(engine, &buffer, 1U, buffer.lines[1U].length,
       &zone) == 0 && zone.kind == CWIKI_ZONE_IGNORE,
       "cursor at the completed on directive enters ignored context");
   check(cwiki_zone_at(engine, &buffer, 4U, 1U, &zone) == 0 &&
       zone.kind == CWIKI_ZONE_IGNORE,
       "cursor prefix within the off directive remains ignored");
   check(cwiki_zone_at(engine, &buffer, 4U, buffer.lines[4U].length,
       &zone) == 0 && zone.kind == CWIKI_ZONE_PROSE,
       "cursor at the completed off directive resumes prose");

   check(cwiki_zone_build_line(engine, &buffer, 1U, &opening) == 0 &&
       opening.span_count == 1U &&
       opening.spans[0].source_end == buffer.lines[1U].length &&
       opening.spans[0].zone.kind == CWIKI_ZONE_IGNORE &&
       opening.spans[0].token == CWIKI_ZONE_OPEN,
       "on directive is one whole-line ignored opener span");
   check(cwiki_zone_build_line(engine, &buffer, 2U, &saved) == 0 &&
       saved.span_count == 1U && saved.spans[0].zone.kind == CWIKI_ZONE_IGNORE &&
       saved.spans[0].token == CWIKI_ZONE_CONTENT,
       "ignored Unicode content is one trusted grapheme-safe span");

   check(cwiki_buffer_delete(&buffer, 4U, 22U, 1U) == 0 &&
       cwiki_zone_recompute(engine, &buffer, 4U, &scanned) == 0 &&
       scanned == buffer.line_count - 4U,
       "editing off invalid propagates ignored state through the suffix");
   check_kind(engine, &buffer, 5U, "math", CWIKI_ZONE_IGNORE,
       "invalidated off directive keeps later content inert");
   check(cwiki_buffer_insert(&buffer, 4U, 22U, "f", 1U) == 0 &&
       cwiki_zone_recompute(engine, &buffer, 4U, &scanned) == 0 &&
       scanned >= 2U,
       "restoring off incrementally recomputes downstream state");
   check_kind(engine, &buffer, 5U, "after", CWIKI_ZONE_PROSE,
       "restored off directive restores downstream prose");

   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
   check(saved.span_count == 1U && saved.spans[0].source_end == 48U &&
       saved.spans[0].zone.kind == CWIKI_ZONE_IGNORE,
       "ignored-line snapshot survives edits and owner destruction");
   cwiki_zone_line_free(&opening);
   cwiki_zone_line_free(&saved);
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
   struct cwiki_zone_line spans = {0};
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
   check(cwiki_zone_build_line(engine, &buffer, 0U, &spans) == 0 &&
       spans.degraded && spans.span_count == 0U && spans.spans == NULL,
       "degraded snapshot publishes no misleading partial spans");
   cwiki_zone_line_free(&spans);
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

static void
test_builtin_spans(void)
{
   const struct {
      const char *open;
      const char *close;
      enum cwiki_zone_kind kind;
      bool in_math;
   } cases[] = {
      {"  ```c", " ```\t", CWIKI_ZONE_CODE, false},
      {" %% ", "%%", CWIKI_ZONE_COMMENT_NOTE, false},
      {"%%", "%%", CWIKI_ZONE_COMMENT_NOTE, false},
      {"<!--", "-->", CWIKI_ZONE_COMMENT_HTML, false},
      {"$$", "$$", CWIKI_ZONE_MATH_DISPLAY, false},
      {"\\[", "\\]", CWIKI_ZONE_MATH_DISPLAY, false},
      {"$", "$", CWIKI_ZONE_MATH_INLINE, false},
      {"\\(", "\\)", CWIKI_ZONE_MATH_INLINE, false},
      {"\\begin{tikzpicture}", "\\end{tikzpicture}", CWIKI_ZONE_TIKZ, false},
      {"\\begin{align*}", "\\end{align*}", CWIKI_ZONE_LATEX, false},
      {"\\ce{", "}", CWIKI_ZONE_CHEMISTRY, true},
      {"\\text{", "}", CWIKI_ZONE_TEXT, true},
      {"\\intertext{", "}", CWIKI_ZONE_TEXT, true},
      {"\\label{", "}", CWIKI_ZONE_REFERENCE, true},
      {"\\ref{", "}", CWIKI_ZONE_REFERENCE, true},
      {"\\eqref{", "}", CWIKI_ZONE_REFERENCE, true}
   };
   struct cwiki_zone_engine *engine = builtin_engine();
   size_t index;

   if (engine == NULL) {
      return;
   }
   for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
      struct cwiki_buffer buffer;
      struct cwiki_zone_line spans = {0};
      char text[160];
      size_t first = cases[index].in_math ? 1U : 0U;
      size_t part;

      (void)snprintf(text, sizeof(text), "%s%s\nαé\n%s\n",
          cases[index].in_math ? "$\n" : "", cases[index].open,
          cases[index].close);
      parse(engine, &buffer, text);
      for (part = 0U; part < 3U; part++) {
         enum cwiki_zone_token token = part == 0U ? CWIKI_ZONE_OPEN :
             (part == 1U ? CWIKI_ZONE_CONTENT : CWIKI_ZONE_CLOSE);

         check(cwiki_zone_build_line(engine, &buffer, first + part,
             &spans) == 0 && !spans.degraded && spans.span_count == 1U,
             "built-in opener/content/closer has one trusted span");
         if (spans.span_count == 1U) {
            check(spans.spans[0].source_start == 0U &&
                spans.spans[0].source_end == buffer.lines[first + part].length &&
                spans.spans[0].zone.kind == cases[index].kind &&
                spans.spans[0].token == token,
                "built-in span owns its exact full match and semantic token");
         }
         cwiki_zone_line_free(&spans);
      }
      cwiki_buffer_free(&buffer);
   }
   cwiki_zone_engine_free(engine);
}

static void
test_span_boundaries_and_snapshots(void)
{
   const char text[] = "π $a$ $$β$$ \\\\$c$ $́d$\n";
   const struct {
      size_t start;
      size_t end;
      enum cwiki_zone_kind kind;
      enum cwiki_zone_token token;
   } expected[] = {
      {0U, 3U, CWIKI_ZONE_PROSE, CWIKI_ZONE_CONTENT},
      {3U, 4U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_OPEN},
      {4U, 5U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_CONTENT},
      {5U, 6U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_CLOSE},
      {6U, 7U, CWIKI_ZONE_PROSE, CWIKI_ZONE_CONTENT},
      {7U, 9U, CWIKI_ZONE_MATH_DISPLAY, CWIKI_ZONE_OPEN},
      {9U, 11U, CWIKI_ZONE_MATH_DISPLAY, CWIKI_ZONE_CONTENT},
      {11U, 13U, CWIKI_ZONE_MATH_DISPLAY, CWIKI_ZONE_CLOSE},
      {13U, 16U, CWIKI_ZONE_PROSE, CWIKI_ZONE_CONTENT},
      {16U, 17U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_OPEN},
      {17U, 18U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_CONTENT},
      {18U, 19U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_CLOSE},
      {19U, 20U, CWIKI_ZONE_PROSE, CWIKI_ZONE_CONTENT},
      {20U, 23U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_OPEN},
      {23U, 24U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_CONTENT},
      {24U, 25U, CWIKI_ZONE_MATH_INLINE, CWIKI_ZONE_CLOSE}
   };
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_zone_line spans = {0};
   struct cwiki_zone_line fresh = {0};
   size_t index;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check(cwiki_zone_build_line(engine, &buffer, 0U, &spans) == 0 &&
       spans.span_count == sizeof(expected) / sizeof(expected[0]),
       "mixed dollars, escape prefix and combining mark produce exact spans");
   for (index = 0U; index < spans.span_count &&
       index < sizeof(expected) / sizeof(expected[0]); index++) {
      const struct cwiki_zone_span *span = &spans.spans[index];

      check(span->source_start == expected[index].start &&
          span->source_end == expected[index].end &&
          span->zone.kind == expected[index].kind &&
          span->token == expected[index].token,
          "span matches independent asymmetric byte-range expectation");
      check(cwiki_grapheme_boundary(buffer.lines[0].bytes,
          buffer.lines[0].length, span->source_start) &&
          cwiki_grapheme_boundary(buffer.lines[0].bytes,
          buffer.lines[0].length, span->source_end),
          "neither endpoint splits a Unicode grapheme");
   }
   check(cwiki_buffer_delete(&buffer, 0U, 7U, 1U) == 0,
       "edit display opener after taking an immutable snapshot");
   errno = 0;
   check(cwiki_zone_build_line(engine, &buffer, 0U, &fresh) == -1 &&
       errno == EINVAL, "dirty line rejects span queries");
   check(cwiki_zone_build_line(engine, &buffer, 1U, &fresh) == -1 &&
       errno == EINVAL, "dirty predecessor rejects span queries");
   check(cwiki_zone_recompute(engine, &buffer, 0U, NULL) == 0 &&
       cwiki_zone_build_line(engine, &buffer, 0U, &fresh) == 0,
       "recomputed spans follow edited bytes");
   check(fresh.span_count > 5U && fresh.spans[5].source_end == 8U &&
       fresh.spans[5].zone.kind == CWIKI_ZONE_MATH_INLINE,
       "removing one display dollar creates an inline opener");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
   check(spans.span_count > 5U && spans.spans[5].source_end == 9U &&
       spans.spans[5].zone.kind == CWIKI_ZONE_MATH_DISPLAY,
       "owned snapshot survives recomputation and buffer/engine destruction");
   cwiki_zone_line_free(&spans);
   cwiki_zone_line_free(&fresh);
}

static char *
repeat_text(const char *prefix, const char *unit, size_t length,
    const char *suffix)
{
   size_t prefix_length = strlen(prefix);
   size_t unit_length = strlen(unit);
   size_t suffix_length = strlen(suffix);
   size_t total = prefix_length + length + suffix_length;
   char *text = malloc(total + 1U);
   size_t offset;

   if (text == NULL) {
      return NULL;
   }
   memcpy(text, prefix, prefix_length);
   for (offset = 0U; offset < length; offset++) {
      text[prefix_length + offset] = unit[offset % unit_length];
   }
   memcpy(text + prefix_length + length, suffix, suffix_length);
   text[total] = '\0';
   return text;
}

static void
test_long_line_cost(void)
{
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_zone_stack start = {{{CWIKI_ZONE_PROSE, 0U, 0U}}, 0U};
   struct cwiki_zone_stack end;
   struct cwiki_zone zone;
   const size_t prose_length = CWIKI_DEGRADED_LINE_BYTES;
   char *text;
   size_t before;

   /* A pattern is not run where its first byte cannot occur. Only the two
    * line-start patterns (fence, block comment) can begin on a space, so a
    * prose line costs at most two evaluations per space: a third of its
    * bytes here, not every region at every byte. A line exactly at the
    * R1.1.5 cap is still scanned. */
   text = repeat_text("", "lorem ipsum ", prose_length, "");
   check(text != NULL, "allocate long prose line");
   before = cwiki_zone_testing_evaluations(engine);
   check(cwiki_zone_scan_line(engine, text, prose_length, &start, &end) == 0 &&
       end.depth == 0U, "scan long prose line at the degraded-line cap");
   check(cwiki_zone_testing_evaluations(engine) - before <= prose_length / 2U,
       "long prose line skips offsets where no region can start");
   free(text);

   /* Inside math only the block-comment child can begin on a space. */
   text = repeat_text("$", "x+y=z ", 64U * 1024U, "$");
   check(text != NULL, "allocate long inline-math line");
   before = cwiki_zone_testing_evaluations(engine);
   check(cwiki_zone_scan_line(engine, text, strlen(text), &start, &end) == 0 &&
       end.depth == 0U, "scan long inline-math line");
   check(cwiki_zone_testing_evaluations(engine) - before <= 64U * 1024U / 4U,
       "long math body skips offsets where no end or child can start");
   free(text);

   /* A line over the R1.1.5 cap is not scanned: the zone stack carries
    * through unchanged and the line is marked degraded. */
   text = repeat_text("$$\n", "a $b$ ", CWIKI_DEGRADED_LINE_BYTES + 1U,
       "\nafter");
   check(text != NULL, "allocate over-cap line fixture");
   before = cwiki_zone_testing_evaluations(engine);
   parse(engine, &buffer, text);
   check(cwiki_zone_testing_evaluations(engine) - before <= 8U,
       "over-cap line costs no pattern evaluations");
   check(cwiki_zone_stack_equal(&buffer.lines[0].end_zones,
       &buffer.lines[1].end_zones) && buffer.lines[1].zone_degraded &&
       cwiki_buffer_line_degraded(&buffer, 1U),
       "over-cap line carries the zone stack unchanged and is degraded");
   check(cwiki_zone_at(engine, &buffer, 1U, 4U, &zone) == 0 &&
       zone.kind == CWIKI_ZONE_MATH_DISPLAY,
       "cursor context inside an over-cap line is the carried zone");
   check(cwiki_zone_at(engine, &buffer, 2U, 0U, &zone) == 0 &&
       zone.kind == CWIKI_ZONE_MATH_DISPLAY,
       "zone after an over-cap line continues from the carried stack");
   cwiki_buffer_free(&buffer);
   free(text);
   cwiki_zone_engine_free(engine);
}

int
main(void)
{
   test_long_line_cost();
   test_builtin_contexts();
   test_multiline_comments_and_escapes();
   test_unicode_boundaries();
   test_declarative_skip_and_containment();
   test_incremental_propagation();
   test_prose_chemistry();
   test_balanced_braces();
   test_synignore_escape_hatch();
   test_depth_bound_and_malformed_input();
   test_regex_resource_degradation();
   test_builtin_spans();
   test_span_boundaries_and_snapshots();
   if (failures != 0) {
      return 1;
   }
   (void)puts("zone tests: ok");
   return 0;
}
