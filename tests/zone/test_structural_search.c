#include "buffer.h"
#include "structural_search.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MATCH_LIMIT 100000U
#define DEPTH_LIMIT 1000U
#define TEST_BUDGET_NS UINT64_C(1000000000)

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static struct cwiki_structural_search *
compile(const char *pattern, uint32_t match_limit)
{
   struct cwiki_regex_compile_error error;
   struct cwiki_structural_search *search = NULL;

   check(cwiki_structural_search_init(&search, pattern, strlen(pattern), 0U,
       match_limit, DEPTH_LIMIT, &error) == CWIKI_REGEX_COMPILE_OK,
       "compile structural search through shared regex runtime");
   return search;
}

static void
load(struct cwiki_buffer *buffer, const char *text)
{
   check(cwiki_buffer_init(buffer) == 0, "initialize search buffer");
   check(cwiki_buffer_load(buffer, text, strlen(text)) == 0,
       "load search fixture");
}

static void
test_directions_and_alternatives(void)
{
   const char pattern[] =
       "(?:(\\\\begin)(*MARK:open)|(\\\\end)(*MARK:close))";
   struct cwiki_structural_search *search = compile(pattern, MATCH_LIMIT);
   struct cwiki_buffer buffer;
   struct cwiki_structural_search_result result;

   if (search == NULL) {
      return;
   }
   load(&buffer, "zero \\begin\nmid \\end then \\begin\nlast \\end\n");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){0U, 5U}, CWIKI_STRUCTURAL_SEARCH_FORWARD, 2U,
       TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_MATCH &&
       result.start.line == 0U && result.start.byte == 5U &&
       result.alternative_number == 1U &&
       result.alternative_name_length == 4U &&
       memcmp(result.alternative_name, "open", 4U) == 0,
       "forward search includes its start and returns named/numbered alternative");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){2U, buffer.lines[2U].length},
       CWIKI_STRUCTURAL_SEARCH_BACKWARD, 2U, TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_MATCH &&
       result.start.line == 2U && result.start.byte == 5U &&
       result.alternative_number == 2U &&
       result.alternative_name_length == 5U &&
       memcmp(result.alternative_name, "close", 5U) == 0,
       "backward search returns nearest asymmetric close alternative");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){1U, 18U}, CWIKI_STRUCTURAL_SEARCH_BACKWARD, 0U,
       TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_MATCH &&
       result.start.line == 1U && result.start.byte == 4U,
       "backward search ignores a match extending beyond its start bound");
   cwiki_buffer_free(&buffer);
   cwiki_structural_search_free(search);
}

static void
test_line_and_time_bounds(void)
{
   struct cwiki_structural_search *search = compile("needle", MATCH_LIMIT);
   struct cwiki_buffer buffer;
   struct cwiki_structural_search_result result;

   if (search == NULL) {
      return;
   }
   load(&buffer, "top\nleft\norigin\nright\nneedle\n");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){2U, 0U}, CWIKI_STRUCTURAL_SEARCH_FORWARD, 1U,
       TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND,
       "forward line-distance exhaustion is distinct from ordinary absence");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){2U, 0U}, CWIKI_STRUCTURAL_SEARCH_FORWARD, 2U,
       TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_MATCH &&
       result.start.line == 4U,
       "forward line-distance boundary is inclusive");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){0U, 0U}, CWIKI_STRUCTURAL_SEARCH_BACKWARD, 7U,
       TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_NOT_FOUND,
       "reaching the backward buffer edge reports ordinary absence");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){2U, 0U}, CWIKI_STRUCTURAL_SEARCH_FORWARD,
       SIZE_MAX, 0U);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND,
       "expired monotonic budget returns bounded absence before scanning");
   cwiki_buffer_free(&buffer);
   cwiki_structural_search_free(search);
}

static void
test_escape_and_comment_idioms(void)
{
   const char pattern[] = CWIKI_STRUCTURAL_NOT_IN_PERCENT_COMMENT
       CWIKI_STRUCTURAL_NOT_ESCAPED "TARGET";
   struct cwiki_structural_search *search = compile(pattern, MATCH_LIMIT);
   struct cwiki_buffer buffer;
   struct cwiki_structural_search_result result;

   if (search == NULL) {
      return;
   }
   load(&buffer,
       "\\TARGET odd\n"
       "\\\\TARGET even\n"
       "prefix % TARGET commented\n"
       "prefix \\% TARGET escaped-comment\n");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){0U, 0U}, CWIKI_STRUCTURAL_SEARCH_FORWARD, 3U,
       TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_MATCH &&
       result.start.line == 1U && result.start.byte == 2U,
       "odd slash escapes target while even slash run admits it");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){2U, 0U}, CWIKI_STRUCTURAL_SEARCH_FORWARD, 1U,
       TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_MATCH &&
       result.start.line == 3U,
       "unescaped percent hides target but escaped percent does not");
   cwiki_buffer_free(&buffer);
   cwiki_structural_search_free(search);
}

static void
test_regex_bound(void)
{
   const char pattern[] = "(*NO_AUTO_POSSESS)^(a+)+$";
   struct cwiki_structural_search *search = compile(pattern, 2U);
   struct cwiki_buffer buffer;
   struct cwiki_structural_search_result result;

   if (search == NULL) {
      return;
   }
   load(&buffer,
       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaX\n");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){0U, 0U}, CWIKI_STRUCTURAL_SEARCH_FORWARD, 0U,
       TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND &&
       result.regex_status == CWIKI_REGEX_MATCH_LIMIT,
       "shared regex resource exhaustion degrades to bounded absence");
   cwiki_buffer_free(&buffer);
   cwiki_structural_search_free(search);
}

static void
test_zero_length_backward_progress(void)
{
   struct cwiki_structural_search *search = compile("(?=a)", MATCH_LIMIT);
   struct cwiki_buffer buffer;
   struct cwiki_structural_search_result result;

   if (search == NULL) {
      return;
   }
   load(&buffer, "a\xce\xb2" "a\n");
   result = cwiki_structural_search_run(search, &buffer,
       (struct cwiki_position){0U, buffer.lines[0U].length},
       CWIKI_STRUCTURAL_SEARCH_BACKWARD, 0U, TEST_BUDGET_NS);
   check(result.status == CWIKI_STRUCTURAL_SEARCH_MATCH &&
       result.start.byte == 3U && result.end.byte == 3U,
       "backward zero-length matches advance across UTF-8 without stalling");
   cwiki_buffer_free(&buffer);
   cwiki_structural_search_free(search);
}

int
main(void)
{
   test_directions_and_alternatives();
   test_line_and_time_bounds();
   test_escape_and_comment_idioms();
   test_regex_bound();
   test_zero_length_backward_progress();
   if (failures != 0) {
      return 1;
   }
   (void)puts("structural search tests: ok");
   return 0;
}
