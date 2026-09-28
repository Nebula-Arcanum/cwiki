#include "input.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum parser_state {
   STATE_GROUND,
   STATE_ESCAPE,
   STATE_CSI,
   STATE_DISCARD,
   STATE_PASTE
};

static const unsigned char paste_end[] = "\x1b[201~";

static bool
unicode_scalar(uint32_t value)
{
   return value <= UINT32_C(0x10ffff) &&
       !(value >= UINT32_C(0xd800) && value <= UINT32_C(0xdfff));
}

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
next_field(const unsigned char *bytes, size_t length, size_t *offset,
    unsigned char separator, const unsigned char **field, size_t *field_len)
{
   size_t start = *offset;

   while (*offset < length && bytes[*offset] != separator) {
      (*offset)++;
   }
   *field = bytes + start;
   *field_len = *offset - start;
   if (*offset < length) {
      (*offset)++;
      return true;
   }
   return false;
}

static bool
append_utf8(uint32_t codepoint, unsigned char *text, size_t capacity,
    size_t *length)
{
   size_t needed;

   if (!unicode_scalar(codepoint) || codepoint < 0x20U ||
       (codepoint >= 0x7fU && codepoint <= 0x9fU)) {
      return false;
   }
   if (codepoint <= 0x7fU) {
      needed = 1U;
   } else if (codepoint <= 0x7ffU) {
      needed = 2U;
   } else if (codepoint <= 0xffffU) {
      needed = 3U;
   } else {
      needed = 4U;
   }
   if (needed > capacity - *length) {
      return false;
   }
   if (needed == 1U) {
      text[(*length)++] = (unsigned char)codepoint;
   } else if (needed == 2U) {
      text[(*length)++] = (unsigned char)(0xc0U | (codepoint >> 6U));
      text[(*length)++] = (unsigned char)(0x80U | (codepoint & 0x3fU));
   } else if (needed == 3U) {
      text[(*length)++] = (unsigned char)(0xe0U | (codepoint >> 12U));
      text[(*length)++] = (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3fU));
      text[(*length)++] = (unsigned char)(0x80U | (codepoint & 0x3fU));
   } else {
      text[(*length)++] = (unsigned char)(0xf0U | (codepoint >> 18U));
      text[(*length)++] = (unsigned char)(0x80U | ((codepoint >> 12U) & 0x3fU));
      text[(*length)++] = (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3fU));
      text[(*length)++] = (unsigned char)(0x80U | (codepoint & 0x3fU));
   }
   return true;
}

static bool
parse_alternate_keys(const unsigned char *bytes, size_t length,
    struct cwiki_input_event *event)
{
   const unsigned char *field;
   size_t field_len;
   size_t offset = 0U;
   bool more;

   more = next_field(bytes, length, &offset, (unsigned char)':', &field,
       &field_len);
   if (!parse_number(field, field_len, &event->key) ||
       !unicode_scalar(event->key)) {
      return false;
   }
   if (!more) {
      return true;
   }
   more = next_field(bytes, length, &offset, (unsigned char)':', &field,
       &field_len);
   if (field_len != 0U) {
      if (!parse_number(field, field_len, &event->shifted_key) ||
          !unicode_scalar(event->shifted_key)) {
         return false;
      }
      event->has_shifted_key = true;
   }
   if (!more) {
      return event->has_shifted_key;
   }
   more = next_field(bytes, length, &offset, (unsigned char)':', &field,
       &field_len);
   if (more || !parse_number(field, field_len, &event->base_layout_key) ||
       !unicode_scalar(event->base_layout_key)) {
      return false;
   }
   event->has_base_layout_key = true;
   return true;
}

static bool
parse_modifiers(const unsigned char *bytes, size_t length,
    struct cwiki_input_event *event)
{
   const unsigned char *field;
   size_t field_len;
   size_t offset = 0U;
   uint32_t encoded = 1U;
   uint32_t action = (uint32_t)CWIKI_INPUT_PRESS;
   bool more;

   more = next_field(bytes, length, &offset, (unsigned char)':', &field,
       &field_len);
   if ((field_len != 0U && !parse_number(field, field_len, &encoded)) ||
       encoded == 0U || encoded > 256U) {
      return false;
   }
   if (more) {
      more = next_field(bytes, length, &offset, (unsigned char)':', &field,
          &field_len);
      if (more || !parse_number(field, field_len, &action) || action < 1U ||
          action > 3U) {
         return false;
      }
   }
   event->modifiers = (unsigned int)(encoded - 1U);
   event->action = (enum cwiki_input_key_action)action;
   return true;
}

static bool
parse_text(const unsigned char *bytes, size_t length, unsigned char *text,
    size_t capacity, size_t *text_len)
{
   const unsigned char *field;
   size_t field_len;
   size_t offset = 0U;
   bool more = true;

   if (length == 0U) {
      return false;
   }
   while (more) {
      uint32_t codepoint;

      more = next_field(bytes, length, &offset, (unsigned char)':', &field,
          &field_len);
      if (!parse_number(field, field_len, &codepoint) ||
          !append_utf8(codepoint, text, capacity, text_len)) {
         return false;
      }
   }
   return true;
}

