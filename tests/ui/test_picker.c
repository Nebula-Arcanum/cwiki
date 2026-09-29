#include "picker.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

static bool
item_is(const struct cwiki_picker *picker, size_t index, const char *id)
{
   struct cwiki_picker_item item;

   return cwiki_picker_at(picker, index, &item) == CWIKI_PICKER_OK &&
       strcmp(item.id, id) == 0;
}

static bool
selected_is(const struct cwiki_picker *picker, const char *id)
{
   return item_is(picker, cwiki_picker_selected(picker), id);
}

static void
test_live_filter_ranking_and_owned_metadata(void)
{
   char mutable_label[] = "Zeta function";
   const struct cwiki_picker_spec specs[] = {
      {"zeta", mutable_label, "Math"},
      {"alpha", "Alpha particle", "Physics"},
      {"alpine", "Alpine note", "Course"},
      {"greek", "\xce\x94\xce\xbf\xce\xba\xce\xb9\xce\xbc\xce\xae", "Unicode"}
   };
   struct cwiki_picker *picker = NULL;
   struct cwiki_picker_item first;
   struct cwiki_picker_item second;

   check(cwiki_picker_init(&picker, specs,
       sizeof(specs) / sizeof(specs[0])) == CWIKI_PICKER_OK,
       "initialize reusable picker");
   mutable_label[0] = 'X';
   check(cwiki_picker_count(picker) == 4U && item_is(picker, 0U, "zeta") &&
       item_is(picker, 1U, "alpha") && item_is(picker, 2U, "alpine"),
       "empty query preserves caller order and picker owns metadata");
   check(cwiki_picker_insert(picker, "ALP", 3U) == CWIKI_PICKER_OK &&
       cwiki_picker_count(picker) == 2U && item_is(picker, 0U, "alpine") &&
       item_is(picker, 1U, "alpha"),
       "case-insensitive prefix matches rank shorter labels first");
   check(cwiki_picker_at(picker, 0U, &first) == CWIKI_PICKER_OK &&
       cwiki_picker_at(picker, 1U, &second) == CWIKI_PICKER_OK &&
       first.score < second.score,
       "ranking exposes deterministic relevance scores");
   check(cwiki_picker_insert(picker, "h", 1U) == CWIKI_PICKER_OK &&
       cwiki_picker_count(picker) == 1U && item_is(picker, 0U, "alpha"),
       "live refinement removes nonmatching candidates");
   check(cwiki_picker_control(picker, 1U) == CWIKI_PICKER_OK &&
       cwiki_picker_control(picker, 11U) == CWIKI_PICKER_OK &&
       cwiki_picker_query_length(picker) == 0U &&
       cwiki_picker_count(picker) == 4U,
       "shared C-a/C-k line editing immediately restores the full list");
   cwiki_picker_free(picker);
}

static void
test_fuzzy_unicode_selection_and_empty_results(void)
{
   const struct cwiki_picker_spec specs[] = {
      {"zeta", "Zeta function", NULL},
      {"alpha", "Alpha particle", ""},
      {"alpine", "Alpine note", ""},
      {"greek", "\xce\x94\xce\xbf\xce\xba\xce\xb9\xce\xbc\xce\xae", ""}
   };
   struct cwiki_picker *picker = NULL;

   check(cwiki_picker_init(&picker, specs,
       sizeof(specs) / sizeof(specs[0])) == CWIKI_PICKER_OK,
       "initialize navigation picker");
   check(cwiki_picker_select_next(picker) == CWIKI_PICKER_OK &&
       selected_is(picker, "alpha"), "selection moves through visible rows");
   check(cwiki_picker_insert(picker, "a", 1U) == CWIKI_PICKER_OK &&
       selected_is(picker, "alpha"),
       "refilter preserves the selected identity when still visible");
   check(cwiki_picker_select_previous(picker) == CWIKI_PICKER_OK &&
       cwiki_picker_selected(picker) == 0U &&
       cwiki_picker_select_previous(picker) == CWIKI_PICKER_OK &&
       cwiki_picker_selected(picker) == 0U,
       "selection movement is bounded at the first row");
   check(cwiki_picker_control(picker, 1U) == CWIKI_PICKER_OK &&
       cwiki_picker_control(picker, 11U) == CWIKI_PICKER_OK &&
       cwiki_picker_insert(picker, "zf", 2U) == CWIKI_PICKER_OK &&
       cwiki_picker_count(picker) == 1U && item_is(picker, 0U, "zeta"),
       "nonconsecutive fuzzy matching finds ordered subsequences");
   check(cwiki_picker_control(picker, 1U) == CWIKI_PICKER_OK &&
       cwiki_picker_control(picker, 11U) == CWIKI_PICKER_OK &&
       cwiki_picker_insert(picker, "\xce\xb4\xce\xbf\xce\xba", 6U) ==
       CWIKI_PICKER_OK && cwiki_picker_count(picker) == 1U &&
       item_is(picker, 0U, "greek"),
       "Unicode case folding matches lowercase Greek query to uppercase label");
   check(cwiki_picker_control(picker, 1U) == CWIKI_PICKER_OK &&
       cwiki_picker_control(picker, 11U) == CWIKI_PICKER_OK &&
       cwiki_picker_insert(picker, "missing", 7U) == CWIKI_PICKER_OK &&
       cwiki_picker_count(picker) == 0U &&
       cwiki_picker_selected(picker) == SIZE_MAX &&
       cwiki_picker_select_next(picker) == CWIKI_PICKER_NOT_FOUND,
       "empty result state has no invalid selected row");
   cwiki_picker_free(picker);
}

static void
test_validation(void)
{
   const struct cwiki_picker_spec duplicate[] = {
      {"same", "One", ""}, {"same", "Two", ""}
   };
   const struct cwiki_picker_spec control[] = {
      {"one", "bad\nlabel", ""}
   };
   struct cwiki_picker *picker = (struct cwiki_picker *)1;
   struct cwiki_picker_item item;

   check(cwiki_picker_init(&picker, duplicate, 2U) == CWIKI_PICKER_INVALID &&
       picker == NULL, "duplicate stable IDs are rejected without ownership");
   picker = (struct cwiki_picker *)1;
   check(cwiki_picker_init(&picker, control, 1U) == CWIKI_PICKER_INVALID &&
       picker == NULL, "control bytes in visible metadata are rejected");
   check(cwiki_picker_init(&picker, NULL, 0U) == CWIKI_PICKER_OK &&
       cwiki_picker_count(picker) == 0U &&
       cwiki_picker_at(picker, 0U, &item) == CWIKI_PICKER_NOT_FOUND &&
       cwiki_picker_control(picker, 99U) == CWIKI_PICKER_NOT_HANDLED,
       "empty pickers and unowned controls have explicit states");
   check(cwiki_picker_at(NULL, 0U, &item) == CWIKI_PICKER_INVALID &&
       cwiki_picker_insert(NULL, "x", 1U) == CWIKI_PICKER_INVALID,
       "invalid picker owners are rejected");
   cwiki_picker_free(picker);
}

int
main(void)
{
   test_live_filter_ranking_and_owned_metadata();
   test_fuzzy_unicode_selection_and_empty_results();
   test_validation();
   if (failures != 0) {
      (void)fprintf(stderr, "%d picker test(s) failed\n", failures);
      return 1;
   }
   (void)puts("picker tests: ok");
   return 0;
}
