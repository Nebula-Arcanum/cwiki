#include "regex.h"
#include "vim_regex.h"

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

static struct cwiki_regex *
compile_result(const struct cwiki_vim_regex_result *translated)
{
   struct cwiki_regex_compile_error error;
   struct cwiki_regex *regex = NULL;
   uint32_t options = translated->caseless ? CWIKI_REGEX_CASELESS : 0U;
   enum cwiki_regex_compile_status status = cwiki_regex_compile(&regex,
       translated->pattern, translated->pattern_length, options, 100000U,
       1000U, &error);

   if (status != CWIKI_REGEX_COMPILE_OK) {
      (void)fprintf(stderr,
          "FAIL: translated PCRE did not compile: %s (offset %zu)\n",
          error.message, error.byte_offset);
      failures++;
   }
   return regex;
}

struct pair {
   const char *vim;
   const char *pcre;
};

static void
test_translation_table(void)
{
   static const struct pair pairs[] = {
       {"^foo\\(bar\\|baz\\)\\+$",
        "(?m:^)foo(bar|baz)+(?m:$)"},
       {"literal+question? braces{}", "literal\\+question\\? braces\\{\\}"},
       {"[A-Cx\\]]\\{2,4}", "[A-Cx\\]]{2,4}"},
       {"[^a-z]\\{-3,}", "[^a-z]{3,}?"},
       {"\\<\\w\\+\\>", "(?<!\\w)(?=\\w)\\w+(?<=\\w)(?!\\w)"},
       {"\\(ab\\)c\\1", "(ab)c\\1"},
       {"\\e\\t\\r\\b\\n", "\\x{1b}\\t\\r\\x{08}\\n"},
       {"\\_.\\_s", "[\\s\\S](?:[ \\t]|\\n)"},
       {"\\x\\X\\o\\h\\a\\l\\u",
        "[0-9A-Fa-f][^0-9A-Fa-f][0-7][A-Za-z_][A-Za-z]\\p{Ll}\\p{Lu}"},
       {"\\p{Greek}\\P{Nd}", "\\p{Greek}\\P{Nd}"},
       {"\\(^a$\\|^b$\\)",
        "((?m:^)a(?m:$)|(?m:^)b(?m:$))"},
       {"^foo$\\V", "(?m:^)foo(?m:$)"},
       {"\\v^(cat|dog)+[0-9]{2,4}$",
        "(?m:^)(cat|dog)+[0-9]{2,4}(?m:$)"},
       {"\\M^a\\.\\*\\(b\\|c\\)\\+$",
        "(?m:^)a.*(b|c)+(?m:$)"},
       {"\\M\\^x\\$", "\\^x\\$"},
       {"\\V\\^a\\.\\*\\$", "(?m:^)a.*(?m:$)"},
       {"a\\V.*\\m.b", "a\\.\\*.b"}
   };
   size_t index;

   for (index = 0U; index < sizeof(pairs) / sizeof(pairs[0]); index++) {
      struct cwiki_vim_regex_result result = cwiki_vim_regex_translate(
          pairs[index].vim, strlen(pairs[index].vim), CWIKI_TYPED_REGEX_VIM);
      struct cwiki_regex *regex;

      check(result.status == CWIKI_VIM_REGEX_OK,
          "table Vim pattern translates successfully");
      if (result.status != CWIKI_VIM_REGEX_OK) {
         (void)fprintf(stderr, "  pattern: %s; diagnostic: %s at %zu\n",
             pairs[index].vim, result.diagnostic, result.source_offset);
         continue;
      }
      check(result.pattern_length == strlen(pairs[index].pcre) &&
          memcmp(result.pattern, pairs[index].pcre,
              result.pattern_length) == 0,
          "table translation has exact asymmetric PCRE bytes");
      if (result.pattern_length != strlen(pairs[index].pcre) ||
          memcmp(result.pattern, pairs[index].pcre,
              result.pattern_length) != 0) {
         (void)fprintf(stderr, "  Vim: %s\n  want: %s\n  got: %s\n",
             pairs[index].vim, pairs[index].pcre, result.pattern);
      }
      regex = compile_result(&result);
      cwiki_regex_free(regex);
      cwiki_vim_regex_result_destroy(&result);
   }
}

