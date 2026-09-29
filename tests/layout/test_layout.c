#include "buffer.h"
#include "conceal.h"
#include "layout.h"
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

struct fixture {
   struct cwiki_buffer buffer;
   struct cwiki_zone_engine *zones;
   struct cwiki_conceal_table conceal;
   struct cwiki_layout_window window;
};

static void
fixture_init(struct fixture *fixture, const char *text)
{
   const struct cwiki_zone_region *regions;
   size_t count;
   size_t scanned;
   uint64_t top_level;

   (void)memset(fixture, 0, sizeof(*fixture));
   regions = cwiki_zone_builtin_regions(&count, &top_level);
   check(cwiki_zone_engine_init(&fixture->zones, regions, count,
       top_level) == 0, "initialize real zone engine");
   check(cwiki_buffer_init(&fixture->buffer) == 0,
       "initialize real buffer");
   check(cwiki_buffer_load(&fixture->buffer, text, strlen(text)) == 0,
       "load real buffer");
   check(cwiki_zone_recompute(fixture->zones, &fixture->buffer, 0U,
       &scanned) == 0 && scanned == fixture->buffer.line_count,
       "parse every fixture line");
   check(cwiki_conceal_table_init_builtin(&fixture->conceal) == 0,
       "initialize real conceal table");
   cwiki_layout_window_init(&fixture->window);
}

static void
fixture_free(struct fixture *fixture)
{
   cwiki_layout_window_free(&fixture->window);
   cwiki_conceal_table_free(&fixture->conceal);
   cwiki_buffer_free(&fixture->buffer);
   cwiki_zone_engine_free(fixture->zones);
}

static struct cwiki_layout_options
options(size_t width)
{
   struct cwiki_layout_options result;

   (void)memset(&result, 0, sizeof(result));
   result.content_width = width;
   result.wrap = true;
   result.break_indent = true;
   result.continuation_marker = ">";
   result.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   result.mode = CWIKI_CONCEAL_MODE_NORMAL;
   result.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
   return result;
}

static void
build(struct fixture *fixture, const struct cwiki_layout_options *settings,
    const char *message)
{
   check(cwiki_layout_rebuild(&fixture->window, &fixture->buffer,
       fixture->zones, &fixture->conceal, settings) == 0, message);
}

static bool
row_bytes(const struct fixture *fixture, size_t row, const char *expected)
{
   const struct cwiki_layout_row *item = cwiki_layout_row_at(
       &fixture->window, row);
   const struct cwiki_layout_line *line;
   size_t length = strlen(expected);

   if (item == NULL) {
      return false;
   }
   line = &fixture->window.lines[item->line];
   return item->display_end - item->display_start == length &&
       memcmp(line->display.display + item->display_start, expected,
       length) == 0;
}

static void
test_word_wrap_indent_and_spans(void)
{
   struct fixture fixture;
   struct cwiki_layout_options settings = options(8U);
   const struct cwiki_layout_row *row;

   fixture_init(&fixture, "  alpha beta gamma\nshort");
   build(&fixture, &settings, "build asymmetric prose layout");
   check(fixture.window.row_count == 4U,
       "asymmetric prose wraps into three rows plus a short line");
   check(row_bytes(&fixture, 0U, "  alpha ") &&
       row_bytes(&fixture, 1U, "beta ") &&
       row_bytes(&fixture, 2U, "gamma") &&
       row_bytes(&fixture, 3U, "short"),
       "row byte spans break at word whitespace without dropping bytes");
   row = cwiki_layout_row_at(&fixture.window, 1U);
   check(row != NULL && row->continuation && row->prefix_columns == 3U &&
       row->column_start == 8U && row->column_end == 13U &&
       row->source_start == 8U && row->source_end == 13U,
       "continuation uses marker plus first non-whitespace break indent");
   row = cwiki_layout_row_at(&fixture.window, 2U);
   check(row != NULL && row->prefix_columns == 3U &&
       row->source_start == 13U && row->source_end == 18U,
       "row spans expose exact source coverage");
   fixture_free(&fixture);
}

