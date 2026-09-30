#include "keymap.h"

#include <stdlib.h>
#include <string.h>

#define KEYMAP_MODIFIERS ((unsigned int)(CWIKI_INPUT_SHIFT | CWIKI_INPUT_ALT | \
    CWIKI_INPUT_CTRL | CWIKI_INPUT_SUPER | CWIKI_INPUT_HYPER | \
    CWIKI_INPUT_META | CWIKI_INPUT_CAPS_LOCK | CWIKI_INPUT_NUM_LOCK))

struct binding {
   enum cwiki_keymap_mode mode;
   struct cwiki_key_sequence sequence;
   char *action_name;
};

struct cwiki_keymap {
   const struct cwiki_action_registry *actions;
   struct binding *bindings;
   size_t count;
};

#ifdef CWIKI_KEYMAP_TESTING
static size_t allocations_left;
static bool fail_enabled;

void
cwiki_keymap_test_fail_allocation_after(size_t successful_allocations)
{
   allocations_left = successful_allocations;
   fail_enabled = true;
}

void
cwiki_keymap_test_reset_allocation(void)
{
   fail_enabled = false;
}

static bool
allocation_fails(void)
{
   if (!fail_enabled) {
      return false;
   }
   if (allocations_left == 0U) {
      return true;
   }
   allocations_left--;
   return false;
}
#else
static bool
allocation_fails(void)
{
   return false;
}
#endif

static void *
keymap_malloc(size_t size)
{
   return allocation_fails() ? NULL : malloc(size);
}

static void *
keymap_realloc(void *pointer, size_t size)
{
   return allocation_fails() ? NULL : realloc(pointer, size);
}

static char *
copy_string(const char *source)
{
   size_t length = strlen(source);
   char *copy;

   if (length == SIZE_MAX) {
      return NULL;
   }
   copy = keymap_malloc(length + 1U);
   if (copy != NULL) {
      (void)memcpy(copy, source, length + 1U);
   }
   return copy;
}

static bool
valid_mode(enum cwiki_keymap_mode mode)
{
   return mode >= CWIKI_KEYMAP_NORMAL && mode < CWIKI_KEYMAP_MODE_COUNT;
}

static bool
valid_key(struct cwiki_key key)
{
   return key.key != 0U && (key.modifiers & ~KEYMAP_MODIFIERS) == 0U;
}

static bool
valid_sequence(const struct cwiki_key_sequence *sequence)
{
   size_t i;

   if (sequence == NULL || sequence->length == 0U ||
       sequence->length > CWIKI_KEYMAP_MAX_SEQUENCE) {
      return false;
   }
   for (i = 0U; i < sequence->length; i++) {
      if (!valid_key(sequence->keys[i])) {
         return false;
      }
   }
   return true;
}

static int
compare_key(struct cwiki_key left, struct cwiki_key right)
{
   if (left.key != right.key) {
      return left.key < right.key ? -1 : 1;
   }
   if (left.modifiers != right.modifiers) {
      return left.modifiers < right.modifiers ? -1 : 1;
   }
   return 0;
}

static int
compare_sequence(const struct cwiki_key_sequence *left,
    const struct cwiki_key_sequence *right)
{
   size_t i;
   size_t common = left->length < right->length ? left->length : right->length;

   for (i = 0U; i < common; i++) {
      int order = compare_key(left->keys[i], right->keys[i]);

      if (order != 0) {
         return order;
      }
   }
   if (left->length == right->length) {
      return 0;
   }
   return left->length < right->length ? -1 : 1;
}

static int
compare_binding(enum cwiki_keymap_mode mode,
    const struct cwiki_key_sequence *sequence, const struct binding *binding)
{
   if (mode != binding->mode) {
      return mode < binding->mode ? -1 : 1;
   }
   return compare_sequence(sequence, &binding->sequence);
}

static size_t
lower_bound(const struct cwiki_keymap *keymap, enum cwiki_keymap_mode mode,
    const struct cwiki_key_sequence *sequence, bool *found)
{
   size_t low = 0U;
   size_t high = keymap->count;

   while (low < high) {
      size_t middle = low + (high - low) / 2U;

      if (compare_binding(mode, sequence, &keymap->bindings[middle]) > 0) {
         low = middle + 1U;
      } else {
         high = middle;
      }
   }
   *found = low < keymap->count &&
       compare_binding(mode, sequence, &keymap->bindings[low]) == 0;
   return low;
}

