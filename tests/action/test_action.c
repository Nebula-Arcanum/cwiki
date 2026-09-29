#include "action.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static int
return_integer(void *context, const struct cwiki_action_argument *argument)
{
   int *calls = context;

   (*calls)++;
   return argument == NULL ? 7 : (int)argument->value.integer;
}

struct availability_context {
   bool enabled;
   int calls;
};

static bool
flag_available(void *context)
{
   return ((const struct availability_context *)context)->enabled;
}

static int
available_handler(void *context, const struct cwiki_action_argument *argument)
{
   struct availability_context *state = context;

   (void)argument;
   state->calls++;
   return 11;
}

static struct cwiki_action_spec
spec(const char *name, cwiki_action_handler handler, void *context)
{
   struct cwiki_action_spec result = {
      name, name, "test action", CWIKI_ACTION_PARAMETER_NONE, NULL,
      handler, NULL, context
   };

   return result;
}

static void
test_registration_lookup_and_enumeration(void)
{
   struct cwiki_action_registry *registry = NULL;
   struct cwiki_action_info info;
   struct cwiki_action_spec third;
   char mutable_name[] = "note.open";
   int calls = 0;

   check(cwiki_action_registry_init(&registry) == CWIKI_ACTION_OK,
       "initialize registry");
   third = spec(mutable_name, return_integer, &calls);
   third.label = "Open note";
   third.description = "Open the selected note";
   check(cwiki_action_register(registry, &third) == CWIKI_ACTION_OK,
       "register action with metadata");
   mutable_name[0] = 'x';
   third = spec("editor.undo", return_integer, &calls);
   check(cwiki_action_register(registry, &third) == CWIKI_ACTION_OK,
       "register lexically earlier action");
   third = spec("window.close", return_integer, &calls);
   check(cwiki_action_register(registry, &third) == CWIKI_ACTION_OK,
       "register lexically later action");

   check(cwiki_action_count(registry) == 3U,
       "count reports every registered action");
   check(cwiki_action_at(registry, 0U, &info) == CWIKI_ACTION_OK &&
       strcmp(info.name, "editor.undo") == 0 &&
       cwiki_action_at(registry, 1U, &info) == CWIKI_ACTION_OK &&
       strcmp(info.name, "note.open") == 0 &&
       cwiki_action_at(registry, 2U, &info) == CWIKI_ACTION_OK &&
       strcmp(info.name, "window.close") == 0,
       "enumeration is deterministic lexical order");
   check(cwiki_action_lookup(registry, "note.open", &info) ==
       CWIKI_ACTION_OK && strcmp(info.label, "Open note") == 0 &&
       strcmp(info.description, "Open the selected note") == 0 &&
       info.parameter_type == CWIKI_ACTION_PARAMETER_NONE &&
       info.parameter_name == NULL,
       "lookup returns owned stable metadata");
   check(cwiki_action_lookup(registry, "note.missing", &info) ==
       CWIKI_ACTION_NOT_FOUND, "lookup distinguishes an unknown action");
   check(cwiki_action_at(registry, 3U, &info) == CWIKI_ACTION_NOT_FOUND,
       "enumeration rejects an out-of-range index");
   cwiki_action_registry_free(registry);
}

