#ifndef CWIKI_CAPABILITIES_H
#define CWIKI_CAPABILITIES_H

#include <stddef.h>
#include <stdint.h>

#define CWIKI_CAPABILITIES_KEYBOARD_QUERY "\x1b[?u"
#define CWIKI_CAPABILITIES_GRAPHICS_QUERY \
   "\x1b_Gi=1129797963,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\"
#define CWIKI_CAPABILITIES_DA1_QUERY "\x1b[c"
#define CWIKI_CAPABILITIES_GRAPHICS_QUERY_ID UINT32_C(1129797963)

enum cwiki_capability_state {
   CWIKI_CAPABILITY_PENDING,
   CWIKI_CAPABILITY_SUPPORTED,
   CWIKI_CAPABILITY_MISSING
};

struct cwiki_capabilities_result {
   enum cwiki_capability_state keyboard;
   enum cwiki_capability_state graphics;
   uint32_t keyboard_flags;
   int complete;
};

struct cwiki_capabilities_parser {
   unsigned char sequence[128];
   size_t sequence_len;
   unsigned int state;
   struct cwiki_capabilities_result result;
};

void cwiki_capabilities_parser_init(struct cwiki_capabilities_parser *parser);
size_t cwiki_capabilities_parser_feed(struct cwiki_capabilities_parser *parser,
    const unsigned char *bytes, size_t length);
struct cwiki_capabilities_result cwiki_capabilities_parser_result(
    const struct cwiki_capabilities_parser *parser);

#endif
