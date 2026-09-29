#include "structural_search.h"

#include <errno.h>
#include <stdlib.h>
#include <time.h>

struct cwiki_structural_search {
   struct cwiki_regex *regex;
};

static struct cwiki_structural_search_result
empty_result(enum cwiki_structural_search_status status)
{
   struct cwiki_structural_search_result result = {
      status, {0U, 0U}, {0U, 0U}, NULL, 0U, 0U,
      CWIKI_REGEX_INTERNAL_ERROR
   };

   return result;
}

enum cwiki_regex_compile_status
cwiki_structural_search_init(struct cwiki_structural_search **output,
    const char *pattern, size_t pattern_length, uint32_t options,
    uint32_t match_limit, uint32_t depth_limit,
    struct cwiki_regex_compile_error *error)
{
   struct cwiki_structural_search *search;
   enum cwiki_regex_compile_status status;

   if (output == NULL) {
      return CWIKI_REGEX_COMPILE_INTERNAL_ERROR;
   }
   *output = NULL;
   search = calloc(1U, sizeof(*search));
   if (search == NULL) {
      return CWIKI_REGEX_COMPILE_NO_MEMORY;
   }
   status = cwiki_regex_compile(&search->regex, pattern, pattern_length,
       options, match_limit, depth_limit, error);
   if (status != CWIKI_REGEX_COMPILE_OK) {
      free(search);
      return status;
   }
   *output = search;
   return CWIKI_REGEX_COMPILE_OK;
}

void
cwiki_structural_search_free(struct cwiki_structural_search *search)
{
   if (search == NULL) {
      return;
   }
   cwiki_regex_free(search->regex);
   free(search);
}

static bool
bounded_regex_status(enum cwiki_regex_status status)
{
   return status == CWIKI_REGEX_MATCH_LIMIT ||
       status == CWIKI_REGEX_DEPTH_LIMIT ||
       status == CWIKI_REGEX_JIT_STACK_LIMIT;
}

static bool
time_expired(const struct timespec *started, uint64_t budget,
    bool *clock_failed)
{
   struct timespec now;
   uint64_t seconds;
   uint64_t nanoseconds;

   if (budget == 0U) {
      return true;
   }
   if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      *clock_failed = true;
      return true;
   }
   seconds = (uint64_t)(now.tv_sec - started->tv_sec);
   if (now.tv_nsec < started->tv_nsec) {
      seconds--;
      nanoseconds = UINT64_C(1000000000) -
          (uint64_t)(started->tv_nsec - now.tv_nsec);
   } else {
      nanoseconds = (uint64_t)(now.tv_nsec - started->tv_nsec);
   }
   if (seconds > UINT64_MAX / UINT64_C(1000000000)) {
      return true;
   }
   return seconds * UINT64_C(1000000000) + nanoseconds >= budget;
}

static size_t
alternative_number(const struct cwiki_regex_result *match)
{
   size_t capture;

   for (capture = 1U; capture < match->capture_count; capture++) {
      if (match->captures[capture].start != CWIKI_REGEX_UNSET) {
         return capture;
      }
   }
   return 0U;
}

static struct cwiki_structural_search_result
matched(const struct cwiki_regex_result *match, size_t line)
{
   struct cwiki_structural_search_result result =
       empty_result(CWIKI_STRUCTURAL_SEARCH_MATCH);

   result.start.line = line;
   result.start.byte = match->captures[0U].start;
   result.end.line = line;
   result.end.byte = match->captures[0U].end;
   result.alternative_name = match->alternative;
   result.alternative_name_length = match->alternative_length;
   result.alternative_number = alternative_number(match);
   result.regex_status = CWIKI_REGEX_MATCH;
   return result;
}

static size_t
next_codepoint(const char *bytes, size_t length, size_t offset)
{
   unsigned char lead;

   if (offset >= length) {
      return length + 1U;
   }
   lead = (unsigned char)bytes[offset];
   if (lead < 0x80U) {
      return offset + 1U;
   }
   if (lead < 0xe0U) {
      return offset + 2U;
   }
   if (lead < 0xf0U) {
      return offset + 3U;
   }
   return offset + 4U;
}

static struct cwiki_structural_search_result
search_forward_line(struct cwiki_structural_search *search,
    const struct cwiki_line *line, size_t line_number, size_t offset)
{
   struct cwiki_regex_result match = cwiki_regex_execute(search->regex,
       line->bytes, line->length, offset, false);

   if (match.status == CWIKI_REGEX_MATCH) {
      return matched(&match, line_number);
   }
   if (bounded_regex_status(match.status)) {
      struct cwiki_structural_search_result result =
          empty_result(CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND);
      result.regex_status = match.status;
      return result;
   }
   if (match.status == CWIKI_REGEX_NO_MATCH) {
      return empty_result(CWIKI_STRUCTURAL_SEARCH_NOT_FOUND);
   }
   {
      struct cwiki_structural_search_result result =
          empty_result(CWIKI_STRUCTURAL_SEARCH_ERROR);
      result.regex_status = match.status;
      return result;
   }
}

