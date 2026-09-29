#include "config.h"

#include "snippet_catalog.h"
#include "unicode.h"

#include <errno.h>
#include <limits.h>
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
#define CONFIG_MAX_SUBJECTS 64U

static const char *const builtin_zone_ids[] = {
   "code-fence", "note-block-comment", "note-inline-comment", "html-comment",
   "display-math-dollar", "display-math-bracket", "inline-math-dollar",
   "inline-math-paren", "tikz-environment", "latex-environment", "chemistry",
   "math-text", "math-intertext", "reference", "latex-line-comment"
};

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

struct config_snippet {
   char *name;
   struct cwiki_snippet_spec spec;
   struct source_mark mark;
   bool disabled;
};

struct config_names {
   char **values;
   size_t count;
};

struct config_zone {
   char *id;
   struct cwiki_zone_region region;
   struct config_names parents;
   struct config_names contains;
   struct source_mark mark;
   bool top_level;
   bool disabled;
};

struct cwiki_config {
   struct config_binding *bindings;
   size_t binding_count;
   struct clue_group *groups;
   size_t group_count;
   struct config_snippet *snippets;
   size_t snippet_count;
   struct config_zone *zones;
   size_t zone_count;
   char *continuation_marker;
   uint32_t conceal_categories;
   uint32_t concealcursor_modes;
   enum cwiki_save_policy save_policy;
   bool conceal;
   bool wrap;
   bool break_indent;
   bool has_conceal;
   bool has_conceal_categories;
   bool has_concealcursor_modes;
   bool has_wrap;
   bool has_break_indent;
   bool has_continuation_marker;
   bool has_save_policy;
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
parse_boolean(const yaml_node_t *node, bool *value,
    struct cwiki_config_error *error)
{
   if (node != NULL && node->type == YAML_SCALAR_NODE &&
       node->data.scalar.style == YAML_PLAIN_SCALAR_STYLE &&
       scalar_is(node, "true")) {
      *value = true;
      return CWIKI_CONFIG_OK;
   }
   if (node != NULL && node->type == YAML_SCALAR_NODE &&
       node->data.scalar.style == YAML_PLAIN_SCALAR_STYLE &&
       scalar_is(node, "false")) {
      *value = false;
      return CWIKI_CONFIG_OK;
   }
   {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column, "expected true or false");
   }
   return CWIKI_CONFIG_SCHEMA_ERROR;
}

static bool
conceal_category(const yaml_node_t *node, enum cwiki_conceal_category *category)
{
   static const char *const names[CWIKI_CONCEAL_CATEGORY_COUNT] = {
      "accents", "greek", "math-symbols", "ligatures", "fractions",
      "math-bounds", "size-modified-delimiters", "sub-superscripts",
      "styles", "environments", "item-markers", "citations", "spacing",
      "sections"
   };
   size_t i;

   if (node == NULL || node->type != YAML_SCALAR_NODE) {
      return false;
   }
   for (i = 0U; i < CWIKI_CONCEAL_CATEGORY_COUNT; i++) {
      if (scalar_is(node, names[i])) {
         *category = (enum cwiki_conceal_category)i;
         return true;
      }
   }
   return false;
}

