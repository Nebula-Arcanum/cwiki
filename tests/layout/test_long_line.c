/* R1.1.5 / R1.9.2: a long but non-degraded line is highlighted, concealed and
 * wrapped normally, so per-line work must stay linear in its length. Work is
 * counted as codepoints decoded by the Unicode layer (validation and grapheme
 * boundary walks), which every per-line consumer goes through. */
#include "buffer.h"
#include "conceal.h"
#include "editor.h"
#include "highlight.h"
#include "layout.h"
#include "motion.h"
#include "render.h"
#include "unicode.h"
#include "zone.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPEAT 1000U
#define UNIT "word $x$ "
/* Generous: linear work is a small multiple of the line length, while the
 * quadratic walk this guards against is hundreds of times the length. */
#define STEPS_PER_BYTE 64U

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static void
check_linear(size_t before, size_t length, const char *stage)
{
   size_t steps = cwiki_unicode_testing_steps() - before;

   if (steps > STEPS_PER_BYTE * length) {
      (void)fprintf(stderr, "FAIL: %s took %zu steps for %zu bytes\n", stage,
          steps, length);
      failures++;
   }
}

static bool
same_layout(const struct cwiki_layout_window *left,
    const struct cwiki_layout_window *right)
{
   size_t line;

   if (left->line_count != right->line_count ||
       left->row_count != right->row_count) {
      return false;
   }
   for (line = 0U; line < left->line_count; line++) {
      const struct cwiki_layout_line *a = &left->lines[line];
      const struct cwiki_layout_line *b = &right->lines[line];

      if (a->source_length != b->source_length ||
          (a->source_length != 0U &&
          memcmp(a->source, b->source, a->source_length) != 0) ||
          a->first_row != b->first_row || a->row_count != b->row_count ||
          a->horizontal_extent != b->horizontal_extent ||
          a->degraded != b->degraded || a->code != b->code ||
          a->display.display_length != b->display.display_length ||
          a->display.columns != b->display.columns ||
          a->display.run_count != b->display.run_count ||
          memcmp(a->display.display, b->display.display,
          a->display.display_length) != 0 ||
          memcmp(a->rows, b->rows, a->row_count * sizeof(*a->rows)) != 0) {
         return false;
      }
      for (size_t run = 0U; run < a->display.run_count; run++) {
         const struct cwiki_conceal_run *x = &a->display.runs[run];
         const struct cwiki_conceal_run *y = &b->display.runs[run];

         if (x->source_start != y->source_start ||
             x->source_end != y->source_end ||
             x->display_start != y->display_start ||
             x->display_end != y->display_end ||
             x->column_start != y->column_start ||
             x->column_end != y->column_end || x->concealed != y->concealed) {
            return false;
         }
      }
   }
   return true;
}

static void
recompute(struct cwiki_zone_engine *zones, struct cwiki_buffer *buffer)
{
   size_t line;

   for (line = 0U; line < buffer->line_count; line++) {
      if (buffer->lines[line].zone_dirty) {
         check(cwiki_zone_recompute(zones, buffer, line, NULL) == 0,
             "recompute edited zones");
      }
   }
}

/* Rebuild the cached window and check it against a from-scratch layout.
 * Returns the Unicode steps the cached rebuild took. */
static size_t
rebuild_step(struct cwiki_layout_window *window, struct cwiki_buffer *buffer,
    struct cwiki_zone_engine *zones, const struct cwiki_conceal_table *conceal,
    const struct cwiki_layout_options *options, const char *stage)
{
   struct cwiki_layout_window fresh;
   size_t before;
   size_t steps;

   recompute(zones, buffer);
   before = cwiki_unicode_testing_steps();
   check(cwiki_layout_rebuild(window, buffer, zones, conceal, options) == 0,
       stage);
   steps = cwiki_unicode_testing_steps() - before;
   cwiki_layout_window_init(&fresh);
   check(cwiki_layout_rebuild(&fresh, buffer, zones, conceal, options) == 0 &&
       same_layout(window, &fresh), stage);
   cwiki_layout_window_free(&fresh);
   return steps;
}

