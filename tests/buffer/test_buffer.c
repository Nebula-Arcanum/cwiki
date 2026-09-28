#include "buffer.h"
#include "unicode.h"

#include <stdint.h>
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

static void
check_line(const struct cwiki_buffer *buffer, size_t line, const char *expected,
    size_t length, const char *message)
{
   check(line < buffer->line_count && buffer->lines[line].length == length &&
       (length == 0U || memcmp(buffer->lines[line].bytes, expected, length) == 0),
       message);
}

static void
check_encoding(const struct cwiki_buffer *buffer, const char *expected,
    size_t expected_length, const char *message)
{
   char *actual = NULL;
   size_t actual_length = 0U;
   int result = cwiki_buffer_encode(buffer, &actual, &actual_length);

   check(result == 0 && actual_length == expected_length &&
       (expected_length == 0U ||
       memcmp(actual, expected, expected_length) == 0), message);
   free(actual);
}

static void
test_line_endings(void)
{
   struct cwiki_buffer buffer;
   const char lf[] = "alpha\n\xce\xb2" "eta\n";
   const char crlf[] = "left\r\nright\r\n";
   const char mixed[] = "one\r\ntwo\n";

   check(cwiki_buffer_init(&buffer) == 0, "initialize one-line LF buffer");
   check(buffer.line_count == 1U &&
       buffer.line_ending == CWIKI_LINE_ENDING_LF,
       "new buffer defaults to one empty LF line");
   check(cwiki_buffer_load(&buffer, lf, sizeof(lf) - 1U) == 0,
       "load valid LF UTF-8");
   check(buffer.line_count == 3U &&
       buffer.line_ending == CWIKI_LINE_ENDING_LF,
       "LF load keeps trailing empty line");
   check_line(&buffer, 1U, "\xce\xb2" "eta", 5U,
       "line owns exact non-normalized UTF-8 bytes");
   check_encoding(&buffer, lf, sizeof(lf) - 1U, "LF bytes round trip exactly");

   check(cwiki_buffer_load(&buffer, crlf, sizeof(crlf) - 1U) == 0,
       "load uniform CRLF input");
   check(buffer.line_ending == CWIKI_LINE_ENDING_CRLF &&
       buffer.line_count == 3U, "CRLF style is recorded at buffer level");
   check_line(&buffer, 0U, "left", 4U, "CR is absent from in-memory line");
   check_encoding(&buffer, crlf, sizeof(crlf) - 1U,
       "CRLF bytes round trip exactly");
   check(cwiki_buffer_load(&buffer, mixed, sizeof(mixed) - 1U) == -1,
       "reject mixed line endings that cannot round trip by one style");
   check_encoding(&buffer, crlf, sizeof(crlf) - 1U,
       "failed mixed-ending load leaves prior buffer unchanged");
   cwiki_buffer_free(&buffer);
}

