#ifndef CWIKI_CLUE_RENDER_H
#define CWIKI_CLUE_RENDER_H

#include "float.h"
#include "keymap.h"

#include <stddef.h>

struct cwiki_action_registry;

struct cwiki_clue_item {
   const char *key;
   size_t key_length;
   const char *label;
   size_t label_length;
};

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

/* Render a noninteractive clue-style list from caller-owned UTF-8 spans. */
int cwiki_clue_render_list_overlay(const struct cwiki_clue_item *items,
    size_t item_count, const struct cwiki_float_area *area,
    enum cwiki_float_border border, const char *title,
    const char *empty_message, char *bytes, size_t capacity, size_t *length);

#endif
