#ifndef CWIKI_REGEX_H
#define CWIKI_REGEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CWIKI_REGEX_ERROR_MESSAGE_SIZE 256U
#define CWIKI_REGEX_JIT_STACK_INITIAL_SIZE (32U * 1024U)
/* PCRE2 recommends 512 KiB to 1 MiB as enough for any pattern. */
#define CWIKI_REGEX_JIT_STACK_MAX_SIZE (512U * 1024U)
#define CWIKI_REGEX_UNSET SIZE_MAX

enum cwiki_regex_compile_option {
   CWIKI_REGEX_CASELESS = 1U << 0
};

enum cwiki_regex_compile_status {
   CWIKI_REGEX_COMPILE_OK,
   CWIKI_REGEX_COMPILE_INVALID_PATTERN,
   CWIKI_REGEX_COMPILE_JIT_UNSUPPORTED,
   CWIKI_REGEX_COMPILE_JIT_FAILED,
   CWIKI_REGEX_COMPILE_NO_MEMORY,
   CWIKI_REGEX_COMPILE_INTERNAL_ERROR
};

struct cwiki_regex_compile_error {
   int engine_code;
   size_t byte_offset;
   char message[CWIKI_REGEX_ERROR_MESSAGE_SIZE];
};

enum cwiki_regex_status {
   CWIKI_REGEX_MATCH,
   CWIKI_REGEX_NO_MATCH,
   CWIKI_REGEX_MATCH_LIMIT,
   CWIKI_REGEX_DEPTH_LIMIT,
   CWIKI_REGEX_JIT_STACK_LIMIT,
   CWIKI_REGEX_INVALID_UTF,
   CWIKI_REGEX_INTERNAL_ERROR
};

struct cwiki_regex_capture {
   size_t start;
   size_t end;
};

struct cwiki_regex_result {
   enum cwiki_regex_status status;
   /* Capture zero is the whole match; unmatched groups use CWIKI_REGEX_UNSET. */
   const struct cwiki_regex_capture *captures;
   size_t capture_count;
   /* The successful (*MARK:name), if any. */
   const char *alternative;
   size_t alternative_length;
   int engine_code;
   /* True only when PCRE2 invoked the assigned JIT-stack callback. */
   bool used_jit;
};

struct cwiki_regex;

enum cwiki_regex_compile_status cwiki_regex_compile(
    struct cwiki_regex **regex, const char *pattern, size_t pattern_length,
    uint32_t options, uint32_t match_limit, uint32_t depth_limit,
    struct cwiki_regex_compile_error *error);
void cwiki_regex_free(struct cwiki_regex *regex);
/* Returned pointers remain valid until this pattern is executed again or freed. */
struct cwiki_regex_result cwiki_regex_execute(struct cwiki_regex *regex,
    const char *subject, size_t subject_length, size_t start_offset,
    bool anchored);

#endif
