#ifndef CWIKI_INPUT_H
#define CWIKI_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CWIKI_INPUT_KEYBOARD_PUSH "\x1b[>29u"
#define CWIKI_INPUT_KEYBOARD_POP "\x1b[<u"
#define CWIKI_INPUT_PASTE_ENABLE "\x1b[?2004h"
#define CWIKI_INPUT_PASTE_DISABLE "\x1b[?2004l"

enum cwiki_input_event_kind {
   CWIKI_INPUT_KEY,
   CWIKI_INPUT_PASTE
};

enum cwiki_input_key_action {
   CWIKI_INPUT_PRESS = 1,
   CWIKI_INPUT_REPEAT = 2,
   CWIKI_INPUT_RELEASE = 3
};

enum cwiki_input_modifier {
   CWIKI_INPUT_SHIFT = 1U,
   CWIKI_INPUT_ALT = 2U,
   CWIKI_INPUT_CTRL = 4U,
   CWIKI_INPUT_SUPER = 8U,
   CWIKI_INPUT_HYPER = 16U,
   CWIKI_INPUT_META = 32U,
   CWIKI_INPUT_CAPS_LOCK = 64U,
   CWIKI_INPUT_NUM_LOCK = 128U
};

struct cwiki_input_event {
   enum cwiki_input_event_kind kind;
   uint32_t key;
   uint32_t shifted_key;
   uint32_t base_layout_key;
   unsigned int modifiers;
   enum cwiki_input_key_action action;
   bool has_shifted_key;
   bool has_base_layout_key;
   /* Valid only for the duration of the emit callback. */
   const unsigned char *text;
   size_t text_len;
};

typedef void (*cwiki_input_emit_fn)(const struct cwiki_input_event *, void *);

struct cwiki_input_parser {
   unsigned char csi[128];
   size_t csi_len;
   unsigned char *paste;
   size_t paste_len;
   size_t paste_cap;
   size_t rejected;
   unsigned int state;
};

void cwiki_input_parser_init(struct cwiki_input_parser *parser);
void cwiki_input_parser_destroy(struct cwiki_input_parser *parser);
int cwiki_input_parser_feed(struct cwiki_input_parser *parser,
    const unsigned char *bytes, size_t length, cwiki_input_emit_fn emit,
    void *context);
size_t cwiki_input_parser_rejected(const struct cwiki_input_parser *parser);

#endif
