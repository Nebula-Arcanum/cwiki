#include "buffer.h"
#include "conceal.h"
#include "layout.h"
#include "motion.h"
#include "zone.h"

#include <errno.h>
#include <stdbool.h>
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
   struct cwiki_layout_window layout;
};

static void
fixture_init(struct fixture *fixture, const char *text, size_t width)
{
   const struct cwiki_zone_region *regions;
   struct cwiki_layout_options options;
   size_t count;
   size_t scanned;
   uint64_t top_level;

   (void)memset(fixture, 0, sizeof(*fixture));
   regions = cwiki_zone_builtin_regions(&count, &top_level);
   check(cwiki_zone_engine_init(&fixture->zones, regions, count,
       top_level) == 0, "initialize zone engine");
   check(cwiki_buffer_init(&fixture->buffer) == 0, "initialize buffer");
   check(cwiki_buffer_load(&fixture->buffer, text, strlen(text)) == 0,
       "load fixture text");
   check(cwiki_zone_recompute(fixture->zones, &fixture->buffer, 0U,
       &scanned) == 0, "parse fixture zones");
   check(cwiki_conceal_table_init_builtin(&fixture->conceal) == 0,
       "initialize conceal table");
   cwiki_layout_window_init(&fixture->layout);
   (void)memset(&options, 0, sizeof(options));
   options.content_width = width;
   options.wrap = true;
   options.break_indent = false;
   options.continuation_marker = "";
   options.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   options.mode = CWIKI_CONCEAL_MODE_NORMAL;
   options.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
   check(cwiki_layout_rebuild(&fixture->layout, &fixture->buffer,
       fixture->zones, &fixture->conceal, &options) == 0,
       "build fixture layout");
}

static void
fixture_free(struct fixture *fixture)
{
   cwiki_layout_window_free(&fixture->layout);
   cwiki_conceal_table_free(&fixture->conceal);
   cwiki_buffer_free(&fixture->buffer);
   cwiki_zone_engine_free(fixture->zones);
}

static void
fixture_rebuild_reveal(struct fixture *fixture, size_t line,
    struct cwiki_conceal_reveal reveal)
{
   struct cwiki_layout_options options;

   (void)memset(&options, 0, sizeof(options));
   options.content_width = 40U;
   options.wrap = true;
   options.continuation_marker = "";
   options.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   options.mode = CWIKI_CONCEAL_MODE_NORMAL;
   options.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
   options.reveal_line = line;
   options.reveal = reveal;
   check(cwiki_layout_rebuild(&fixture->layout, &fixture->buffer,
       fixture->zones, &fixture->conceal, &options) == 0,
       "rebuild layout with one revealed run");
}

static struct cwiki_motion_state
state_at(size_t line, size_t byte)
{
   struct cwiki_motion_state state;

   state.cursor = (struct cwiki_position){line, byte};
   state.desired_display_column = SIZE_MAX;
   state.desired_source_column = SIZE_MAX;
   return state;
}

static struct cwiki_motion_result
move(struct fixture *fixture, struct cwiki_motion_state *state,
    enum cwiki_motion motion)
{
   struct cwiki_motion_result result;

   (void)memset(&result, 0, sizeof(result));
   check(cwiki_motion_apply(&fixture->buffer, &fixture->layout,
       fixture->zones, state, motion, NULL, &result) == 0,
       "apply motion");
   return result;
}

static bool
position_is(struct cwiki_position position, size_t line, size_t byte)
{
   return position.line == line && position.byte == byte;
}

static void
test_graphemes_ranges_and_conceal(void)
{
   struct fixture fixture;
   struct cwiki_motion_state state;
   struct cwiki_motion_result result;

   fixture_init(&fixture, "Ae\xcc\x81界 $\\alpha$", 40U);
   state = state_at(0U, 1U);
   result = move(&fixture, &state, CWIKI_MOTION_RIGHT);
   check(position_is(state.cursor, 0U, 4U),
       "l advances over one combining grapheme, not one codepoint");
   check(position_is(result.range.start, 0U, 1U) &&
       position_is(result.range.end, 0U, 4U) &&
       result.range.shape == CWIKI_MOTION_CHARACTERWISE,
       "forward exclusive range contains the source grapheme under cursor");
   result = move(&fixture, &state, CWIKI_MOTION_LEFT);
   check(position_is(state.cursor, 0U, 1U) &&
       position_is(result.range.start, 0U, 1U) &&
       position_is(result.range.end, 0U, 4U),
       "backward range normalizes without reversing endpoints");

   state = state_at(0U, 8U);
   result = move(&fixture, &state, CWIKI_MOTION_RIGHT);
   check(position_is(state.cursor, 0U, 9U) && result.reveal.active &&
       result.reveal.source_start == 9U && result.reveal.source_end == 15U,
       "horizontal approach requests reveal of exactly one concealed run");
   fixture_rebuild_reveal(&fixture, result.reveal_line, result.reveal);
   result = move(&fixture, &state, CWIKI_MOTION_RIGHT);
   check(position_is(state.cursor, 0U, 10U) && result.reveal.active &&
       result.reveal.source_start == 9U && result.reveal.source_end == 15U,
       "horizontal motion inside a revealed run keeps that run revealed");
   fixture_free(&fixture);
}

