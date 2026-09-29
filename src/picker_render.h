#ifndef CWIKI_PICKER_RENDER_H
#define CWIKI_PICKER_RENDER_H

#include "float.h"

#include <stddef.h>

struct cwiki_picker;

/*
 * Builds an ANSI overlay only; append it after the base frame inside the same
 * synchronized update. NULL bytes with capacity 0 queries the required length.
 * On error, output and *length are unchanged. The selected item is kept visible.
 */
int cwiki_picker_render_overlay(const struct cwiki_picker *picker,
    const struct cwiki_float_area *area, enum cwiki_float_border border,
    char *bytes, size_t capacity, size_t *length);

#endif