static enum cwiki_keymap_status
validate_action(const struct cwiki_keymap *keymap, const char *action_name)
{
   struct cwiki_action_info info;
   enum cwiki_action_status status;

   if (action_name == NULL) {
      return CWIKI_KEYMAP_UNKNOWN_ACTION;
   }
   status = cwiki_action_lookup(keymap->actions, action_name, &info);
   return status == CWIKI_ACTION_OK ? CWIKI_KEYMAP_OK :
       CWIKI_KEYMAP_UNKNOWN_ACTION;
}

enum cwiki_keymap_status
cwiki_keymap_init(struct cwiki_keymap **keymap,
    const struct cwiki_action_registry *actions)
{
   struct cwiki_keymap *created;

   if (keymap == NULL || actions == NULL) {
      return CWIKI_KEYMAP_INVALID;
   }
   *keymap = NULL;
   created = keymap_malloc(sizeof(*created));
   if (created == NULL) {
      return CWIKI_KEYMAP_NO_MEMORY;
   }
   created->actions = actions;
   created->bindings = NULL;
   created->count = 0U;
   *keymap = created;
   return CWIKI_KEYMAP_OK;
}

enum cwiki_keymap_status
cwiki_keymap_clone(struct cwiki_keymap **copy,
    const struct cwiki_keymap *source)
{
   struct cwiki_keymap *created = NULL;
   size_t i;

   if (copy == NULL || source == NULL) {
      return CWIKI_KEYMAP_INVALID;
   }
   *copy = NULL;
   if (cwiki_keymap_init(&created, source->actions) != CWIKI_KEYMAP_OK) {
      return CWIKI_KEYMAP_NO_MEMORY;
   }
   for (i = 0U; i < source->count; i++) {
      enum cwiki_keymap_status status = cwiki_keymap_bind(created,
          source->bindings[i].mode, &source->bindings[i].sequence,
          source->bindings[i].action_name);

      if (status != CWIKI_KEYMAP_OK) {
         cwiki_keymap_free(created);
         return status;
      }
   }
   *copy = created;
   return CWIKI_KEYMAP_OK;
}

void
cwiki_keymap_free(struct cwiki_keymap *keymap)
{
   size_t i;

   if (keymap == NULL) {
      return;
   }
   for (i = 0U; i < keymap->count; i++) {
      free(keymap->bindings[i].action_name);
   }
   free(keymap->bindings);
   free(keymap);
}

enum cwiki_keymap_status
cwiki_keymap_bind(struct cwiki_keymap *keymap, enum cwiki_keymap_mode mode,
    const struct cwiki_key_sequence *sequence, const char *action_name)
{
   struct binding *grown;
   char *name;
   size_t index;
   bool found;

   if (keymap == NULL || !valid_mode(mode) || !valid_sequence(sequence)) {
      return CWIKI_KEYMAP_INVALID;
   }
   if (validate_action(keymap, action_name) != CWIKI_KEYMAP_OK) {
      return CWIKI_KEYMAP_UNKNOWN_ACTION;
   }
   index = lower_bound(keymap, mode, sequence, &found);
   if (found) {
      return CWIKI_KEYMAP_CONFLICT;
   }
   if (keymap->count == SIZE_MAX / sizeof(*keymap->bindings)) {
      return CWIKI_KEYMAP_NO_MEMORY;
   }
   name = copy_string(action_name);
   if (name == NULL) {
      return CWIKI_KEYMAP_NO_MEMORY;
   }
   grown = keymap_realloc(keymap->bindings,
       (keymap->count + 1U) * sizeof(*keymap->bindings));
   if (grown == NULL) {
      free(name);
      return CWIKI_KEYMAP_NO_MEMORY;
   }
   keymap->bindings = grown;
   if (index < keymap->count) {
      (void)memmove(&keymap->bindings[index + 1U], &keymap->bindings[index],
          (keymap->count - index) * sizeof(*keymap->bindings));
   }
   keymap->bindings[index].mode = mode;
   keymap->bindings[index].sequence = *sequence;
   keymap->bindings[index].action_name = name;
   keymap->count++;
   return CWIKI_KEYMAP_OK;
}