static void
test_word_and_word_end_semantics(void)
{
   struct fixture fixture;
   struct cwiki_motion_state state;
   struct cwiki_motion_result result;
   const char *text = "éclair... foo-bar\nβeta  end";
   size_t punctuation = (size_t)(strstr(text, "...") - text);
   size_t foo = (size_t)(strstr(text, "foo") - text);
   size_t bar = (size_t)(strstr(text, "bar") - text);

   fixture_init(&fixture, text, 80U);
   state = state_at(0U, 0U);
   result = move(&fixture, &state, CWIKI_MOTION_WORD_FORWARD);
   check(position_is(state.cursor, 0U, punctuation),
       "w distinguishes Unicode keyword characters from punctuation");
   check(position_is(result.range.start, 0U, 0U) &&
       position_is(result.range.end, 0U, punctuation),
       "w operator endpoint is exclusive");
   state = state_at(0U, 0U);
   (void)move(&fixture, &state, CWIKI_MOTION_WORD_FORWARD_BIG);
   check(position_is(state.cursor, 0U, foo),
       "W treats punctuation as part of a whitespace-delimited WORD");
   state = state_at(0U, bar);
   (void)move(&fixture, &state, CWIKI_MOTION_WORD_BACKWARD);
   check(position_is(state.cursor, 0U, bar - 1U),
       "b stops at the punctuation class before a keyword");
   state = state_at(0U, bar);
   (void)move(&fixture, &state, CWIKI_MOTION_WORD_BACKWARD_BIG);
   check(position_is(state.cursor, 0U, foo),
       "B traverses punctuation within one WORD");
   state = state_at(0U, foo);
   result = move(&fixture, &state, CWIKI_MOTION_WORD_END);
   check(position_is(state.cursor, 0U, foo + 2U) &&
       position_is(result.range.end, 0U, foo + 3U),
       "e lands on the word end and returns an inclusive half-open range");
   state = state_at(0U, foo);
   result = move(&fixture, &state, CWIKI_MOTION_WORD_END_BIG);
   check(position_is(state.cursor, 0U, bar + 2U) &&
       position_is(result.range.end, 0U, bar + 3U),
       "E includes punctuation through the WORD end");
   state = state_at(0U, bar + 2U);
   (void)move(&fixture, &state, CWIKI_MOTION_WORD_FORWARD);
   check(position_is(state.cursor, 1U, 0U),
       "a source newline separates otherwise identical word classes");
   fixture_free(&fixture);
}

static void
test_vertical_goals_rows_and_lines(void)
{
   struct fixture fixture;
   struct cwiki_motion_state state;
   struct cwiki_motion_result result;

   fixture_init(&fixture, "abcdef\nx\n123456", 3U);
   state = state_at(0U, 2U);
   result = move(&fixture, &state, CWIKI_MOTION_DISPLAY_DOWN);
   check(position_is(state.cursor, 0U, 5U) &&
       state.desired_display_column == 2U &&
       result.range.shape == CWIKI_MOTION_DISPLAY_ROWS &&
       position_is(result.range.start, 0U, 0U) &&
       position_is(result.range.end, 0U, 6U),
       "j preserves display goal and returns exact wrapped-row coverage");
   (void)move(&fixture, &state, CWIKI_MOTION_DISPLAY_DOWN);
   check(position_is(state.cursor, 1U, 1U) &&
       state.desired_display_column == 2U,
       "j clamps on a ragged row without losing its goal");
   (void)move(&fixture, &state, CWIKI_MOTION_DISPLAY_DOWN);
   check(position_is(state.cursor, 2U, 2U),
       "later j restores the preserved display goal");
   (void)move(&fixture, &state, CWIKI_MOTION_LINE_START);
   check(state.desired_display_column == SIZE_MAX &&
       state.desired_source_column == SIZE_MAX,
       "a nonvertical motion resets both desired columns");

   state = state_at(0U, 5U);
   result = move(&fixture, &state, CWIKI_MOTION_SOURCE_DOWN);
   check(position_is(state.cursor, 1U, 1U) &&
       state.desired_source_column == 5U &&
       result.range.shape == CWIKI_MOTION_LINEWISE &&
       position_is(result.range.start, 0U, 0U) &&
       position_is(result.range.end, 2U, 0U),
       "gj clamps by raw source column and returns whole source lines");
   (void)move(&fixture, &state, CWIKI_MOTION_SOURCE_DOWN);
   check(position_is(state.cursor, 2U, 5U),
       "repeated gj restores its independent source-column goal");
   fixture_free(&fixture);
}