static struct cwiki_structural_search_result
search_backward_line(struct cwiki_structural_search *search,
    const struct cwiki_line *line, size_t line_number, size_t limit,
    bool include_limit, const struct timespec *started, uint64_t budget,
    bool *clock_failed)
{
   struct cwiki_structural_search_result best =
       empty_result(CWIKI_STRUCTURAL_SEARCH_NOT_FOUND);
   size_t offset = 0U;

   while (offset <= line->length) {
      struct cwiki_regex_result match;
      size_t next;

      if (time_expired(started, budget, clock_failed)) {
         return empty_result(*clock_failed ? CWIKI_STRUCTURAL_SEARCH_ERROR :
             CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND);
      }
      match = cwiki_regex_execute(search->regex, line->bytes, line->length,
          offset, false);
      if (match.status == CWIKI_REGEX_NO_MATCH) {
         break;
      }
      if (bounded_regex_status(match.status)) {
         best = empty_result(CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND);
         best.regex_status = match.status;
         return best;
      }
      if (match.status != CWIKI_REGEX_MATCH) {
         best = empty_result(CWIKI_STRUCTURAL_SEARCH_ERROR);
         best.regex_status = match.status;
         return best;
      }
      if (match.captures[0U].end > limit ||
          (!include_limit && match.captures[0U].start >= limit)) {
         break;
      }
      best = matched(&match, line_number);
      next = match.captures[0U].start;
      if (next == match.captures[0U].end) {
         next = next_codepoint(line->bytes, line->length, next);
      } else {
         next = match.captures[0U].end;
      }
      if (next <= offset) {
         return empty_result(CWIKI_STRUCTURAL_SEARCH_ERROR);
      }
      offset = next;
   }
   return best;
}

struct cwiki_structural_search_result
cwiki_structural_search_run(struct cwiki_structural_search *search,
    const struct cwiki_buffer *buffer, struct cwiki_position start,
    enum cwiki_structural_search_direction direction,
    size_t max_line_distance, uint64_t time_budget_ns)
{
   struct cwiki_structural_search_result result;
   struct timespec started;
   size_t distance;
   bool clock_failed = false;

   if (search == NULL || buffer == NULL || start.line >= buffer->line_count ||
       start.byte > buffer->lines[start.line].length ||
       (direction != CWIKI_STRUCTURAL_SEARCH_FORWARD &&
       direction != CWIKI_STRUCTURAL_SEARCH_BACKWARD) ||
       clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
      errno = EINVAL;
      return empty_result(CWIKI_STRUCTURAL_SEARCH_ERROR);
   }
   for (distance = 0U; distance <= max_line_distance; distance++) {
      size_t line_number;
      size_t offset;

      if (time_expired(&started, time_budget_ns, &clock_failed)) {
         return empty_result(clock_failed ? CWIKI_STRUCTURAL_SEARCH_ERROR :
             CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND);
      }
      if (direction == CWIKI_STRUCTURAL_SEARCH_FORWARD) {
         if (distance > SIZE_MAX - start.line ||
             start.line + distance >= buffer->line_count) {
            return empty_result(CWIKI_STRUCTURAL_SEARCH_NOT_FOUND);
         }
         line_number = start.line + distance;
         offset = distance == 0U ? start.byte : 0U;
         result = search_forward_line(search, &buffer->lines[line_number],
             line_number, offset);
      } else {
         if (distance > start.line) {
            return empty_result(CWIKI_STRUCTURAL_SEARCH_NOT_FOUND);
         }
         line_number = start.line - distance;
         offset = distance == 0U ? start.byte :
             buffer->lines[line_number].length;
         result = search_backward_line(search, &buffer->lines[line_number],
             line_number, offset, distance != 0U, &started, time_budget_ns,
             &clock_failed);
      }
      if (result.status != CWIKI_STRUCTURAL_SEARCH_NOT_FOUND) {
         return result;
      }
   }
   if ((direction == CWIKI_STRUCTURAL_SEARCH_FORWARD &&
       max_line_distance >= buffer->line_count - 1U - start.line) ||
       (direction == CWIKI_STRUCTURAL_SEARCH_BACKWARD &&
       max_line_distance >= start.line)) {
      return empty_result(CWIKI_STRUCTURAL_SEARCH_NOT_FOUND);
   }
   return empty_result(CWIKI_STRUCTURAL_SEARCH_BOUNDED_NOT_FOUND);
}