static void
test_smartcase_and_pcre_copy(void)
{
   static const char direct[] = "\\p{Lu}+café";
   struct cwiki_vim_regex_result result = cwiki_vim_regex_translate(
       "καφέ", sizeof("καφέ") - 1U, CWIKI_TYPED_REGEX_VIM);

   check(result.status == CWIKI_VIM_REGEX_OK && result.caseless,
       "lowercase Unicode Vim pattern requests caseless compilation");
   cwiki_vim_regex_result_destroy(&result);

   result = cwiki_vim_regex_translate("καΦέ", sizeof("καΦέ") - 1U,
       CWIKI_TYPED_REGEX_VIM);
   check(result.status == CWIKI_VIM_REGEX_OK && !result.caseless,
       "unescaped uppercase Unicode literal disables caseless compilation");
   cwiki_vim_regex_result_destroy(&result);

   result = cwiki_vim_regex_translate("\\p{Lu}καφέ", sizeof("\\p{Lu}καφέ") - 1U,
       CWIKI_TYPED_REGEX_VIM);
   check(result.status == CWIKI_VIM_REGEX_OK && result.caseless,
       "uppercase Unicode property control text does not trigger smartcase");
   cwiki_vim_regex_result_destroy(&result);

   result = cwiki_vim_regex_translate(direct, sizeof(direct) - 1U,
       CWIKI_TYPED_REGEX_PCRE);
   check(result.status == CWIKI_VIM_REGEX_OK && result.caseless &&
       result.pattern_length == sizeof(direct) - 1U &&
       memcmp(result.pattern, direct, sizeof(direct) - 1U) == 0 &&
       result.pattern != direct,
       "PCRE dialect returns an owned byte-for-byte copy with smartcase");
   cwiki_vim_regex_result_destroy(&result);

   result = cwiki_vim_regex_translate("Σigma", sizeof("Σigma") - 1U,
       CWIKI_TYPED_REGEX_PCRE);
   check(result.status == CWIKI_VIM_REGEX_OK && !result.caseless,
       "PCRE dialect detects an uppercase Unicode literal");
   cwiki_vim_regex_result_destroy(&result);

   result = cwiki_vim_regex_translate("(?<Upper>café)",
       sizeof("(?<Upper>café)") - 1U, CWIKI_TYPED_REGEX_PCRE);
   check(result.status == CWIKI_VIM_REGEX_OK && result.caseless,
       "uppercase PCRE group-name control text does not trigger smartcase");
   cwiki_vim_regex_result_destroy(&result);
}

static void
check_rejection(const char *pattern, enum cwiki_vim_regex_status status,
    size_t offset, const char *diagnostic)
{
   struct cwiki_vim_regex_result result = cwiki_vim_regex_translate(pattern,
       strlen(pattern), CWIKI_TYPED_REGEX_VIM);

   check(result.status == status && result.pattern == NULL &&
       result.pattern_length == 0U && result.source_offset == offset &&
       strcmp(result.diagnostic, diagnostic) == 0,
       "rejection has exact status, offset, diagnostic, and no partial output");
   cwiki_vim_regex_result_destroy(&result);
}