enum cwiki_keymap_status
cwiki_keymap_rebind(struct cwiki_keymap *keymap, enum cwiki_keymap_mode mode,
    const struct cwiki_key_sequence *sequence, const char *action_name)
{
   char *name;
   size_t index;
   bool found;

   if (keymap == NULL || !valid_mode(mode) || !valid_sequence(sequence)) {
      return CWIKI_KEYMAP_INVALID;
   }
   if (validate_action(keymap, action_name) != CWIKI_KEYMAP_OK) {
      return CWIKI_KEYMAP_UNKNOWN_ACTION;
   }
   index = lower_bound(keymap, mode, sequence, &found);
   if (!found) {
      return CWIKI_KEYMAP_NOT_FOUND;
   }
   name = copy_string(action_name);
   if (name == NULL) {
      return CWIKI_KEYMAP_NO_MEMORY;
   }
   free(keymap->bindings[index].action_name);
   keymap->bindings[index].action_name = name;
   return CWIKI_KEYMAP_OK;
}

enum cwiki_keymap_status
cwiki_keymap_unbind(struct cwiki_keymap *keymap, enum cwiki_keymap_mode mode,
    const struct cwiki_key_sequence *sequence)
{
   size_t index;
   bool found;

   if (keymap == NULL || !valid_mode(mode) || !valid_sequence(sequence)) {
      return CWIKI_KEYMAP_INVALID;
   }
   index = lower_bound(keymap, mode, sequence, &found);
   if (!found) {
      return CWIKI_KEYMAP_NOT_FOUND;
   }
   free(keymap->bindings[index].action_name);
   (void)memmove(&keymap->bindings[index], &keymap->bindings[index + 1U],
       (keymap->count - index - 1U) * sizeof(*keymap->bindings));
   keymap->count--;
   return CWIKI_KEYMAP_OK;
}

static bool
event_key(const struct cwiki_input_event *event, struct cwiki_key *key)
{
   if (event->kind != CWIKI_INPUT_KEY || event->action == CWIKI_INPUT_RELEASE) {
      return false;
   }
   key->key = event->has_base_layout_key ? event->base_layout_key : event->key;
   key->modifiers = event->modifiers;
   return valid_key(*key);
}

static bool
events_to_sequence(const struct cwiki_input_event *events, size_t count,
    struct cwiki_key_sequence *sequence)
{
   size_t i;

   if (count > CWIKI_KEYMAP_MAX_SEQUENCE || (count != 0U && events == NULL)) {
      return false;
   }
   sequence->length = count;
   for (i = 0U; i < count; i++) {
      if (!event_key(&events[i], &sequence->keys[i])) {
         return false;
      }
   }
   return true;
}

static bool
has_prefix(const struct binding *binding, enum cwiki_keymap_mode mode,
    const struct cwiki_key_sequence *prefix)
{
   size_t i;

   if (binding->mode != mode || binding->sequence.length < prefix->length) {
      return false;
   }
   for (i = 0U; i < prefix->length; i++) {
      if (compare_key(binding->sequence.keys[i], prefix->keys[i]) != 0) {
         return false;
      }
   }
   return true;
}

static enum cwiki_keymap_status
action_info(const struct cwiki_keymap *keymap, const struct binding *binding,
    struct cwiki_action_info *info)
{
   return cwiki_action_lookup(keymap->actions, binding->action_name, info) ==
       CWIKI_ACTION_OK ? CWIKI_KEYMAP_OK : CWIKI_KEYMAP_UNKNOWN_ACTION;
}

