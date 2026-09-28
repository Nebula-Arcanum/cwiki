#include "buffer.h"
#include "zone.h"

#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
   static const char alphabet[] =
       "abc $%\\{}[]()<>!-`_\n0123456789";
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_buffer buffer;
   const struct cwiki_zone_region *regions;
   uint64_t top_level;
   char *text;
   size_t region_count;
   size_t index;
   size_t line;

   if (size > 4096U) {
      size = 4096U;
   }
   text = malloc(size == 0U ? 1U : size);
   if (text == NULL) {
      return 0;
   }
   for (index = 0U; index < size; index++) {
      text[index] = alphabet[data[index] % (sizeof(alphabet) - 1U)];
   }
   regions = cwiki_zone_builtin_regions(&region_count, &top_level);
   if (cwiki_zone_engine_init(&engine, regions, region_count, top_level) != 0 ||
       cwiki_buffer_init(&buffer) != 0) {
      cwiki_zone_engine_free(engine);
      free(text);
      return 0;
   }
   if (cwiki_buffer_load(&buffer, text, size) == 0 &&
       cwiki_zone_recompute(engine, &buffer, 0U, NULL) == 0) {
      for (line = 0U; line < buffer.line_count; line++) {
         struct cwiki_zone zone;
         size_t byte;

         if (buffer.lines[line].end_zones.depth > CWIKI_ZONE_MAX_DEPTH) {
            abort();
         }
         for (byte = 0U; byte <= buffer.lines[line].length; byte++) {
            if (cwiki_zone_at(engine, &buffer, line, byte, &zone) != 0) {
               abort();
            }
         }
      }
   }
   cwiki_buffer_free(&buffer);
   cwiki_zone_engine_free(engine);
   free(text);
   return 0;
}

#ifdef CWIKI_ZONE_FUZZ_STANDALONE
#include <stdio.h>

static uint32_t
next_random(uint32_t *state)
{
   *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
   return *state;
}

int
main(void)
{
   uint8_t input[512];
   uint32_t state = UINT32_C(0x7a6f6e65);
   size_t iteration;

   for (iteration = 0U; iteration < 2000U; iteration++) {
      size_t length = (size_t)(next_random(&state) %
          (uint32_t)(sizeof(input) + 1U));
      size_t index;

      for (index = 0U; index < length; index++) {
         input[index] = (uint8_t)next_random(&state);
      }
      (void)LLVMFuzzerTestOneInput(input, length);
   }
   (void)puts("zone fuzz smoke: ok (2000 deterministic inputs)");
   return 0;
}
#endif