static void
expect_cached(size_t steps, size_t long_length, const char *stage)
{
   if (steps >= long_length / 8U) {
      (void)fprintf(stderr, "FAIL: %s rebuilt the long line (%zu steps)\n",
          stage, steps);
      failures++;
   }
}

static void
expect_rebuilt(size_t steps, size_t long_length, const char *stage)
{
   if (steps < long_length) {
      (void)fprintf(stderr, "FAIL: %s reused a stale long line (%zu steps)\n",
          stage, steps);
      failures++;
   }
}

/* Typing on a short line must not re-conceal or re-wrap an untouched long
 * line; any input that changes the long line's layout must rebuild it. */
static void
test_incremental(struct cwiki_zone_engine *zones,
    const struct cwiki_conceal_table *conceal, const char *long_text,
    size_t long_length)
{
   struct cwiki_buffer buffer;
   struct cwiki_layout_window window;
   struct cwiki_layout_options options;
   size_t first_line;
   size_t end_line;
   size_t zone_end;
   size_t steps;
   uint64_t long_id;

   check(cwiki_buffer_init(&buffer) == 0 &&
       cwiki_buffer_load(&buffer, "intro\nshort $\\alpha$ line\n",
       strlen("intro\nshort $\\alpha$ line\n")) == 0,
       "load short lines");
   check(cwiki_buffer_split(&buffer, 1U, buffer.lines[1].length) == 0 &&
       cwiki_buffer_insert(&buffer, 2U, 0U, long_text, long_length) == 0 &&
       cwiki_buffer_split(&buffer, 2U, long_length) == 0 &&
       cwiki_buffer_insert(&buffer, 3U, 0U, "tail $\\beta$",
       strlen("tail $\\beta$")) == 0 &&
       buffer.line_count >= 4U && buffer.lines[2].length == long_length,
       "assemble mixed buffer");
   (void)memset(&options, 0, sizeof(options));
   options.content_width = 80U;
   options.wrap = true;
   options.break_indent = true;
   options.continuation_marker = ">";
   options.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   options.mode = CWIKI_CONCEAL_MODE_NORMAL;
   options.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
   options.cursor = (struct cwiki_position){1U, 0U};
   cwiki_layout_window_init(&window);
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "initial mixed layout");
   expect_rebuilt(steps, long_length, "initial layout");

   long_id = window.lines[2].id;
   options.mode = CWIKI_CONCEAL_MODE_INSERT;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "enter insert on short line");
   expect_cached(steps, long_length, "enter insert on short line");
   for (size_t i = 0U; i < 5U; i++) {
      check(cwiki_buffer_insert(&buffer, 1U, 5U, "x", 1U) == 0,
          "type on short line");
      options.cursor = (struct cwiki_position){1U, 6U};
      steps = rebuild_step(&window, &buffer, zones, conceal, &options,
          "type on short line");
      expect_cached(steps, long_length, "type on short line");
   }
   check(cwiki_buffer_split(&buffer, 1U, 3U) == 0, "split short line");
   options.cursor = (struct cwiki_position){2U, 0U};
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "split above long line");
   expect_cached(steps, long_length, "split above long line");
   check(cwiki_buffer_join(&buffer, 1U) == 0, "join short line");
   options.cursor = (struct cwiki_position){1U, 3U};
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "join above long line");
   expect_cached(steps, long_length, "join above long line");

   /* The id keys cached highlights: kept while the line is reused. */
   check(long_id != 0U && window.lines[2].id == long_id &&
       window.lines[1].id != window.lines[2].id,
       "reused long line keeps its id");
   options.cursor = (struct cwiki_position){2U, 0U};
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "insert cursor onto long line");
   check(window.lines[2].id != long_id, "rebuilt long line gets a new id");
   expect_rebuilt(steps, long_length, "insert cursor onto long line");
   /* Checking the cursor boundary walks the line once; no assertion. */
   options.cursor = (struct cwiki_position){2U, 9U};
   (void)rebuild_step(&window, &buffer, zones, conceal, &options,
       "insert cursor within long line");
   options.mode = CWIKI_CONCEAL_MODE_NORMAL;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "normal mode on long line");
   expect_rebuilt(steps, long_length, "normal mode on long line");
   options.mode = CWIKI_CONCEAL_MODE_INSERT;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "insert mode on long line");
   expect_rebuilt(steps, long_length, "insert mode on long line");
   options.cursor = (struct cwiki_position){1U, 0U};
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "insert cursor off long line");
   expect_rebuilt(steps, long_length, "insert cursor off long line");

   options.mode = CWIKI_CONCEAL_MODE_NORMAL;
   options.cursor = (struct cwiki_position){0U, 0U};
   options.reveal_line = 2U;
   options.reveal.active = true;
   options.reveal.source_start = 5U;
   options.reveal.source_end = 8U;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "reveal a long-line run");
   expect_rebuilt(steps, long_length, "reveal a long-line run");
   options.reveal.active = false;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "hide a long-line run");
   expect_rebuilt(steps, long_length, "hide a long-line run");

   zone_end = buffer.lines[1].length;
   check(cwiki_buffer_insert(&buffer, 1U, zone_end, " $$", 3U) == 0,
       "open display math above long line");
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "zone change reaches long line");
   expect_rebuilt(steps, long_length, "zone change reaches long line");
   check(cwiki_buffer_delete(&buffer, 1U, zone_end, 3U) == 0,
       "close display math above long line");
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "zone change leaves long line");
   expect_rebuilt(steps, long_length, "zone change leaves long line");

   options.content_width = 60U;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options, "resize");
   expect_rebuilt(steps, long_length, "resize");
   options.wrap = false;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "wrap toggle");
   expect_rebuilt(steps, long_length, "wrap toggle");
   options.conceal_categories = 0U;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "conceal toggle");
   expect_rebuilt(steps, long_length, "conceal toggle");

   /* Painting only highlights lines whose rows meet the viewport. */
   options.wrap = true;
   steps = rebuild_step(&window, &buffer, zones, conceal, &options,
       "wrap back on");
   cwiki_layout_visible_lines(&window, 0U, 2U, &first_line, &end_line);
   check(first_line == 0U && end_line == 2U,
       "viewport above long line excludes it");
   cwiki_layout_visible_lines(&window, window.lines[2].first_row + 3U, 10U,
       &first_line, &end_line);
   check(first_line == 2U && end_line == 3U,
       "viewport inside long line holds only it");
   cwiki_layout_visible_lines(&window, 1U, window.row_count, &first_line,
       &end_line);
   check(first_line == 1U && end_line == window.line_count,
       "viewport through the end reaches the last line");

   cwiki_layout_window_free(&window);
   cwiki_buffer_free(&buffer);
}

