#include "regex.h"

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

static struct cwiki_regex *
compile(const char *pattern, uint32_t options, uint32_t match_limit,
    uint32_t depth_limit)
{
   struct cwiki_regex_compile_error error;
   struct cwiki_regex *regex = NULL;
   enum cwiki_regex_compile_status status;

   status = cwiki_regex_compile(&regex, pattern, strlen(pattern), options,
       match_limit, depth_limit, &error);
   if (status != CWIKI_REGEX_COMPILE_OK) {
      (void)fprintf(stderr, "FAIL: compile %s: %s (code %d, offset %zu)\n",
          pattern, error.message, error.engine_code, error.byte_offset);
      failures++;
   }
   return regex;
}

static void
test_unicode_properties(void)
{
   const char subject[] = "12 Καφέ déjà";
   struct cwiki_regex *regex = compile("\\p{Greek}+", 0U, 100000U, 1000U);
   struct cwiki_regex_result result;

   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, subject, sizeof(subject) - 1U, 0U,
       false);
   check(result.status == CWIKI_REGEX_MATCH && result.capture_count == 1U &&
       result.captures[0].start == 3U && result.captures[0].end == 11U,
       "Unicode properties find Greek letters at independently counted bytes");
   cwiki_regex_free(regex);

   regex = compile("^\\w+$", 0U, 100000U, 1000U);
   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, "élève", sizeof("élève") - 1U, 0U,
       true);
   check(result.status == CWIKI_REGEX_MATCH &&
       result.captures[0].end == sizeof("élève") - 1U,
       "UCP makes accented letters word characters");
   cwiki_regex_free(regex);
}

static void
test_codepoints_graphemes_and_casefolding(void)
{
   static const char decomposed[] = "e\xcc\x81";
   struct cwiki_regex *regex = compile("^.$", 0U, 100000U, 1000U);
   struct cwiki_regex_result result;

   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, decomposed, sizeof(decomposed) - 1U,
       0U, true);
   check(result.status == CWIKI_REGEX_NO_MATCH,
       "dot matches one codepoint rather than one grapheme");
   cwiki_regex_free(regex);

   regex = compile("^\\X$", 0U, 100000U, 1000U);
   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, decomposed, sizeof(decomposed) - 1U,
       0U, true);
   check(result.status == CWIKI_REGEX_MATCH && result.captures[0].start == 0U &&
       result.captures[0].end == 3U,
       "\\X spans the independently counted three-byte grapheme");
   cwiki_regex_free(regex);

   regex = compile("ΣÉ", CWIKI_REGEX_CASELESS, 100000U, 1000U);
   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, "σé", sizeof("σé") - 1U, 0U, true);
   check(result.status == CWIKI_REGEX_MATCH,
       "caseless option applies Unicode case folding");
   cwiki_regex_free(regex);
}

static void
test_captures_and_alternatives(void)
{
   const char pattern[] =
       "(?:(α+)(*MARK:greek)|([0-9]+)-([A-Z]+)(*MARK:code))";
   const char subject[] = "xx12-ABCyy";
   struct cwiki_regex *regex = compile(pattern, 0U, 100000U, 1000U);
   struct cwiki_regex_result result;

   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, subject, sizeof(subject) - 1U, 0U,
       false);
   check(result.status == CWIKI_REGEX_MATCH && result.capture_count == 4U,
       "asymmetric alternatives report every capture slot");
   check(result.captures[0].start == 2U && result.captures[0].end == 8U &&
       result.captures[1].start == CWIKI_REGEX_UNSET &&
       result.captures[1].end == CWIKI_REGEX_UNSET &&
       result.captures[2].start == 2U && result.captures[2].end == 4U &&
       result.captures[3].start == 5U && result.captures[3].end == 8U,
       "capture offsets distinguish unmatched and asymmetric groups");
   check(result.alternative_length == 4U &&
       memcmp(result.alternative, "code", 4U) == 0,
       "successful MARK identifies the alternative without rescanning text");
   cwiki_regex_free(regex);
}