static enum cwiki_config_status
parse_conceal_categories(yaml_document_t *document, struct cwiki_config *config,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   yaml_node_item_t *item;
   uint32_t mask = 0U;

   if (node == NULL || node->type != YAML_SEQUENCE_NODE) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column,
          "conceal-categories must be a sequence");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   for (item = node->data.sequence.items.start;
       item < node->data.sequence.items.top; item++) {
      yaml_node_t *value = yaml_document_get_node(document, *item);
      enum cwiki_conceal_category category;
      uint32_t bit;

      if (!conceal_category(value, &category)) {
         struct source_mark at = value == NULL ? mark(node) : mark(value);

         set_error(error, at.line, at.column, "unknown conceal category");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      bit = CWIKI_CONCEAL_CATEGORY_BIT(category);
      if ((mask & bit) != 0U) {
         struct source_mark at = mark(value);

         set_error(error, at.line, at.column, "duplicate conceal category");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      mask |= bit;
   }
   config->conceal_categories = mask;
   config->has_conceal_categories = true;
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_concealcursor(struct cwiki_config *config, const yaml_node_t *node,
    struct cwiki_config_error *error)
{
   uint32_t modes = 0U;
   size_t i;

   if (node == NULL || node->type != YAML_SCALAR_NODE ||
       memchr(node->data.scalar.value, '\0', node->data.scalar.length) != NULL) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column,
          "conceal-cursor must contain only n, i, v and c");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   for (i = 0U; i < node->data.scalar.length; i++) {
      enum cwiki_conceal_mode mode;
      uint32_t bit;

      switch (node->data.scalar.value[i]) {
      case 'n': mode = CWIKI_CONCEAL_MODE_NORMAL; break;
      case 'i': mode = CWIKI_CONCEAL_MODE_INSERT; break;
      case 'v': mode = CWIKI_CONCEAL_MODE_VISUAL; break;
      case 'c': mode = CWIKI_CONCEAL_MODE_COMMAND; break;
      default:
         {
            struct source_mark at = mark(node);

            set_error(error, at.line, at.column,
                "conceal-cursor must contain only n, i, v and c");
         }
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      bit = CWIKI_CONCEAL_MODE_BIT(mode);
      if ((modes & bit) != 0U) {
         struct source_mark at = mark(node);

         set_error(error, at.line, at.column,
             "conceal-cursor contains a duplicate mode");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      modes |= bit;
   }
   config->concealcursor_modes = modes;
   config->has_concealcursor_modes = true;
   return CWIKI_CONFIG_OK;
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

static enum cwiki_config_status
parse_marker(struct cwiki_config *config, const yaml_node_t *node,
    struct cwiki_config_error *error)
{
   size_t offset = 0U;
   size_t i;
   char *value;

   if (node == NULL || node->type != YAML_SCALAR_NODE ||
       node->data.scalar.length > CONFIG_MAX_TEXT ||
       memchr(node->data.scalar.value, '\0', node->data.scalar.length) != NULL ||
       !cwiki_utf8_validate((const char *)node->data.scalar.value,
       node->data.scalar.length)) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column,
          "continuation-marker must be valid UTF-8 display text");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   for (i = 0U; i < node->data.scalar.length; i++) {
      if (node->data.scalar.value[i] < 0x20U ||
          node->data.scalar.value[i] == 0x7fU) {
         struct source_mark at = mark(node);

         set_error(error, at.line, at.column,
             "continuation-marker must be valid UTF-8 display text");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
   }
   while (offset < node->data.scalar.length) {
      size_t next = cwiki_grapheme_next((const char *)node->data.scalar.value,
          node->data.scalar.length, offset);

      if (next == SIZE_MAX || cwiki_grapheme_width(
          (const char *)node->data.scalar.value + offset, next - offset) < 0) {
         struct source_mark at = mark(node);

         set_error(error, at.line, at.column,
             "continuation-marker must be valid UTF-8 display text");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      offset = next;
   }
   value = copy_scalar(node);
   if (value == NULL) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   free(config->continuation_marker);
   config->continuation_marker = value;
   config->has_continuation_marker = true;
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_display(yaml_document_t *document, struct cwiki_config *config,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   static const char *const allowed[] = {"conceal", "conceal-categories",
       "conceal-cursor", "wrap", "break-indent", "continuation-marker"};
   yaml_node_t *value;
   enum cwiki_config_status status;

   status = mapping_check(document, node, allowed,
       sizeof(allowed) / sizeof(allowed[0]), error);
   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "conceal");
   if (value != NULL) {
      status = parse_boolean(value, &config->conceal, error);
      if (status != CWIKI_CONFIG_OK) {
         return status;
      }
      config->has_conceal = true;
   }
   value = mapping_get(document, node, "conceal-categories");
   if (value != NULL && (status = parse_conceal_categories(document, config,
       value, error)) != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "conceal-cursor");
   if (value != NULL && (status = parse_concealcursor(config, value, error)) !=
       CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "wrap");
   if (value != NULL) {
      status = parse_boolean(value, &config->wrap, error);
      if (status != CWIKI_CONFIG_OK) {
         return status;
      }
      config->has_wrap = true;
   }
   value = mapping_get(document, node, "break-indent");
   if (value != NULL) {
      status = parse_boolean(value, &config->break_indent, error);
      if (status != CWIKI_CONFIG_OK) {
         return status;
      }
      config->has_break_indent = true;
   }
   value = mapping_get(document, node, "continuation-marker");
   if (value != NULL && (status = parse_marker(config, value, error)) !=
       CWIKI_CONFIG_OK) {
      return status;
   }
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_save_policy(struct cwiki_config *config, const yaml_node_t *node,
    struct cwiki_config_error *error)
{
   if (scalar_is(node, "insert-leave")) {
      config->save_policy = CWIKI_SAVE_INSERT_LEAVE;
   } else if (scalar_is(node, "manual")) {
      config->save_policy = CWIKI_SAVE_MANUAL;
   } else if (scalar_is(node, "idle")) {
      config->save_policy = CWIKI_SAVE_IDLE;
   } else {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column,
          "save-policy must be insert-leave, manual or idle");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   config->has_save_policy = true;
   return CWIKI_CONFIG_OK;
}

static bool
snippet_name_valid(const yaml_node_t *node)
{
   size_t i;
   bool segment = false;

   if (node == NULL || node->type != YAML_SCALAR_NODE ||
       node->data.scalar.length == 0U ||
       node->data.scalar.length > CONFIG_MAX_TEXT) {
      return false;
   }
   for (i = 0U; i < node->data.scalar.length; i++) {
      unsigned char value = node->data.scalar.value[i];

      if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
          value == '-') {
         segment = true;
      } else if (value == '.' && segment &&
          i + 1U < node->data.scalar.length) {
         segment = false;
      } else {
         return false;
      }
   }
   return segment;
}

static bool
parse_zone(const yaml_node_t *node, enum cwiki_zone_kind *zone)
{
   static const char *const names[] = {"prose", "code", "math-inline",
       "math-display", "chemistry", "text", "reference", "latex", "tikz",
       "comment-percent", "comment-note", "comment-html", "custom"};
   size_t i;

   for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
      if (scalar_is(node, names[i])) {
         *zone = (enum cwiki_zone_kind)i;
         return true;
      }
   }
   return false;
}

static enum cwiki_config_status
parse_snippet_bodies(yaml_document_t *document, struct config_snippet *snippet,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   yaml_node_pair_t *pair;
   size_t count;
   size_t used = 0U;

   if (node == NULL || node->type != YAML_MAPPING_NODE ||
       node->data.mapping.pairs.start == node->data.mapping.pairs.top) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column,
          "snippet bodies must be a non-empty zone mapping");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   count = (size_t)(node->data.mapping.pairs.top -
       node->data.mapping.pairs.start);
   if (count > CWIKI_ZONE_CUSTOM + 1U) {
      struct source_mark at = mark(node);

      set_error(error, at.line, at.column, "too many snippet bodies");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   snippet->spec.bodies = calloc(count, sizeof(*snippet->spec.bodies));
   if (snippet->spec.bodies == NULL) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   for (pair = node->data.mapping.pairs.start;
       pair < node->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(document, pair->key);
      yaml_node_t *value = yaml_document_get_node(document, pair->value);
      struct cwiki_snippet_body_spec *body =
          (struct cwiki_snippet_body_spec *)&snippet->spec.bodies[used];
      enum cwiki_zone_kind zone;
      size_t i;

      if (!parse_zone(key, &zone)) {
         struct source_mark at = key == NULL ? mark(node) : mark(key);

         set_error(error, at.line, at.column, "unknown snippet body zone");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      for (i = 0U; i < used; i++) {
         if (snippet->spec.bodies[i].zone == zone) {
            struct source_mark at = mark(key);

            set_error(error, at.line, at.column, "duplicate snippet body zone");
            return CWIKI_CONFIG_SCHEMA_ERROR;
         }
      }
      if (value == NULL || value->type != YAML_SCALAR_NODE ||
          value->data.scalar.length > CONFIG_MAX_TEXT ||
          memchr(value->data.scalar.value, '\0',
          value->data.scalar.length) != NULL ||
          !cwiki_utf8_validate((const char *)value->data.scalar.value,
          value->data.scalar.length)) {
         struct source_mark at = value == NULL ? mark(node) : mark(value);

         set_error(error, at.line, at.column,
             "snippet body must be valid UTF-8 text");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      body->body = copy_scalar(value);
      if (body->body == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
      body->zone = zone;
      body->body_length = value->data.scalar.length;
      used++;
      snippet->spec.body_count = used;
   }
   snippet->spec.required_zone = snippet->spec.bodies[0].zone;
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_expand(yaml_document_t *document, struct config_snippet *snippet,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   yaml_node_item_t *item;
   uint32_t flags = 0U;

   if (node == NULL || node->type != YAML_SEQUENCE_NODE ||
       node->data.sequence.items.start == node->data.sequence.items.top) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column,
          "snippet expand must be a non-empty sequence");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   for (item = node->data.sequence.items.start;
       item < node->data.sequence.items.top; item++) {
      yaml_node_t *value = yaml_document_get_node(document, *item);
      uint32_t flag;

      if (scalar_is(value, "auto")) {
         flag = CWIKI_SNIPPET_TRIGGER_AUTO;
      } else if (scalar_is(value, "explicit")) {
         flag = CWIKI_SNIPPET_TRIGGER_EXPLICIT;
      } else {
         struct source_mark at = value == NULL ? mark(node) : mark(value);

         set_error(error, at.line, at.column,
             "snippet expand accepts auto and explicit");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      if ((flags & flag) != 0U) {
         struct source_mark at = mark(value);

         set_error(error, at.line, at.column, "duplicate snippet expand mode");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      flags |= flag;
   }
   snippet->spec.flags |= flags;
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_priority(const yaml_node_t *node, int *priority,
    struct cwiki_config_error *error)
{
   char *text;
   char *end;
   long value;

   if (node == NULL || node->type != YAML_SCALAR_NODE ||
       node->data.scalar.style != YAML_PLAIN_SCALAR_STYLE ||
       node->data.scalar.length == 0U || node->data.scalar.length > 32U) {
      goto invalid;
   }
   text = copy_scalar(node);
   if (text == NULL) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   errno = 0;
   value = strtol(text, &end, 10);
   if (errno != 0 || *end != '\0' || value < INT_MIN || value > INT_MAX) {
      free(text);
      goto invalid;
   }
   free(text);
   *priority = (int)value;
   return CWIKI_CONFIG_OK;
invalid:
   {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column,
          "snippet priority must be an integer");
   }
   return CWIKI_CONFIG_SCHEMA_ERROR;
}

static enum cwiki_config_status
parse_snippet_definition(yaml_document_t *document,
    struct config_snippet *snippet, const yaml_node_t *node,
    struct cwiki_config_error *error)
{
   static const char *const allowed[] = {"trigger", "kind", "expand",
       "word-boundary", "beginning-of-line", "priority", "subject", "bodies"};
   yaml_node_t *value;
   enum cwiki_config_status status;
   struct cwiki_snippet_registry *validation = NULL;
   enum cwiki_snippet_status snippet_status;
   bool enabled;

   status = mapping_check(document, node, allowed,
       sizeof(allowed) / sizeof(allowed[0]), error);
   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "trigger");
   if (value == NULL || value->type != YAML_SCALAR_NODE ||
       value->data.scalar.length == 0U ||
       value->data.scalar.length > CONFIG_MAX_TEXT ||
       memchr(value->data.scalar.value, '\0', value->data.scalar.length) != NULL ||
       !cwiki_utf8_validate((const char *)value->data.scalar.value,
       value->data.scalar.length)) {
      struct source_mark at = value == NULL ? mark(node) : mark(value);

      set_error(error, at.line, at.column,
          "snippet trigger must be non-empty UTF-8 text");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   snippet->spec.trigger = copy_scalar(value);
   if (snippet->spec.trigger == NULL) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   snippet->spec.trigger_length = value->data.scalar.length;
   snippet->spec.kind = CWIKI_SNIPPET_LITERAL;
   value = mapping_get(document, node, "kind");
   if (value != NULL) {
      if (scalar_is(value, "literal")) {
         snippet->spec.kind = CWIKI_SNIPPET_LITERAL;
      } else if (scalar_is(value, "regex")) {
         snippet->spec.kind = CWIKI_SNIPPET_REGEX;
      } else {
         struct source_mark at = mark(value);

         set_error(error, at.line, at.column,
             "snippet kind must be literal or regex");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
   }
   snippet->spec.flags = CWIKI_SNIPPET_TRIGGER_EXPLICIT;
   value = mapping_get(document, node, "expand");
   if (value != NULL) {
      snippet->spec.flags = 0U;
      status = parse_expand(document, snippet, value, error);
      if (status != CWIKI_CONFIG_OK) {
         return status;
      }
   }
   value = mapping_get(document, node, "word-boundary");
   if (value != NULL) {
      status = parse_boolean(value, &enabled, error);
      if (status != CWIKI_CONFIG_OK) {
         return status;
      }
      if (enabled) {
         snippet->spec.flags |= CWIKI_SNIPPET_TRIGGER_WORD_BOUNDARY;
      }
   }
   value = mapping_get(document, node, "beginning-of-line");
   if (value != NULL) {
      status = parse_boolean(value, &enabled, error);
      if (status != CWIKI_CONFIG_OK) {
         return status;
      }
      if (enabled) {
         snippet->spec.flags |= CWIKI_SNIPPET_TRIGGER_BEGINNING_OF_LINE;
      }
   }
   value = mapping_get(document, node, "priority");
   if (value != NULL && (status = parse_priority(value, &snippet->spec.priority,
       error)) != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "subject");
   if (value != NULL) {
      if (value->type != YAML_SCALAR_NODE ||
          value->data.scalar.length == 0U ||
          value->data.scalar.length > CONFIG_MAX_TEXT ||
          memchr(value->data.scalar.value, '\0',
          value->data.scalar.length) != NULL ||
          !cwiki_utf8_validate((const char *)value->data.scalar.value,
          value->data.scalar.length)) {
         struct source_mark at = mark(value);

         set_error(error, at.line, at.column,
             "snippet subject must be non-empty UTF-8 text");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      snippet->spec.subject = copy_scalar(value);
      if (snippet->spec.subject == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
      snippet->spec.subject_length = value->data.scalar.length;
   }
   value = mapping_get(document, node, "bodies");
   if (value == NULL) {
      struct source_mark at = mark(node);

      set_error(error, at.line, at.column, "snippet bodies are required");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   status = parse_snippet_bodies(document, snippet, value, error);
   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   snippet->spec.match_limit = CWIKI_SNIPPET_DEFAULT_MATCH_LIMIT;
   snippet->spec.depth_limit = CWIKI_SNIPPET_DEFAULT_DEPTH_LIMIT;
   snippet_status = cwiki_snippet_registry_init(&validation);
   if (snippet_status == CWIKI_SNIPPET_OK) {
      snippet_status = cwiki_snippet_registry_add(validation, &snippet->spec);
   }
   cwiki_snippet_registry_free(validation);
   if (snippet_status == CWIKI_SNIPPET_NO_MEMORY) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   if (snippet_status != CWIKI_SNIPPET_OK) {
      struct source_mark at = mark(node);

      set_error(error, at.line, at.column,
          snippet_status == CWIKI_SNIPPET_REGEX_ERROR ?
          "snippet regex failed to compile" : "invalid snippet definition");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_snippets(yaml_document_t *document, struct cwiki_config *config,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   yaml_node_pair_t *pair;
   size_t count;
   size_t used = 0U;

   if (node == NULL || node->type != YAML_MAPPING_NODE) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column, "snippets must be a mapping");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   count = (size_t)(node->data.mapping.pairs.top -
       node->data.mapping.pairs.start);
   if (count > CONFIG_MAX_ENTRIES) {
      struct source_mark at = mark(node);

      set_error(error, at.line, at.column, "too many snippets");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   if (count != 0U) {
      config->snippets = calloc(count, sizeof(*config->snippets));
      if (config->snippets == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
   }
   for (pair = node->data.mapping.pairs.start;
       pair < node->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(document, pair->key);
      yaml_node_t *value = yaml_document_get_node(document, pair->value);
      struct config_snippet *snippet = &config->snippets[used];
      yaml_node_pair_t *earlier;
      enum cwiki_config_status status;

      if (!snippet_name_valid(key)) {
         struct source_mark at = key == NULL ? mark(node) : mark(key);

         set_error(error, at.line, at.column,
             "snippet names must be lowercase dot-separated identifiers");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      for (earlier = node->data.mapping.pairs.start; earlier < pair; earlier++) {
         yaml_node_t *previous = yaml_document_get_node(document, earlier->key);

         if (previous != NULL && previous->type == YAML_SCALAR_NODE &&
             previous->data.scalar.length == key->data.scalar.length &&
             memcmp(previous->data.scalar.value, key->data.scalar.value,
             key->data.scalar.length) == 0) {
            struct source_mark at = mark(key);

            set_error(error, at.line, at.column, "duplicate snippet name");
            return CWIKI_CONFIG_SCHEMA_ERROR;
         }
      }
      snippet->name = copy_scalar(key);
      if (snippet->name == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
      config->snippet_count = used + 1U;
      snippet->mark = mark(key);
      snippet->disabled = null_node(value);
      if (!snippet->disabled) {
         status = parse_snippet_definition(document, snippet, value, error);
         if (status != CWIKI_CONFIG_OK) {
            return status;
         }
      }
      used++;
   }
   return CWIKI_CONFIG_OK;
}

static bool
builtin_zone_id(const char *id)
{
   size_t i;

   for (i = 0U; i < sizeof(builtin_zone_ids) / sizeof(builtin_zone_ids[0]); i++) {
      if (strcmp(id, builtin_zone_ids[i]) == 0) {
         return true;
      }
   }
   return false;
}

static enum cwiki_config_status
parse_names(yaml_document_t *document, struct config_names *names,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   yaml_node_item_t *item;
   size_t count;
   size_t used = 0U;

   if (node == NULL || node->type != YAML_SEQUENCE_NODE) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column, "region names must be a sequence");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   count = (size_t)(node->data.sequence.items.top -
       node->data.sequence.items.start);
   if (count > CWIKI_ZONE_MAX_REGIONS) {
      struct source_mark at = mark(node);

      set_error(error, at.line, at.column, "too many region names");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   if (count != 0U) {
      names->values = calloc(count, sizeof(*names->values));
      if (names->values == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
   }
   for (item = node->data.sequence.items.start;
       item < node->data.sequence.items.top; item++) {
      yaml_node_t *value = yaml_document_get_node(document, *item);
      size_t i;

      if (!snippet_name_valid(value)) {
         struct source_mark at = value == NULL ? mark(node) : mark(value);

         set_error(error, at.line, at.column,
             "region references must be lowercase identifiers");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      for (i = 0U; i < used; i++) {
         if (scalar_is(value, names->values[i])) {
            struct source_mark at = mark(value);

            set_error(error, at.line, at.column, "duplicate region reference");
            return CWIKI_CONFIG_SCHEMA_ERROR;
         }
      }
      names->values[used] = copy_scalar(value);
      if (names->values[used] == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
      used++;
      names->count = used;
   }
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_capture(const yaml_node_t *node, uint8_t *capture,
    struct cwiki_config_error *error)
{
   char *text;
   char *end;
   unsigned long value;

   if (node == NULL || node->type != YAML_SCALAR_NODE ||
       node->data.scalar.style != YAML_PLAIN_SCALAR_STYLE ||
       node->data.scalar.length == 0U || node->data.scalar.length > 3U) {
      goto invalid;
   }
   text = copy_scalar(node);
   if (text == NULL) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   errno = 0;
   value = strtoul(text, &end, 10);
   if (errno != 0 || *end != '\0' || value > UINT8_MAX) {
      free(text);
      goto invalid;
   }
   free(text);
   *capture = (uint8_t)value;
   return CWIKI_CONFIG_OK;
invalid:
   {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column,
          "detail capture must be an integer from 0 to 255");
   }
   return CWIKI_CONFIG_SCHEMA_ERROR;
}

static enum cwiki_config_status
parse_pattern(const yaml_node_t *node, char **pattern, bool required,
    uint8_t detail_capture, struct cwiki_config_error *error)
{
   struct cwiki_regex *compiled = NULL;
   struct cwiki_regex_compile_error regex_error;
   enum cwiki_regex_compile_status status;

   if (node == NULL) {
      if (!required) {
         return CWIKI_CONFIG_OK;
      }
      set_error(error, 1U, 1U, "region pattern is required");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   if (node->type != YAML_SCALAR_NODE || node->data.scalar.length == 0U ||
       node->data.scalar.length > CONFIG_MAX_TEXT ||
       memchr(node->data.scalar.value, '\0', node->data.scalar.length) != NULL ||
       !cwiki_utf8_validate((const char *)node->data.scalar.value,
       node->data.scalar.length)) {
      struct source_mark at = mark(node);

      set_error(error, at.line, at.column,
          "region pattern must be non-empty UTF-8 text");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   status = cwiki_regex_compile(&compiled, (const char *)node->data.scalar.value,
       node->data.scalar.length, 0U, CWIKI_ZONE_DEFAULT_MATCH_LIMIT,
       CWIKI_ZONE_DEFAULT_DEPTH_LIMIT, &regex_error);
   if (status != CWIKI_REGEX_COMPILE_OK || (detail_capture != 0U &&
       (size_t)detail_capture >= cwiki_regex_capture_count(compiled))) {
      struct source_mark at = mark(node);

      cwiki_regex_free(compiled);
      if (status == CWIKI_REGEX_COMPILE_NO_MEMORY) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
      set_error(error, at.line, at.column,
          "region pattern or detail capture is invalid");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   cwiki_regex_free(compiled);
   *pattern = copy_scalar(node);
   return *pattern == NULL ? CWIKI_CONFIG_NO_MEMORY : CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_zone_definition(yaml_document_t *document, struct config_zone *zone,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   static const char *const allowed[] = {"kind", "start", "end", "skip",
       "contains", "parents", "top-level", "ends-at-line",
       "start-detail-capture", "end-detail-capture"};
   yaml_node_t *value;
   yaml_node_t *start;
   yaml_node_t *end;
   yaml_node_t *skip;
   enum cwiki_config_status status;

   status = mapping_check(document, node, allowed,
       sizeof(allowed) / sizeof(allowed[0]), error);
   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "kind");
   if (!parse_zone(value, &zone->region.kind)) {
      struct source_mark at = value == NULL ? mark(node) : mark(value);

      set_error(error, at.line, at.column, "unknown region zone kind");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   zone->top_level = true;
   value = mapping_get(document, node, "top-level");
   if (value != NULL && (status = parse_boolean(value, &zone->top_level,
       error)) != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "ends-at-line");
   if (value != NULL && (status = parse_boolean(value,
       &zone->region.ends_at_line, error)) != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "start-detail-capture");
   if (value != NULL && (status = parse_capture(value,
       &zone->region.start_detail_capture, error)) != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "end-detail-capture");
   if (value != NULL && (status = parse_capture(value,
       &zone->region.end_detail_capture, error)) != CWIKI_CONFIG_OK) {
      return status;
   }
   start = mapping_get(document, node, "start");
   end = mapping_get(document, node, "end");
   skip = mapping_get(document, node, "skip");
   if ((zone->region.ends_at_line && end != NULL) ||
       (zone->region.ends_at_line && zone->region.end_detail_capture != 0U)) {
      struct source_mark at = end == NULL ? mark(node) : mark(end);

      set_error(error, at.line, at.column,
          "line-ending regions cannot define an end pattern or capture");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   status = parse_pattern(start, (char **)&zone->region.start, true,
       zone->region.start_detail_capture, error);
   if (status == CWIKI_CONFIG_OK) {
      status = parse_pattern(end, (char **)&zone->region.end,
          !zone->region.ends_at_line, zone->region.end_detail_capture, error);
   }
   if (status == CWIKI_CONFIG_OK) {
      status = parse_pattern(skip, (char **)&zone->region.skip, false, 0U, error);
   }
   if (status != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "contains");
   if (value != NULL && (status = parse_names(document, &zone->contains, value,
       error)) != CWIKI_CONFIG_OK) {
      return status;
   }
   value = mapping_get(document, node, "parents");
   if (value != NULL && (status = parse_names(document, &zone->parents, value,
       error)) != CWIKI_CONFIG_OK) {
      return status;
   }
   zone->region.name = zone->id;
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_zones(yaml_document_t *document, struct cwiki_config *config,
    const yaml_node_t *node, struct cwiki_config_error *error)
{
   yaml_node_pair_t *pair;
   size_t count;
   size_t used = 0U;

   if (node == NULL || node->type != YAML_MAPPING_NODE) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line, at.column, "zones must be a mapping");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   count = (size_t)(node->data.mapping.pairs.top -
       node->data.mapping.pairs.start);
   if (count > CWIKI_ZONE_MAX_REGIONS) {
      struct source_mark at = mark(node);

      set_error(error, at.line, at.column, "too many custom zones");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   if (count != 0U) {
      config->zones = calloc(count, sizeof(*config->zones));
      if (config->zones == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
   }
   for (pair = node->data.mapping.pairs.start;
       pair < node->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(document, pair->key);
      yaml_node_t *value = yaml_document_get_node(document, pair->value);
      struct config_zone *zone = &config->zones[used];
      yaml_node_pair_t *earlier;
      enum cwiki_config_status status;

      if (!snippet_name_valid(key)) {
         struct source_mark at = key == NULL ? mark(node) : mark(key);

         set_error(error, at.line, at.column,
             "zone names must be lowercase dot-separated identifiers");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      for (earlier = node->data.mapping.pairs.start; earlier < pair; earlier++) {
         yaml_node_t *previous = yaml_document_get_node(document, earlier->key);

         if (previous != NULL && previous->type == YAML_SCALAR_NODE &&
             previous->data.scalar.length == key->data.scalar.length &&
             memcmp(previous->data.scalar.value, key->data.scalar.value,
             key->data.scalar.length) == 0) {
            struct source_mark at = mark(key);

            set_error(error, at.line, at.column, "duplicate custom zone name");
            return CWIKI_CONFIG_SCHEMA_ERROR;
         }
      }
      zone->id = copy_scalar(key);
      if (zone->id == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
      config->zone_count = used + 1U;
      if (builtin_zone_id(zone->id)) {
         struct source_mark at = mark(key);

         set_error(error, at.line, at.column,
             "builtin zones cannot be replaced or disabled");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      zone->mark = mark(key);
      zone->disabled = null_node(value);
      if (!zone->disabled) {
         status = parse_zone_definition(document, zone, value, error);
         if (status != CWIKI_CONFIG_OK) {
            return status;
         }
      }
      used++;
   }
   return CWIKI_CONFIG_OK;
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
   for (i = 0U; i < config->snippet_count; i++) {
      size_t j;

      free(config->snippets[i].name);
      free((char *)config->snippets[i].spec.trigger);
      free((char *)config->snippets[i].spec.subject);
      for (j = 0U; j < config->snippets[i].spec.body_count; j++) {
         free((char *)config->snippets[i].spec.bodies[j].body);
      }
      free((struct cwiki_snippet_body_spec *)
          config->snippets[i].spec.bodies);
   }
   for (i = 0U; i < config->zone_count; i++) {
      size_t j;

      free(config->zones[i].id);
      free((char *)config->zones[i].region.start);
      free((char *)config->zones[i].region.end);
      free((char *)config->zones[i].region.skip);
      for (j = 0U; j < config->zones[i].parents.count; j++) {
         free(config->zones[i].parents.values[j]);
      }
      for (j = 0U; j < config->zones[i].contains.count; j++) {
         free(config->zones[i].contains.values[j]);
      }
      free(config->zones[i].parents.values);
      free(config->zones[i].contains.values);
   }
   free(config->bindings);
   free(config->groups);
   free(config->snippets);
   free(config->zones);
   free(config->continuation_marker);
   free(config);
}

enum cwiki_config_status
cwiki_config_parse(struct cwiki_config **config, const unsigned char *bytes,
    size_t length, const struct cwiki_action_registry *actions,
    struct cwiki_config_error *error)
{
   static const char *const allowed[] = {"keymaps", "clue-groups", "display",
       "save-policy", "snippets", "zones"};
   static const unsigned char empty[] = "";
   yaml_parser_t parser;
   yaml_document_t document;
   yaml_document_t trailing;
   struct cwiki_config *created = NULL;
   yaml_node_t *root;
   yaml_node_t *keymaps;
   yaml_node_t *groups;
   yaml_node_t *display;
   yaml_node_t *save_policy;
   yaml_node_t *snippets;
   yaml_node_t *zones;
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
   display = mapping_get(&document, root, "display");
   save_policy = mapping_get(&document, root, "save-policy");
   snippets = mapping_get(&document, root, "snippets");
   zones = mapping_get(&document, root, "zones");
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
   if (display != NULL) {
      status = parse_display(&document, created, display, error);
      if (status != CWIKI_CONFIG_OK) {
         goto done;
      }
   }
   if (save_policy != NULL) {
      status = parse_save_policy(created, save_policy, error);
      if (status != CWIKI_CONFIG_OK) {
         goto done;
      }
   }
   if (snippets != NULL) {
      status = parse_snippets(&document, created, snippets, error);
      if (status != CWIKI_CONFIG_OK) {
         goto done;
      }
   }
   if (zones != NULL) {
      status = parse_zones(&document, created, zones, error);
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

static const struct config_snippet *
last_snippet(const struct cwiki_config *const *configs, size_t config_count,
    const char *name)
{
   size_t i;

   for (i = config_count; i != 0U; i--) {
      size_t j;

      for (j = configs[i - 1U]->snippet_count; j != 0U; j--) {
         const struct config_snippet *snippet =
             &configs[i - 1U]->snippets[j - 1U];

         if (strcmp(snippet->name, name) == 0) {
            return snippet;
         }
      }
   }
   return NULL;
}

static bool
builtin_snippet(const char *name)
{
   const struct cwiki_snippet_catalog_entry *entries;
   size_t count;
   size_t i;

   entries = cwiki_snippet_catalog_entries(&count);
   for (i = 0U; i < count; i++) {
      if (strcmp(entries[i].name, name) == 0) {
         return true;
      }
   }
   return false;
}

static enum cwiki_config_status
snippet_build_error(enum cwiki_snippet_status status,
    const struct config_snippet *snippet, struct cwiki_config_error *error)
{
   if (status == CWIKI_SNIPPET_NO_MEMORY) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   if (snippet != NULL) {
      set_error(error, snippet->mark.line, snippet->mark.column,
          status == CWIKI_SNIPPET_REGEX_ERROR ?
          "snippet regex failed to compile" : "invalid snippet definition");
   } else {
      set_error(error, 1U, 1U, "failed to install builtin snippets");
   }
   return CWIKI_CONFIG_SCHEMA_ERROR;
}

enum cwiki_config_status
cwiki_config_build_snippets(const struct cwiki_config *const *configs,
    size_t config_count, struct cwiki_snippet_registry **candidate,
    struct cwiki_config_error *error)
{
   const struct cwiki_snippet_catalog_entry *entries;
   struct cwiki_snippet_catalog_override *overrides = NULL;
   struct cwiki_snippet_registry *created = NULL;
   size_t builtin_count;
   size_t override_count = 0U;
   size_t i;
   enum cwiki_snippet_status snippet_status;
   enum cwiki_config_status status = CWIKI_CONFIG_OK;

   if (candidate == NULL || (configs == NULL && config_count != 0U)) {
      return CWIKI_CONFIG_INVALID;
   }
   *candidate = NULL;
   if (error != NULL) {
      *error = (struct cwiki_config_error){0};
   }
   for (i = 0U; i < config_count; i++) {
      if (configs[i] == NULL) {
         return CWIKI_CONFIG_INVALID;
      }
   }
   entries = cwiki_snippet_catalog_entries(&builtin_count);
   if (builtin_count != 0U) {
      overrides = calloc(builtin_count, sizeof(*overrides));
      if (overrides == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
   }
   for (i = 0U; i < builtin_count; i++) {
      const struct config_snippet *snippet = last_snippet(configs, config_count,
          entries[i].name);

      if (snippet != NULL) {
         overrides[override_count].name = entries[i].name;
         overrides[override_count].replacement = snippet->disabled ? NULL :
             &snippet->spec;
         override_count++;
      }
   }
   snippet_status = cwiki_snippet_registry_init(&created);
   if (snippet_status == CWIKI_SNIPPET_OK) {
      snippet_status = cwiki_snippet_catalog_install(created, overrides,
          override_count);
   }
   if (snippet_status != CWIKI_SNIPPET_OK) {
      const struct config_snippet *failed = NULL;

      for (i = 0U; i < override_count && failed == NULL; i++) {
         if (overrides[i].replacement != NULL) {
            failed = last_snippet(configs, config_count, overrides[i].name);
         }
      }
      status = snippet_build_error(snippet_status, failed, error);
      goto done;
   }
   for (i = 0U; i < config_count; i++) {
      size_t j;

      for (j = 0U; j < configs[i]->snippet_count; j++) {
         const struct config_snippet *snippet = &configs[i]->snippets[j];

         if (builtin_snippet(snippet->name) ||
             last_snippet(configs, config_count, snippet->name) != snippet ||
             snippet->disabled) {
            continue;
         }
         snippet_status = cwiki_snippet_registry_add(created, &snippet->spec);
         if (snippet_status != CWIKI_SNIPPET_OK) {
            status = snippet_build_error(snippet_status, snippet, error);
            goto done;
         }
      }
   }
   *candidate = created;
   created = NULL;
done:
   cwiki_snippet_registry_free(created);
   free(overrides);
   return status;
}

static const struct config_zone *
last_zone(const struct cwiki_config *const *configs, size_t config_count,
    const char *id)
{
   size_t i;

   for (i = config_count; i != 0U; i--) {
      size_t j;

      for (j = configs[i - 1U]->zone_count; j != 0U; j--) {
         const struct config_zone *zone = &configs[i - 1U]->zones[j - 1U];

         if (strcmp(zone->id, id) == 0) {
            return zone;
         }
      }
   }
   return NULL;
}

static size_t
zone_index(const struct config_zone *const *effective, size_t effective_count,
    const char *id)
{
   size_t builtin_count = sizeof(builtin_zone_ids) /
       sizeof(builtin_zone_ids[0]);
   size_t i;

   for (i = 0U; i < builtin_count; i++) {
      if (strcmp(builtin_zone_ids[i], id) == 0) {
         return i;
      }
   }
   for (i = 0U; i < effective_count; i++) {
      if (strcmp(effective[i]->id, id) == 0) {
         return builtin_count + i;
      }
   }
   return SIZE_MAX;
}

static enum cwiki_config_status
set_zone_reference_error(const struct cwiki_config *const *configs,
    size_t config_count, const struct config_zone *zone,
    struct cwiki_config_error *error, const char *message)
{
   size_t i;

   set_error(error, zone->mark.line, zone->mark.column, message);
   for (i = 0U; i < config_count; i++) {
      size_t j;

      for (j = 0U; j < configs[i]->zone_count; j++) {
         if (&configs[i]->zones[j] == zone) {
            if (error != NULL) {
               error->source_index = i;
            }
            return CWIKI_CONFIG_SCHEMA_ERROR;
         }
      }
   }
   return CWIKI_CONFIG_SCHEMA_ERROR;
}

static enum cwiki_config_status
resolve_zone_names(struct cwiki_config_zone_table *table,
    const struct config_zone *const *effective, size_t effective_count,
    const struct cwiki_config *const *configs, size_t config_count,
    struct cwiki_config_error *error)
{
   size_t builtin_count = sizeof(builtin_zone_ids) /
       sizeof(builtin_zone_ids[0]);
   size_t i;

   for (i = 0U; i < effective_count; i++) {
      const struct config_zone *zone = effective[i];
      size_t region_index = builtin_count + i;
      size_t j;

      if (zone->top_level) {
         table->top_level |= CWIKI_ZONE_REGION_BIT(region_index);
      }
      for (j = 0U; j < zone->contains.count; j++) {
         size_t child = zone_index(effective, effective_count,
             zone->contains.values[j]);

         if (child == SIZE_MAX) {
            return set_zone_reference_error(configs, config_count, zone, error,
                "custom zone contains an unknown region name");
         }
         table->regions[region_index].contains |=
             CWIKI_ZONE_REGION_BIT(child);
      }
      for (j = 0U; j < zone->parents.count; j++) {
         size_t parent = zone_index(effective, effective_count,
             zone->parents.values[j]);

         if (parent == SIZE_MAX) {
            return set_zone_reference_error(configs, config_count, zone, error,
                "custom zone has an unknown parent region name");
         }
         table->regions[parent].contains |=
             CWIKI_ZONE_REGION_BIT(region_index);
      }
   }
   return CWIKI_CONFIG_OK;
}

enum cwiki_config_status
cwiki_config_build_zones(const struct cwiki_config *const *configs,
    size_t config_count, struct cwiki_config_zone_table *table,
    struct cwiki_config_error *error)
{
   const struct cwiki_zone_region *builtins;
   const struct config_zone **effective = NULL;
   struct cwiki_zone_engine *validation = NULL;
   size_t builtin_count;
   size_t effective_count = 0U;
   size_t i;
   enum cwiki_config_status status;

   if (table == NULL || (configs == NULL && config_count != 0U)) {
      return CWIKI_CONFIG_INVALID;
   }
   *table = (struct cwiki_config_zone_table){0};
   if (error != NULL) {
      *error = (struct cwiki_config_error){0};
   }
   for (i = 0U; i < config_count; i++) {
      if (configs[i] == NULL) {
         return CWIKI_CONFIG_INVALID;
      }
   }
   builtins = cwiki_zone_builtin_regions(&builtin_count, &table->top_level);
   effective = calloc(CWIKI_ZONE_MAX_REGIONS, sizeof(*effective));
   if (effective == NULL) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   for (i = 0U; i < config_count; i++) {
      size_t j;

      for (j = 0U; j < configs[i]->zone_count; j++) {
         const struct config_zone *zone = &configs[i]->zones[j];

         if (last_zone(configs, config_count, zone->id) == zone &&
             !zone->disabled) {
            if (builtin_count + effective_count >= CWIKI_ZONE_MAX_REGIONS) {
               set_error(error, zone->mark.line, zone->mark.column,
                   "builtin and custom zones exceed the 64-region limit");
               status = CWIKI_CONFIG_SCHEMA_ERROR;
               goto done;
            }
            effective[effective_count++] = zone;
         }
      }
   }
   table->count = builtin_count + effective_count;
   table->regions = calloc(table->count, sizeof(*table->regions));
   if (table->regions == NULL) {
      status = CWIKI_CONFIG_NO_MEMORY;
      goto done;
   }
   memcpy(table->regions, builtins, builtin_count * sizeof(*table->regions));
   for (i = 0U; i < effective_count; i++) {
      table->regions[builtin_count + i] = effective[i]->region;
      table->regions[builtin_count + i].contains = 0U;
   }
   status = resolve_zone_names(table, effective, effective_count, configs,
       config_count, error);
   if (status != CWIKI_CONFIG_OK) {
      goto done;
   }
   if (cwiki_zone_engine_init(&validation, table->regions, table->count,
       table->top_level) != 0) {
      status = errno == ENOMEM ? CWIKI_CONFIG_NO_MEMORY :
          CWIKI_CONFIG_SCHEMA_ERROR;
      if (status == CWIKI_CONFIG_SCHEMA_ERROR) {
         set_error(error, effective_count == 0U ? 1U :
             effective[0]->mark.line, effective_count == 0U ? 1U :
             effective[0]->mark.column, "invalid composed zone table");
      }
      goto done;
   }
   status = CWIKI_CONFIG_OK;
done:
   cwiki_zone_engine_free(validation);
   free(effective);
   if (status != CWIKI_CONFIG_OK) {
      cwiki_config_zone_table_free(table);
   }
   return status;
}

void
cwiki_config_zone_table_free(struct cwiki_config_zone_table *table)
{
   if (table == NULL) {
      return;
   }
   free(table->regions);
   *table = (struct cwiki_config_zone_table){0};
}

void
cwiki_config_subjects_free(struct cwiki_config_subjects *subjects)
{
   size_t i;

   if (subjects == NULL) {
      return;
   }
   for (i = 0U; i < subjects->count; i++) {
      free((char *)subjects->items[i].value);
   }
   free(subjects->items);
   *subjects = (struct cwiki_config_subjects){0};
}

static enum cwiki_config_status
note_subject_values(yaml_document_t *document, const yaml_node_t *node,
    struct cwiki_config_subjects *subjects, struct cwiki_config_error *error)
{
   yaml_node_item_t *item;
   size_t count;

   if (node == NULL || node->type != YAML_SEQUENCE_NODE) {
      struct source_mark at = node == NULL ? (struct source_mark){1U, 1U} :
          mark(node);

      set_error(error, at.line + 1U, at.column,
          "note subject must be a sequence");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   count = (size_t)(node->data.sequence.items.top -
       node->data.sequence.items.start);
   if (count > CONFIG_MAX_SUBJECTS) {
      struct source_mark at = mark(node);

      set_error(error, at.line + 1U, at.column, "too many note subjects");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   if (count != 0U) {
      subjects->items = calloc(count, sizeof(*subjects->items));
      if (subjects->items == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
   }
   for (item = node->data.sequence.items.start;
       item < node->data.sequence.items.top; item++) {
      yaml_node_t *value = yaml_document_get_node(document, *item);
      size_t i;
      char *copy;

      if (value == NULL || value->type != YAML_SCALAR_NODE ||
          value->data.scalar.length == 0U ||
          value->data.scalar.length > CONFIG_MAX_TEXT ||
          memchr(value->data.scalar.value, '\0',
          value->data.scalar.length) != NULL ||
          !cwiki_utf8_validate((const char *)value->data.scalar.value,
          value->data.scalar.length)) {
         struct source_mark at = value == NULL ? mark(node) : mark(value);

         set_error(error, at.line + 1U, at.column,
             "note subjects must be non-empty UTF-8 text");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      for (i = 0U; i < subjects->count; i++) {
         if (subjects->items[i].length == value->data.scalar.length &&
             memcmp(subjects->items[i].value, value->data.scalar.value,
             value->data.scalar.length) == 0) {
            struct source_mark at = mark(value);

            set_error(error, at.line + 1U, at.column,
                "duplicate note subject");
            return CWIKI_CONFIG_SCHEMA_ERROR;
         }
      }
      copy = copy_scalar(value);
      if (copy == NULL) {
         return CWIKI_CONFIG_NO_MEMORY;
      }
      subjects->items[subjects->count].value = copy;
      subjects->items[subjects->count].length = value->data.scalar.length;
      subjects->count++;
   }
   return CWIKI_CONFIG_OK;
}

static enum cwiki_config_status
parse_note_subject_document(const unsigned char *bytes, size_t length,
    struct cwiki_config_subjects *subjects, struct cwiki_config_error *error)
{
   yaml_parser_t parser;
   yaml_document_t document;
   yaml_document_t trailing;
   yaml_node_t *root;
   yaml_node_t *subject = NULL;
   yaml_node_pair_t *pair;
   enum cwiki_config_status status = CWIKI_CONFIG_OK;
   bool document_ready = false;

   if (!yaml_parser_initialize(&parser)) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   yaml_parser_set_input_string(&parser, bytes, length);
   if (!yaml_parser_load(&parser, &document)) {
      set_error(error, parser.problem_mark.line + 2U,
          parser.problem_mark.column + 1U,
          parser.problem == NULL ? "invalid note frontmatter" : parser.problem);
      status = parser.error == YAML_MEMORY_ERROR ? CWIKI_CONFIG_NO_MEMORY :
          CWIKI_CONFIG_YAML_ERROR;
      goto done;
   }
   document_ready = true;
   root = yaml_document_get_root_node(&document);
   if (root == NULL) {
      goto trailing;
   }
   if (root->type != YAML_MAPPING_NODE) {
      struct source_mark at = mark(root);

      set_error(error, at.line + 1U, at.column,
          "note frontmatter must be a mapping");
      status = CWIKI_CONFIG_SCHEMA_ERROR;
      goto done;
   }
   for (pair = root->data.mapping.pairs.start;
       pair < root->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(&document, pair->key);

      if (key != NULL && scalar_is(key, "subject")) {
         if (subject != NULL) {
            struct source_mark at = mark(key);

            set_error(error, at.line + 1U, at.column,
                "duplicate note subject key");
            status = CWIKI_CONFIG_SCHEMA_ERROR;
            goto done;
         }
         subject = yaml_document_get_node(&document, pair->value);
      }
   }
   if (subject != NULL) {
      status = note_subject_values(&document, subject, subjects, error);
      if (status != CWIKI_CONFIG_OK) {
         goto done;
      }
   }
trailing:
   if (!yaml_parser_load(&parser, &trailing)) {
      set_error(error, parser.problem_mark.line + 2U,
          parser.problem_mark.column + 1U,
          parser.problem == NULL ? "invalid note frontmatter" : parser.problem);
      status = parser.error == YAML_MEMORY_ERROR ? CWIKI_CONFIG_NO_MEMORY :
          CWIKI_CONFIG_YAML_ERROR;
      goto done;
   }
   if (yaml_document_get_root_node(&trailing) != NULL) {
      yaml_node_t *extra = yaml_document_get_root_node(&trailing);
      struct source_mark at = mark(extra);

      set_error(error, at.line + 1U, at.column,
          "note frontmatter must contain one YAML document");
      status = CWIKI_CONFIG_SCHEMA_ERROR;
   }
   yaml_document_delete(&trailing);
done:
   if (document_ready) {
      yaml_document_delete(&document);
   }
   yaml_parser_delete(&parser);
   return status;
}

enum cwiki_config_status
cwiki_config_parse_note_subjects(const struct cwiki_buffer *buffer,
    struct cwiki_config_subjects *subjects, struct cwiki_config_error *error)
{
   unsigned char *bytes = NULL;
   size_t length = 0U;
   size_t closing;
   size_t line;
   enum cwiki_config_status status;

   if (buffer == NULL || subjects == NULL) {
      return CWIKI_CONFIG_INVALID;
   }
   *subjects = (struct cwiki_config_subjects){0};
   if (error != NULL) {
      *error = (struct cwiki_config_error){0};
   }
   if (buffer->line_count == 0U || buffer->lines[0].length != 3U ||
       memcmp(buffer->lines[0].bytes, "---", 3U) != 0) {
      return CWIKI_CONFIG_OK;
   }
   for (closing = 1U; closing < buffer->line_count; closing++) {
      if (buffer->lines[closing].length == 3U &&
          memcmp(buffer->lines[closing].bytes, "---", 3U) == 0) {
         break;
      }
   }
   if (closing == buffer->line_count) {
      set_error(error, 1U, 1U, "note frontmatter has no closing delimiter");
      return CWIKI_CONFIG_SCHEMA_ERROR;
   }
   for (line = 1U; line < closing; line++) {
      if (buffer->lines[line].length > CONFIG_MAX_BYTES - length ||
          length + buffer->lines[line].length > CONFIG_MAX_BYTES - 1U) {
         set_error(error, line + 1U, 1U,
             "note frontmatter exceeds the 1 MiB limit");
         return CWIKI_CONFIG_SCHEMA_ERROR;
      }
      length += buffer->lines[line].length + 1U;
   }
   bytes = malloc(length == 0U ? 1U : length);
   if (bytes == NULL) {
      return CWIKI_CONFIG_NO_MEMORY;
   }
   length = 0U;
   for (line = 1U; line < closing; line++) {
      memcpy(bytes + length, buffer->lines[line].bytes,
          buffer->lines[line].length);
      length += buffer->lines[line].length;
      bytes[length++] = '\n';
   }
   status = parse_note_subject_document(bytes, length, subjects, error);
   free(bytes);
   if (status != CWIKI_CONFIG_OK) {
      cwiki_config_subjects_free(subjects);
   }
   return status;
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

void
cwiki_config_settings_defaults(struct cwiki_config_settings *settings)
{
   if (settings == NULL) {
      return;
   }
   *settings = (struct cwiki_config_settings){
      true, CWIKI_CONCEAL_DEFAULT_MASK, CWIKI_CONCEALCURSOR_DEFAULT,
      true, true, "", CWIKI_SAVE_INSERT_LEAVE
   };
}

void
cwiki_config_apply_settings(const struct cwiki_config *config,
    struct cwiki_config_settings *settings)
{
   if (config == NULL || settings == NULL) {
      return;
   }
   if (config->has_conceal) {
      settings->conceal = config->conceal;
   }
   if (config->has_conceal_categories) {
      settings->conceal_categories = config->conceal_categories;
   }
   if (config->has_concealcursor_modes) {
      settings->concealcursor_modes = config->concealcursor_modes;
   }
   if (config->has_wrap) {
      settings->wrap = config->wrap;
   }
   if (config->has_break_indent) {
      settings->break_indent = config->break_indent;
   }
   if (config->has_continuation_marker) {
      settings->continuation_marker = config->continuation_marker;
   }
   if (config->has_save_policy) {
      settings->save_policy = config->save_policy;
   }
}
