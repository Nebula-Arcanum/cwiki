#include "buffer.h"
#include "highlight.h"
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
       "initialize built-in zone engine");
   return engine;
}

static void
parse(struct cwiki_zone_engine *engine, struct cwiki_buffer *buffer,
    const char *text)
{
   size_t scanned = 0U;

   check(cwiki_buffer_init(buffer) == 0, "initialize highlight fixture");
   check(cwiki_buffer_load(buffer, text, strlen(text)) == 0,
       "load highlight fixture");
   check(cwiki_zone_recompute(engine, buffer, 0U, &scanned) == 0 &&
       scanned == buffer->line_count, "compute fixture zone cache");
}

static struct cwiki_highlight_line
highlight(struct cwiki_zone_engine *engine, const struct cwiki_buffer *buffer,
    size_t line)
{
   struct cwiki_highlight_line result = {0};

   check(cwiki_highlight_build_line(engine, buffer, line, &result) == 0,
       "build semantic highlight line");
   return result;
}

static void
check_runs(const struct cwiki_highlight_line *line,
    const struct cwiki_highlight_run *expected, size_t count,
    const char *message)
{
   size_t index;

   if (line->run_count != count) {
      size_t actual;

      (void)fprintf(stderr, "FAIL: %s (got %zu runs, expected %zu)\n",
          message, line->run_count, count);
      for (actual = 0U; actual < line->run_count; actual++) {
         (void)fprintf(stderr, "  actual %zu: [%zu,%zu) role %d\n", actual,
             line->runs[actual].source_start,
             line->runs[actual].source_end, (int)line->runs[actual].role);
      }
      failures++;
      return;
   }
   for (index = 0U; index < count; index++) {
      if (line->runs[index].source_start != expected[index].source_start ||
          line->runs[index].source_end != expected[index].source_end ||
          line->runs[index].role != expected[index].role) {
         (void)fprintf(stderr,
             "FAIL: %s run %zu (got [%zu,%zu) role %d, expected "
             "[%zu,%zu) role %d)\n", message, index,
             line->runs[index].source_start, line->runs[index].source_end,
             (int)line->runs[index].role, expected[index].source_start,
             expected[index].source_end, (int)expected[index].role);
         failures++;
      }
   }
}