static void
test_registration_rejections(void)
{
   struct cwiki_action_registry *registry = NULL;
   struct cwiki_action_spec valid;
   const char *const invalid_names[] = {
      "", ".note.open", "note..open", "note.open.", "Note.open",
      "note_open", "9note.open"
   };
   size_t i;
   int calls = 0;

   check(cwiki_action_registry_init(&registry) == CWIKI_ACTION_OK,
       "initialize rejection registry");
   valid = spec("note.open", return_integer, &calls);
   check(cwiki_action_register(registry, &valid) == CWIKI_ACTION_OK &&
       cwiki_action_register(registry, &valid) == CWIKI_ACTION_DUPLICATE,
       "duplicate stable names are rejected");
   for (i = 0U; i < sizeof(invalid_names) / sizeof(invalid_names[0]); i++) {
      struct cwiki_action_spec invalid =
          spec(invalid_names[i], return_integer, &calls);
      check(cwiki_action_register(registry, &invalid) == CWIKI_ACTION_INVALID,
          "invalid action name is rejected");
   }
   valid = spec("note.save", NULL, &calls);
   check(cwiki_action_register(registry, &valid) == CWIKI_ACTION_INVALID,
       "missing implementation is rejected");
   valid = spec("note.save", return_integer, &calls);
   valid.label = "";
   check(cwiki_action_register(registry, &valid) == CWIKI_ACTION_INVALID,
       "empty label is rejected");
   valid = spec("note.save", return_integer, &calls);
   valid.parameter_type = CWIKI_ACTION_PARAMETER_STRING;
   check(cwiki_action_register(registry, &valid) == CWIKI_ACTION_INVALID,
       "typed parameter requires a parameter name");
   valid.parameter_type = CWIKI_ACTION_PARAMETER_NONE;
   valid.parameter_name = "path";
   check(cwiki_action_register(registry, &valid) == CWIKI_ACTION_INVALID,
       "parameter-free action rejects a parameter name");
   valid.parameter_type = (enum cwiki_action_parameter_type)99;
   valid.parameter_name = "path";
   check(cwiki_action_register(registry, &valid) == CWIKI_ACTION_INVALID,
       "unknown parameter type is rejected");
   check(cwiki_action_count(registry) == 1U,
       "failed registrations leave the registry unchanged");
   cwiki_action_registry_free(registry);
}

static void
test_dispatch_parameters_and_availability(void)
{
   struct cwiki_action_registry *registry = NULL;
   struct cwiki_action_spec action;
   struct cwiki_action_argument integer = {
      CWIKI_ACTION_PARAMETER_INTEGER, {.integer = INT64_C(42)}
   };
   struct cwiki_action_argument string = {
      CWIKI_ACTION_PARAMETER_STRING, {.string = "wrong"}
   };
   struct cwiki_action_info info;
   struct availability_context state = {false, 0};
   bool available = true;
   int calls = 0;
   int result = -1;

   check(cwiki_action_registry_init(&registry) == CWIKI_ACTION_OK,
       "initialize dispatch registry");
   action = spec("cursor.goto-line", return_integer, &calls);
   action.parameter_type = CWIKI_ACTION_PARAMETER_INTEGER;
   action.parameter_name = "line";
   check(cwiki_action_register(registry, &action) == CWIKI_ACTION_OK,
       "register parameterized action");
   check(cwiki_action_lookup(registry, "cursor.goto-line", &info) ==
       CWIKI_ACTION_OK &&
       info.parameter_type == CWIKI_ACTION_PARAMETER_INTEGER &&
       strcmp(info.parameter_name, "line") == 0,
       "lookup exposes the typed parameter schema");
   check(cwiki_action_dispatch(registry, "cursor.goto-line", &integer,
       &result) == CWIKI_ACTION_OK && result == 42 && calls == 1,
       "dispatch passes a correctly typed argument and handler result");
   check(cwiki_action_dispatch(registry, "cursor.goto-line", NULL,
       &result) == CWIKI_ACTION_INVALID && calls == 1,
       "dispatch rejects a missing required argument before invocation");
   check(cwiki_action_dispatch(registry, "cursor.goto-line", &string,
       &result) == CWIKI_ACTION_INVALID && calls == 1,
       "dispatch rejects a wrong argument type before invocation");

   action = spec("note.save", available_handler, &state);
   action.available = flag_available;
   check(cwiki_action_register(registry, &action) == CWIKI_ACTION_OK,
       "register conditionally available action");
   check(cwiki_action_is_available(registry, "note.save", &available) ==
       CWIKI_ACTION_OK && !available,
       "availability can be reported without dispatch");
   check(cwiki_action_dispatch(registry, "note.save", NULL, &result) ==
       CWIKI_ACTION_DISABLED && state.calls == 0,
       "disabled action cannot dispatch");
   state.enabled = true;
   check(cwiki_action_is_available(registry, "note.save", &available) ==
       CWIKI_ACTION_OK && available,
       "availability reflects current application state");
   check(cwiki_action_dispatch(registry, "note.save", NULL, &result) ==
       CWIKI_ACTION_OK && result == 11 && state.calls == 1,
       "available action dispatches through the same path");
   check(cwiki_action_dispatch(registry, "missing.action", NULL, &result) ==
       CWIKI_ACTION_NOT_FOUND, "dispatch distinguishes an unknown action");
   cwiki_action_registry_free(registry);
}

struct mutation_context {
   struct cwiki_action_registry *registry;
   bool registered;
   int calls;
};

