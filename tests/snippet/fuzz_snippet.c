#include "snippet.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
   struct cwiki_snippet_body *body = NULL;
   enum cwiki_snippet_status status = cwiki_snippet_body_compile(&body,
       (const char *)data, size, 9U);
   if (status == CWIKI_SNIPPET_OK) {
      assert(body != NULL);
      assert(cwiki_snippet_body_valid(body, 9U));
   } else {
      assert(body == NULL);
   }
   cwiki_snippet_body_free(body);
   return 0;
}

#ifdef CWIKI_SNIPPET_FUZZ_STANDALONE
int
main(void)
{
   static const uint8_t corpus[][32] = {
      "$0", "$${1:x}$1$0", "${capture:8|upper}$0",
      "${stop:2|lower}${visual}$0", "${1:a}${1:b}$0",
      "${capture:9}$0", "${stop:1|unknown}$0", "$", "${", "$00"
   };
   uint8_t bytes[64];
   uint32_t state = UINT32_C(0x91e10da5);
   size_t i, j;
   for (i = 0U; i < sizeof(corpus) / sizeof(corpus[0]); i++)
      (void)LLVMFuzzerTestOneInput(corpus[i], sizeof(corpus[i]));
   for (i = 0U; i < 10000U; i++) {
      size_t length;
      state = state * UINT32_C(1664525) + UINT32_C(1013904223);
      length = (size_t)(state % (uint32_t)sizeof(bytes));
      for (j = 0U; j < length; j++) {
         state = state * UINT32_C(1664525) + UINT32_C(1013904223);
         bytes[j] = (uint8_t)(state >> 24U);
      }
      (void)LLVMFuzzerTestOneInput(bytes, length);
   }
   (void)puts("snippet deterministic fuzz passed");
   return EXIT_SUCCESS;
}
#endif
