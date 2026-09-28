#ifndef CWIKI_TESTS_SUPPORT_REFERENCE_BUFFER_H
#define CWIKI_TESTS_SUPPORT_REFERENCE_BUFFER_H

#include <stddef.h>

struct reference_buffer {
   unsigned char *bytes;
   size_t length;
   size_t capacity;
};

void reference_buffer_init(struct reference_buffer *buffer);
void reference_buffer_free(struct reference_buffer *buffer);
const unsigned char *reference_buffer_bytes(const struct reference_buffer *buffer);
size_t reference_buffer_length(const struct reference_buffer *buffer);
int reference_buffer_insert(struct reference_buffer *buffer, size_t offset,
    const void *bytes, size_t length);
int reference_buffer_delete(struct reference_buffer *buffer, size_t offset,
    size_t length);

#endif
