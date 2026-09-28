#include "input.h"

#include <stdio.h>
#include <string.h>

#define EVENT_CAPACITY 16U
#define TEXT_CAPACITY 512U

struct captured_event {
   struct cwiki_input_event event;
   unsigned char text[TEXT_CAPACITY];
};

struct capture {
   struct captured_event events[EVENT_CAPACITY];
   size_t count;
};

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
capture_event(const struct cwiki_input_event *event, void *context)
{
   struct capture *capture = context;
   struct captured_event *saved;

   if (capture->count == EVENT_CAPACITY || event->text_len > TEXT_CAPACITY) {
      failures++;
      return;
   }
   saved = &capture->events[capture->count++];
   saved->event = *event;
   if (event->text_len != 0U) {
      (void)memcpy(saved->text, event->text, event->text_len);
      saved->event.text = saved->text;
   }
}

static void
check_fragmentation(const unsigned char *fixture, size_t length,
    enum cwiki_input_event_kind kind, const char *message)
{
   size_t split;

   for (split = 0U; split <= length; split++) {
      struct cwiki_input_parser parser;
      struct capture capture = {0};

      cwiki_input_parser_init(&parser);
      check(cwiki_input_parser_feed(&parser, fixture, split, capture_event,
          &capture) == 0, message);
      check(cwiki_input_parser_feed(&parser, fixture + split, length - split,
          capture_event, &capture) == 0, message);
      check(capture.count == 1U && capture.events[0].event.kind == kind,
          message);
      cwiki_input_parser_destroy(&parser);
   }
   {
      struct cwiki_input_parser parser;
      struct capture capture = {0};
      size_t i;

      cwiki_input_parser_init(&parser);
      for (i = 0U; i < length; i++) {
         check(cwiki_input_parser_feed(&parser, fixture + i, 1U,
             capture_event, &capture) == 0, message);
      }
      check(capture.count == 1U && capture.events[0].event.kind == kind,
          message);
      cwiki_input_parser_destroy(&parser);
   }
}

static void
test_protocol_strings(void)
{
   check(strcmp(CWIKI_INPUT_KEYBOARD_PUSH, "\033[>29u") == 0,
       "keyboard flags use stack push form with flags 29");
   check(strcmp(CWIKI_INPUT_KEYBOARD_POP, "\033[<u") == 0,
       "keyboard flags use stack pop form");
   check(strcmp(CWIKI_INPUT_PASTE_ENABLE, "\033[?2004h") == 0,
       "bracketed paste enable uses DECSET 2004");
   check(strcmp(CWIKI_INPUT_PASTE_DISABLE, "\033[?2004l") == 0,
       "bracketed paste disable uses DECRST 2004");
}

static void
test_key_fields(void)
{
   static const unsigned char key[] =
       "\033[1089::99;6:2;1055:1088:1080u";
   static const unsigned char expected_text[] = {
      0xd0U, 0x9fU, 0xd1U, 0x80U, 0xd0U, 0xb8U
   };
   struct cwiki_input_parser parser;
   struct capture capture = {0};
   const struct cwiki_input_event *event;

   check_fragmentation(key, sizeof(key) - 1U, CWIKI_INPUT_KEY,
       "key event survives every fragmentation boundary");
   cwiki_input_parser_init(&parser);
   check(cwiki_input_parser_feed(&parser, key, sizeof(key) - 1U,
       capture_event, &capture) == 0, "parse complete alternate-layout key");
   event = &capture.events[0].event;
   check(event->key == 1089U && !event->has_shifted_key &&
       event->has_base_layout_key && event->base_layout_key == 99U,
       "preserve base-layout alternate key with omitted shifted key");
   check(event->modifiers == (CWIKI_INPUT_CTRL | CWIKI_INPUT_SHIFT) &&
       event->action == CWIKI_INPUT_REPEAT,
       "decode modifiers and event action");
   check(event->text_len == sizeof(expected_text) &&
       memcmp(event->text, expected_text, sizeof(expected_text)) == 0,
       "decode associated Unicode code points as UTF-8");
   cwiki_input_parser_destroy(&parser);
}