static void
test_inline_math_holes_and_unicode(void)
{
   const char text[] =
       "# μ prose $α + \\ce{H2O} + \\text{words $β$} + \\ref{eq:γ}$ tail\n";
   const struct cwiki_highlight_run expected[] = {
      {0U, 11U, CWIKI_HIGHLIGHT_PROSE},
      {11U, 12U, CWIKI_HIGHLIGHT_MATH_INLINE_DELIMITER},
      {12U, 17U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {17U, 21U, CWIKI_HIGHLIGHT_CHEMISTRY_DELIMITER},
      {21U, 24U, CWIKI_HIGHLIGHT_CHEMISTRY},
      {24U, 25U, CWIKI_HIGHLIGHT_CHEMISTRY_DELIMITER},
      {25U, 28U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {28U, 34U, CWIKI_HIGHLIGHT_TEXT_DELIMITER},
      {34U, 40U, CWIKI_HIGHLIGHT_TEXT},
      {40U, 41U, CWIKI_HIGHLIGHT_MATH_INLINE_DELIMITER},
      {41U, 43U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {43U, 44U, CWIKI_HIGHLIGHT_MATH_INLINE_DELIMITER},
      {44U, 45U, CWIKI_HIGHLIGHT_TEXT_DELIMITER},
      {45U, 48U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {48U, 53U, CWIKI_HIGHLIGHT_REFERENCE_DELIMITER},
      {53U, 58U, CWIKI_HIGHLIGHT_REFERENCE},
      {58U, 59U, CWIKI_HIGHLIGHT_REFERENCE_DELIMITER},
      {59U, 60U, CWIKI_HIGHLIGHT_MATH_INLINE_DELIMITER},
      {60U, 65U, CWIKI_HIGHLIGHT_PROSE}
   };
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_highlight_line line;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   line = highlight(engine, &buffer, 0U);
   check(!line.degraded, "ordinary Unicode source remains trusted");
   check_runs(&line, expected, sizeof(expected) / sizeof(expected[0]),
       "asymmetric Markdown/math/hole roles preserve exact UTF-8 offsets");
   check(line.runs[0].source_end == 11U &&
       memcmp(buffer.lines[0].bytes + 11U, "$", 1U) == 0,
       "opening math delimiter follows zone engine ownership");
   cwiki_highlight_line_free(&line);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_display_comments_and_empty_line(void)
{
   const char text[] =
       "$$\n"
       "α + β % TODO γ\n"
       "\n"
       "$$\n"
       "left %%NOTE $x$%% right <!--HACK--> end\n";
   const struct cwiki_highlight_run display[] = {
      {0U, 8U, CWIKI_HIGHLIGHT_MATH_DISPLAY},
      {8U, 9U, CWIKI_HIGHLIGHT_COMMENT_DELIMITER},
      {9U, 17U, CWIKI_HIGHLIGHT_COMMENT}
   };
   const struct cwiki_highlight_run comments[] = {
      {0U, 5U, CWIKI_HIGHLIGHT_PROSE},
      {5U, 7U, CWIKI_HIGHLIGHT_COMMENT_DELIMITER},
      {7U, 15U, CWIKI_HIGHLIGHT_COMMENT},
      {15U, 17U, CWIKI_HIGHLIGHT_COMMENT_DELIMITER},
      {17U, 24U, CWIKI_HIGHLIGHT_PROSE},
      {24U, 28U, CWIKI_HIGHLIGHT_COMMENT_DELIMITER},
      {28U, 32U, CWIKI_HIGHLIGHT_COMMENT},
      {32U, 35U, CWIKI_HIGHLIGHT_COMMENT_DELIMITER},
      {35U, 39U, CWIKI_HIGHLIGHT_PROSE}
   };
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_highlight_line line;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   line = highlight(engine, &buffer, 0U);
   check_runs(&line, (const struct cwiki_highlight_run[]){
       {0U, 2U, CWIKI_HIGHLIGHT_MATH_DISPLAY_DELIMITER}}, 1U,
       "both opening dollars are one display delimiter");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 1U);
   check_runs(&line, display, sizeof(display) / sizeof(display[0]),
       "display math and percent comment have exact byte runs");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 2U);
   check(line.run_count == 0U && line.runs == NULL && !line.degraded,
       "empty line has an immutable empty run set");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 3U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 2U,
       CWIKI_HIGHLIGHT_MATH_DISPLAY_DELIMITER}}, 1U,
       "both closing dollars are one display delimiter");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 4U);
   check_runs(&line, comments, sizeof(comments) / sizeof(comments[0]),
       "note and HTML comment delimiters remain in source runs");
   cwiki_highlight_line_free(&line);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_code_latex_and_tikz(void)
{
   const char text[] =
       "```python\n"
       "$literal$ μ\n"
       "```\n"
       "\\begin{figure}\n"
       "latex α\n"
       "\\begin{tikzpicture}\n"
       "draw β % FIXME\n"
       "\\end{tikzpicture}\n"
       "\\end{figure}\n";
   const struct cwiki_highlight_run tikz_comment[] = {
      {0U, 8U, CWIKI_HIGHLIGHT_TIKZ},
      {8U, 9U, CWIKI_HIGHLIGHT_COMMENT_DELIMITER},
      {9U, 15U, CWIKI_HIGHLIGHT_COMMENT}
   };
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_highlight_line line;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   line = highlight(engine, &buffer, 0U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 9U,
       CWIKI_HIGHLIGHT_CODE_DELIMITER}}, 1U,
       "opening fence including language is a code delimiter");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 1U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 12U,
       CWIKI_HIGHLIGHT_CODE}}, 1U,
       "fenced code suppresses nested Markdown and math highlighting");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 2U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 3U,
       CWIKI_HIGHLIGHT_CODE_DELIMITER}}, 1U,
       "closing fence is a code delimiter");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 3U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 14U,
       CWIKI_HIGHLIGHT_LATEX_DELIMITER}}, 1U,
       "opening LaTeX environment is a delimiter");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 4U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 8U,
       CWIKI_HIGHLIGHT_LATEX}}, 1U, "LaTeX content has its semantic role");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 5U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 19U,
       CWIKI_HIGHLIGHT_TIKZ_DELIMITER}}, 1U,
       "nested TikZ opener has its own delimiter role");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 6U);
   check_runs(&line, tikz_comment,
       sizeof(tikz_comment) / sizeof(tikz_comment[0]),
       "TikZ and its percent comment have exact source offsets");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 7U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 17U,
       CWIKI_HIGHLIGHT_TIKZ_DELIMITER}}, 1U,
       "TikZ closing delimiter has the TikZ delimiter role");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 8U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 12U,
       CWIKI_HIGHLIGHT_LATEX_DELIMITER}}, 1U,
       "LaTeX closing delimiter has the LaTeX delimiter role");
   cwiki_highlight_line_free(&line);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_custom_and_degraded_fallback(void)
{
   const struct cwiki_zone_region custom = {
      CWIKI_ZONE_CUSTOM, "custom", "<", ">", NULL, 0U, 0U, 0U, 0U, false
   };
   const struct cwiki_zone_region pathological = {
      CWIKI_ZONE_CUSTOM, "bounded", "(*NO_AUTO_POSSESS)^(a+)+$", ">",
      NULL, 0U, 0U, 0U, 0U, false
   };
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_buffer buffer;
   struct cwiki_highlight_line line;

   check(cwiki_zone_engine_init(&engine, &custom, 1U,
       CWIKI_ZONE_REGION_BIT(0)) == 0, "initialize custom-zone engine");
   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, "<x> tail\n");
   line = highlight(engine, &buffer, 0U);
   check_runs(&line, (const struct cwiki_highlight_run[]){
       {0U, 1U, CWIKI_HIGHLIGHT_CUSTOM_DELIMITER},
       {1U, 2U, CWIKI_HIGHLIGHT_CUSTOM},
       {2U, 3U, CWIKI_HIGHLIGHT_CUSTOM_DELIMITER},
       {3U, 8U, CWIKI_HIGHLIGHT_PROSE}}, 4U,
       "custom zone maps without hard-coded custom syntax");
   cwiki_highlight_line_free(&line);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);

   engine = NULL;
   check(cwiki_zone_engine_init_with_limits(&engine, &pathological, 1U,
       CWIKI_ZONE_REGION_BIT(0), 2U, 1000U) == 0,
       "initialize deliberately bounded zone engine");
   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer,
       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaX\n");
   check(cwiki_buffer_line_zone_degraded(&buffer, 0U),
       "fixture exhausts zone match limit");
   line = highlight(engine, &buffer, 0U);
   check(line.degraded, "degraded zone cache visibly selects raw fallback");
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 65U,
       CWIKI_HIGHLIGHT_RAW}}, 1U,
       "degraded line is one deterministic full-source raw run");
   cwiki_highlight_line_free(&line);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_api_validation_and_role_names(void)
{
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_highlight_line line = {0};
   size_t role;

   if (engine == NULL) {
      return;
   }
   for (role = 0U; role < CWIKI_HIGHLIGHT_ROLE_COUNT; role++) {
      check(cwiki_highlight_role_name((enum cwiki_highlight_role)role) != NULL,
          "every semantic role has a stable theme-facing name");
   }
   check(cwiki_highlight_role_name(CWIKI_HIGHLIGHT_ROLE_COUNT) == NULL,
       "invalid semantic role has no name");
   check(cwiki_buffer_init(&buffer) == 0, "initialize dirty-cache fixture");
   check(cwiki_buffer_load(&buffer, "$x$", 3U) == 0,
       "load dirty-cache fixture");
   errno = 0;
   check(cwiki_highlight_build_line(engine, &buffer, 0U, &line) == -1 &&
       errno == EINVAL,
       "highlighting rejects a dirty zone cache rather than guessing");
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_grapheme_delimiters(void)
{
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_highlight_line line;
   const struct cwiki_highlight_run expected[] = {
      {0U, 5U, CWIKI_HIGHLIGHT_PROSE},
      {5U, 8U, CWIKI_HIGHLIGHT_MATH_INLINE_DELIMITER},
      {8U, 9U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {9U, 10U, CWIKI_HIGHLIGHT_MATH_INLINE_DELIMITER},
      {10U, 15U, CWIKI_HIGHLIGHT_PROSE},
      {15U, 17U, CWIKI_HIGHLIGHT_MATH_DISPLAY_DELIMITER},
      {17U, 19U, CWIKI_HIGHLIGHT_MATH_DISPLAY},
      {19U, 21U, CWIKI_HIGHLIGHT_MATH_DISPLAY_DELIMITER}
   };

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, "😀 $́x$ é $$β$$\n");
   line = highlight(engine, &buffer, 0U);
   check_runs(&line, expected, sizeof(expected) / sizeof(expected[0]),
       "delimiter roles preserve combining clusters and four-byte prose");
   cwiki_highlight_line_free(&line);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

int
main(void)
{
   test_inline_math_holes_and_unicode();
   test_display_comments_and_empty_line();
   test_code_latex_and_tikz();
   test_custom_and_degraded_fallback();
   test_api_validation_and_role_names();
   test_grapheme_delimiters();
   if (failures != 0) {
      (void)fprintf(stderr, "%d highlight test(s) failed\n", failures);
      return EXIT_FAILURE;
   }
   (void)puts("highlight tests passed");
   return EXIT_SUCCESS;
}