static void
test_document_viewport_and_line_motions(void)
{
   struct fixture fixture;
   struct cwiki_motion_state state;
   struct cwiki_motion_result result;
   struct cwiki_motion_viewport viewport = {1U, 4U};

   fixture_init(&fixture, "  abcdef\n\n x\nlast", 3U);
   state = state_at(3U, 2U);
   result = move(&fixture, &state, CWIKI_MOTION_DOCUMENT_FIRST);
   check(position_is(state.cursor, 0U, 2U) &&
       result.range.shape == CWIKI_MOTION_LINEWISE &&
       position_is(result.range.start, 0U, 0U) &&
       position_is(result.range.end, 4U, 0U),
       "gg is linewise, lands on first nonblank, and uses the EOF sentinel");
   result = move(&fixture, &state, CWIKI_MOTION_DOCUMENT_LAST);
   check(position_is(state.cursor, 3U, 0U) &&
       result.range.shape == CWIKI_MOTION_LINEWISE,
       "G lands on the last line and is linewise");

   state = state_at(3U, 1U);
   check(cwiki_motion_apply(&fixture.buffer, &fixture.layout, fixture.zones,
       &state, CWIKI_MOTION_VIEWPORT_HIGH, &viewport, &result) == 0 &&
       position_is(state.cursor, 0U, 2U),
       "H lands within the explicit first viewport display row");
   check(cwiki_motion_apply(&fixture.buffer, &fixture.layout, fixture.zones,
       &state, CWIKI_MOTION_VIEWPORT_MIDDLE, &viewport, &result) == 0 &&
       position_is(state.cursor, 0U, 5U),
       "M lands within the explicit middle display row");
   check(cwiki_motion_apply(&fixture.buffer, &fixture.layout, fixture.zones,
       &state, CWIKI_MOTION_VIEWPORT_LOW, &viewport, &result) == 0 &&
       position_is(state.cursor, 2U, 1U),
       "L maps the explicit last viewport display row to first nonblank");

   state = state_at(0U, 5U);
   result = move(&fixture, &state, CWIKI_MOTION_LINE_END);
   check(position_is(state.cursor, 0U, 7U) &&
       position_is(result.range.start, 0U, 5U) &&
       position_is(result.range.end, 0U, 8U),
       "$ includes the final grapheme in its half-open operator range");
   (void)move(&fixture, &state, CWIKI_MOTION_LINE_START);
   check(position_is(state.cursor, 0U, 0U), "0 lands at byte zero");
   (void)move(&fixture, &state, CWIKI_MOTION_FIRST_NONBLANK);
   check(position_is(state.cursor, 0U, 2U), "^ lands at first nonblank");
   result = move(&fixture, &state, CWIKI_MOTION_NEXT_LINE);
   check(position_is(state.cursor, 1U, 0U) &&
       result.range.shape == CWIKI_MOTION_LINEWISE,
       "+ lands on an empty line and remains linewise");
   result = move(&fixture, &state, CWIKI_MOTION_PREVIOUS_LINE);
   check(position_is(state.cursor, 0U, 2U) &&
       result.range.shape == CWIKI_MOTION_LINEWISE,
       "- is backward linewise and lands on first nonblank");
   state = state_at(2U, 0U);
   (void)move(&fixture, &state, CWIKI_MOTION_LINE_FIRST_NONBLANK);
   check(position_is(state.cursor, 2U, 1U),
       "_ without a count means first nonblank of the current line");
   fixture_free(&fixture);
}

