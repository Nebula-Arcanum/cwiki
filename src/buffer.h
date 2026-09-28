#ifndef CWIKI_BUFFER_H
#define CWIKI_BUFFER_H

#include "zone.h"

#include <stdbool.h>
#include <stddef.h>

#define CWIKI_DEGRADED_LINE_BYTES (1024U * 1024U)

enum cwiki_line_ending {
   CWIKI_LINE_ENDING_LF,
   CWIKI_LINE_ENDING_CRLF
};

struct cwiki_line {
   char *bytes;
   size_t length;
   size_t capacity;
   struct cwiki_zone_stack end_zones;
   bool zone_dirty;
};

struct cwiki_position {
   size_t line;
   size_t byte;
};

struct cwiki_buffer {
   struct cwiki_line *lines;
   size_t line_count;
   size_t line_capacity;
   enum cwiki_line_ending line_ending;
   struct cwiki_position **positions;
   size_t position_count;
   size_t position_capacity;
};

int cwiki_buffer_init(struct cwiki_buffer *buffer);
void cwiki_buffer_free(struct cwiki_buffer *buffer);
int cwiki_buffer_load(struct cwiki_buffer *buffer, const char *bytes,
    size_t length);
int cwiki_buffer_encode(const struct cwiki_buffer *buffer, char **bytes,
    size_t *length);
bool cwiki_buffer_line_degraded(const struct cwiki_buffer *buffer, size_t line);
int cwiki_buffer_register_position(struct cwiki_buffer *buffer,
    struct cwiki_position *position);
void cwiki_buffer_unregister_position(struct cwiki_buffer *buffer,
    struct cwiki_position *position);
int cwiki_buffer_insert(struct cwiki_buffer *buffer, size_t line, size_t byte,
    const char *bytes, size_t length);
int cwiki_buffer_delete(struct cwiki_buffer *buffer, size_t line, size_t byte,
    size_t length);
int cwiki_buffer_split(struct cwiki_buffer *buffer, size_t line, size_t byte);
int cwiki_buffer_join(struct cwiki_buffer *buffer, size_t line);

#endif
