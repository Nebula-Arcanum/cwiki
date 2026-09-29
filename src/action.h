#ifndef CWIKI_ACTION_H
#define CWIKI_ACTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cwiki_action_registry;

enum cwiki_action_status {
   CWIKI_ACTION_OK,
   CWIKI_ACTION_INVALID,
   CWIKI_ACTION_NO_MEMORY,
   CWIKI_ACTION_DUPLICATE,
   CWIKI_ACTION_NOT_FOUND,
   CWIKI_ACTION_DISABLED
};

enum cwiki_action_parameter_type {
   CWIKI_ACTION_PARAMETER_NONE,
   CWIKI_ACTION_PARAMETER_BOOLEAN,
   CWIKI_ACTION_PARAMETER_INTEGER,
   CWIKI_ACTION_PARAMETER_STRING
};

struct cwiki_action_argument {
   enum cwiki_action_parameter_type type;
   union {
      bool boolean;
      int64_t integer;
      const char *string;
   } value;
};

typedef int (*cwiki_action_handler)(void *context,
    const struct cwiki_action_argument *argument);
typedef bool (*cwiki_action_available)(void *context);

struct cwiki_action_spec {
   const char *name;
   const char *label;
   const char *description;
   enum cwiki_action_parameter_type parameter_type;
   const char *parameter_name;
   cwiki_action_handler handler;
   cwiki_action_available available;
   void *context;
};

struct cwiki_action_info {
   const char *name;
   const char *label;
   const char *description;
   enum cwiki_action_parameter_type parameter_type;
   const char *parameter_name;
};

enum cwiki_action_status cwiki_action_registry_init(
    struct cwiki_action_registry **registry);
void cwiki_action_registry_free(struct cwiki_action_registry *registry);
/* Names are lowercase dot-separated components of letters, digits, and '-'. */
enum cwiki_action_status cwiki_action_register(
    struct cwiki_action_registry *registry,
    const struct cwiki_action_spec *spec);

size_t cwiki_action_count(const struct cwiki_action_registry *registry);
/* Returned metadata is owned by the registry and remains valid until free. */
enum cwiki_action_status cwiki_action_at(
    const struct cwiki_action_registry *registry, size_t index,
    struct cwiki_action_info *info);
enum cwiki_action_status cwiki_action_lookup(
    const struct cwiki_action_registry *registry, const char *name,
    struct cwiki_action_info *info);
enum cwiki_action_status cwiki_action_is_available(
    const struct cwiki_action_registry *registry, const char *name,
    bool *available);

/*
 * Dispatch validates the argument, checks availability, and invokes the same
 * path for every caller. The callback result is written to handler_result.
 * Availability and handler callbacks may register actions in this registry;
 * dispatch uses the registration captured before either callback runs.
 */
enum cwiki_action_status cwiki_action_dispatch(
    struct cwiki_action_registry *registry, const char *name,
    const struct cwiki_action_argument *argument, int *handler_result);

#ifdef CWIKI_ACTION_TESTING
void cwiki_action_test_fail_allocation_after(size_t successful_allocations);
void cwiki_action_test_reset_allocation(void);
#endif

#endif
