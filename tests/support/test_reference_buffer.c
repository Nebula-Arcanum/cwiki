#include "reference_buffer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
check(int condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static void
check_bytes(const struct reference_buffer *buffer, const unsigned char *expected,
    size_t length, const char *message)
{
   check(reference_buffer_length(buffer) == length &&
       memcmp(reference_buffer_bytes(buffer), expected, length) == 0, message);
}

int
main(void)
{
   struct reference_buffer buffer;
   const unsigned char middle[] = {'a', 'b', 'c', 'd', 'e'};
   const unsigned char binary[] = {0x00U, 'a', 'b', 'c', 'd', 'e', 0xffU, 'Z'};
   const unsigned char after_delete[] = {0x00U, 'a', 'd', 'e', 0xffU, 'Z'};
   const unsigned char grown[] = "0123456789abcdefghij";

   reference_buffer_init(&buffer);
   check(reference_buffer_insert(&buffer, 0U, NULL, 0U) == 0,
       "allow empty insert into empty buffer");
   check(reference_buffer_insert(&buffer, 0U, "ace", 3U) == 0,
       "insert into empty buffer");
   check(reference_buffer_insert(&buffer, 1U, "b", 1U) == 0,
       "insert at asymmetric interior offset");
   check(reference_buffer_insert(&buffer, 3U, "d", 1U) == 0,
       "insert at second asymmetric interior offset");
   check_bytes(&buffer, middle, sizeof(middle),
       "interior inserts produce independently expected bytes");

   check(reference_buffer_insert(&buffer, 0U, "\0", 1U) == 0,
       "insert binary byte at start boundary");
   check(reference_buffer_insert(&buffer, reference_buffer_length(&buffer),
       "\xffZ", 2U) == 0, "insert binary bytes at end boundary");
   check_bytes(&buffer, binary, sizeof(binary),
       "boundary inserts preserve independently expected binary bytes");

   check(reference_buffer_delete(&buffer, 2U, 2U) == 0,
       "delete asymmetric interior range");
   check_bytes(&buffer, after_delete, sizeof(after_delete),
       "interior delete produces independently expected bytes");
   check(reference_buffer_delete(&buffer, 1U, 0U) == 0,
       "allow empty delete at valid offset");

   check(reference_buffer_insert(&buffer, 7U, "x", 1U) == -1,
       "reject insert beyond end");
   check(reference_buffer_insert(&buffer, 1U, NULL, 1U) == -1,
       "reject nonempty insert from null bytes");
   check(reference_buffer_delete(&buffer, 7U, 0U) == -1,
       "reject delete offset beyond end");
   check(reference_buffer_delete(&buffer, 5U, 2U) == -1,
       "reject delete range beyond end");
   check(reference_buffer_insert(&buffer, 0U, "x", SIZE_MAX) == -1,
       "reject overflowing insert length");
   check_bytes(&buffer, after_delete, sizeof(after_delete),
       "invalid ranges leave bytes unchanged");

   check(reference_buffer_delete(&buffer, 0U,
       reference_buffer_length(&buffer)) == 0, "delete full buffer boundaries");
   check(reference_buffer_length(&buffer) == 0U, "full delete leaves empty buffer");
   check(reference_buffer_insert(&buffer, 0U, grown, sizeof(grown) - 1U) == 0,
       "grow allocation for a larger insert");
   check_bytes(&buffer, grown, sizeof(grown) - 1U,
       "grown buffer has independently expected bytes");
   reference_buffer_free(&buffer);
   if (failures != 0) {
      return 1;
   }
   (void)puts("reference buffer support: ok");
   return 0;
}