static void
test_utf8_and_graphemes(void)
{
   const char decomposed[] = "e\xcc\x81x";
   const char family[] =
       "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d"
       "\xf0\x9f\x91\xa7";
   const char flag[] = "\xf0\x9f\x87\xa8\xf0\x9f\x87\xa6";
   const char malformed_lead[] = "\xc0\xaf";
   const char malformed_tail[] = "\xe2\x82";
   const char malformed_continuation[] = "\x80";
   const char surrogate[] = "\xed\xa0\x80";
   const char heart[] = "\xe2\x9d\xa4";
   const char heart_emoji[] = "\xe2\x9d\xa4\xef\xb8\x8f";
   const char cjk[] = "\xe7\x95\x8c";
   const char combining[] = "\xcc\x81";
   const char soft_hyphen[] = "\xc2\xad";

   check(cwiki_utf8_validate(decomposed, sizeof(decomposed) - 1U),
       "accept valid decomposed UTF-8 without normalizing bytes");
   check(!cwiki_utf8_validate(malformed_lead, sizeof(malformed_lead) - 1U),
       "reject overlong UTF-8");
   check(!cwiki_utf8_validate(malformed_tail, sizeof(malformed_tail) - 1U),
       "reject truncated UTF-8");
   check(!cwiki_utf8_validate(malformed_continuation,
       sizeof(malformed_continuation) - 1U), "reject stray continuation byte");
   check(!cwiki_utf8_validate(surrogate, sizeof(surrogate) - 1U),
       "reject UTF-8 encoded surrogate");

   check(cwiki_grapheme_next(decomposed, sizeof(decomposed) - 1U, 0U) == 3U,
       "combining sequence is one grapheme");
   check(!cwiki_grapheme_boundary(decomposed, sizeof(decomposed) - 1U, 1U) &&
       cwiki_grapheme_boundary(decomposed, sizeof(decomposed) - 1U, 3U),
       "only complete cluster byte offsets are grapheme boundaries");
   check(cwiki_grapheme_previous(decomposed, sizeof(decomposed) - 1U, 4U) == 3U,
       "previous grapheme uses asymmetric final boundary");
   check(cwiki_grapheme_next(family, sizeof(family) - 1U, 0U) ==
       sizeof(family) - 1U, "ZWJ family is one extended grapheme");
   check(cwiki_grapheme_next(flag, sizeof(flag) - 1U, 0U) ==
       sizeof(flag) - 1U, "regional-indicator pair is one grapheme");
   check(cwiki_grapheme_next(malformed_tail, sizeof(malformed_tail) - 1U, 0U) ==
       SIZE_MAX, "grapheme traversal rejects malformed UTF-8");

   check(cwiki_grapheme_width("A", 1U) == 1, "ASCII cluster is one cell");
   check(cwiki_grapheme_width(cjk, sizeof(cjk) - 1U) == 2,
       "East Asian wide base is two cells");
   check(cwiki_grapheme_width(heart, sizeof(heart) - 1U) == 1,
       "text-presentation heart is one cell");
   check(cwiki_grapheme_width(heart_emoji, sizeof(heart_emoji) - 1U) == 2,
       "VS16 makes its base two cells");
   check(cwiki_grapheme_width(family, sizeof(family) - 1U) == 2,
       "emoji ZWJ cluster takes its base width");
   check(cwiki_grapheme_width(flag, sizeof(flag) - 1U) == 2,
       "emoji-presentation regional-indicator cluster is two cells");
   check(cwiki_grapheme_width(combining, sizeof(combining) - 1U) == 0,
       "standalone combining cluster is zero cells");
   check(cwiki_grapheme_width(soft_hyphen, sizeof(soft_hyphen) - 1U) == 0,
       "default-ignorable cluster is zero cells");
   check(cwiki_grapheme_width("ab", 2U) == -1,
       "width requires exactly one cluster");
}

static void
test_edits_and_positions(void)
{
   struct cwiki_buffer buffer;
   struct cwiki_position before = {0U, 1U};
   struct cwiki_position at = {0U, 2U};
   struct cwiki_position after = {0U, 5U};
   struct cwiki_position later = {1U, 1U};
   const char initial[] = "abcdef\nXYZ";
   const char malformed[] = "\xf0\x28\x8c\xbc";
   const char multibyte[] = "\xce\xb2";

   check(cwiki_buffer_init(&buffer) == 0, "initialize edit buffer");
   check(cwiki_buffer_load(&buffer, initial, sizeof(initial) - 1U) == 0,
       "load edit fixture");
   check(cwiki_buffer_register_position(&buffer, &before) == 0 &&
       cwiki_buffer_register_position(&buffer, &at) == 0 &&
       cwiki_buffer_register_position(&buffer, &after) == 0 &&
       cwiki_buffer_register_position(&buffer, &later) == 0,
       "register asymmetric live positions");
   check(cwiki_buffer_insert(&buffer, 0U, 2U, "pq", 2U) == 0,
       "insert at interior byte boundary");
   check(before.byte == 1U && at.byte == 4U && after.byte == 7U &&
       later.line == 1U && later.byte == 1U,
       "insert shifts same-line positions at or after offset only");
   check_line(&buffer, 0U, "abpqcdef", 8U,
       "insert produces independently expected bytes");
   check(cwiki_buffer_delete(&buffer, 0U, 3U, 3U) == 0,
       "delete asymmetric interior byte range");
   check(before.byte == 1U && at.byte == 3U && after.byte == 4U,
       "delete collapses covered positions and shifts following positions");
   check_line(&buffer, 0U, "abpef", 5U,
       "delete produces independently expected bytes");

   check(cwiki_buffer_split(&buffer, 0U, 3U) == 0,
       "split line at asymmetric interior offset");
   check(buffer.line_count == 3U && at.line == 1U && at.byte == 0U &&
       after.line == 1U && after.byte == 1U && later.line == 2U,
       "split moves positions at split and increments later line indices");
   check_line(&buffer, 0U, "abp", 3U, "split keeps expected head bytes");
   check_line(&buffer, 1U, "ef", 2U, "split moves expected tail bytes");
   check(cwiki_buffer_join(&buffer, 0U) == 0,
       "join adjacent lines after split");
   check(buffer.line_count == 2U && at.line == 0U && at.byte == 3U &&
       after.line == 0U && after.byte == 4U && later.line == 1U,
       "join restores line indices and offsets deterministically");
   check_encoding(&buffer, "abpef\nXYZ", 9U,
       "split then join preserves expected LF bytes");

   check(cwiki_buffer_insert(&buffer, 0U, 1U, malformed,
       sizeof(malformed) - 1U) == -1, "reject malformed UTF-8 insert");
   check(cwiki_buffer_insert(&buffer, 0U, 1U, "\n", 1U) == -1,
       "line insert rejects embedded newline");
   check(cwiki_buffer_insert(&buffer, 1U, 0U, multibyte,
       sizeof(multibyte) - 1U) == 0,
       "insert valid multi-byte codepoint");
   check(cwiki_buffer_insert(&buffer, 1U, 1U, "q", 1U) == -1,
       "reject insert inside multi-byte codepoint");
   check(cwiki_buffer_delete(&buffer, 1U, 0U, 1U) == -1,
       "reject delete ending inside multi-byte codepoint");
   check(cwiki_buffer_delete(&buffer, 1U, 0U, sizeof(multibyte) - 1U) == 0,
       "delete complete multi-byte codepoint");
   check(cwiki_buffer_delete(&buffer, 0U, 99U, 1U) == -1,
       "reject delete beyond line");
   check_encoding(&buffer, "abpef\nXYZ", 9U,
       "invalid edits leave bytes unchanged");
   cwiki_buffer_unregister_position(&buffer, &at);
   check(buffer.position_count == 3U, "unregister one live position");
   cwiki_buffer_free(&buffer);
}

