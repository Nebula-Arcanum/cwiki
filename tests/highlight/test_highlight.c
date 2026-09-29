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
      {0U, 12U, CWIKI_HIGHLIGHT_PROSE},
      {12U, 21U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {21U, 25U, CWIKI_HIGHLIGHT_CHEMISTRY},
      {25U, 34U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {34U, 41U, CWIKI_HIGHLIGHT_TEXT},
      {41U, 44U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {44U, 45U, CWIKI_HIGHLIGHT_TEXT},
      {45U, 53U, CWIKI_HIGHLIGHT_MATH_INLINE},
      {53U, 59U, CWIKI_HIGHLIGHT_REFERENCE},
      {59U, 60U, CWIKI_HIGHLIGHT_MATH_INLINE},
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
   check(line.runs[0].source_end == 12U &&
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
      {0U, 9U, CWIKI_HIGHLIGHT_MATH_DISPLAY},
      {9U, 17U, CWIKI_HIGHLIGHT_COMMENT}
   };
   const struct cwiki_highlight_run comments[] = {
      {0U, 7U, CWIKI_HIGHLIGHT_PROSE},
      {7U, 17U, CWIKI_HIGHLIGHT_COMMENT},
      {17U, 28U, CWIKI_HIGHLIGHT_PROSE},
      {28U, 35U, CWIKI_HIGHLIGHT_COMMENT},
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
       {0U, 1U, CWIKI_HIGHLIGHT_PROSE},
       {1U, 2U, CWIKI_HIGHLIGHT_MATH_INLINE}}, 2U,
       "opening display delimiter preserves bounded prefix-query ownership");
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
       CWIKI_HIGHLIGHT_MATH_DISPLAY}}, 1U,
       "closing display delimiter keeps asymmetric inner ownership");
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
      {0U, 9U, CWIKI_HIGHLIGHT_TIKZ},
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
       CWIKI_HIGHLIGHT_PROSE}}, 1U,
       "opening fence delimiter follows outer-zone ownership");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 1U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 12U,
       CWIKI_HIGHLIGHT_CODE}}, 1U,
       "fenced code suppresses nested Markdown and math highlighting");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 2U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 3U,
       CWIKI_HIGHLIGHT_CODE}}, 1U,
       "closing fence delimiter follows inner-zone ownership");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 3U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 14U,
       CWIKI_HIGHLIGHT_PROSE}}, 1U,
       "opening LaTeX environment delimiter remains source prose");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 4U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 8U,
       CWIKI_HIGHLIGHT_LATEX}}, 1U, "LaTeX content has its semantic role");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 5U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 19U,
       CWIKI_HIGHLIGHT_LATEX}}, 1U,
       "nested TikZ opener follows containing LaTeX ownership");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 6U);
   check_runs(&line, tikz_comment,
       sizeof(tikz_comment) / sizeof(tikz_comment[0]),
       "TikZ and its percent comment have exact source offsets");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 7U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 17U,
       CWIKI_HIGHLIGHT_TIKZ}}, 1U,
       "TikZ closing delimiter follows inner-zone ownership");
   cwiki_highlight_line_free(&line);
   line = highlight(engine, &buffer, 8U);
   check_runs(&line, (const struct cwiki_highlight_run[]){{0U, 12U,
       CWIKI_HIGHLIGHT_LATEX}}, 1U,
       "LaTeX closing delimiter follows inner-zone ownership");
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
       {0U, 1U, CWIKI_HIGHLIGHT_PROSE},
       {1U, 3U, CWIKI_HIGHLIGHT_CUSTOM},
       {3U, 8U, CWIKI_HIGHLIGHT_PROSE}}, 3U,
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

int
main(void)
{
   test_inline_math_holes_and_unicode();
   test_display_comments_and_empty_line();
   test_code_latex_and_tikz();
   test_custom_and_degraded_fallback();
   test_api_validation_and_role_names();
   if (failures != 0) {
      (void)fprintf(stderr, "%d highlight test(s) failed\n", failures);
      return EXIT_FAILURE;
   }
   (void)puts("highlight tests passed");
   return EXIT_SUCCESS;
}
