#include "action.h"

#include <stdlib.h>
#include <string.h>

struct action {
   char *name;
   char *label;
   char *description;
   enum cwiki_action_parameter_type parameter_type;
   char *parameter_name;
   cwiki_action_handler handler;
   cwiki_action_available available;
   void *context;
};

struct cwiki_action_registry {
   struct action *actions;
   size_t count;
};

#ifdef CWIKI_ACTION_TESTING
static size_t allocations_left;
static bool fail_enabled;

void
cwiki_action_test_fail_allocation_after(size_t successful_allocations)
{
   allocations_left = successful_allocations;
   fail_enabled = true;
}

void
cwiki_action_test_reset_allocation(void)
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
action_malloc(size_t size)
{
   return allocation_fails() ? NULL : malloc(size);
}

static void *
action_realloc(void *pointer, size_t size)
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
   copy = action_malloc(length + 1U);
   if (copy != NULL) {
      (void)memcpy(copy, source, length + 1U);
   }
   return copy;
}

static bool
valid_name(const char *name)
{
   size_t i;
   bool component_start = true;

   if (name == NULL || name[0] == '\0') {
      return false;
   }
   for (i = 0U; name[i] != '\0'; i++) {
      char byte = name[i];

      if (component_start) {
         if (byte < 'a' || byte > 'z') {
            return false;
         }
         component_start = false;
      } else if (byte == '.') {
         component_start = true;
      } else if (!((byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9') || byte == '-')) {
         return false;
      }
   }
   return !component_start;
}

static bool
valid_parameter_name(const char *name)
{
   size_t i;

   if (name == NULL || name[0] == '\0') {
      return false;
   }
   for (i = 0U; name[i] != '\0'; i++) {
      char byte = name[i];

      if (!((byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9') || byte == '-')) {
         return false;
      }
   }
   return true;
}

static bool
valid_spec(const struct cwiki_action_spec *spec)
{
   if (spec == NULL || !valid_name(spec->name) || spec->label == NULL ||
       spec->label[0] == '\0' || spec->description == NULL ||
       spec->description[0] == '\0' || spec->handler == NULL ||
       spec->parameter_type < CWIKI_ACTION_PARAMETER_NONE ||
       spec->parameter_type > CWIKI_ACTION_PARAMETER_STRING) {
      return false;
   }
   if (spec->parameter_type == CWIKI_ACTION_PARAMETER_NONE) {
      return spec->parameter_name == NULL;
   }
   return valid_parameter_name(spec->parameter_name);
}

static size_t
lower_bound(const struct cwiki_action_registry *registry, const char *name,
    bool *found)
{
   size_t low = 0U;
   size_t high = registry->count;

   while (low < high) {
      size_t middle = low + (high - low) / 2U;
      int order = strcmp(registry->actions[middle].name, name);

      if (order < 0) {
         low = middle + 1U;
      } else {
         high = middle;
      }
   }
   *found = low < registry->count &&
       strcmp(registry->actions[low].name, name) == 0;
   return low;
}

static void
action_free(struct action *action)
{
   free(action->name);
   free(action->label);
   free(action->description);
   free(action->parameter_name);
}

enum cwiki_action_status
cwiki_action_registry_init(struct cwiki_action_registry **registry)
{
   struct cwiki_action_registry *created;

   if (registry == NULL) {
      return CWIKI_ACTION_INVALID;
   }
   *registry = NULL;
   created = action_malloc(sizeof(*created));
   if (created == NULL) {
      return CWIKI_ACTION_NO_MEMORY;
   }
   created->actions = NULL;
   created->count = 0U;
   *registry = created;
   return CWIKI_ACTION_OK;
}

void
cwiki_action_registry_free(struct cwiki_action_registry *registry)
{
   size_t i;

   if (registry == NULL) {
      return;
   }
   for (i = 0U; i < registry->count; i++) {
      action_free(&registry->actions[i]);
   }
   free(registry->actions);
   free(registry);
}

enum cwiki_action_status
cwiki_action_register(struct cwiki_action_registry *registry,
    const struct cwiki_action_spec *spec)
{
   struct action added = {0};
   struct action *grown;
   size_t index;
   bool found;

   if (registry == NULL || !valid_spec(spec)) {
      return CWIKI_ACTION_INVALID;
   }
   index = lower_bound(registry, spec->name, &found);
   if (found) {
      return CWIKI_ACTION_DUPLICATE;
   }
   if (registry->count == SIZE_MAX / sizeof(*registry->actions)) {
      return CWIKI_ACTION_NO_MEMORY;
   }
   added.name = copy_string(spec->name);
   added.label = copy_string(spec->label);
   added.description = copy_string(spec->description);
   if (spec->parameter_name != NULL) {
      added.parameter_name = copy_string(spec->parameter_name);
   }
   if (added.name == NULL || added.label == NULL || added.description == NULL ||
       (spec->parameter_name != NULL && added.parameter_name == NULL)) {
      action_free(&added);
      return CWIKI_ACTION_NO_MEMORY;
   }
   grown = action_realloc(registry->actions,
       (registry->count + 1U) * sizeof(*registry->actions));
   if (grown == NULL) {
      action_free(&added);
      return CWIKI_ACTION_NO_MEMORY;
   }
   registry->actions = grown;
   (void)memmove(&registry->actions[index + 1U],
       &registry->actions[index],
       (registry->count - index) * sizeof(*registry->actions));
   added.parameter_type = spec->parameter_type;
   added.handler = spec->handler;
   added.available = spec->available;
   added.context = spec->context;
   registry->actions[index] = added;
   registry->count++;
   return CWIKI_ACTION_OK;
}

size_t
cwiki_action_count(const struct cwiki_action_registry *registry)
{
   return registry == NULL ? 0U : registry->count;
}

static void
fill_info(const struct action *action, struct cwiki_action_info *info)
{
   info->name = action->name;
   info->label = action->label;
   info->description = action->description;
   info->parameter_type = action->parameter_type;
   info->parameter_name = action->parameter_name;
}

enum cwiki_action_status
cwiki_action_at(const struct cwiki_action_registry *registry, size_t index,
    struct cwiki_action_info *info)
{
   if (registry == NULL || info == NULL) {
      return CWIKI_ACTION_INVALID;
   }
   if (index >= registry->count) {
      return CWIKI_ACTION_NOT_FOUND;
   }
   fill_info(&registry->actions[index], info);
   return CWIKI_ACTION_OK;
}

enum cwiki_action_status
cwiki_action_lookup(const struct cwiki_action_registry *registry,
    const char *name, struct cwiki_action_info *info)
{
   size_t index;
   bool found;

   if (registry == NULL || !valid_name(name) || info == NULL) {
      return CWIKI_ACTION_INVALID;
   }
   index = lower_bound(registry, name, &found);
   if (!found) {
      return CWIKI_ACTION_NOT_FOUND;
   }
   fill_info(&registry->actions[index], info);
   return CWIKI_ACTION_OK;
}

enum cwiki_action_status
cwiki_action_is_available(const struct cwiki_action_registry *registry,
    const char *name, bool *available)
{
   cwiki_action_available check;
   void *context;
   size_t index;
   bool found;

   if (registry == NULL || !valid_name(name) || available == NULL) {
      return CWIKI_ACTION_INVALID;
   }
   index = lower_bound(registry, name, &found);
   if (!found) {
      return CWIKI_ACTION_NOT_FOUND;
   }
   check = registry->actions[index].available;
   context = registry->actions[index].context;
   *available = check == NULL || check(context);
   return CWIKI_ACTION_OK;
}

static bool
argument_matches(enum cwiki_action_parameter_type expected,
    const struct cwiki_action_argument *argument)
{
   if (expected == CWIKI_ACTION_PARAMETER_NONE) {
      return argument == NULL || argument->type == CWIKI_ACTION_PARAMETER_NONE;
   }
   if (argument == NULL || argument->type != expected) {
      return false;
   }
   return expected != CWIKI_ACTION_PARAMETER_STRING ||
       argument->value.string != NULL;
}

enum cwiki_action_status
cwiki_action_dispatch(struct cwiki_action_registry *registry, const char *name,
    const struct cwiki_action_argument *argument, int *handler_result)
{
   cwiki_action_handler handler;
   cwiki_action_available available;
   enum cwiki_action_parameter_type parameter_type;
   void *context;
   size_t index;
   bool found;

   if (registry == NULL || !valid_name(name) || handler_result == NULL) {
      return CWIKI_ACTION_INVALID;
   }
   index = lower_bound(registry, name, &found);
   if (!found) {
      return CWIKI_ACTION_NOT_FOUND;
   }
   handler = registry->actions[index].handler;
   available = registry->actions[index].available;
   parameter_type = registry->actions[index].parameter_type;
   context = registry->actions[index].context;
   if (!argument_matches(parameter_type, argument)) {
      return CWIKI_ACTION_INVALID;
   }
   if (available != NULL && !available(context)) {
      return CWIKI_ACTION_DISABLED;
   }
   *handler_result = handler(context, argument);
   return CWIKI_ACTION_OK;
}