static void
test_rejections(void)
{
   static const char malformed_utf8[] = {'a', (char)0xc3, '(', '\0'};
   struct cwiki_vim_regex_result result;

   check_rejection("a\\zsb", CWIKI_VIM_REGEX_UNSUPPORTED, 1U, "Vim \\zs");
   check_rejection("a\\zeb", CWIKI_VIM_REGEX_UNSUPPORTED, 1U, "Vim \\ze");
   check_rejection("x\\%V", CWIKI_VIM_REGEX_UNSUPPORTED, 1U, "Vim \\%V");
   check_rejection("x\\%d123", CWIKI_VIM_REGEX_UNSUPPORTED, 1U,
       "Vim decimal character atom \\%d123");
   check_rejection("[[=a=]]", CWIKI_VIM_REGEX_UNSUPPORTED, 1U,
       "Vim collection equivalence class");
   check_rejection("[[.ch.]]", CWIKI_VIM_REGEX_UNSUPPORTED, 1U,
       "Vim collection collating element");
   check_rejection("[[:alpha:]]", CWIKI_VIM_REGEX_UNSUPPORTED, 1U,
       "Vim POSIX character class");
   check_rejection("\\(abc", CWIKI_VIM_REGEX_MALFORMED, 5U,
       "unterminated Vim group");
   check_rejection("abc\\", CWIKI_VIM_REGEX_MALFORMED, 3U,
       "trailing Vim backslash");
   check_rejection("[abc", CWIKI_VIM_REGEX_MALFORMED, 0U,
       "unterminated Vim collection");
   check_rejection("\\+abc", CWIKI_VIM_REGEX_MALFORMED, 0U,
       "Vim quantifier has no preceding atom");
   check_rejection("a\\{4,2}", CWIKI_VIM_REGEX_MALFORMED, 1U,
       "descending Vim repetition bounds");
   check_rejection("[z-a]", CWIKI_VIM_REGEX_MALFORMED, 3U,
       "descending Vim collection range");
   check_rejection("\\9", CWIKI_VIM_REGEX_MALFORMED, 0U,
       "Vim backreference has no preceding group");
   check_rejection("\\@=", CWIKI_VIM_REGEX_UNSUPPORTED, 0U,
       "Vim lookaround atom \\@");
   check_rejection("\\cabc", CWIKI_VIM_REGEX_UNSUPPORTED, 0U,
       "Vim inline case override");
   check_rejection("\\p{NotAProperty}", CWIKI_VIM_REGEX_UNSUPPORTED, 0U,
       "unsupported Unicode property");

   result = cwiki_vim_regex_translate(malformed_utf8, 3U,
       CWIKI_TYPED_REGEX_VIM);
   check(result.status == CWIKI_VIM_REGEX_INVALID_UTF8 &&
       result.source_offset == 1U && result.pattern == NULL,
       "malformed UTF-8 reports its source byte offset without output");
   cwiki_vim_regex_result_destroy(&result);
}

static void
check_match(const char *vim, const char *subject,
    enum cwiki_regex_status expected, size_t expected_start,
    size_t expected_end, const char *message)
{
   struct cwiki_vim_regex_result translated = cwiki_vim_regex_translate(vim,
       strlen(vim), CWIKI_TYPED_REGEX_VIM);
   struct cwiki_regex *regex;
   struct cwiki_regex_result match;

   check(translated.status == CWIKI_VIM_REGEX_OK, message);
   if (translated.status != CWIKI_VIM_REGEX_OK) {
      return;
   }
   regex = compile_result(&translated);
   if (regex != NULL) {
      match = cwiki_regex_execute(regex, subject, strlen(subject), 0U, false);
      check(match.status == expected &&
          (expected != CWIKI_REGEX_MATCH ||
           (match.captures[0].start == expected_start &&
            match.captures[0].end == expected_end)), message);
   }
   cwiki_regex_free(regex);
   cwiki_vim_regex_result_destroy(&translated);
}

static void
test_matching_differences(void)
{
   static const char backspace[] = {'x', '\b', 'y', '\0'};

   check_match("a+b", "xxa+bzz", CWIKI_REGEX_MATCH, 2U, 5U,
       "default-magic plus is literal");
   check_match("a\\+b", "xxaaabzz", CWIKI_REGEX_MATCH, 2U, 6U,
       "escaped default-magic plus repeats");
   check_match("\\<cat\\>", "a cat! scatter", CWIKI_REGEX_MATCH, 2U, 5U,
       "Vim start/end word atoms preserve whole-word behavior");
   check_match("x\\by", backspace, CWIKI_REGEX_MATCH, 0U, 3U,
       "Vim backslash-b matches backspace rather than PCRE word boundary");
   check_match("a\\sb", "a\nb", CWIKI_REGEX_NO_MATCH, 0U, 0U,
       "Vim whitespace class excludes newline");
   check_match("a\\_.b", "a\nb", CWIKI_REGEX_MATCH, 0U, 3U,
       "Vim newline-inclusive dot spans a line ending");
   check_match("^[A-C]\\{2}$", "zz\nBC\nyy", CWIKI_REGEX_MATCH, 3U, 5U,
       "translated Vim anchors apply at internal line boundaries");
}

int
main(void)
{
   test_translation_table();
   test_smartcase_and_pcre_copy();
   test_rejections();
   test_matching_differences();

   if (failures != 0) {
      (void)fprintf(stderr, "%d Vim regex test(s) failed\n", failures);
      return 1;
   }
   (void)puts("Vim regex tests: ok");
   return 0;
}
