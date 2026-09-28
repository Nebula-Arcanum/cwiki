#include "capabilities.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failures;

static const unsigned char keyboard_reply[] = "\x1b[?29u";
static const unsigned char graphics_reply[] =
   "\x1b_Gi=1129797963;EINVAL:query image rejected\x1b\\";
static const unsigned char da1_reply[] = "\x1b[?1;2c";

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static bool
supported(struct cwiki_capabilities_result result)
{
   return result.complete != 0 &&
       result.keyboard == CWIKI_CAPABILITY_SUPPORTED &&
       result.graphics == CWIKI_CAPABILITY_SUPPORTED;
}

static void
check_every_split(const unsigned char *fixture, size_t length,
    const char *message)
{
   size_t split;

   for (split = 0U; split <= length; split++) {
      struct cwiki_capabilities_parser parser;

      cwiki_capabilities_parser_init(&parser);
      cwiki_capabilities_parser_feed(&parser, fixture, split);
      cwiki_capabilities_parser_feed(&parser, fixture + split, length - split);
      check(supported(cwiki_capabilities_parser_result(&parser)), message);
   }
   {
      struct cwiki_capabilities_parser parser;
      size_t i;

      cwiki_capabilities_parser_init(&parser);
      for (i = 0U; i < length; i++) {
         cwiki_capabilities_parser_feed(&parser, fixture + i, 1U);
      }
      check(supported(cwiki_capabilities_parser_result(&parser)), message);
   }
}

static void
test_query_strings(void)
{
   check(strcmp(CWIKI_CAPABILITIES_KEYBOARD_QUERY, "\033[?u") == 0,
       "keyboard query is CSI ? u");
   check(strcmp(CWIKI_CAPABILITIES_GRAPHICS_QUERY,
       "\033_Gi=1129797963,s=1,v=1,a=q,t=d,f=24;AAAA\033\\") == 0,
       "graphics query is a non-storing 1x1 direct RGB query with owned id");
   check(strcmp(CWIKI_CAPABILITIES_DA1_QUERY, "\033[c") == 0,
       "barrier query is DA1");
}

static void
test_fragmentation_and_order(void)
{
   unsigned char fixture[256];
   size_t length = 0U;

   (void)memcpy(fixture + length, "noise\033[31m", sizeof("noise\033[31m") - 1U);
   length += sizeof("noise\033[31m") - 1U;
   (void)memcpy(fixture + length, keyboard_reply,
       sizeof(keyboard_reply) - 1U);
   length += sizeof(keyboard_reply) - 1U;
   (void)memcpy(fixture + length, graphics_reply,
       sizeof(graphics_reply) - 1U);
   length += sizeof(graphics_reply) - 1U;
   (void)memcpy(fixture + length, da1_reply, sizeof(da1_reply) - 1U);
   length += sizeof(da1_reply) - 1U;
   check_every_split(fixture, length,
       "keyboard-first replies survive every fragmentation boundary");

   length = 0U;
   (void)memcpy(fixture + length, graphics_reply,
       sizeof(graphics_reply) - 1U);
   length += sizeof(graphics_reply) - 1U;
   (void)memcpy(fixture + length, "typed input", sizeof("typed input") - 1U);
   length += sizeof("typed input") - 1U;
   (void)memcpy(fixture + length, keyboard_reply,
       sizeof(keyboard_reply) - 1U);
   length += sizeof(keyboard_reply) - 1U;
   (void)memcpy(fixture + length, da1_reply, sizeof(da1_reply) - 1U);
   length += sizeof(da1_reply) - 1U;
   check_every_split(fixture, length,
       "graphics-first replies survive every fragmentation boundary");
}