static void
test_distinct_keys_and_concatenation(void)
{
   static const unsigned char fixture[] =
       "\033[105;5u\033[9;1u\033[27;1u\033[120;3;120u\033[97:65;2;65u"
       "\033[0;;229u";
   static const unsigned char pure_text[] = {0xc3U, 0xa5U};
   struct cwiki_input_parser parser;
   struct capture capture = {0};

   cwiki_input_parser_init(&parser);
   check(cwiki_input_parser_feed(&parser, fixture, sizeof(fixture) - 1U,
       capture_event, &capture) == 0, "parse concatenated key events");
   check(capture.count == 6U, "emit every concatenated key event");
   check(capture.events[0].event.key == 105U &&
       capture.events[0].event.modifiers == CWIKI_INPUT_CTRL &&
       capture.events[1].event.key == 9U &&
       capture.events[1].event.modifiers == 0U,
       "distinguish Ctrl+i from Tab");
   check(capture.events[2].event.key == 27U &&
       capture.events[2].event.modifiers == 0U &&
       capture.events[3].event.key == 120U &&
       capture.events[3].event.modifiers == CWIKI_INPUT_ALT,
       "distinguish Escape from Alt chord without timeout");
   check(capture.events[0].event.text_len == 0U &&
       capture.events[0].event.text == NULL,
       "represent absent associated text without bytes");
   check(capture.events[4].event.has_shifted_key &&
       capture.events[4].event.shifted_key == 65U,
       "preserve shifted alternate key");
   check(capture.events[5].event.key == 0U &&
       capture.events[5].event.modifiers == 0U &&
       capture.events[5].event.text_len == sizeof(pure_text) &&
       memcmp(capture.events[5].event.text, pure_text, sizeof(pure_text)) == 0,
       "decode pure text event with omitted modifier field");
   cwiki_input_parser_destroy(&parser);
}

static void
test_literal_paste(void)
{
   static const unsigned char fixture[] =
       "\033[200~a\033b\033[201X\033[97;1;97u\000z\033[201~";
   static const unsigned char expected[] =
       "a\033b\033[201X\033[97;1;97u\000z";
   struct cwiki_input_parser parser;
   struct capture capture = {0};

   check_fragmentation(fixture, sizeof(fixture) - 1U, CWIKI_INPUT_PASTE,
       "paste survives every fragmentation boundary");
   cwiki_input_parser_init(&parser);
   check(cwiki_input_parser_feed(&parser, fixture, sizeof(fixture) - 1U,
       capture_event, &capture) == 0, "parse bracketed paste");
   check(capture.count == 1U && capture.events[0].event.kind == CWIKI_INPUT_PASTE,
       "emit one event for entire paste");
   check(capture.events[0].event.text_len == sizeof(expected) - 1U &&
       memcmp(capture.events[0].event.text, expected, sizeof(expected) - 1U) == 0,
       "preserve escape, CSI-u, NUL, and false end marker bytes literally");
   cwiki_input_parser_destroy(&parser);
}

static void
test_rejection_and_recovery(void)
{
   static const unsigned char fixture[] =
       "\033[12x\033[97;0u\033[97;257u\033[97;;;97u"
       "\033[97:65;1u\033[97;1;31u\033[42949672960;1u\033[98;1;98u";
   struct cwiki_input_parser parser;
   struct capture capture = {0};
   unsigned char overflow[180];
   size_t offset = 0U;

   cwiki_input_parser_init(&parser);
   check(cwiki_input_parser_feed(&parser, fixture, sizeof(fixture) - 1U,
       capture_event, &capture) == 0, "reject malformed fields and continue");
   check(cwiki_input_parser_rejected(&parser) == 7U,
       "count each malformed sequence once");
   check(capture.count == 1U && capture.events[0].event.key == 98U &&
       capture.events[0].event.text_len == 1U &&
       capture.events[0].event.text[0] == (unsigned char)'b',
       "recover at valid key after malformed fields and numeric overflow");
   cwiki_input_parser_destroy(&parser);

   overflow[offset++] = 0x1bU;
   overflow[offset++] = (unsigned char)'[';
   (void)memset(overflow + offset, '9', 150U);
   offset += 150U;
   (void)memcpy(overflow + offset, "\033[99;1;99u",
       sizeof("\033[99;1;99u") - 1U);
   offset += sizeof("\033[99;1;99u") - 1U;
   (void)memset(&capture, 0, sizeof(capture));
   cwiki_input_parser_init(&parser);
   check(cwiki_input_parser_feed(&parser, overflow, offset, capture_event,
       &capture) == 0, "reject overlong CSI and continue");
   check(cwiki_input_parser_rejected(&parser) == 1U && capture.count == 1U &&
       capture.events[0].event.key == 99U,
       "recover at event following overlong CSI");
   cwiki_input_parser_destroy(&parser);
}

int
main(void)
{
   test_protocol_strings();
   test_key_fields();
   test_distinct_keys_and_concatenation();
   test_literal_paste();
   test_rejection_and_recovery();
   if (failures != 0) {
      return 1;
   }
   (void)puts("input parser: ok");
   return 0;
}
