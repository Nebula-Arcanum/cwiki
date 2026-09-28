#define PCRE2_CODE_UNIT_WIDTH 8

#include "regex.h"

#include <pcre2.h>
#include <stdlib.h>
#include <string.h>

struct cwiki_regex {
   pcre2_code *code;
   pcre2_match_context *context;
   pcre2_match_data *match_data;
   pcre2_jit_stack *jit_stack;
   struct cwiki_regex_capture *captures;
   size_t capture_count;
   bool used_jit;
};

static pcre2_jit_stack *
use_jit_stack(void *data)
{
   struct cwiki_regex *regex = data;

   regex->used_jit = true;
   return regex->jit_stack;
}

static void
set_error(struct cwiki_regex_compile_error *error, int code, size_t offset)
{
   int result;

   if (error == NULL) {
      return;
   }
   error->engine_code = code;
   error->byte_offset = offset;
   error->message[0] = '\0';
   result = pcre2_get_error_message(code, (PCRE2_UCHAR *)error->message,
       sizeof(error->message));
   if (result < 0) {
      (void)strncpy(error->message, "unknown PCRE2 error",
          sizeof(error->message) - 1U);
      error->message[sizeof(error->message) - 1U] = '\0';
   }
}

static void
set_jit_failed_error(struct cwiki_regex_compile_error *error)
{
   if (error == NULL) {
      return;
   }
   error->engine_code = 0;
   error->byte_offset = CWIKI_REGEX_UNSET;
   (void)strncpy(error->message, "JIT compilation produced no executable code",
       sizeof(error->message) - 1U);
   error->message[sizeof(error->message) - 1U] = '\0';
}

static enum cwiki_regex_compile_status
prepare_match_storage(struct cwiki_regex *regex)
{
   uint32_t captures;

   if (pcre2_pattern_info(regex->code, PCRE2_INFO_CAPTURECOUNT, &captures) !=
       0) {
      return CWIKI_REGEX_COMPILE_INTERNAL_ERROR;
   }
   regex->capture_count = (size_t)captures + 1U;
   if (regex->capture_count > SIZE_MAX / sizeof(*regex->captures)) {
      return CWIKI_REGEX_COMPILE_NO_MEMORY;
   }
   regex->captures = malloc(regex->capture_count * sizeof(*regex->captures));
   regex->match_data = pcre2_match_data_create_from_pattern(regex->code, NULL);
   regex->context = pcre2_match_context_create(NULL);
   if (regex->captures == NULL || regex->match_data == NULL ||
       regex->context == NULL) {
      return CWIKI_REGEX_COMPILE_NO_MEMORY;
   }
   return CWIKI_REGEX_COMPILE_OK;
}

void
cwiki_regex_free(struct cwiki_regex *regex)
{
   if (regex == NULL) {
      return;
   }
   free(regex->captures);
   pcre2_match_data_free(regex->match_data);
   pcre2_jit_stack_free(regex->jit_stack);
   pcre2_match_context_free(regex->context);
   pcre2_code_free(regex->code);
   free(regex);
}