static void
test_current_keyboard_flags(void)
{
   static const struct {
      const char *reply;
      uint32_t flags;
   } valid[] = {
      {"\033[?0u", 0U},
      {"\033[?6u", 6U},
      {"\033[?29u", 29U},
      {"\033[?31u", 31U},
      {"\033[?32u", 32U},
      {"\033[?4294967295u", UINT32_MAX}
   };
   static const char *const malformed[] = {
      "\033[?4294967296u", "\033[?u", "\033[?-1u", "\033[?1;2u"
   };
   size_t i;

   for (i = 0U; i < sizeof(valid) / sizeof(valid[0]); i++) {
      struct cwiki_capabilities_parser parser;
      struct cwiki_capabilities_result result;

      cwiki_capabilities_parser_init(&parser);
      cwiki_capabilities_parser_feed(&parser,
          (const unsigned char *)valid[i].reply, strlen(valid[i].reply));
      cwiki_capabilities_parser_feed(&parser, graphics_reply,
          sizeof(graphics_reply) - 1U);
      cwiki_capabilities_parser_feed(&parser, da1_reply,
          sizeof(da1_reply) - 1U);
      result = cwiki_capabilities_parser_result(&parser);
      check(result.complete != 0 && supported(result) &&
          result.keyboard_flags == valid[i].flags,
          "any valid current keyboard flags prove support and are preserved");
   }

   for (i = 0U; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
      struct cwiki_capabilities_parser parser;
      struct cwiki_capabilities_result result;

      cwiki_capabilities_parser_init(&parser);
      cwiki_capabilities_parser_feed(&parser,
          (const unsigned char *)malformed[i], strlen(malformed[i]));
      cwiki_capabilities_parser_feed(&parser, graphics_reply,
          sizeof(graphics_reply) - 1U);
      cwiki_capabilities_parser_feed(&parser, da1_reply,
          sizeof(da1_reply) - 1U);
      result = cwiki_capabilities_parser_result(&parser);
      check(result.complete != 0 &&
          result.keyboard == CWIKI_CAPABILITY_MISSING &&
          result.graphics == CWIKI_CAPABILITY_SUPPORTED,
          "malformed or overflowing keyboard flags do not prove support");
   }
}

static void
test_graphics_id_is_exact(void)
{
   static const char *const replies[] = {
      "\033_Gi=1129797962;OK\033\\",
      "\033_Gi=11297979630;OK\033\\",
      "\033_Gi=4294967296;OK\033\\",
      "\033_Gi=1129797963;\033\\",
      "\033_Gi=1129797963,p=1;OK\033\\"
   };
   size_t i;

   for (i = 0U; i < sizeof(replies) / sizeof(replies[0]); i++) {
      struct cwiki_capabilities_parser parser;
      struct cwiki_capabilities_result result;

      cwiki_capabilities_parser_init(&parser);
      cwiki_capabilities_parser_feed(&parser, keyboard_reply,
          sizeof(keyboard_reply) - 1U);
      cwiki_capabilities_parser_feed(&parser,
          (const unsigned char *)replies[i], strlen(replies[i]));
      cwiki_capabilities_parser_feed(&parser, da1_reply,
          sizeof(da1_reply) - 1U);
      result = cwiki_capabilities_parser_result(&parser);
      check(result.complete != 0 &&
          result.keyboard == CWIKI_CAPABILITY_SUPPORTED &&
          result.graphics == CWIKI_CAPABILITY_MISSING,
          "graphics support requires the exact cwiki query id and reply form");
   }
}

static void
test_malformed_then_valid_recovery(void)
{
   unsigned char fixture[512];
   size_t length = 0U;

   (void)memcpy(fixture + length,
       "\033[?42949672960u\033[?29;1u\033_Gi=x;OK\033\\\033[?1;;2c",
       sizeof("\033[?42949672960u\033[?29;1u\033_Gi=x;OK\033\\"
       "\033[?1;;2c") - 1U);
   length += sizeof("\033[?42949672960u\033[?29;1u\033_Gi=x;OK\033\\"
       "\033[?1;;2c") - 1U;
   fixture[length++] = 0x1bU;
   fixture[length++] = (unsigned char)'_';
   fixture[length++] = (unsigned char)'G';
   (void)memset(fixture + length, '9', 140U);
   length += 140U;
   fixture[length++] = 0x1bU;
   fixture[length++] = (unsigned char)'\\';
   (void)memcpy(fixture + length, keyboard_reply,
       sizeof(keyboard_reply) - 1U);
   length += sizeof(keyboard_reply) - 1U;
   (void)memcpy(fixture + length, graphics_reply,
       sizeof(graphics_reply) - 1U);
   length += sizeof(graphics_reply) - 1U;
   (void)memcpy(fixture + length, da1_reply, sizeof(da1_reply) - 1U);
   length += sizeof(da1_reply) - 1U;

   check_every_split(fixture, length,
       "malformed and overflowing replies do not prevent valid recovery");
}

