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
