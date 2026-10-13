#ifndef CWIKI_UNICODE_H
#define CWIKI_UNICODE_H

#include <stdbool.h>
#include <stddef.h>

bool cwiki_utf8_validate(const char *bytes, size_t length);
bool cwiki_grapheme_boundary(const char *bytes, size_t length, size_t offset);
size_t cwiki_grapheme_next(const char *bytes, size_t length, size_t offset);
size_t cwiki_grapheme_previous(const char *bytes, size_t length, size_t offset);
int cwiki_grapheme_width(const char *bytes, size_t length);

/*
 * Unchecked stepping for loops over a line the caller already holds.
 * Precondition, not verified: offset is a grapheme boundary of bytes (the
 * line start, or a value this function or the checked API returned). Returns
 * the next boundary (length when offset >= length) in time proportional to
 * one cluster, where cwiki_grapheme_next revalidates and rewalks the whole
 * prefix on every call. Invalid UTF-8 met inside the cluster yields SIZE_MAX.
 * Use the checked API for untrusted or one-off positions.
 */
size_t cwiki_grapheme_next_unchecked(const char *bytes, size_t length,
    size_t offset);

#ifdef CWIKI_UNICODE_TESTING
/* Codepoints decoded by validation and boundary walks since process start. */
size_t cwiki_unicode_testing_steps(void);
#endif

#endif