static void
test_da1_concludes_missing(void)
{
   struct cwiki_capabilities_parser parser;
   struct cwiki_capabilities_result result;

   cwiki_capabilities_parser_init(&parser);
   cwiki_capabilities_parser_feed(&parser, da1_reply,
       sizeof(da1_reply) - 1U);
   result = cwiki_capabilities_parser_result(&parser);
   check(result.complete != 0 &&
       result.keyboard == CWIKI_CAPABILITY_MISSING &&
       result.graphics == CWIKI_CAPABILITY_MISSING,
       "DA1 concludes both absent replies are missing");

   cwiki_capabilities_parser_init(&parser);
   cwiki_capabilities_parser_feed(&parser, keyboard_reply,
       sizeof(keyboard_reply) - 1U);
   cwiki_capabilities_parser_feed(&parser, da1_reply,
       sizeof(da1_reply) - 1U);
   result = cwiki_capabilities_parser_result(&parser);
   check(result.complete != 0 &&
       result.keyboard == CWIKI_CAPABILITY_SUPPORTED &&
       result.graphics == CWIKI_CAPABILITY_MISSING,
       "DA1 preserves a valid keyboard reply and marks graphics missing");

   cwiki_capabilities_parser_init(&parser);
   cwiki_capabilities_parser_feed(&parser, graphics_reply,
       sizeof(graphics_reply) - 1U);
   cwiki_capabilities_parser_feed(&parser, da1_reply,
       sizeof(da1_reply) - 1U);
   result = cwiki_capabilities_parser_result(&parser);
   check(result.complete != 0 &&
       result.keyboard == CWIKI_CAPABILITY_MISSING &&
       result.graphics == CWIKI_CAPABILITY_SUPPORTED,
       "DA1 preserves a valid graphics reply and marks keyboard missing");
}

static void
test_da1_preserves_unconsumed_input(void)
{
   static const unsigned char fixture[] = "\x1b[?1;2c\x1b[97;1u";
   static const unsigned char pending[] = "\x1b[97;1u";
   struct cwiki_capabilities_parser parser;
   struct cwiki_capabilities_result result;
   size_t consumed;

   cwiki_capabilities_parser_init(&parser);
   check(cwiki_capabilities_parser_feed(&parser, pending,
       sizeof(pending) - 1U) == sizeof(pending) - 1U,
       "pending parser consumes the complete input chunk");

   cwiki_capabilities_parser_init(&parser);
   consumed = cwiki_capabilities_parser_feed(&parser, fixture,
       sizeof(fixture) - 1U);
   result = cwiki_capabilities_parser_result(&parser);
   check(consumed == sizeof(da1_reply) - 1U && result.complete != 0,
       "feed stops exactly after the DA1 final byte");
   check(cwiki_capabilities_parser_feed(&parser, fixture + consumed,
       sizeof(fixture) - 1U - consumed) == 0U,
       "completed parser consumes no post-barrier key input");
}

int
main(void)
{
   test_query_strings();
   test_fragmentation_and_order();
   test_current_keyboard_flags();
   test_graphics_id_is_exact();
   test_malformed_then_valid_recovery();
   test_da1_concludes_missing();
   test_da1_preserves_unconsumed_input();
   if (failures != 0) {
      return 1;
   }
   (void)puts("capability parser: ok");
   return 0;
}
