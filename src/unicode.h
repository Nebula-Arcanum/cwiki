#ifndef CWIKI_UNICODE_H
#define CWIKI_UNICODE_H

#include <stdbool.h>
#include <stddef.h>

bool cwiki_utf8_validate(const char *bytes, size_t length);
bool cwiki_grapheme_boundary(const char *bytes, size_t length, size_t offset);
size_t cwiki_grapheme_next(const char *bytes, size_t length, size_t offset);
size_t cwiki_grapheme_previous(const char *bytes, size_t length, size_t offset);
int cwiki_grapheme_width(const char *bytes, size_t length);

#endif
