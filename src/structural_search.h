#ifndef CWIKI_STRUCTURAL_SEARCH_H
#define CWIKI_STRUCTURAL_SEARCH_H

#include "buffer.h"
#include "regex.h"

#include <stddef.h>
#include <stdint.h>

/* Prefix a token with NOT_ESCAPED. Put NOT_IN_PERCENT_COMMENT first. */
#define CWIKI_STRUCTURAL_NOT_ESCAPED "(?<!\\\\)(?:\\\\\\\\)*\\K"
/* Line-anchored PCRE2 equivalent of the unbounded negative-lookbehind idiom. */
#define CWIKI_STRUCTURAL_NOT_IN_PERCENT_COMMENT \
   "(?m)^(?:[^%\\\\]|\\\\.)*?\\K"

enum cwiki_structural_search_direction {
   CWIKI_STRUCTURAL_SEARCH_FORWARD,
   CWIKI_STRUCTURAL_SEARCH_BACKWARD
};

enum cwiki_structural_search_status {
   CWIKI_STRUCTURAL_SEARCH_MATCH,
   CWIKI_STRUCTURAL_SEARCH_NOT_FOUND,
   CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND,
   CWIKI_STRUCTURAL_SEARCH_ERROR
};

struct cwiki_structural_search_result {
   enum cwiki_structural_search_status status;
   struct cwiki_position start;
   struct cwiki_position end;
   /* Successful (*MARK:name), valid until the search object is freed. */
   const char *alternative_name;
   size_t alternative_name_length;
   /* Lowest-numbered participating capture, or zero when none participated. */
   size_t alternative_number;
   enum cwiki_regex_status regex_status;
};

struct cwiki_structural_search;

enum cwiki_regex_compile_status cwiki_structural_search_init(
    struct cwiki_structural_search **search, const char *pattern,
    size_t pattern_length, uint32_t options, uint32_t match_limit,
    uint32_t depth_limit, struct cwiki_regex_compile_error *error);
void cwiki_structural_search_free(struct cwiki_structural_search *search);

/*
 * Forward search includes start. Backward search accepts matches ending at or
 * before start and returns the nearest one. A line distance of zero searches
 * only start.line. The time budget is elapsed CLOCK_MONOTONIC nanoseconds.
 */
struct cwiki_structural_search_result cwiki_structural_search_run(
    struct cwiki_structural_search *search, const struct cwiki_buffer *buffer,
    struct cwiki_position start,
    enum cwiki_structural_search_direction direction,
    size_t max_line_distance, uint64_t time_budget_ns);

#endif
