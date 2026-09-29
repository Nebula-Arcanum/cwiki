#ifndef CWIKI_CLUE_RENDER_H
#define CWIKI_CLUE_RENDER_H

#include "float.h"
#include "keymap.h"

#include <stddef.h>

struct cwiki_action_registry;

/*
 * Render immediate keymap continuations directly from keymap/action metadata.
 * NULL bytes with capacity 0 queries the required length. On error, output and
 * *length are unchanged. Deeper branches use "More…" until configured clue
 * group labels are available.
 */
int cwiki_clue_render_overlay(const struct cwiki_keymap *keymap,
    const struct cwiki_action_registry *actions,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count, const struct cwiki_float_area *area,
    enum cwiki_float_border border, const char *title, char *bytes,
    size_t capacity, size_t *length);

#endif
