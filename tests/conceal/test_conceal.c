#include "buffer.h"
#include "conceal.h"
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
       "initialize real zone engine");
   return engine;
}

static void
parse(struct cwiki_zone_engine *engine, struct cwiki_buffer *buffer,
    const char *text)
{
   size_t scanned;

   check(cwiki_buffer_init(buffer) == 0, "initialize fixture buffer");
   check(cwiki_buffer_load(buffer, text, strlen(text)) == 0,
       "load fixture buffer");
   check(cwiki_zone_recompute(engine, buffer, 0U, &scanned) == 0 &&
       scanned == buffer->line_count, "parse every fixture line");
}

static struct cwiki_conceal_line
render(const struct cwiki_conceal_table *table,
    struct cwiki_zone_engine *engine, const struct cwiki_buffer *buffer,
    size_t line, uint32_t categories, bool cursor_line,
    enum cwiki_conceal_mode mode, uint32_t concealcursor,
    const struct cwiki_conceal_reveal *reveal)
{
   struct cwiki_conceal_line result = {0};
   const struct cwiki_line *source = &buffer->lines[line];

   check(cwiki_conceal_build_line(table, engine, buffer, line, source->bytes,
       source->length, categories, cursor_line, mode, concealcursor, reveal,
       &result) == 0, "build display line");
   return result;
}

static bool
equals_source(const struct cwiki_conceal_line *display,
    const struct cwiki_line *source)
{
   return display->display_length == source->length &&
       memcmp(display->display, source->bytes, source->length) == 0;
}

static void
test_categories_and_table_validation(void)
{
   const char *const names[] = {
      "accents", "Greek", "math symbols", "ligatures", "fractions",
      "math bounds", "size-modified delimiters", "sub/superscripts",
      "styles", "environments", "item markers", "citations", "spacing",
      "sections"
   };
   const uint32_t expected_default =
       CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_ACCENTS) |
       CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_GREEK) |
       CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_MATH_SYMBOLS) |
       CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_LIGATURES) |
       CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_FRACTIONS) |
       CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_SIZE_DELIMITERS) |
       CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_SUB_SUPERSCRIPTS);
   struct cwiki_conceal_table table = {0};
   bool represented[CWIKI_CONCEAL_CATEGORY_COUNT] = {false};
   size_t i;

   check(CWIKI_CONCEAL_CATEGORY_COUNT == 14,
       "the public category table has exactly fourteen categories");
   check(CWIKI_CONCEAL_DEFAULT_MASK == expected_default,
       "balanced default enables exactly the seven accepted categories");
   check(CWIKI_CONCEALCURSOR_DEFAULT ==
       (CWIKI_CONCEAL_MODE_BIT(CWIKI_CONCEAL_MODE_NORMAL) |
       CWIKI_CONCEAL_MODE_BIT(CWIKI_CONCEAL_MODE_COMMAND)),
       "default concealcursor is exactly nc");
   for (i = 0U; i < CWIKI_CONCEAL_CATEGORY_COUNT; i++) {
      check(strcmp(cwiki_conceal_category_name(
          (enum cwiki_conceal_category)i), names[i]) == 0,
          "category names preserve the configured order");
   }
   check(cwiki_conceal_table_init_builtin(&table) == 0,
       "built-in replacement table passes width validation");
   for (i = 0U; i < table.count; i++) {
      represented[table.entries[i].category] = true;
   }
   for (i = 0U; i < CWIKI_CONCEAL_CATEGORY_COUNT; i++) {
      check(represented[i], "every category has a representative built-in");
   }
   cwiki_conceal_table_free(&table);

   {
      const struct cwiki_conceal_entry ambiguous[] = {
         {"\\custom", "α", CWIKI_CONCEAL_GREEK,
             CWIKI_CONCEAL_CONTEXT_MATH_INLINE}
      };
      const struct cwiki_conceal_entry multiple[] = {
         {"\\custom", "ab", CWIKI_CONCEAL_GREEK,
             CWIKI_CONCEAL_CONTEXT_MATH_INLINE}
      };
      const struct cwiki_conceal_entry zero_width[] = {
         {"\\custom", "\xcc\x82", CWIKI_CONCEAL_GREEK,
             CWIKI_CONCEAL_CONTEXT_MATH_INLINE}
      };
      const struct cwiki_conceal_entry invalid_source[] = {
         {"\xc3(", "𝛼", CWIKI_CONCEAL_GREEK,
             CWIKI_CONCEAL_CONTEXT_MATH_INLINE}
      };
      const struct cwiki_conceal_entry wide[] = {
         {"\\custom", "📖", CWIKI_CONCEAL_CITATIONS,
             CWIKI_CONCEAL_CONTEXT_LATEX}
      };

      errno = 0;
      check(cwiki_conceal_table_init(&table, ambiguous, 1U) == -1 &&
          errno == EINVAL, "ambiguous-width user replacement is rejected");
      check(cwiki_conceal_table_init(&table, multiple, 1U) == -1 &&
          errno == EINVAL, "multi-grapheme user replacement is rejected");
      check(cwiki_conceal_table_init(&table, zero_width, 1U) == -1 &&
          errno == EINVAL, "zero-width user replacement is rejected");
      check(cwiki_conceal_table_init(&table, invalid_source, 1U) == -1 &&
          errno == EINVAL, "invalid UTF-8 user source is rejected");
      check(cwiki_conceal_table_init(&table, wide, 1U) == 0,
          "unambiguous width-two user replacement is accepted");
      cwiki_conceal_table_free(&table);
   }
}