static void
test_degraded_line(void)
{
   struct cwiki_buffer buffer;
   char *large = malloc(CWIKI_DEGRADED_LINE_BYTES + 1U);

   check(large != NULL, "allocate degradation-boundary fixture");
   if (large == NULL) {
      return;
   }
   memset(large, 'x', CWIKI_DEGRADED_LINE_BYTES + 1U);
   check(cwiki_buffer_init(&buffer) == 0, "initialize degradation buffer");
   check(cwiki_buffer_load(&buffer, large, CWIKI_DEGRADED_LINE_BYTES) == 0 &&
       !cwiki_buffer_line_degraded(&buffer, 0U),
       "exactly 1 MiB is not degraded");
   check(cwiki_buffer_load(&buffer, large, CWIKI_DEGRADED_LINE_BYTES + 1U) == 0 &&
       cwiki_buffer_line_degraded(&buffer, 0U),
       "line longer than 1 MiB reports degraded state");
   check(!cwiki_buffer_line_degraded(&buffer, 1U),
       "out-of-range line is not reported degraded");
   cwiki_buffer_free(&buffer);
   free(large);
}

struct model {
   char *bytes;
   size_t length;
   size_t capacity;
   size_t positions[5];
};

static void
model_reserve(struct model *model, size_t needed)
{
   size_t capacity = model->capacity == 0U ? 16U : model->capacity;
   char *grown;

   while (capacity < needed) {
      capacity *= 2U;
   }
   grown = realloc(model->bytes, capacity);
   if (grown == NULL) {
      (void)fprintf(stderr, "model allocation failed\n");
      exit(2);
   }
   model->bytes = grown;
   model->capacity = capacity;
}

static void
model_insert(struct model *model, size_t offset, char byte)
{
   size_t index;

   model_reserve(model, model->length + 1U);
   memmove(model->bytes + offset + 1U, model->bytes + offset,
       model->length - offset);
   model->bytes[offset] = byte;
   model->length++;
   for (index = 0U; index < 5U; index++) {
      if (model->positions[index] >= offset) {
         model->positions[index]++;
      }
   }
}

static void
model_delete(struct model *model, size_t offset, size_t length)
{
   size_t index;
   size_t end = offset + length;

   memmove(model->bytes + offset, model->bytes + end, model->length - end);
   model->length -= length;
   for (index = 0U; index < 5U; index++) {
      if (model->positions[index] > offset) {
         model->positions[index] = model->positions[index] <= end ? offset :
             model->positions[index] - length;
      }
   }
}

static void
model_coordinates(const struct model *model, size_t absolute, size_t *line,
    size_t *byte)
{
   size_t index;

   *line = 0U;
   *byte = 0U;
   for (index = 0U; index < absolute; index++) {
      if (model->bytes[index] == '\n') {
         (*line)++;
         *byte = 0U;
      } else {
         (*byte)++;
      }
   }
}

