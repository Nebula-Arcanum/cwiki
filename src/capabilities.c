#include "capabilities.h"

#include <limits.h>
#include <stdbool.h>

enum parser_state {
   STATE_GROUND,
   STATE_ESCAPE,
   STATE_CSI,
   STATE_APC,
   STATE_APC_ESCAPE,
   STATE_DISCARD_APC,
   STATE_DISCARD_APC_ESCAPE
};

static bool
parse_number(const unsigned char *bytes, size_t length, uint32_t *value)
{
   size_t i;
   uint32_t result = 0U;

   if (length == 0U) {
      return false;
   }
   for (i = 0U; i < length; i++) {
      unsigned int digit;

      if (bytes[i] < (unsigned char)'0' || bytes[i] > (unsigned char)'9') {
         return false;
      }
      digit = (unsigned int)(bytes[i] - (unsigned char)'0');
      if (result > (UINT32_MAX - digit) / 10U) {
         return false;
      }
      result = result * 10U + digit;
   }
   *value = result;
   return true;
}

static bool
valid_da1(const unsigned char *bytes, size_t length)
{
   size_t start;
   size_t i;
   uint32_t value;

   if (length < 2U || bytes[0] != (unsigned char)'?') {
      return false;
   }
   start = 1U;
   for (i = 1U; i <= length; i++) {
      if (i == length || bytes[i] == (unsigned char)';') {
         if (!parse_number(bytes + start, i - start, &value)) {
            return false;
         }
         start = i + 1U;
      } else if (bytes[i] < (unsigned char)'0' ||
          bytes[i] > (unsigned char)'9') {
         return false;
      }
   }
   return true;
}

static void
finish_csi(struct cwiki_capabilities_parser *parser, unsigned char final)
{
   uint32_t flags;

   if (final == (unsigned char)'u' && parser->sequence_len >= 2U &&
       parser->sequence[0] == (unsigned char)'?' &&
       parse_number(parser->sequence + 1U, parser->sequence_len - 1U,
       &flags)) {
      parser->result.keyboard = CWIKI_CAPABILITY_SUPPORTED;
      parser->result.keyboard_flags = flags;
   } else if (final == (unsigned char)'c' &&
       valid_da1(parser->sequence, parser->sequence_len)) {
      if (parser->result.keyboard == CWIKI_CAPABILITY_PENDING) {
         parser->result.keyboard = CWIKI_CAPABILITY_MISSING;
      }
      if (parser->result.graphics == CWIKI_CAPABILITY_PENDING) {
         parser->result.graphics = CWIKI_CAPABILITY_MISSING;
      }
      parser->result.complete = 1;
   }
}

static void
finish_apc(struct cwiki_capabilities_parser *parser)
{
   size_t semicolon;
   uint32_t id;

   if (parser->sequence_len < 5U ||
       parser->sequence[0] != (unsigned char)'G' ||
       parser->sequence[1] != (unsigned char)'i' ||
       parser->sequence[2] != (unsigned char)'=') {
      return;
   }
   semicolon = 3U;
   while (semicolon < parser->sequence_len &&
       parser->sequence[semicolon] != (unsigned char)';') {
      semicolon++;
   }
   if (semicolon == parser->sequence_len ||
       !parse_number(parser->sequence + 3U, semicolon - 3U, &id) ||
       id != CWIKI_CAPABILITIES_GRAPHICS_QUERY_ID ||
       semicolon + 1U == parser->sequence_len) {
      return;
   }
   for (semicolon++; semicolon < parser->sequence_len; semicolon++) {
      if (parser->sequence[semicolon] < 0x20U ||
          parser->sequence[semicolon] > 0x7eU) {
         return;
      }
   }
   parser->result.graphics = CWIKI_CAPABILITY_SUPPORTED;
}

void
cwiki_capabilities_parser_init(struct cwiki_capabilities_parser *parser)
{
   parser->sequence_len = 0U;
   parser->state = STATE_GROUND;
   parser->result.keyboard = CWIKI_CAPABILITY_PENDING;
   parser->result.graphics = CWIKI_CAPABILITY_PENDING;
   parser->result.keyboard_flags = 0U;
   parser->result.complete = 0;
}

size_t
cwiki_capabilities_parser_feed(struct cwiki_capabilities_parser *parser,
    const unsigned char *bytes, size_t length)
{
   size_t i;

   for (i = 0U; i < length && parser->result.complete == 0; i++) {
      unsigned char byte = bytes[i];

      switch (parser->state) {
      case STATE_GROUND:
         if (byte == 0x1bU) {
            parser->state = STATE_ESCAPE;
         }
         break;
      case STATE_ESCAPE:
         parser->sequence_len = 0U;
         if (byte == (unsigned char)'[') {
            parser->state = STATE_CSI;
         } else if (byte == (unsigned char)'_') {
            parser->state = STATE_APC;
         } else if (byte != 0x1bU) {
            parser->state = STATE_GROUND;
         }
         break;
      case STATE_CSI:
         if (byte >= 0x40U && byte <= 0x7eU) {
            finish_csi(parser, byte);
            parser->state = STATE_GROUND;
         } else if (byte == 0x1bU) {
            parser->state = STATE_ESCAPE;
         } else if (byte < 0x20U || byte > 0x3fU ||
             parser->sequence_len == sizeof(parser->sequence)) {
            parser->state = STATE_GROUND;
         } else {
            parser->sequence[parser->sequence_len++] = byte;
         }
         break;
      case STATE_APC:
         if (byte == 0x1bU) {
            parser->state = STATE_APC_ESCAPE;
         } else if (parser->sequence_len == sizeof(parser->sequence)) {
            parser->state = STATE_DISCARD_APC;
         } else {
            parser->sequence[parser->sequence_len++] = byte;
         }
         break;
      case STATE_APC_ESCAPE:
         if (byte == (unsigned char)'\\') {
            finish_apc(parser);
            parser->state = STATE_GROUND;
         } else if (byte != 0x1bU) {
            parser->state = STATE_DISCARD_APC;
         }
         break;
      case STATE_DISCARD_APC:
         if (byte == 0x1bU) {
            parser->state = STATE_DISCARD_APC_ESCAPE;
         }
         break;
      case STATE_DISCARD_APC_ESCAPE:
         if (byte == (unsigned char)'\\') {
            parser->state = STATE_GROUND;
         } else if (byte != 0x1bU) {
            parser->state = STATE_DISCARD_APC;
         }
         break;
      }
   }
   return i;
}

struct cwiki_capabilities_result
cwiki_capabilities_parser_result(const struct cwiki_capabilities_parser *parser)
{
   return parser->result;
}