static void
test_narrow_and_unicode_fallback(void)
{
   struct fixture fixture;
   struct cwiki_layout_options settings = options(1U);
   size_t row;

   fixture_init(&fixture, "    abc\ne\xcc\x81界x\n");
   build(&fixture, &settings, "build width-one Unicode layout");
   check(fixture.window.lines[0].row_count == 7U,
       "width one falls back to one grapheme per row for a long token");
   for (row = 1U; row < fixture.window.lines[0].row_count; row++) {
      check(fixture.window.lines[0].rows[row].prefix_columns == 0U &&
          fixture.window.lines[0].rows[row].column_end >
          fixture.window.lines[0].rows[row].column_start,
          "narrow continuation clips indent and marker while progressing");
   }
   check(fixture.window.lines[1].row_count == 3U &&
       row_bytes(&fixture, 7U, "e\xcc\x81") &&
       row_bytes(&fixture, 8U, "界") && row_bytes(&fixture, 9U, "x"),
       "combining clusters stay intact and width-two glyphs make progress");
   check(fixture.window.lines[1].rows[1].column_end -
       fixture.window.lines[1].rows[1].column_start == 2U,
       "a width-two grapheme may exceed a width-one row atomically");
   fixture_free(&fixture);
}

static void
test_conceal_reveal_and_bidirectional_mapping(void)
{
   struct fixture fixture;
   struct cwiki_layout_options settings = options(5U);
   struct cwiki_position source;
   size_t row;
   size_t column;

   fixture_init(&fixture, "$\\alpha x$\nplain\n");
   build(&fixture, &settings, "build concealed layout");
   check(strcmp(fixture.window.lines[0].display.display, "$𝛼 x$") == 0 &&
       fixture.window.lines[0].row_count == 1U,
       "normal cursor line wraps by concealed displayed width");
   check(cwiki_layout_source_to_display(&fixture.window, &fixture.buffer,
       (struct cwiki_position){0U, 3U}, &row, &column) == 0 &&
       row == 0U && column == 1U,
       "source inside a concealed run maps to its display cell");
   check(cwiki_layout_display_to_source(&fixture.window, &fixture.buffer,
       0U, 1U, &source) == 0 && source.line == 0U && source.byte == 1U,
       "concealed display cell maps back to run start");

   settings.reveal_line = 0U;
   settings.reveal = (struct cwiki_conceal_reveal){1U, 7U, true};
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "revealing a run invalidates the view cache");
   build(&fixture, &settings, "rebuild revealed layout");
   check(strcmp(fixture.window.lines[0].display.display, "$\\alpha x$") == 0 &&
       fixture.window.lines[0].row_count == 3U,
       "revealed real source reflows the cursor line");
   check(cwiki_layout_source_to_display(&fixture.window, &fixture.buffer,
       (struct cwiki_position){0U, 4U}, &row, &column) == 0 &&
       row == 0U && column == 4U,
       "revealed run maps per source grapheme");
   check(cwiki_layout_display_to_source(&fixture.window, &fixture.buffer,
       0U, 4U, &source) == 0 && source.line == 0U && source.byte == 4U,
       "revealed display position maps back per source grapheme");

   settings.reveal.active = false;
   settings.mode = CWIKI_CONCEAL_MODE_INSERT;
   build(&fixture, &settings, "rebuild insert cursor layout");
   check(strcmp(fixture.window.lines[0].display.display, "$\\alpha x$") == 0 &&
       fixture.window.lines[0].row_count == 3U,
       "insert mode exposes and reflows the cursor line");
   settings.cursor.line = 1U;
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "moving insert cursor lines invalidates conceal layout");
   build(&fixture, &settings, "rebuild after cursor line change");
   check(strcmp(fixture.window.lines[0].display.display, "$𝛼 x$") == 0 &&
       fixture.window.lines[0].row_count == 1U,
       "line re-conceals and contracts after cursor leaves");
   fixture_free(&fixture);
}