static int
mutation_handler(void *opaque, const struct cwiki_action_argument *argument)
{
   struct mutation_context *context = opaque;
   struct cwiki_action_spec added =
       spec("mutation.from-handler", return_integer, &context->calls);

   (void)argument;
   context->calls++;
   if (!context->registered) {
      context->registered = cwiki_action_register(context->registry, &added) ==
          CWIKI_ACTION_OK;
   }
   return 23;
}

static bool
mutation_available(void *opaque)
{
   struct mutation_context *context = opaque;
   struct cwiki_action_spec added =
       spec("aaa.from-availability", return_integer, &context->calls);

   if (!context->registered) {
      context->registered = cwiki_action_register(context->registry, &added) ==
          CWIKI_ACTION_OK;
   }
   return true;
}

static int
mutation_available_handler(void *opaque,
    const struct cwiki_action_argument *argument)
{
   struct mutation_context *context = opaque;

   (void)argument;
   context->calls++;
   return 7;
}

static void
test_mutation_safe_callbacks(void)
{
   struct cwiki_action_registry *registry = NULL;
   struct mutation_context context = {0};
   struct cwiki_action_spec action;
   int result = 0;

   check(cwiki_action_registry_init(&registry) == CWIKI_ACTION_OK,
       "initialize callback mutation registry");
   context.registry = registry;
   action = spec("mutation.handler", mutation_handler, &context);
   check(cwiki_action_register(registry, &action) == CWIKI_ACTION_OK &&
       cwiki_action_dispatch(registry, "mutation.handler", NULL, &result) ==
       CWIKI_ACTION_OK && result == 23 && context.registered &&
       cwiki_action_count(registry) == 2U,
       "handler may grow and reorder the registry during dispatch");
   cwiki_action_registry_free(registry);

   registry = NULL;
   context = (struct mutation_context){0};
   check(cwiki_action_registry_init(&registry) == CWIKI_ACTION_OK,
       "reinitialize callback mutation registry");
   context.registry = registry;
   action = spec("mutation.available", mutation_available_handler, &context);
   action.available = mutation_available;
   check(cwiki_action_register(registry, &action) == CWIKI_ACTION_OK &&
       cwiki_action_dispatch(registry, "mutation.available", NULL, &result) ==
       CWIKI_ACTION_OK && result == 7 && context.registered &&
       context.calls == 1 && cwiki_action_count(registry) == 2U,
       "availability may grow and reorder the registry before invocation");
   cwiki_action_registry_free(registry);
}

static void
test_allocation_failures_are_transactional(void)
{
   size_t failure_point;

   for (failure_point = 0U; failure_point < 5U; failure_point++) {
      struct cwiki_action_registry *registry = NULL;
      struct cwiki_action_spec action;
      int calls = 0;

      cwiki_action_test_reset_allocation();
      check(cwiki_action_registry_init(&registry) == CWIKI_ACTION_OK,
          "initialize allocation failure registry");
      action = spec("note.rename", return_integer, &calls);
      action.parameter_type = CWIKI_ACTION_PARAMETER_STRING;
      action.parameter_name = "title";
      cwiki_action_test_fail_allocation_after(failure_point);
      check(cwiki_action_register(registry, &action) ==
          CWIKI_ACTION_NO_MEMORY && cwiki_action_count(registry) == 0U,
          "allocation failure leaves registry unchanged");
      cwiki_action_test_reset_allocation();
      check(cwiki_action_register(registry, &action) == CWIKI_ACTION_OK,
          "registry remains usable after allocation failure");
      cwiki_action_registry_free(registry);
   }
   {
      struct cwiki_action_registry *registry = (void *)(uintptr_t)1U;
      cwiki_action_test_fail_allocation_after(0U);
      check(cwiki_action_registry_init(&registry) == CWIKI_ACTION_NO_MEMORY &&
          registry == NULL, "initial allocation failure has no partial output");
      cwiki_action_test_reset_allocation();
   }
}

int
main(void)
{
   test_registration_lookup_and_enumeration();
   test_registration_rejections();
   test_dispatch_parameters_and_availability();
   test_mutation_safe_callbacks();
   test_allocation_failures_are_transactional();
   if (failures != 0) {
      (void)fprintf(stderr, "%d action test(s) failed\n", failures);
      return 1;
   }
   (void)puts("action tests passed");
   return 0;
}