enum cwiki_keymap_status
cwiki_keymap_match(const struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *events,
    size_t event_count, struct cwiki_keymap_match *match)
{
   struct cwiki_key_sequence sequence;
   size_t i;

   if (keymap == NULL || !valid_mode(mode) || match == NULL) {
      return CWIKI_KEYMAP_INVALID;
   }
   (void)memset(match, 0, sizeof(*match));
   match->kind = CWIKI_KEYMAP_NO_MATCH;
   if (!events_to_sequence(events, event_count, &sequence)) {
      return event_count <= CWIKI_KEYMAP_MAX_SEQUENCE && events != NULL ?
          CWIKI_KEYMAP_OK : CWIKI_KEYMAP_INVALID;
   }
   for (i = 0U; i < keymap->count; i++) {
      const struct binding *binding = &keymap->bindings[i];

      if (!has_prefix(binding, mode, &sequence)) {
         continue;
      }
      if (binding->sequence.length == sequence.length) {
         match->kind = CWIKI_KEYMAP_COMPLETE;
         if (action_info(keymap, binding, &match->action) != CWIKI_KEYMAP_OK) {
            return CWIKI_KEYMAP_UNKNOWN_ACTION;
         }
      } else {
         match->has_continuations = true;
         if (match->kind != CWIKI_KEYMAP_COMPLETE) {
            match->kind = CWIKI_KEYMAP_PREFIX;
         }
      }
   }
   return CWIKI_KEYMAP_OK;
}

static bool
next_continuation(const struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_key_sequence *prefix,
    size_t wanted, struct cwiki_keymap_continuation *continuation)
{
   struct cwiki_key previous = {0};
   bool have_previous = false;
   size_t distinct = 0U;
   size_t i;

   for (i = 0U; i < keymap->count; i++) {
      const struct binding *binding = &keymap->bindings[i];
      struct cwiki_key next;

      if (!has_prefix(binding, mode, prefix) ||
          binding->sequence.length <= prefix->length) {
         continue;
      }
      next = binding->sequence.keys[prefix->length];
      if (have_previous && compare_key(previous, next) == 0) {
         if (distinct - 1U == wanted && continuation != NULL) {
            if (binding->sequence.length == prefix->length + 1U) {
               continuation->completes = true;
               (void)action_info(keymap, binding, &continuation->action);
            } else {
               continuation->has_continuations = true;
            }
         }
         continue;
      }
      previous = next;
      have_previous = true;
      if (distinct == wanted && continuation != NULL) {
         (void)memset(continuation, 0, sizeof(*continuation));
         continuation->key = next;
         if (binding->sequence.length == prefix->length + 1U) {
            continuation->completes = true;
            (void)action_info(keymap, binding, &continuation->action);
         } else {
            continuation->has_continuations = true;
         }
      }
      distinct++;
   }
   return wanted < distinct;
}

size_t
cwiki_keymap_continuation_count(const struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count)
{
   struct cwiki_key_sequence sequence;
   size_t count = 0U;

   if (keymap == NULL || !valid_mode(mode) ||
       !events_to_sequence(prefix, prefix_count, &sequence)) {
      return 0U;
   }
   while (next_continuation(keymap, mode, &sequence, count, NULL)) {
      count++;
   }
   return count;
}

enum cwiki_keymap_status
cwiki_keymap_continuation_at(const struct cwiki_keymap *keymap,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count, size_t index,
    struct cwiki_keymap_continuation *continuation)
{
   struct cwiki_key_sequence sequence;

   if (keymap == NULL || !valid_mode(mode) || continuation == NULL ||
       !events_to_sequence(prefix, prefix_count, &sequence)) {
      return CWIKI_KEYMAP_INVALID;
   }
   return next_continuation(keymap, mode, &sequence, index, continuation) ?
       CWIKI_KEYMAP_OK : CWIKI_KEYMAP_NOT_FOUND;
}

size_t
cwiki_keymap_binding_count(const struct cwiki_keymap *keymap)
{
   return keymap == NULL ? 0U : keymap->count;
}

enum cwiki_keymap_status
cwiki_keymap_binding_at(const struct cwiki_keymap *keymap, size_t index,
    struct cwiki_keymap_binding_info *binding)
{
   if (keymap == NULL || binding == NULL || index >= keymap->count) {
      return CWIKI_KEYMAP_INVALID;
   }
   binding->mode = keymap->bindings[index].mode;
   binding->sequence = keymap->bindings[index].sequence;
   binding->action_name = keymap->bindings[index].action_name;
   return CWIKI_KEYMAP_OK;
}
