#include "line_edit.h"

#include <stdbool.h>
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
is_text(const struct cwiki_line_edit *edit, const char *expected)
{
   return cwiki_line_edit_length(edit) == strlen(expected) &&
       memcmp(cwiki_line_edit_bytes(edit), expected, strlen(expected)) == 0;
}

static void
test_unicode_movement_and_editing(void)
{
   struct cwiki_line_edit *edit = NULL;
   const char initial[] = "Ae\xcc\x81\xe7\x95\x8c";

   check(cwiki_line_edit_init(&edit, initial, sizeof(initial) - 1U) ==
       CWIKI_LINE_EDIT_OK, "initialize UTF-8 line");
   check(cwiki_line_edit_cursor(edit) == sizeof(initial) - 1U,
       "initial cursor starts at end");
   check(cwiki_line_edit_control(edit, 2U) == CWIKI_LINE_EDIT_OK &&
       cwiki_line_edit_cursor(edit) == 4U,
       "C-b crosses one wide grapheme");
   check(cwiki_line_edit_control(edit, 2U) == CWIKI_LINE_EDIT_OK &&
       cwiki_line_edit_cursor(edit) == 1U,
       "C-b keeps a combining sequence indivisible");
   check(cwiki_line_edit_insert(edit, "-", 1U) == CWIKI_LINE_EDIT_OK &&
       is_text(edit, "A-e\xcc\x81\xe7\x95\x8c") &&
       cwiki_line_edit_cursor(edit) == 2U,
       "insert writes at a grapheme boundary");
   check(cwiki_line_edit_control(edit, 6U) == CWIKI_LINE_EDIT_OK &&
       cwiki_line_edit_cursor(edit) == 5U,
       "C-f crosses one combining grapheme");
   check(cwiki_line_edit_control(edit, 4U) == CWIKI_LINE_EDIT_OK &&
       is_text(edit, "A-e\xcc\x81") && cwiki_line_edit_cursor(edit) == 5U,
       "C-d deletes one whole following grapheme");
   check(cwiki_line_edit_control(edit, 8U) == CWIKI_LINE_EDIT_OK &&
       is_text(edit, "A-") && cwiki_line_edit_cursor(edit) == 2U,
       "C-h deletes one whole previous grapheme");
   cwiki_line_edit_free(edit);
}

static void
test_home_end_kill_and_word_delete(void)
{
   struct cwiki_line_edit *edit = NULL;
   const char initial[] =
       "one  \xce\xb4\xce\xbf\xce\xba\xce\xb9\xce\xbc\xce\xae  three";

   check(cwiki_line_edit_init(&edit, initial, sizeof(initial) - 1U) ==
       CWIKI_LINE_EDIT_OK, "initialize word-edit line");
   check(cwiki_line_edit_control(edit, 23U) == CWIKI_LINE_EDIT_OK &&
       is_text(edit, "one  \xce\xb4\xce\xbf\xce\xba\xce\xb9\xce\xbc\xce\xae  "),
       "C-w removes the previous word but retains earlier spacing");
   check(cwiki_line_edit_control(edit, 23U) == CWIKI_LINE_EDIT_OK &&
       is_text(edit, "one  "),
       "C-w skips spacing and removes a Unicode word");
   check(cwiki_line_edit_control(edit, 1U) == CWIKI_LINE_EDIT_OK &&
       cwiki_line_edit_cursor(edit) == 0U,
       "C-a moves to the beginning");
   check(cwiki_line_edit_control(edit, 6U) == CWIKI_LINE_EDIT_OK &&
       cwiki_line_edit_cursor(edit) == 1U,
       "C-f advances from the beginning");
   check(cwiki_line_edit_control(edit, 11U) == CWIKI_LINE_EDIT_OK &&
       is_text(edit, "o") && cwiki_line_edit_cursor(edit) == 1U,
       "C-k deletes through the end");
   check(cwiki_line_edit_control(edit, 5U) == CWIKI_LINE_EDIT_OK &&
       cwiki_line_edit_cursor(edit) == 1U,
       "C-e moves to the current end");
   check(cwiki_line_edit_control(edit, 99U) == CWIKI_LINE_EDIT_NOT_HANDLED,
       "non-editor control keys remain available to the prompt owner");
   cwiki_line_edit_free(edit);
}

static void
test_validation_and_transactional_allocation(void)
{
   struct cwiki_line_edit *edit = NULL;
   struct cwiki_line_edit *failed = (struct cwiki_line_edit *)1;
   const char invalid[] = {(char)0xc3, '('};

   check(cwiki_line_edit_init(&edit, "ok", 2U) == CWIKI_LINE_EDIT_OK,
       "initialize allocation fixture");
   check(cwiki_line_edit_insert(edit, "\n", 1U) == CWIKI_LINE_EDIT_INVALID &&
       cwiki_line_edit_insert(edit, invalid, sizeof(invalid)) ==
       CWIKI_LINE_EDIT_INVALID && is_text(edit, "ok"),
       "invalid or multiline insertion leaves text unchanged");
   cwiki_line_edit_test_fail_allocation_after(0U);
   check(cwiki_line_edit_insert(edit, " expansion", 10U) ==
       CWIKI_LINE_EDIT_NO_MEMORY && is_text(edit, "ok") &&
       cwiki_line_edit_cursor(edit) == 2U,
       "failed growth leaves text and cursor unchanged");
   cwiki_line_edit_test_fail_allocation_after(0U);
   check(cwiki_line_edit_init(&failed, "new", 3U) ==
       CWIKI_LINE_EDIT_NO_MEMORY && failed == NULL,
       "failed initialization transfers no ownership");
   cwiki_line_edit_test_reset_allocation();
   check(cwiki_line_edit_control(NULL, 1U) == CWIKI_LINE_EDIT_INVALID &&
       cwiki_line_edit_init(NULL, "", 0U) == CWIKI_LINE_EDIT_INVALID,
       "invalid owners are rejected");
   cwiki_line_edit_free(edit);
}

int
main(void)
{
   test_unicode_movement_and_editing();
   test_home_end_kill_and_word_delete();
   test_validation_and_transactional_allocation();
   if (failures != 0) {
      (void)fprintf(stderr, "%d line editor test(s) failed\n", failures);
      return 1;
   }
   (void)puts("line editor tests: ok");
   return 0;
}