enum cwiki_regex_compile_status
cwiki_regex_compile(struct cwiki_regex **output, const char *pattern,
    size_t pattern_length, uint32_t options, uint32_t match_limit,
    uint32_t depth_limit, struct cwiki_regex_compile_error *error)
{
   struct cwiki_regex *regex;
   enum cwiki_regex_compile_status status;
   uint32_t compile_options = PCRE2_UTF | PCRE2_UCP;
   uint32_t jit_available;
   size_t jit_size;
   PCRE2_SIZE error_offset;
   int result;
   int error_code;

   if (output == NULL || pattern == NULL || match_limit == 0U ||
       depth_limit == 0U ||
       (options & ~(uint32_t)CWIKI_REGEX_CASELESS) != 0U) {
      set_error(error, PCRE2_ERROR_BADDATA, CWIKI_REGEX_UNSET);
      return CWIKI_REGEX_COMPILE_INTERNAL_ERROR;
   }
   *output = NULL;
   if ((options & (uint32_t)CWIKI_REGEX_CASELESS) != 0U) {
      compile_options |= PCRE2_CASELESS;
   }
   regex = calloc(1U, sizeof(*regex));
   if (regex == NULL) {
      set_error(error, PCRE2_ERROR_NOMEMORY, CWIKI_REGEX_UNSET);
      return CWIKI_REGEX_COMPILE_NO_MEMORY;
   }
   regex->code = pcre2_compile((PCRE2_SPTR)pattern, pattern_length,
       compile_options, &error_code, &error_offset, NULL);
   if (regex->code == NULL) {
      set_error(error, error_code, error_offset);
      free(regex);
      return error_code == PCRE2_ERROR_NOMEMORY ?
          CWIKI_REGEX_COMPILE_NO_MEMORY : CWIKI_REGEX_COMPILE_INVALID_PATTERN;
   }
   status = prepare_match_storage(regex);
   if (status != CWIKI_REGEX_COMPILE_OK) {
      set_error(error, status == CWIKI_REGEX_COMPILE_NO_MEMORY ?
          PCRE2_ERROR_NOMEMORY : PCRE2_ERROR_INTERNAL, CWIKI_REGEX_UNSET);
      cwiki_regex_free(regex);
      return status;
   }
   result = pcre2_set_match_limit(regex->context, match_limit);
   if (result == 0) {
      result = pcre2_set_depth_limit(regex->context, depth_limit);
   }
   if (result != 0) {
      set_error(error, result, CWIKI_REGEX_UNSET);
      cwiki_regex_free(regex);
      return CWIKI_REGEX_COMPILE_INTERNAL_ERROR;
   }
   result = pcre2_config(PCRE2_CONFIG_JIT, &jit_available);
   if (result != 0) {
      set_error(error, result, CWIKI_REGEX_UNSET);
      cwiki_regex_free(regex);
      return CWIKI_REGEX_COMPILE_INTERNAL_ERROR;
   }
   if (jit_available == 0U) {
      set_error(error, PCRE2_ERROR_JIT_BADOPTION, CWIKI_REGEX_UNSET);
      cwiki_regex_free(regex);
      return CWIKI_REGEX_COMPILE_JIT_UNSUPPORTED;
   }
   result = pcre2_jit_compile(regex->code, PCRE2_JIT_COMPLETE);
   if (result != 0) {
      set_error(error, result, CWIKI_REGEX_UNSET);
      cwiki_regex_free(regex);
      return result == PCRE2_ERROR_JIT_BADOPTION ?
          CWIKI_REGEX_COMPILE_JIT_UNSUPPORTED : CWIKI_REGEX_COMPILE_JIT_FAILED;
   }
   result = pcre2_pattern_info(regex->code, PCRE2_INFO_JITSIZE, &jit_size);
   if (result != 0) {
      set_error(error, result, CWIKI_REGEX_UNSET);
      cwiki_regex_free(regex);
      return CWIKI_REGEX_COMPILE_INTERNAL_ERROR;
   }
   if (jit_size == 0U) {
      set_jit_failed_error(error);
      cwiki_regex_free(regex);
      return CWIKI_REGEX_COMPILE_JIT_FAILED;
   }
   regex->jit_stack = pcre2_jit_stack_create(
       CWIKI_REGEX_JIT_STACK_INITIAL_SIZE, CWIKI_REGEX_JIT_STACK_MAX_SIZE,
       NULL);
   if (regex->jit_stack == NULL) {
      set_error(error, PCRE2_ERROR_NOMEMORY, CWIKI_REGEX_UNSET);
      cwiki_regex_free(regex);
      return CWIKI_REGEX_COMPILE_NO_MEMORY;
   }
   pcre2_jit_stack_assign(regex->context, use_jit_stack, regex);
   if (error != NULL) {
      error->engine_code = 0;
      error->byte_offset = CWIKI_REGEX_UNSET;
      error->message[0] = '\0';
   }
   *output = regex;
   return CWIKI_REGEX_COMPILE_OK;
}

static bool
is_utf_error(int error)
{
   return (error <= PCRE2_ERROR_UTF8_ERR1 &&
       error >= PCRE2_ERROR_UTF8_ERR21) || error == PCRE2_ERROR_BADUTFOFFSET;
}

struct cwiki_regex_result
cwiki_regex_execute(struct cwiki_regex *regex, const char *subject,
    size_t subject_length, size_t start_offset, bool anchored)
{
   struct cwiki_regex_result match = {CWIKI_REGEX_INTERNAL_ERROR, NULL, 0U,
       NULL, 0U, PCRE2_ERROR_NULL, false};
   PCRE2_SIZE *offsets;
   PCRE2_SPTR mark;
   uint32_t options = anchored ? PCRE2_ANCHORED : 0U;
   const char *input = subject;
   size_t index;
   int result;

   if (regex == NULL || (subject == NULL && subject_length != 0U) ||
       start_offset > subject_length) {
      return match;
   }
   if (input == NULL) {
      input = "";
   }
   regex->used_jit = false;
   result = pcre2_match(regex->code, (PCRE2_SPTR)input, subject_length,
       start_offset, options, regex->match_data, regex->context);
   match.used_jit = regex->used_jit;
   match.engine_code = result < 0 ? result : 0;
   if (result == PCRE2_ERROR_NOMATCH) {
      match.status = CWIKI_REGEX_NO_MATCH;
      return match;
   }
   if (result == PCRE2_ERROR_MATCHLIMIT) {
      match.status = CWIKI_REGEX_MATCH_LIMIT;
      return match;
   }
   if (result == PCRE2_ERROR_DEPTHLIMIT) {
      match.status = CWIKI_REGEX_DEPTH_LIMIT;
      return match;
   }
   if (result == PCRE2_ERROR_JIT_STACKLIMIT) {
      match.status = CWIKI_REGEX_JIT_STACK_LIMIT;
      return match;
   }
   if (is_utf_error(result)) {
      match.status = CWIKI_REGEX_INVALID_UTF;
      return match;
   }
   if (result < 0) {
      return match;
   }
   offsets = pcre2_get_ovector_pointer(regex->match_data);
   for (index = 0U; index < regex->capture_count; index++) {
      PCRE2_SIZE start = offsets[index * 2U];
      PCRE2_SIZE end = offsets[index * 2U + 1U];

      regex->captures[index].start = start == PCRE2_UNSET ?
          CWIKI_REGEX_UNSET : (size_t)start;
      regex->captures[index].end = end == PCRE2_UNSET ?
          CWIKI_REGEX_UNSET : (size_t)end;
   }
   mark = pcre2_get_mark(regex->match_data);
   match.status = CWIKI_REGEX_MATCH;
   match.captures = regex->captures;
   match.capture_count = regex->capture_count;
   match.alternative = (const char *)mark;
   match.alternative_length = mark == NULL ? 0U : strlen((const char *)mark);
   return match;
}