static void
test_code_wrap_off_empty_and_extents(void)
{
   struct fixture fixture;
   struct cwiki_layout_options settings = options(4U);

   fixture_init(&fixture,
       "```c\nlong code words\n```\nprose words here\n\n");
   build(&fixture, &settings, "build fenced code layout");
   check(fixture.window.lines[0].code && fixture.window.lines[1].code &&
       fixture.window.lines[2].code,
       "opening, body, and closing fence lines are code-zone lines");
   check(fixture.window.lines[1].row_count == 1U &&
       fixture.window.lines[1].horizontal_extent == 15U &&
       fixture.window.lines[1].rows[0].horizontal_extent == 15U,
       "fenced code stays one row and exposes scrollable width");
   check(fixture.window.lines[3].row_count > 1U,
       "prose beside fenced code still wraps");
   check(fixture.window.lines[4].row_count == 1U &&
       fixture.window.lines[4].rows[0].source_start == 0U &&
       fixture.window.lines[4].rows[0].source_end == 0U,
       "empty source line owns one empty display row");

   settings.wrap = false;
   build(&fixture, &settings, "build wrap-disabled layout");
   check(fixture.window.row_count == fixture.buffer.line_count &&
       fixture.window.lines[3].row_count == 1U &&
       fixture.window.lines[3].horizontal_extent == 16U,
       "wrap off makes every source line exactly one scrollable row");
   fixture_free(&fixture);
}

static void
test_movement_clamping_and_ranges(void)
{
   struct fixture fixture;
   struct cwiki_layout_options settings = options(3U);
   struct cwiki_position to;
   struct cwiki_layout_range range;
   size_t desired;

   settings.break_indent = false;
   settings.continuation_marker = "";
   fixture_init(&fixture, "abcdef\nxy\n123456\n");
   build(&fixture, &settings, "build movement fixture");

   desired = SIZE_MAX;
   check(cwiki_layout_move_display_row(&fixture.window, &fixture.buffer,
       (struct cwiki_position){0U, 2U}, true, &desired, &to, &range) == 0 &&
       desired == 2U && to.line == 0U && to.byte == 5U,
       "display-row down preserves desired screen column within a wrap");
   check(range.kind == CWIKI_LAYOUT_RANGE_DISPLAY_ROWS &&
       range.start.line == 0U && range.start.byte == 0U &&
       range.end.line == 0U && range.end.byte == 6U,
       "display-row motion exposes only its covered display rows");
   check(cwiki_layout_move_display_row(&fixture.window, &fixture.buffer,
       to, true, &desired, &to, &range) == 0 && to.line == 1U &&
       to.byte == 2U && desired == 2U,
       "display-row motion clamps to a shorter row without losing goal");
   check(cwiki_layout_move_display_row(&fixture.window, &fixture.buffer,
       to, false, &desired, &to, &range) == 0 && to.line == 0U &&
       to.byte == 5U,
       "reverse display-row motion restores the preserved goal column");

   desired = SIZE_MAX;
   check(cwiki_layout_move_source_line(&fixture.window, &fixture.buffer,
       (struct cwiki_position){0U, 5U}, true, &desired, &to, &range) == 0 &&
       desired == 5U && to.line == 1U && to.byte == 2U,
       "source-line down clamps a desired raw source column");
   check(range.kind == CWIKI_LAYOUT_RANGE_SOURCE_LINES &&
       range.start.line == 0U && range.start.byte == 0U &&
       range.end.line == 1U && range.end.byte == 2U,
       "source-line motion exposes whole source-line ranges");
   check(cwiki_layout_move_source_line(&fixture.window, &fixture.buffer,
       to, true, &desired, &to, &range) == 0 && to.line == 2U &&
       to.byte == 5U,
       "source-line motion restores its goal on a later long line");
   fixture_free(&fixture);
}