int
main(void)
{
   const struct cwiki_zone_region *regions;
   struct cwiki_zone_engine *zones = NULL;
   struct cwiki_conceal_table conceal;
   struct cwiki_layout_window window;
   struct cwiki_layout_options options;
   struct cwiki_document document;
   struct cwiki_buffer *buffer = &document.buffer;
   struct cwiki_editor editor;
   struct cwiki_render_viewport view;
   size_t frame_length;
   struct cwiki_zone_line spans;
   struct cwiki_highlight_line highlight;
   struct cwiki_conceal_line display;
   struct cwiki_motion_state state;
   struct cwiki_motion_viewport viewport = {0U, 23U};
   struct cwiki_motion_result result;
   size_t unit = strlen(UNIT);
   size_t length = unit * REPEAT;
   size_t count;
   size_t scanned;
   size_t before;
   uint64_t top_level;
   char *text = malloc(length + 1U);
   size_t i;

   check(text != NULL, "allocate long line");
   if (text == NULL) {
      return 1;
   }
   for (i = 0U; i < REPEAT; i++) {
      (void)memcpy(text + i * unit, UNIT, unit);
   }
   text[length] = '\n';
   regions = cwiki_zone_builtin_regions(&count, &top_level);
   check(cwiki_zone_engine_init(&zones, regions, count, top_level) == 0,
       "initialize zone engine");
   (void)memset(&document, 0, sizeof(document));
   document.path = "/fixture/long.md";
   check(cwiki_buffer_init(buffer) == 0 &&
       cwiki_buffer_load(buffer, text, length + 1U) == 0 &&
       buffer->lines[0].length == length, "load long line");
   check(cwiki_conceal_table_init_builtin(&conceal) == 0,
       "initialize conceal table");

   before = cwiki_unicode_testing_steps();
   check(cwiki_zone_recompute(zones, buffer, 0U, &scanned) == 0,
       "recompute zones");
   check_linear(before, length, "zone recompute");

   before = cwiki_unicode_testing_steps();
   check(cwiki_zone_build_line(zones, buffer, 0U, &spans) == 0 &&
       !spans.degraded, "build zone spans");
   check_linear(before, length, "zone spans");
   cwiki_zone_line_free(&spans);

   before = cwiki_unicode_testing_steps();
   check(cwiki_highlight_build_line(zones, buffer, 0U, &highlight) == 0,
       "build highlight");
   check_linear(before, length, "highlight");
   cwiki_highlight_line_free(&highlight);

   before = cwiki_unicode_testing_steps();
   check(cwiki_conceal_build_line(&conceal, zones, buffer, 0U,
       buffer->lines[0].bytes, length, CWIKI_CONCEAL_DEFAULT_MASK, false,
       CWIKI_CONCEAL_MODE_NORMAL, CWIKI_CONCEALCURSOR_DEFAULT, NULL,
       &display) == 0, "build conceal");
   check_linear(before, length, "conceal");
   cwiki_conceal_line_free(&display);

   (void)memset(&options, 0, sizeof(options));
   options.content_width = 80U;
   options.wrap = true;
   options.break_indent = true;
   options.continuation_marker = ">";
   options.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   options.mode = CWIKI_CONCEAL_MODE_NORMAL;
   options.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
   cwiki_layout_window_init(&window);
   before = cwiki_unicode_testing_steps();
   check(cwiki_layout_rebuild(&window, buffer, zones, &conceal,
       &options) == 0 && window.row_count > 1U, "rebuild wrapped layout");
   check_linear(before, length, "layout rebuild");

   state.cursor = (struct cwiki_position){0U, 0U};
   state.desired_display_column = SIZE_MAX;
   state.desired_source_column = SIZE_MAX;
   before = cwiki_unicode_testing_steps();
   check(cwiki_motion_apply(buffer, &window, zones, &state,
       CWIKI_MOTION_LINE_END, &viewport, &result) == 0 &&
       result.destination.byte == length - 1U, "move to line end");
   check_linear(before, length, "line-end motion");

   /* Paint the screenful at the end of the line, where every row sits deep
    * inside the display string. */
   (void)memset(&editor, 0, sizeof(editor));
   editor.document = &document;
   editor.mode = CWIKI_EDITOR_NORMAL;
   editor.motion = state;
   editor.motion.cursor = result.destination;
   (void)memset(&view, 0, sizeof(view));
   view.rows = 48U;
   view.columns = 80U;
   view.first_row = window.row_count - (view.rows - 1U);
   before = cwiki_unicode_testing_steps();
   check(cwiki_render_frame(&window, NULL, &editor, &view, NULL, 0U,
       &frame_length) == 0 && frame_length != 0U, "render final screenful");
   check_linear(before, length, "render frame");

   cwiki_layout_window_free(&window);
   test_incremental(zones, &conceal, text, length);
   cwiki_conceal_table_free(&conceal);
   cwiki_buffer_free(buffer);
   cwiki_zone_engine_free(zones);
   free(text);
   if (failures != 0) {
      (void)fprintf(stderr, "%d long-line test(s) failed\n", failures);
      return 1;
   }
   return 0;
}