static void
test_anchoring(void)
{
   struct cwiki_regex *regex = compile("cat", 0U, 100000U, 1000U);
   struct cwiki_regex_result result;

   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, "xxcat", 5U, 0U, false);
   check(result.status == CWIKI_REGEX_MATCH && result.captures[0].start == 2U &&
       result.captures[0].end == 5U,
       "unanchored execution searches after the start offset");
   result = cwiki_regex_execute(regex, "xxcat", 5U, 0U, true);
   check(result.status == CWIKI_REGEX_NO_MATCH,
       "anchored execution only accepts a match at the start offset");
   result = cwiki_regex_execute(regex, "xxcat", 5U, 2U, true);
   check(result.status == CWIKI_REGEX_MATCH && result.captures[0].start == 2U,
       "anchored execution honors a nonzero start offset");
   cwiki_regex_free(regex);
}

static void
test_invalid_utf_and_compile_diagnostic(void)
{
   static const char malformed[] = {'a', (char)0xc3, '(', '\0'};
   static const char continuation[] = "é";
   struct cwiki_regex_compile_error error;
   struct cwiki_regex *regex = compile(".", 0U, 100000U, 1000U);
   struct cwiki_regex_result result;
   enum cwiki_regex_compile_status status;

   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, malformed, 3U, 0U, false);
   check(result.status == CWIKI_REGEX_INVALID_UTF,
       "malformed subject UTF has its own execution status");
   result = cwiki_regex_execute(regex, continuation,
       sizeof(continuation) - 1U, 1U, false);
   check(result.status == CWIKI_REGEX_INVALID_UTF,
       "a continuation-byte start offset is invalid UTF");
   cwiki_regex_free(regex);

   regex = NULL;
   status = cwiki_regex_compile(&regex, "é)", sizeof("é)") - 1U, 0U,
       100000U, 1000U, &error);
   check(status == CWIKI_REGEX_COMPILE_INVALID_PATTERN && regex == NULL &&
       error.byte_offset == 2U && error.message[0] != '\0',
       "invalid pattern diagnostic reports an independently counted byte offset");
}

static void
test_jit_failure(void)
{
   struct cwiki_regex_compile_error error;
   struct cwiki_regex *regex = NULL;
   enum cwiki_regex_compile_status status;

   status = cwiki_regex_compile(&regex, "\\C", 2U, 0U, 100000U, 1000U,
       &error);
   check(status == CWIKI_REGEX_COMPILE_JIT_FAILED && regex == NULL &&
       error.engine_code != 0 && error.message[0] != '\0',
       "a pattern unsupported by JIT reports JIT failure, not unavailability");
}

static void
test_limits(void)
{
   static const char subject[] =
       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaX";
   struct cwiki_regex *regex = compile("^(a+)+$", 0U, 2U, 1000U);
   struct cwiki_regex_result result;

   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, subject, sizeof(subject) - 1U, 0U,
       true);
   check(result.status == CWIKI_REGEX_MATCH_LIMIT,
       "tiny match limit has a distinct execution status");
   cwiki_regex_free(regex);

   regex = compile("(*NO_START_OPT)(*NO_AUTO_POSSESS)^((a)*)*b", 0U,
       100000U, 1U);
   if (regex == NULL) {
      return;
   }
   result = cwiki_regex_execute(regex, "aaaa", 4U, 0U, true);
   check(result.status == CWIKI_REGEX_DEPTH_LIMIT,
       "tiny depth limit has a distinct execution status");
   cwiki_regex_free(regex);
}

int
main(void)
{
   test_unicode_properties();
   test_codepoints_graphemes_and_casefolding();
   test_captures_and_alternatives();
   test_anchoring();
   test_invalid_utf_and_compile_diagnostic();
   test_jit_failure();
   test_limits();

   if (failures != 0) {
      (void)fprintf(stderr, "%d regex test(s) failed\n", failures);
      return 1;
   }
   (void)puts("regex tests: ok");
   return 0;
}