static void
test_cache_invalidation_and_transaction(void)
{
   struct fixture fixture;
   struct cwiki_layout_options settings = options(6U);
   size_t scanned;

   fixture_init(&fixture, "alpha beta\n$\\alpha$\n");
   build(&fixture, &settings, "build invalidation fixture");
   check(!cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "identical inputs reuse the explicit cache");
   settings.content_width++;
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "content width invalidates cache");
   settings.content_width--;
   settings.wrap = false;
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "wrap option invalidates cache");
   settings.wrap = true;
   settings.break_indent = false;
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "break-indent option invalidates cache");
   settings.break_indent = true;
   settings.continuation_marker = "↪";
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "continuation marker invalidates cache");
   settings.continuation_marker = ">";
   settings.conceal_categories = 0U;
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "conceal categories invalidate cache");
   settings.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   settings.concealcursor_modes = 0U;
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "concealcursor policy invalidates cache");
   settings.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
   settings.mode = CWIKI_CONCEAL_MODE_INSERT;
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "cursor conceal mode invalidates cache");
   settings.mode = CWIKI_CONCEAL_MODE_NORMAL;
   settings.cursor.byte = 1U;
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "cursor position invalidates cache");
   settings.cursor.byte = 0U;

   check(cwiki_buffer_insert(&fixture.buffer, 0U, 0U, "Z", 1U) == 0,
       "edit invalidation fixture");
   check(cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "buffer bytes and zone dirtiness invalidate cache");
   errno = 0;
   check(cwiki_layout_display_to_source(&fixture.window, &fixture.buffer,
       0U, 0U, &(struct cwiki_position){0U, 0U}) == -1 && errno == EINVAL,
       "mapping refuses a stale dirty-zone cache");
   errno = 0;
   check(cwiki_layout_rebuild(&fixture.window, &fixture.buffer,
       fixture.zones, &fixture.conceal, &settings) == -1 && errno == EAGAIN &&
       row_bytes(&fixture, 0U, "alpha "),
       "dirty-zone rebuild refuses derivation and preserves old cache");
   check(cwiki_zone_recompute(fixture.zones, &fixture.buffer, 0U,
       &scanned) == 0, "recompute zones after edit");
   build(&fixture, &settings, "rebuild edited cache");
   check(!cwiki_layout_needs_rebuild(&fixture.window, &fixture.buffer,
       &settings), "rebuilt cache records edited source snapshot");
   fixture_free(&fixture);
}

static void
test_degraded_lines(void)
{
   struct fixture fixture;
   struct cwiki_layout_options settings = options(3U);
   char *large;
   size_t length = CWIKI_DEGRADED_LINE_BYTES + 1U;

   fixture_init(&fixture, "$\\alpha alpha beta$\n");
   fixture.buffer.lines[0].zone_degraded = true;
   build(&fixture, &settings, "build regex-degraded line");
   check(fixture.window.lines[0].degraded &&
       fixture.window.lines[0].row_count == 1U &&
       fixture.window.lines[0].rows[0].degraded &&
       strcmp(fixture.window.lines[0].display.display,
       "$\\alpha alpha beta$") == 0,
       "regex-degraded line is visible raw and unwrapped");
   fixture_free(&fixture);

   large = malloc(length + 1U);
   check(large != NULL, "allocate over-one-MiB degraded fixture");
   if (large == NULL) {
      return;
   }
   (void)memset(large, 'x', length);
   large[length] = '\0';
   fixture_init(&fixture, "x");
   check(cwiki_buffer_load(&fixture.buffer, large, length) == 0,
       "load over-one-MiB buffer without deriving zones");
   fixture.buffer.lines[0].zone_dirty = false;
   build(&fixture, &settings, "build over-one-MiB degraded line");
   check(fixture.window.lines[0].degraded &&
       fixture.window.lines[0].row_count == 1U &&
       fixture.window.lines[0].rows[0].source_end == length &&
       fixture.window.lines[0].horizontal_extent == length,
       "over-one-MiB line bypasses derived wrapping with visible extent");
   fixture_free(&fixture);
   free(large);
}

static void
test_invalid_inputs(void)
{
   struct fixture fixture;
   struct cwiki_layout_options settings = options(0U);

   fixture_init(&fixture, "text\n");
   errno = 0;
   check(cwiki_layout_rebuild(&fixture.window, &fixture.buffer,
       fixture.zones, &fixture.conceal, &settings) == -1 && errno == EINVAL,
       "zero content width is rejected");
   settings.content_width = SIZE_MAX;
   settings.continuation_marker = "x";
   settings.cursor.byte = SIZE_MAX;
   errno = 0;
   check(cwiki_layout_rebuild(&fixture.window, &fixture.buffer,
       fixture.zones, &fixture.conceal, &settings) == -1 && errno == EINVAL,
       "overflow-sized cursor input is rejected");
   fixture_free(&fixture);
}

int
main(void)
{
   test_word_wrap_indent_and_spans();
   test_narrow_and_unicode_fallback();
   test_conceal_reveal_and_bidirectional_mapping();
   test_code_wrap_off_empty_and_extents();
   test_movement_clamping_and_ranges();
   test_cache_invalidation_and_transaction();
   test_degraded_lines();
   test_invalid_inputs();
   if (failures != 0) {
      (void)fprintf(stderr, "%d layout test(s) failed\n", failures);
      return EXIT_FAILURE;
   }
   (void)puts("layout tests passed");
   return EXIT_SUCCESS;
}
