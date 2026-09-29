#include "config.h"

#include "unicode.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utf8proc.h>
#include <yaml.h>

#define CONFIG_MAX_BYTES (1024U * 1024U)
#define CONFIG_MAX_ENTRIES 4096U
#define CONFIG_MAX_KEY_TOKEN 64U
#define CONFIG_MAX_TEXT 1024U

struct source_mark {
   size_t line;
   size_t column;
};

struct config_binding {
   enum cwiki_keymap_mode mode;
   struct cwiki_key_sequence sequence;
   char *action;
   struct source_mark mark;
};

struct clue_group {
   enum cwiki_keymap_mode mode;
   struct cwiki_key_sequence prefix;
   char *label;
};

struct cwiki_config {
   struct config_binding *bindings;
   size_t binding_count;
   struct clue_group *groups;
   size_t group_count;
};

static void
set_error(struct cwiki_config_error *error, size_t line, size_t column,
    const char *message)
{
   if (error == NULL) {
      return;
   }
   error->line = line;
   error->column = column;
   (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static struct source_mark
mark(const yaml_node_t *node)
{
   struct source_mark result = {node->start_mark.line + 1U,
       node->start_mark.column + 1U};

   return result;
}

static bool
scalar_is(const yaml_node_t *node, const char *value)
{
   size_t length = strlen(value);

   return node != NULL && node->type == YAML_SCALAR_NODE &&
       node->data.scalar.length == length &&
       memcmp(node->data.scalar.value, value, length) == 0;
}

static bool
null_node(const yaml_node_t *node)
{
   static const char *const values[] = {"", "~", "null", "Null", "NULL"};
   size_t i;

   if (node == NULL || node->type != YAML_SCALAR_NODE ||
       node->data.scalar.style != YAML_PLAIN_SCALAR_STYLE) {
      return false;
   }
   for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++) {
      if (scalar_is(node, values[i])) {
         return true;
      }
   }
   return false;
}

static char *
copy_scalar(const yaml_node_t *node)
{
   size_t length;
   char *copy;

   if (node == NULL || node->type != YAML_SCALAR_NODE) {
      return NULL;
   }
   length = node->data.scalar.length;
   if (length == SIZE_MAX) {
      return NULL;
   }
   copy = malloc(length + 1U);
   if (copy != NULL) {
      (void)memcpy(copy, node->data.scalar.value, length);
      copy[length] = '\0';
   }
   return copy;
}

static enum cwiki_config_status
mapping_check(yaml_document_t *document, const yaml_node_t *mapping,
    const char *const *allowed, size_t allowed_count,
    struct cwiki_config_error *error)
{
   yaml_node_pair_t *pair;

   if (mapping == NULL || mapping->type != YAML_MAPPING_NODE) {
      struct source_mark at = mapping == NULL ? (struct source_mark){1U, 1U} :
          mark(mapping);

      set_error(error, at.line, at.column, "expected a mapping");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   for (pair = mapping->data.mapping.pairs.start;
       pair < mapping->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(document, pair->key);
      yaml_node_pair_t *earlier;
      size_t i;
      bool known = false;

      if (key == NULL || key->type != YAML_SCALAR_NODE ||
          memchr(key->data.scalar.value, '\0', key->data.scalar.length) != NULL) {
         struct source_mark at = key == NULL ? mark(mapping) : mark(key);

         set_error(error, at.line, at.column, "mapping keys must be strings");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      for (earlier = mapping->data.mapping.pairs.start; earlier < pair;
          earlier++) {
         yaml_node_t *previous = yaml_document_get_node(document, earlier->key);

         if (previous != NULL && previous->type == YAML_SCALAR_NODE &&
             previous->data.scalar.length == key->data.scalar.length &&
             memcmp(previous->data.scalar.value, key->data.scalar.value,
             key->data.scalar.length) == 0) {
            struct source_mark at = mark(key);

            set_error(error, at.line, at.column, "duplicate mapping key");
            return CWIKI_CONFIG_SCHEMA_ERROR;
         }
      }
      for (i = 0U; i < allowed_count; i++) {
         if (scalar_is(key, allowed[i])) {
            known = true;
            break;
         }
      }
      if (!known) {
         struct source_mark at = mark(key);

         set_error(error, at.line, at.column, "unknown configuration key");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
   }
   return CWIKI_CONFIG_OK;
}

static yaml_node_t *
mapping_get(yaml_document_t *document, const yaml_node_t *mapping,
    const char *name)
{
   yaml_node_pair_t *pair;

   if (document == NULL || mapping == NULL ||
       mapping->type != YAML_MAPPING_NODE) {
      return NULL;
   }
   for (pair = mapping->data.mapping.pairs.start;
       pair < mapping->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(document, pair->key);

      if (scalar_is(key, name)) {
         return yaml_document_get_node(document, pair->value);
      }
   }
   return NULL;
}

static bool
parse_mode(const yaml_node_t *node, enum cwiki_keymap_mode *mode)
{
   static const char *const names[] = {"normal", "insert", "replace", "command"};
   size_t i;

   for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
      if (scalar_is(node, names[i])) {
         *mode = (enum cwiki_keymap_mode)i;
         return true;
      }
   }
   return false;
}

static bool
prefix(const unsigned char *bytes, size_t length, const char *value)
{
   size_t wanted = strlen(value);

   return length >= wanted && memcmp(bytes, value, wanted) == 0;
}

static bool
hex_key(const unsigned char *bytes, size_t length, uint32_t *key)
{
   uint32_t value = 0U;
   size_t i;

   if (length < 3U || !prefix(bytes, length, "U+")) {
      return false;
   }
   for (i = 2U; i < length; i++) {
      unsigned int digit;

      if (bytes[i] >= '0' && bytes[i] <= '9') {
         digit = bytes[i] - '0';
      } else if (bytes[i] >= 'A' && bytes[i] <= 'F') {
         digit = bytes[i] - 'A' + 10U;
      } else if (bytes[i] >= 'a' && bytes[i] <= 'f') {
         digit = bytes[i] - 'a' + 10U;
      } else {
         return false;
      }
      if (value > (UINT32_MAX - digit) / 16U) {
         return false;
      }
      value = value * 16U + digit;
   }
   if (value == 0U || value > 0x10ffffU ||
       (value >= 0xd800U && value <= 0xdfffU)) {
      return false;
   }
   *key = value;
   return true;
}

static bool
key_token(const yaml_node_t *node, struct cwiki_key *key)
{
   static const struct {
      const char *prefix;
      unsigned int modifier;
   } modifiers[] = {
      {"C-", CWIKI_INPUT_CTRL}, {"A-", CWIKI_INPUT_ALT},
      {"S-", CWIKI_INPUT_SHIFT}, {"Super-", CWIKI_INPUT_SUPER},
      {"Hyper-", CWIKI_INPUT_HYPER}, {"Meta-", CWIKI_INPUT_META},
      {"Caps-", CWIKI_INPUT_CAPS_LOCK}, {"Num-", CWIKI_INPUT_NUM_LOCK}
   };
   static const struct {
      const char *name;
      uint32_t key;
   } special[] = {
      {"Backspace", 8U}, {"Tab", 9U}, {"Enter", 13U}, {"Esc", 27U},
      {"Space", 32U}, {"Delete", 127U}
   };
   const unsigned char *bytes;
   size_t length;
   bool consumed;

   if (node == NULL || node->type != YAML_SCALAR_NODE ||
       node->data.scalar.length == 0U ||
       node->data.scalar.length > CONFIG_MAX_KEY_TOKEN) {
      return false;
   }
   bytes = node->data.scalar.value;
   length = node->data.scalar.length;
   *key = (struct cwiki_key){0};
   do {
      size_t i;

      consumed = false;
      for (i = 0U; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
         size_t part = strlen(modifiers[i].prefix);

         if (prefix(bytes, length, modifiers[i].prefix)) {
            if ((key->modifiers & modifiers[i].modifier) != 0U) {
               return false;
            }
            key->modifiers |= modifiers[i].modifier;
            bytes += part;
            length -= part;
            consumed = true;
            break;
         }
      }
   } while (consumed);
   {
      size_t i;

      for (i = 0U; i < sizeof(special) / sizeof(special[0]); i++) {
         size_t name_length = strlen(special[i].name);

         if (length == name_length &&
             memcmp(bytes, special[i].name, length) == 0) {
            key->key = special[i].key;
            return true;
         }
      }
   }
   if (hex_key(bytes, length, &key->key)) {
      return true;
   }
   {
      utf8proc_int32_t codepoint;
      utf8proc_ssize_t used = utf8proc_iterate(bytes, (utf8proc_ssize_t)length,
          &codepoint);

      if (used <= 0 || (size_t)used != length || codepoint <= 0) {
         return false;
      }
      key->key = (uint32_t)codepoint;
      return true;
   }
}

static enum cwiki_config_status
key_sequence(yaml_document_t *document, const yaml_node_t *node,
    struct cwiki_key_sequence *sequence, struct cwiki_config_error *error)
{
   yaml_node_item_t *item;

   *sequence = (struct cwiki_key_sequence){0};
   if (node == NULL || node->type != YAML_SEQUENCE_NODE ||
       node->data.sequence.items.start == node->data.sequence.items.top) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column, "keys must be a non-empty sequence");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   for (item = node->data.sequence.items.start;
       item < node->data.sequence.items.top; item++) {
      yaml_node_t *token = yaml_document_get_node(document, *item);

      if (sequence->length == CWIKI_KEYMAP_MAX_SEQUENCE ||
          !key_token(token, &sequence->keys[sequence->length])) {
         struct source_mark at = token == NULL ? mark(node) : mark(token);

         set_error(error, at.line, at.column, "invalid physical key token");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      sequence->length++;
   }
   return CWIKI_CONFIG_OK;
}

static bool
same_sequence(const struct cwiki_key_sequence *left,
    const struct cwiki_key_sequence *right)
{
   return left->length == right->length && memcmp(left->keys, right->keys,
       left->length * sizeof(left->keys[0])) == 0;
}

static enum cwiki_config_status
add_binding(struct cwiki_config *config, enum cwiki_keymap_mode mode,
    const struct cwiki_key_sequence *sequence, char *action,
    struct source_mark at, struct cwiki_config_error *error)
{
   struct config_binding *grown;
   size_t i;

   for (i = 0U; i < config->binding_count; i++) {
      if (config->bindings[i].mode == mode &&
          same_sequence(&config->bindings[i].sequence, sequence)) {
         free(action);
         set_error(error, at.line, at.column, "duplicate key binding");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
   }
   if (config->binding_count == CONFIG_MAX_ENTRIES) {
      free(action);
      set_error(error, at.line, at.column, "too many key bindings");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   if (config->binding_count == SIZE_MAX / sizeof(*config->bindings)) {
      free(action);
      return CWIKI_CONFIG_NO_MEMORY;
   }
   grown = realloc(config->bindings,
       (config->binding_count + 1U) * sizeof(*config->bindings));
   if (grown == NULL) {
      free(action);
      return CWIKI_CONFIG_NO_MEMORY;
   }
   config->bindings = grown;
   config->bindings[config->binding_count++] =
       (struct config_binding){mode, *sequence, action, at};
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_binding(yaml_document_t *document, struct cwiki_config *config,
    enum cwiki_keymap_mode mode, const yaml_node_t *node,
    const struct cwiki_action_registry *actions,
    struct cwiki_config_error *error)
{
   static const char *const allowed[] = {"keys", "action"};
   struct cwiki_key_sequence sequence;
   yaml_node_t *keys;
   yaml_node_t *action_node;
   char *action = NULL;
   struct cwiki_action_info info;
   enum cwiki_config_status status;
   struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
       mark(node);

   status = mapping_check(document, node, allowed,
       sizeof(allowed) / sizeof(allowed[0]), error);
   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   keys = mapping_get(document, node, "keys");
   action_node = mapping_get(document, node, "action");
   if (keys == NULL || action_node == NULL) {
      set_error(error, at.line, at.column, "binding requires keys and action");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   status = key_sequence(document, keys, &sequence, error);
   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   if (!null_node(action_node)) {
      if (action_node->type != YAML_SCALAR_NODE ||
          action_node->data.scalar.length > CONFIG_MAX_TEXT ||
          memchr(action_node->data.scalar.value, '\0',
          action_node->data.scalar.length) != NULL ||
          !cwiki_utf8_validate((const char *)action_node->data.scalar.value,
          action_node->data.scalar.length)) {
         at = mark(action_node);
         set_error(error, at.line, at.column, "action must be a name or null");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      action = copy_scalar(action_node);
      if (action == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
      if (cwiki_action_lookup(actions, action, &info) != CWIKI_ACTION_OK) {
         at = mark(action_node);
         free(action);
         set_error(error, at.line, at.column, "unknown action name");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
   }
   return add_binding(config, mode, &sequence, action, at, error);
}

static enum cwiki_config_status
parse_keymaps(yaml_document_t *document, struct cwiki_config *config,
    const yaml_node_t *node, const struct cwiki_action_registry *actions,
    struct cwiki_config_error *error)
{
   static const char *const modes[] = {"normal", "insert", "replace", "command"};
   yaml_node_pair_t *pair;
   enum cwiki_config_status status = mapping_check(document, node, modes,
       sizeof(modes) / sizeof(modes[0]), error);

   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   for (pair = node->data.mapping.pairs.start;
       pair < node->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(document, pair->key);
      yaml_node_t *list = yaml_document_get_node(document, pair->value);
      enum cwiki_keymap_mode mode;
      yaml_node_item_t *item;

      if (!parse_mode(key, &mode) || list == NULL ||
          list->type != YAML_SEQUENCE_NODE) {
         struct source_mark at = list == NULL ? mark(key) : mark(list);

         set_error(error, at.line, at.column,
             "keymap mode must contain a binding sequence");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      for (item = list->data.sequence.items.start;
          item < list->data.sequence.items.top; item++) {
         status = parse_binding(document, config, mode,
             yaml_document_get_node(document, *item), actions, error);
         if (status != CWIKI_CONFIG_OK) {
            return status;
         }
      }
   }
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
add_group(struct cwiki_config *config, enum cwiki_keymap_mode mode,
    const struct cwiki_key_sequence *prefix_value, char *label,
    struct source_mark at, struct cwiki_config_error *error)
{
   struct clue_group *grown;
   size_t i;

   for (i = 0U; i < config->group_count; i++) {
      if (config->groups[i].mode == mode &&
          same_sequence(&config->groups[i].prefix, prefix_value)) {
         free(label);
         set_error(error, at.line, at.column, "duplicate clue group");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
   }
   if (config->group_count == CONFIG_MAX_ENTRIES) {
      free(label);
      set_error(error, at.line, at.column, "too many clue groups");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   if (config->group_count == SIZE_MAX / sizeof(*config->groups)) {
      free(label);
      return CWIKI_CONFIG_NO_MEMORY;
   }
   grown = realloc(config->groups,
       (config->group_count + 1U) * sizeof(*config->groups));
   if (grown == NULL) {
      free(label);
      return CWIKI_CONFIG_NO_MEMORY;
   }
   config->groups = grown;
   config->groups[config->group_count++] =
       (struct clue_group){mode, *prefix_value, label};
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_group(yaml_document_t *document, struct cwiki_config *config,
    enum cwiki_keymap_mode mode, const yaml_node_t *node,
    struct cwiki_config_error *error)
{
   static const char *const allowed[] = {"prefix", "label"};
   yaml_node_t *prefix_node;
   yaml_node_t *label_node;
   struct cwiki_key_sequence prefix_value;
   struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
       mark(node);
   enum cwiki_config_status status = mapping_check(document, node, allowed,
       sizeof(allowed) / sizeof(allowed[0]), error);
   char *label;

   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   prefix_node = mapping_get(document, node, "prefix");
   label_node = mapping_get(document, node, "label");
   if (prefix_node == NULL || label_node == NULL) {
      set_error(error, at.line, at.column, "clue group requires prefix and label");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   status = key_sequence(document, prefix_node, &prefix_value, error);
   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   if (label_node->type != YAML_SCALAR_NODE ||
       label_node->data.scalar.length > CONFIG_MAX_TEXT ||
       memchr(label_node->data.scalar.value, '\0',
       label_node->data.scalar.length) != NULL ||
       !cwiki_utf8_validate((const char *)label_node->data.scalar.value,
       label_node->data.scalar.length) || label_node->data.scalar.length == 0U) {
      at = mark(label_node);
      set_error(error, at.line, at.column,
          "clue label must be non-empty UTF-8");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   label = copy_scalar(label_node);
   if (label == NULL) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   return add_group(config, mode, &prefix_value, label, at, error);
}

static enum cwiki_config_status
parse_groups(yaml_document_t *document, struct cwiki_config *config,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   static const char *const modes[] = {"normal", "insert", "replace", "command"};
   yaml_node_pair_t *pair;
   enum cwiki_config_status status = mapping_check(document, node, modes,
       sizeof(modes) / sizeof(modes[0]), error);

   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   for (pair = node->data.mapping.pairs.start;
       pair < node->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(document, pair->key);
      yaml_node_t *list = yaml_document_get_node(document, pair->value);
      enum cwiki_keymap_mode mode;
      yaml_node_item_t *item;

      if (!parse_mode(key, &mode) || list == NULL ||
          list->type != YAML_SEQUENCE_NODE) {
         struct source_mark at = list == NULL ? mark(key) : mark(list);

         set_error(error, at.line, at.column,
             "clue mode must contain a group sequence");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      for (item = list->data.sequence.items.start;
          item < list->data.sequence.items.top; item++) {
         status = parse_group(document, config, mode,
             yaml_document_get_node(document, *item), error);
         if (status != CWIKI_CONFIG_OK) {
            return status;
         }
      }
   }
   return CWIKI_CONFIG_OK;
}

void
cwiki_config_free(struct cwiki_config *config)
{
   size_t i;

   if (config == NULL) {
      return;
   }
   for (i = 0U; i < config->binding_count; i++) {
      free(config->bindings[i].action);
   }
   for (i = 0U; i < config->group_count; i++) {
      free(config->groups[i].label);
   }
   free(config->bindings);
   free(config->groups);
   free(config);
}

enum cwiki_config_status
cwiki_config_parse(struct cwiki_config **config, const unsigned char *bytes,
    size_t length, const struct cwiki_action_registry *actions,
    struct cwiki_config_error *error)
{
   static const char *const allowed[] = {"keymaps", "clue-groups"};
   static const unsigned char empty[] = "";
   yaml_parser_t parser;
   yaml_document_t document;
   yaml_document_t trailing;
   struct cwiki_config *created = NULL;
   yaml_node_t *root;
   yaml_node_t *keymaps;
   yaml_node_t *groups;
   enum cwiki_config_status status;
   bool parser_ready = false;
   bool document_ready = false;

   if (config == NULL || actions == NULL || (bytes == NULL && length != 0U)) {
      return CWIKI_CONFIG_INVALID;
   }
   *config = NULL;
   if (error != NULL) {
      *error = (struct cwiki_config_error){0};
   }
   if (length > CONFIG_MAX_BYTES) {
      set_error(error, 1U, 1U, "configuration exceeds the 1 MiB limit");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   if (!yaml_parser_initialize(&parser)) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   parser_ready = true;
   yaml_parser_set_input_string(&parser, bytes == NULL ? empty : bytes, length);
   if (!yaml_parser_load(&parser, &document)) {
      set_error(error, parser.problem_mark.line + 1U,
          parser.problem_mark.column + 1U,
          parser.problem == NULL ? "invalid YAML" : parser.problem);
      status = parser.error == YAML_MEMORY_ERROR ? CWIKI_CONFIG_NO_MEMORY :
          CWIKI_CONFIG_YAML_ERROR;
      goto done;
   }
   document_ready = true;
   root = yaml_document_get_root_node(&document);
   if (root == NULL) {
      set_error(error, 1U, 1U, "configuration document is empty");
      status = CWIKI_CONFIG_SCHEMA_ERROR;
      goto done;
   }
   status = mapping_check(&document, root, allowed,
       sizeof(allowed) / sizeof(allowed[0]), error);
   if (status != CWIKI_CONFIG_OK) {
      goto done;
   }
   created = calloc(1U, sizeof(*created));
   if (created == NULL) {
      status = CWIKI_CONFIG_NO_MEMORY;
      goto done;
   }
   keymaps = mapping_get(&document, root, "keymaps");
   groups = mapping_get(&document, root, "clue-groups");
   if (keymaps != NULL) {
      status = parse_keymaps(&document, created, keymaps, actions, error);
      if (status != CWIKI_CONFIG_OK) {
         goto done;
      }
   }
   if (groups != NULL) {
      status = parse_groups(&document, created, groups, error);
      if (status != CWIKI_CONFIG_OK) {
         goto done;
      }
   }
   if (!yaml_parser_load(&parser, &trailing)) {
      set_error(error, parser.problem_mark.line + 1U,
          parser.problem_mark.column + 1U,
          parser.problem == NULL ? "invalid YAML" : parser.problem);
      status = parser.error == YAML_MEMORY_ERROR ? CWIKI_CONFIG_NO_MEMORY :
          CWIKI_CONFIG_YAML_ERROR;
      goto done;
   }
   if (yaml_document_get_root_node(&trailing) != NULL) {
      yaml_node_t *extra = yaml_document_get_root_node(&trailing);
      struct source_mark at = mark(extra);

      set_error(error, at.line, at.column,
          "configuration must contain one YAML document");
      yaml_document_delete(&trailing);
      status = CWIKI_CONFIG_SCHEMA_ERROR;
      goto done;
   }
   yaml_document_delete(&trailing);
   *config = created;
   created = NULL;
   status = CWIKI_CONFIG_OK;
done:
   cwiki_config_free(created);
   if (document_ready) {
      yaml_document_delete(&document);
   }
   if (parser_ready) {
      yaml_parser_delete(&parser);
   }
   return status;
}

static void
binding_error(struct cwiki_config_error *error,
    const struct config_binding *binding, const char *message)
{
   set_error(error, binding->mark.line, binding->mark.column, message);
}

enum cwiki_config_status
cwiki_config_build_keymap(const struct cwiki_config *config,
    const struct cwiki_keymap *base, struct cwiki_keymap **candidate,
    struct cwiki_config_error *error)
{
   struct cwiki_keymap *created = NULL;
   size_t i;

   if (config == NULL || base == NULL || candidate == NULL) {
      return CWIKI_CONFIG_INVALID;
   }
   *candidate = NULL;
   if (error != NULL) {
      *error = (struct cwiki_config_error){0};
   }
   if (cwiki_keymap_clone(&created, base) != CWIKI_KEYMAP_OK) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   for (i = 0U; i < config->binding_count; i++) {
      const struct config_binding *binding = &config->bindings[i];
      struct cwiki_input_event events[CWIKI_KEYMAP_MAX_SEQUENCE] = {{0}};
      struct cwiki_keymap_match match_value;
      enum cwiki_keymap_status keymap_status;
      size_t j;

      for (j = 0U; j < binding->sequence.length; j++) {
         events[j].kind = CWIKI_INPUT_KEY;
         events[j].key = binding->sequence.keys[j].key;
         events[j].modifiers = binding->sequence.keys[j].modifiers;
         events[j].action = CWIKI_INPUT_PRESS;
      }
      keymap_status = cwiki_keymap_match(created, binding->mode, events,
          binding->sequence.length, &match_value);
      if (keymap_status != CWIKI_KEYMAP_OK) {
         binding_error(error, binding, "invalid key binding");
         cwiki_keymap_free(created);
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      if (binding->action == NULL) {
         keymap_status = cwiki_keymap_unbind(created, binding->mode,
             &binding->sequence);
         if (keymap_status != CWIKI_KEYMAP_OK) {
            binding_error(error, binding, "cannot unbind a missing key sequence");
         }
      } else if (match_value.kind == CWIKI_KEYMAP_COMPLETE) {
         keymap_status = cwiki_keymap_rebind(created, binding->mode,
             &binding->sequence, binding->action);
      } else {
         keymap_status = cwiki_keymap_bind(created, binding->mode,
             &binding->sequence, binding->action);
      }
      if (keymap_status != CWIKI_KEYMAP_OK) {
         cwiki_keymap_free(created);
         return keymap_status == CWIKI_KEYMAP_NO_MEMORY ?
             CWIKI_CONFIG_NO_MEMORY : CWIKI_CONFIG_SCHEMA_ERROR;
      }
   }
   *candidate = created;
   return CWIKI_CONFIG_OK;
}

const char *
cwiki_config_clue_group(const struct cwiki_config *config,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix_value,
    size_t prefix_count)
{
   size_t i;

   if (config == NULL || (prefix_value == NULL && prefix_count != 0U) ||
       prefix_count == 0U || prefix_count > CWIKI_KEYMAP_MAX_SEQUENCE) {
      return NULL;
   }
   for (i = 0U; i < config->group_count; i++) {
      const struct clue_group *group = &config->groups[i];
      size_t j;

      if (group->mode != mode || group->prefix.length != prefix_count) {
         continue;
      }
      for (j = 0U; j < prefix_count; j++) {
         uint32_t identity = prefix_value[j].has_base_layout_key ?
             prefix_value[j].base_layout_key : prefix_value[j].key;

         if (prefix_value[j].kind != CWIKI_INPUT_KEY ||
             prefix_value[j].action == CWIKI_INPUT_RELEASE ||
             identity != group->prefix.keys[j].key ||
             prefix_value[j].modifiers != group->prefix.keys[j].modifiers) {
            break;
         }
      }
      if (j == prefix_count) {
         return group->label;
      }
   }
   return NULL;
}