static size_t
model_line_count(const struct model *model)
{
   size_t count = 1U;
   size_t index;

   for (index = 0U; index < model->length; index++) {
      if (model->bytes[index] == '\n') {
         count++;
      }
   }
   return count;
}

static void
model_line(const struct model *model, size_t wanted, size_t *start,
    size_t *length)
{
   size_t line = 0U;
   size_t index = 0U;

   while (line < wanted) {
      if (model->bytes[index++] == '\n') {
         line++;
      }
   }
   *start = index;
   while (index < model->length && model->bytes[index] != '\n') {
      index++;
   }
   *length = index - *start;
}

static uint32_t
random_next(uint32_t *state)
{
   *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
   return *state;
}

static void
check_model(const struct cwiki_buffer *buffer, const struct model *model,
    const struct cwiki_position *positions)
{
   size_t index;

   check_encoding(buffer, model->bytes, model->length,
       "model bytes match line-array encoding after each edit");
   for (index = 0U; index < 5U; index++) {
      size_t expected_line;
      size_t expected_byte;

      model_coordinates(model, model->positions[index], &expected_line,
          &expected_byte);
      check(positions[index].line == expected_line &&
          positions[index].byte == expected_byte,
          "registered position matches independent absolute-offset model");
   }
}

static void
test_model_edits(void)
{
   struct cwiki_buffer buffer;
   struct cwiki_position positions[5] = {
      {0U, 0U}, {0U, 2U}, {0U, 4U}, {1U, 1U}, {2U, 1U}
   };
   struct model model = {NULL, 0U, 0U, {0U, 2U, 4U, 6U, 9U}};
   const char initial[] = "abcd\nef\nghi";
   uint32_t random = UINT32_C(0x5eed1234);
   size_t step;
   size_t index;

   model_reserve(&model, sizeof(initial) - 1U);
   memcpy(model.bytes, initial, sizeof(initial) - 1U);
   model.length = sizeof(initial) - 1U;
   check(cwiki_buffer_init(&buffer) == 0 &&
       cwiki_buffer_load(&buffer, initial, sizeof(initial) - 1U) == 0,
       "initialize deterministic model fixture");
   for (index = 0U; index < 5U; index++) {
      check(cwiki_buffer_register_position(&buffer, &positions[index]) == 0,
          "register model position");
   }
   for (step = 0U; step < 2000U; step++) {
      uint32_t choice = random_next(&random) % UINT32_C(4);
      size_t lines = model_line_count(&model);
      size_t line = (size_t)(random_next(&random) % (uint32_t)lines);
      size_t start;
      size_t line_length;

      model_line(&model, line, &start, &line_length);
      if (choice == 0U || (choice == 1U && line_length == 0U)) {
         size_t byte = (size_t)(random_next(&random) %
             (uint32_t)(line_length + 1U));
         char inserted = (char)('a' + (random_next(&random) % UINT32_C(26)));

         check(cwiki_buffer_insert(&buffer, line, byte, &inserted, 1U) == 0,
             "model insert succeeds");
         model_insert(&model, start + byte, inserted);
      } else if (choice == 1U) {
         size_t byte = (size_t)(random_next(&random) % (uint32_t)line_length);
         size_t available = line_length - byte;
         size_t count = 1U + (size_t)(random_next(&random) %
             (uint32_t)available);

         check(cwiki_buffer_delete(&buffer, line, byte, count) == 0,
             "model delete succeeds");
         model_delete(&model, start + byte, count);
      } else if (choice == 2U) {
         size_t byte = (size_t)(random_next(&random) %
             (uint32_t)(line_length + 1U));

         check(cwiki_buffer_split(&buffer, line, byte) == 0,
             "model split succeeds");
         model_insert(&model, start + byte, '\n');
      } else if (lines > 1U) {
         line %= lines - 1U;
         model_line(&model, line, &start, &line_length);
         check(cwiki_buffer_join(&buffer, line) == 0, "model join succeeds");
         model_delete(&model, start + line_length, 1U);
      }
      check_model(&buffer, &model, positions);
   }
   cwiki_buffer_free(&buffer);
   free(model.bytes);
}

int
main(void)
{
   test_line_endings();
   test_utf8_and_graphemes();
   test_edits_and_positions();
   test_degraded_line();
   test_model_edits();
   if (failures != 0) {
      return 1;
   }
   (void)puts("buffer tests: ok (2000 deterministic model edits)");
   return 0;
}