static void
test_display_runs_and_mapping(void)
{
   const char text[] = "$é\\alpha Z$\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_conceal_table table = {0};
   struct cwiki_conceal_line line;
   struct cwiki_conceal_line revealed;
   struct cwiki_conceal_line reconcealed;
   struct cwiki_conceal_reveal reveal = {3U, 9U, true};
   size_t column;
   size_t source;
   size_t run;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check(cwiki_conceal_table_init_builtin(&table) == 0,
       "initialize mapping table");
   line = render(&table, engine, &buffer, 0U, CWIKI_CONCEAL_DEFAULT_MASK,
       false, CWIKI_CONCEAL_MODE_INSERT, CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   check(strcmp(line.display, "$é𝛼 Z$") == 0,
       "display bytes preserve multibyte source and replace Greek exactly");
   check(line.columns == 6U, "display columns use cluster widths");
   check(line.run_count == 6U && line.runs[2].source_start == 3U &&
       line.runs[2].source_end == 9U && line.runs[2].display_start == 3U &&
       line.runs[2].display_end == 7U && line.runs[2].column_start == 2U &&
       line.runs[2].column_end == 3U && line.runs[2].concealed,
       "ordered run records explicit byte and column intervals");
   check(cwiki_conceal_source_to_display(&line, buffer.lines[0].bytes,
       buffer.lines[0].length, 3U, &column) == 0 && column == 2U,
       "concealed source start maps to replacement column");
   check(cwiki_conceal_source_to_display(&line, buffer.lines[0].bytes,
       buffer.lines[0].length, 5U, &column) == 0 && column == 2U,
       "concealed internal grapheme boundary deterministically maps to start");
   check(cwiki_conceal_source_to_display(&line, buffer.lines[0].bytes,
       buffer.lines[0].length, 9U, &column) == 0 && column == 3U,
       "concealed source end maps to replacement end");
   check(cwiki_conceal_display_to_source(&line, buffer.lines[0].bytes,
       buffer.lines[0].length, 2U, &source, &run) == 0 && source == 3U &&
       run == 2U, "display query identifies concealed source run");
   check(cwiki_conceal_display_to_source(&line, buffer.lines[0].bytes,
       buffer.lines[0].length, 3U, &source, &run) == 0 && source == 9U &&
       run == 3U, "replacement end round-trips to the next source boundary");
   errno = 0;
   check(cwiki_conceal_source_to_display(&line, buffer.lines[0].bytes,
       buffer.lines[0].length, 2U, &column) == -1 && errno == EINVAL,
       "source mapping rejects a non-grapheme UTF-8 boundary");

   revealed = render(&table, engine, &buffer, 0U,
       CWIKI_CONCEAL_DEFAULT_MASK, true, CWIKI_CONCEAL_MODE_NORMAL,
       CWIKI_CONCEALCURSOR_DEFAULT, &reveal);
   check(strcmp(revealed.display, "$é\\alpha Z$") == 0 &&
       !revealed.runs[2].concealed,
       "approaching a concealed run reveals only its real source");
   check(cwiki_conceal_source_to_display(&revealed, buffer.lines[0].bytes,
       buffer.lines[0].length, 4U, &column) == 0 && column == 3U,
       "revealed source supports per-grapheme forward motion");
   check(cwiki_conceal_display_to_source(&revealed, buffer.lines[0].bytes,
       buffer.lines[0].length, 6U, &source, &run) == 0 && source == 7U &&
       run == 2U, "revealed display maps back through source graphemes");
   reconcealed = render(&table, engine, &buffer, 0U,
       CWIKI_CONCEAL_DEFAULT_MASK, true, CWIKI_CONCEAL_MODE_NORMAL,
       CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   check(strcmp(reconcealed.display, line.display) == 0,
       "leaving a revealed run deterministically re-conceals it");
   check(memcmp(buffer.lines[0].bytes, "$é\\alpha Z$", 12U) == 0,
       "all transformations leave source bytes immutable");

   cwiki_conceal_line_free(&reconcealed);
   cwiki_conceal_line_free(&revealed);
   cwiki_conceal_line_free(&line);
   cwiki_conceal_table_free(&table);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_cursor_modes_and_masks(void)
{
   const char text[] = "$\\alpha \\) \\mathbf$\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_conceal_table table = {0};
   struct cwiki_conceal_line normal;
   struct cwiki_conceal_line command;
   struct cwiki_conceal_line insert;
   struct cwiki_conceal_line visual;
   struct cwiki_conceal_line noncursor;
   struct cwiki_conceal_line all;
   uint32_t every = (UINT32_C(1) << CWIKI_CONCEAL_CATEGORY_COUNT) - 1U;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check(cwiki_conceal_table_init_builtin(&table) == 0,
       "initialize mode table");
   normal = render(&table, engine, &buffer, 0U, CWIKI_CONCEAL_DEFAULT_MASK,
       true, CWIKI_CONCEAL_MODE_NORMAL, CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   command = render(&table, engine, &buffer, 0U, CWIKI_CONCEAL_DEFAULT_MASK,
       true, CWIKI_CONCEAL_MODE_COMMAND, CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   insert = render(&table, engine, &buffer, 0U, CWIKI_CONCEAL_DEFAULT_MASK,
       true, CWIKI_CONCEAL_MODE_INSERT, CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   visual = render(&table, engine, &buffer, 0U, CWIKI_CONCEAL_DEFAULT_MASK,
       true, CWIKI_CONCEAL_MODE_VISUAL, CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   noncursor = render(&table, engine, &buffer, 0U,
       CWIKI_CONCEAL_DEFAULT_MASK, false, CWIKI_CONCEAL_MODE_INSERT,
       CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   all = render(&table, engine, &buffer, 0U, every, true,
       CWIKI_CONCEAL_MODE_NORMAL, CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   check(strstr(normal.display, "𝛼") != NULL &&
       strstr(command.display, "𝛼") != NULL,
       "default nc conceals the cursor line in normal and command modes");
   check(equals_source(&insert, &buffer.lines[0]) &&
       equals_source(&visual, &buffer.lines[0]),
       "default nc exposes the cursor line in insert and visual modes");
   check(strstr(noncursor.display, "𝛼") != NULL,
       "non-cursor lines remain concealed regardless of current mode");
   check(strstr(normal.display, "\\)") != NULL &&
       strstr(normal.display, "\\mathbf") != NULL,
       "structural categories remain disabled in balanced defaults");
   check(strstr(all.display, "⁆") != NULL && strstr(all.display, "𝐁") != NULL,
       "opt-in structural masks activate table rules");

   cwiki_conceal_line_free(&all);
   cwiki_conceal_line_free(&noncursor);
   cwiki_conceal_line_free(&visual);
   cwiki_conceal_line_free(&insert);
   cwiki_conceal_line_free(&command);
   cwiki_conceal_line_free(&normal);
   cwiki_conceal_table_free(&table);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_context_gating_and_unicode_only_scripts(void)
{
   const char text[] =
       "prose \\alpha\n"
       "$\\alpha \\left \\\\alpha \\text{\\beta} \\ref{\\alpha} "
       "\\ce{\\pi} x^5 x_0 x^{2n} "
       "% \\alpha\n"
       "continued \\alpha$\n"
       "```tex\n"
       "$\\alpha$\n"
       "```\n"
       "\\begin{equation}\n"
       "\\AA \\alpha\n"
       "\\end{equation}\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_conceal_table table = {0};
   struct cwiki_conceal_line lines[9];
   size_t i;

   if (engine == NULL) {
      return;
   }
   (void)memset(lines, 0, sizeof(lines));
   parse(engine, &buffer, text);
   check(cwiki_conceal_table_init_builtin(&table) == 0,
       "initialize context table");
   for (i = 0U; i < 9U; i++) {
      lines[i] = render(&table, engine, &buffer, i,
          CWIKI_CONCEAL_DEFAULT_MASK, false, CWIKI_CONCEAL_MODE_NORMAL,
          CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   }
   check(strcmp(lines[0].display, "prose \\alpha") == 0,
       "prose does not conceal LaTeX-looking text");
   check(strstr(lines[1].display, "𝛼") != NULL &&
       strstr(lines[1].display, "\\left \\\\alpha") != NULL &&
       strstr(lines[1].display, "\\text{\\beta}") != NULL &&
       strstr(lines[1].display, "\\ref{\\alpha}") != NULL &&
       strstr(lines[1].display, "\\ce{𝜋}") != NULL,
       "rules respect command/escape boundaries and innermost zones");
   check(strstr(lines[1].display, "x⁵") != NULL &&
       strstr(lines[1].display, "x₀") != NULL &&
       strstr(lines[1].display, "x^{2n}") != NULL,
       "simple scripts conceal only when an exact whole Unicode form exists");
   check(strstr(lines[1].display, "% \\alpha") != NULL &&
       strstr(lines[2].display, "𝛼") != NULL,
       "percent comments do not conceal and end at their line boundary");
   check(strcmp(lines[4].display, "$\\alpha$") == 0,
       "fenced code suppresses conceal despite math-looking bytes");
   check(strstr(lines[7].display, "Å") != NULL &&
       strstr(lines[7].display, "𝛼") != NULL,
       "generic LaTeX zones admit configured LaTeX substitutions");

   for (i = 0U; i < 9U; i++) {
      cwiki_conceal_line_free(&lines[i]);
   }
   cwiki_conceal_table_free(&table);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

static void
test_enabled_representatives(void)
{
   const char text[] =
       "$\\^a \\alpha \\le \\AA \\frac{1}{10} "
       "\\Bigl\\langle x^5 x_0$\n";
   const char expected[] = "$â 𝛼 ⩽ Å ⅒ ⟨ x⁵ x₀$";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer buffer;
   struct cwiki_conceal_table table = {0};
   struct cwiki_conceal_line line;

   if (engine == NULL) {
      return;
   }
   parse(engine, &buffer, text);
   check(cwiki_conceal_table_init_builtin(&table) == 0,
       "initialize representative table");
   line = render(&table, engine, &buffer, 0U, CWIKI_CONCEAL_DEFAULT_MASK,
       false, CWIKI_CONCEAL_MODE_NORMAL, CWIKI_CONCEALCURSOR_DEFAULT, NULL);
   check(strcmp(line.display, expected) == 0,
       "balanced defaults exercise every enabled category with exact bytes");

   cwiki_conceal_line_free(&line);
   cwiki_conceal_table_free(&table);
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
}

int
main(void)
{
   test_categories_and_table_validation();
   test_display_runs_and_mapping();
   test_cursor_modes_and_masks();
   test_context_gating_and_unicode_only_scripts();
   test_enabled_representatives();

   if (failures != 0) {
      (void)fprintf(stderr, "%d conceal test(s) failed\n", failures);
      return EXIT_FAILURE;
   }
   (void)puts("conceal: ok");
   return EXIT_SUCCESS;
}
