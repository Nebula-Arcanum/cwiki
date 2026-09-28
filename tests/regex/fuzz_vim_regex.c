#include "regex.h"
#include "vim_regex.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <utf8proc.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool
valid_utf8(const char *bytes, size_t length)
{
   size_t offset = 0U;

   while (offset < length) {
      utf8proc_int32_t codepoint;
      utf8proc_ssize_t available =
          (utf8proc_ssize_t)(length - offset < 4U ? length - offset : 4U);
      utf8proc_ssize_t used = utf8proc_iterate(
          (const utf8proc_uint8_t *)bytes + offset, available, &codepoint);

      if (used <= 0) {
         return false;
      }
      offset += (size_t)used;
   }
   return true;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
   struct cwiki_vim_regex_result translated;

   if (size > 4096U) {
      size = 4096U;
   }
   translated = cwiki_vim_regex_translate((const char *)data, size,
       CWIKI_TYPED_REGEX_VIM);
   if (translated.status == CWIKI_VIM_REGEX_OK) {
      struct cwiki_regex_compile_error error = {0, 0U, "not compiled"};
      struct cwiki_regex *regex = NULL;
      uint32_t options = translated.caseless ? CWIKI_REGEX_CASELESS : 0U;

      if (translated.pattern == NULL ||
          !valid_utf8(translated.pattern, translated.pattern_length) ||
          cwiki_regex_compile(&regex, translated.pattern,
              translated.pattern_length, options, 100000U, 1000U, &error) !=
              CWIKI_REGEX_COMPILE_OK || regex == NULL) {
         (void)fprintf(stderr, "invalid emitted PCRE for %zu input bytes: ",
             size);
         (void)fwrite(data, 1U, size, stderr);
         (void)fprintf(stderr, "\nemitted: ");
         if (translated.pattern != NULL) {
            (void)fwrite(translated.pattern, 1U, translated.pattern_length,
                stderr);
         }
         (void)fprintf(stderr, "\ncompile diagnostic: %s at %zu\n",
             error.message, error.byte_offset);
         abort();
      }
      cwiki_regex_free(regex);
   } else if (translated.status < CWIKI_VIM_REGEX_INVALID_ARGUMENT ||
       translated.status > CWIKI_VIM_REGEX_NO_MEMORY ||
       translated.pattern != NULL || translated.pattern_length != 0U ||
       translated.source_offset > size || translated.diagnostic == NULL ||
       translated.diagnostic[0] == '\0') {
      abort();
   }
   cwiki_vim_regex_result_destroy(&translated);
   return 0;
}

#ifdef CWIKI_VIM_REGEX_FUZZ_STANDALONE
static uint32_t
next_random(uint32_t *state)
{
   *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
   return *state;
}

int
main(void)
{
   static const uint8_t alphabet[] =
       "abcXYZ09\\()[]{}.*+?=|^$,-_ <>/\n"
       "vVmMzse%d@cpPGreekLundxXoOhHaAwWsStreb123456789";
   static const uint8_t corpus[] =
       "abc\\()[]{}.*+?=|^$\\v\\m\\M\\V\\zs\\ze\\%V\\%d123"
       "\\_.\\_s\\p{Greek}\\1\n"
       "\xce\xba\xce\xb1\xce\xa6\xce\xad\xc3\x28\x80\0";
   uint8_t input[512];
   uint8_t pair[2];
   uint8_t range[] = {'[', 0U, '-', 0U, ']'};
   uint32_t state = UINT32_C(0x76696d72);
   size_t iteration;
   size_t left;
   size_t right;

   for (left = 0U; left < 128U; left++) {
      pair[0] = (uint8_t)left;
      (void)LLVMFuzzerTestOneInput(pair, 1U);
      for (right = 0U; right < 128U; right++) {
         pair[1] = (uint8_t)right;
         (void)LLVMFuzzerTestOneInput(pair, sizeof(pair));
      }
   }
   for (left = 32U; left < 127U; left++) {
      range[1] = (uint8_t)left;
      for (right = 32U; right < 127U; right++) {
         range[3] = (uint8_t)right;
         (void)LLVMFuzzerTestOneInput(range, sizeof(range));
      }
   }
   for (iteration = 0U; iteration < sizeof(corpus); iteration++) {
      (void)LLVMFuzzerTestOneInput(corpus, iteration);
      (void)LLVMFuzzerTestOneInput(corpus + iteration,
          sizeof(corpus) - iteration);
   }
   for (iteration = 0U; iteration < 5000U; iteration++) {
      size_t length = (size_t)(next_random(&state) %
          (uint32_t)(sizeof(input) + 1U));
      size_t index;

      for (index = 0U; index < length; index++) {
         input[index] = (uint8_t)next_random(&state);
      }
      (void)LLVMFuzzerTestOneInput(input, length);
      for (index = 0U; index < length; index++) {
         input[index] = alphabet[input[index] % (sizeof(alphabet) - 1U)];
      }
      (void)LLVMFuzzerTestOneInput(input, length);
   }
   (void)puts(
       "Vim regex fuzz smoke: ok (deterministic corpus + 10000 inputs)");
   return 0;
}
#endif