static void
test_paragraphs_sentences_and_zones(void)
{
   struct fixture fixture;
   struct cwiki_motion_state state;
   struct cwiki_motion_result result;
   const char *text = "One $x. y$ still. Next! Last\ncontinued\n   \nSecond para";
   size_t next = (size_t)(strstr(text, "Next") - text);
   size_t last = (size_t)(strstr(text, "Last") - text);

   fixture_init(&fixture, text, 80U);
   state = state_at(0U, 0U);
   result = move(&fixture, &state, CWIKI_MOTION_SENTENCE_FORWARD);
   check(position_is(state.cursor, 0U, next) &&
       position_is(result.range.end, 0U, next),
       ") ignores sentence punctuation inside the non-prose math zone");
   (void)move(&fixture, &state, CWIKI_MOTION_SENTENCE_FORWARD);
   check(position_is(state.cursor, 0U, last),
       ") finds the following prose sentence asymmetrically");
   (void)move(&fixture, &state, CWIKI_MOTION_SENTENCE_BACKWARD);
   check(position_is(state.cursor, 0U, next),
       "( returns to the previous prose sentence start");

   state = state_at(0U, 3U);
   (void)move(&fixture, &state, CWIKI_MOTION_PARAGRAPH_FORWARD);
   check(position_is(state.cursor, 3U, 0U),
       "} crosses whitespace-only separators to the next paragraph");
   result = move(&fixture, &state, CWIKI_MOTION_PARAGRAPH_BACKWARD);
   check(position_is(state.cursor, 0U, 0U) &&
       result.range.shape == CWIKI_MOTION_CHARACTERWISE,
       "{ returns to the preceding paragraph with a normalized range");
   fixture_free(&fixture);
}

static void
test_boundaries_and_invalid_inputs(void)
{
   struct fixture fixture;
   struct cwiki_motion_state state;
   struct cwiki_motion_result result;
   struct cwiki_motion_viewport bad = {2U, 1U};

   fixture_init(&fixture, "a\n\n", 4U);
   state = state_at(0U, 0U);
   result = move(&fixture, &state, CWIKI_MOTION_LEFT);
   check(position_is(state.cursor, 0U, 0U) &&
       position_is(result.range.start, 0U, 0U) &&
       position_is(result.range.end, 0U, 0U),
       "h at the document boundary is a stable empty range");
   result = move(&fixture, &state, CWIKI_MOTION_LINE_END);
   check(position_is(result.range.start, 0U, 0U) &&
       position_is(result.range.end, 0U, 1U),
       "$ on a one-grapheme line still includes that grapheme");
   state = state_at(1U, 0U);
   result = move(&fixture, &state, CWIKI_MOTION_LINE_END);
   check(position_is(result.range.start, 1U, 0U) &&
       position_is(result.range.end, 1U, 0U),
       "$ on an empty line remains an empty valid range");
   state = state_at(0U, 0U);
   result = move(&fixture, &state, CWIKI_MOTION_DISPLAY_UP);
   check(result.range.shape == CWIKI_MOTION_DISPLAY_ROWS &&
       position_is(result.range.start, 0U, 0U) &&
       position_is(result.range.end, 0U, 0U),
       "failed k returns an empty display-row operator range");
   result = move(&fixture, &state, CWIKI_MOTION_SOURCE_UP);
   check(result.range.shape == CWIKI_MOTION_LINEWISE &&
       position_is(result.range.start, 0U, 0U) &&
       position_is(result.range.end, 0U, 0U),
       "failed gk returns an empty linewise operator range");
   state = state_at(1U, 0U);
   errno = 0;
   check(cwiki_motion_apply(&fixture.buffer, &fixture.layout, fixture.zones,
       &state, CWIKI_MOTION_VIEWPORT_HIGH, &bad, &result) == -1 &&
       errno == EINVAL && position_is(state.cursor, 1U, 0U),
       "invalid viewport input fails transactionally");
   fixture_free(&fixture);
}

int
main(void)
{
   test_graphemes_ranges_and_conceal();
   test_word_and_word_end_semantics();
   test_vertical_goals_rows_and_lines();
   test_document_viewport_and_line_motions();
   test_paragraphs_sentences_and_zones();
   test_boundaries_and_invalid_inputs();
   if (failures != 0) {
      (void)fprintf(stderr, "%d motion test(s) failed\n", failures);
      return EXIT_FAILURE;
   }
   (void)puts("motion tests passed");
   return EXIT_SUCCESS;
}