static bool
parse_key(const unsigned char *bytes, size_t length,
    struct cwiki_input_event *event, unsigned char *text, size_t text_capacity)
{
   const unsigned char *field;
   size_t field_len;
   size_t offset = 0U;
   bool more;

   (void)memset(event, 0, sizeof(*event));
   event->kind = CWIKI_INPUT_KEY;
   event->action = CWIKI_INPUT_PRESS;
   more = next_field(bytes, length, &offset, (unsigned char)';', &field,
       &field_len);
   if (!parse_alternate_keys(field, field_len, event)) {
      return false;
   }
   if (!more) {
      return !event->has_shifted_key;
   }
   more = next_field(bytes, length, &offset, (unsigned char)';', &field,
       &field_len);
   if (!parse_modifiers(field, field_len, event) ||
       (event->has_shifted_key &&
       (event->modifiers & CWIKI_INPUT_SHIFT) == 0U)) {
      return false;
   }
   if (!more) {
      return true;
   }
   more = next_field(bytes, length, &offset, (unsigned char)';', &field,
       &field_len);
   if (more || !parse_text(field, field_len, text, text_capacity,
       &event->text_len)) {
      return false;
   }
   event->text = text;
   return true;
}

static int
append_paste(struct cwiki_input_parser *parser, unsigned char byte)
{
   unsigned char *grown;
   size_t capacity;

   if (parser->paste_len == parser->paste_cap) {
      if (parser->paste_cap > SIZE_MAX / 2U) {
         return -1;
      }
      capacity = parser->paste_cap == 0U ? 256U : parser->paste_cap * 2U;
      grown = realloc(parser->paste, capacity);
      if (grown == NULL) {
         return -1;
      }
      parser->paste = grown;
      parser->paste_cap = capacity;
   }
   parser->paste[parser->paste_len++] = byte;
   return 0;
}

void
cwiki_input_parser_init(struct cwiki_input_parser *parser)
{
   (void)memset(parser, 0, sizeof(*parser));
}

void
cwiki_input_parser_destroy(struct cwiki_input_parser *parser)
{
   free(parser->paste);
   (void)memset(parser, 0, sizeof(*parser));
}

int
cwiki_input_parser_feed(struct cwiki_input_parser *parser,
    const unsigned char *bytes, size_t length, cwiki_input_emit_fn emit,
    void *context)
{
   size_t i;

   if (parser == NULL || (length != 0U && bytes == NULL) || emit == NULL) {
      return -1;
   }
   for (i = 0U; i < length; i++) {
      unsigned char byte = bytes[i];

      if (parser->state == STATE_PASTE) {
         struct cwiki_input_event event;

         if (append_paste(parser, byte) != 0) {
            return -1;
         }
         if (parser->paste_len >= sizeof(paste_end) - 1U &&
             memcmp(parser->paste + parser->paste_len - (sizeof(paste_end) - 1U),
             paste_end, sizeof(paste_end) - 1U) == 0) {
            parser->paste_len -= sizeof(paste_end) - 1U;
            (void)memset(&event, 0, sizeof(event));
            event.kind = CWIKI_INPUT_PASTE;
            event.text = parser->paste;
            event.text_len = parser->paste_len;
            emit(&event, context);
            parser->paste_len = 0U;
            parser->state = STATE_GROUND;
         }
         continue;
      }
      if (parser->state == STATE_GROUND) {
         if (byte == 0x1bU) {
            parser->state = STATE_ESCAPE;
         } else {
            parser->rejected++;
         }
         continue;
      }
      if (parser->state == STATE_ESCAPE) {
         if (byte == (unsigned char)'[') {
            parser->csi_len = 0U;
            parser->state = STATE_CSI;
         } else {
            parser->rejected++;
            if (byte != 0x1bU) {
               parser->state = STATE_GROUND;
            }
         }
         continue;
      }
      if (parser->state == STATE_DISCARD) {
         if (byte == 0x1bU) {
            parser->state = STATE_ESCAPE;
         } else if (byte >= 0x40U && byte <= 0x7eU) {
            parser->state = STATE_GROUND;
         }
         continue;
      }
      if (byte == 0x1bU) {
         parser->rejected++;
         parser->state = STATE_ESCAPE;
      } else if (parser->csi_len == sizeof(parser->csi)) {
         parser->rejected++;
         parser->state = STATE_DISCARD;
      } else {
         parser->csi[parser->csi_len++] = byte;
         if (byte == (unsigned char)'u') {
            struct cwiki_input_event event;
            unsigned char text[sizeof(parser->csi) * 4U];

            if (parse_key(parser->csi, parser->csi_len - 1U, &event, text,
                sizeof(text))) {
               emit(&event, context);
            } else {
               parser->rejected++;
            }
            parser->state = STATE_GROUND;
         } else if (byte == (unsigned char)'~') {
            if (parser->csi_len == 4U &&
                memcmp(parser->csi, "200~", 4U) == 0) {
               parser->paste_len = 0U;
               parser->state = STATE_PASTE;
            } else {
               parser->rejected++;
               parser->state = STATE_GROUND;
            }
         } else if (byte >= 0x40U && byte <= 0x7eU) {
            parser->rejected++;
            parser->state = STATE_GROUND;
         }
      }
   }
   return 0;
}

size_t
cwiki_input_parser_rejected(const struct cwiki_input_parser *parser)
{
   return parser->rejected;
}
