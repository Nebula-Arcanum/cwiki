#ifndef CWIKI_KEYMAP_H
#define CWIKI_KEYMAP_H

#include "action.h"
#include "input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CWIKI_KEYMAP_MAX_SEQUENCE 16U

struct cwiki_keymap;

enum cwiki_keymap_mode {
   CWIKI_KEYMAP_NORMAL,
   CWIKI_KEYMAP_INSERT,
   CWIKI_KEYMAP_REPLACE,
   CWIKI_KEYMAP_COMMAND,
   CWIKI_KEYMAP_MODE_COUNT
};

struct cwiki_key {
   uint32_t key;
   unsigned int modifiers;
};

struct cwiki_key_sequence {
   struct cwiki_key keys[CWIKI_KEYMAP_MAX_SEQUENCE];
   size_t length;
};

enum cwiki_keymap_status {
   CWIKI_KEYMAP_OK,
   CWIKI_KEYMAP_INVALID,
   CWIKI_KEYMAP_NO_MEMORY,
   CWIKI_KEYMAP_CONFLICT,
   CWIKI_KEYMAP_NOT_FOUND,
   CWIKI_KEYMAP_UNKNOWN_ACTION
};

enum cwiki_keymap_match_kind {
   CWIKI_KEYMAP_NO_MATCH,
   CWIKI_KEYMAP_PREFIX,
   CWIKI_KEYMAP_COMPLETE
};

struct cwiki_keymap_match {
   enum cwiki_keymap_match_kind kind;
   struct cwiki_action_info action;
   bool has_continuations;
};

struct cwiki_keymap_continuation {
   struct cwiki_key key;
   bool completes;
   bool has_continuations;
   struct cwiki_action_info action;
};

struct cwiki_keymap_binding_info {
   enum cwiki_keymap_mode mode;
   struct cwiki_key_sequence sequence;
   const char *action_name;
};

/* The action registry must outlive the keymap. */
enum cwiki_keymap_status cwiki_keymap_init(struct cwiki_keymap **keymap,
    const struct cwiki_action_registry *actions);
enum cwiki_keymap_status cwiki_keymap_clone(struct cwiki_keymap **copy,
    const struct cwiki_keymap *source);
void cwiki_keymap_free(struct cwiki_keymap *keymap);

enum cwiki_keymap_status cwiki_keymap_bind(struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_key_sequence *sequence,
    const char *action_name);
enum cwiki_keymap_status cwiki_keymap_rebind(struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_key_sequence *sequence,
    const char *action_name);
enum cwiki_keymap_status cwiki_keymap_unbind(struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_key_sequence *sequence);

/* Paste and release events produce CWIKI_KEYMAP_NO_MATCH. */
enum cwiki_keymap_status cwiki_keymap_match(
    const struct cwiki_keymap *keymap, enum cwiki_keymap_mode mode,
    const struct cwiki_input_event *events, size_t event_count,
    struct cwiki_keymap_match *match);

size_t cwiki_keymap_continuation_count(const struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count);
enum cwiki_keymap_status cwiki_keymap_continuation_at(
    const struct cwiki_keymap *keymap, enum cwiki_keymap_mode mode,
    const struct cwiki_input_event *prefix, size_t prefix_count, size_t index,
    struct cwiki_keymap_continuation *continuation);

size_t cwiki_keymap_binding_count(const struct cwiki_keymap *keymap);
/* Returned action_name remains owned by keymap. */
enum cwiki_keymap_status cwiki_keymap_binding_at(
    const struct cwiki_keymap *keymap, size_t index,
    struct cwiki_keymap_binding_info *binding);

#ifdef CWIKI_KEYMAP_TESTING
void cwiki_keymap_test_fail_allocation_after(size_t successful_allocations);
void cwiki_keymap_test_reset_allocation(void);
#endif

#endif
