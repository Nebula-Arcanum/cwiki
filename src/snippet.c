#include "snippet.h"

#include "regex.h"
#include "undo.h"
#include "unicode.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <utf8proc.h>

#define REGEX_BUCKETS 257U
#define MAX_STOP 9999U

enum token_kind { TOKEN_TEXT, TOKEN_STOP, TOKEN_CAPTURE, TOKEN_VISUAL };

struct body_token {
   enum token_kind kind;
   char *text;
   size_t length;
   unsigned number;
   enum cwiki_snippet_transform transform;
   bool has_default;
};

struct cwiki_snippet_body {
   struct body_token *tokens;
   size_t count;
   size_t capacity;
   size_t final_index;
};

struct body_variant {
   enum cwiki_zone_kind zone;
   struct cwiki_snippet_body *body;
};

struct definition {
   enum cwiki_snippet_trigger_kind kind;
   char *trigger;
   size_t trigger_length;
   char *subject;
   size_t subject_length;
   enum cwiki_zone_kind required_zone;
   struct body_variant *bodies;
   size_t body_count;
   uint32_t flags;
   int priority;
   size_t order;
   struct cwiki_regex *regex;
   uint32_t final_codepoint;
   bool disabled;
   bool diagnostic_pending;
};

struct regex_bucket {
   struct definition **definitions;
   size_t count;
   size_t capacity;
};

struct trie_edge;
struct trie_node {
   struct trie_edge *edges;
   struct definition **definitions;
   size_t count;
   size_t capacity;
};
struct trie_edge {
   unsigned char byte;
   struct trie_node *child;
   struct trie_edge *next;
};

struct cwiki_snippet_registry {
   struct definition **definitions;
   size_t count;
   size_t capacity;
   size_t longest_literal;
   char *subject;
   size_t subject_length;
   struct trie_node literal_root;
   struct regex_bucket regex_buckets[CWIKI_ZONE_CUSTOM + 1U][REGEX_BUCKETS];
};

struct occurrence {
   unsigned number;
   enum cwiki_snippet_transform transform;
   bool primary;
   struct cwiki_position start;
   struct cwiki_position end;
};

struct session {
   struct occurrence *occurrences;
   size_t occurrence_count;
   unsigned *stops;
   size_t stop_count;
   size_t current;
   struct cwiki_position extent_start;
   struct cwiki_position extent_end;
   struct cwiki_position final;
};

struct cwiki_snippet_engine {
   struct cwiki_buffer *buffer;
   struct cwiki_undo *undo;
   struct session sessions[CWIKI_SNIPPET_SESSION_MAX_DEPTH];
   size_t depth;
   bool updating;
};

static struct occurrence *primary_for(struct session *session,
    unsigned number);

#ifdef CWIKI_SNIPPET_TESTING
static size_t allocations_left;
static bool fail_enabled;
void cwiki_snippet_test_fail_allocation_after(size_t n) { allocations_left = n; fail_enabled = true; }
void cwiki_snippet_test_reset_allocation(void) { fail_enabled = false; }
static bool should_fail(void) { if (!fail_enabled) return false; if (allocations_left == 0U) { errno = ENOMEM; return true; } allocations_left--; return false; }
#else
static bool should_fail(void) { return false; }
#endif

static void *s_malloc(size_t n) { return should_fail() ? NULL : malloc(n == 0U ? 1U : n); }
static void *s_calloc(size_t n, size_t z) { return should_fail() ? NULL : calloc(n == 0U ? 1U : n, z == 0U ? 1U : z); }
static void *s_realloc(void *p, size_t n) { return should_fail() ? NULL : realloc(p, n); }

static int
reserve(void **p, size_t element, size_t *capacity, size_t needed)
{
   size_t grown = *capacity == 0U ? 4U : *capacity;
   void *next;
   if (needed <= *capacity) return 0;
   while (grown < needed) {
      if (grown > SIZE_MAX / 2U) { grown = needed; break; }
      grown *= 2U;
   }
   if (element != 0U && grown > SIZE_MAX / element) { errno = ENOMEM; return -1; }
   next = s_realloc(*p, grown * element);
   if (next == NULL) return -1;
   *p = next; *capacity = grown; return 0;
}

static char *
copy_bytes(const char *p, size_t n)
{
   char *copy = s_malloc(n + 1U);
   if (copy == NULL) return NULL;
   if (n != 0U) memcpy(copy, p, n);
   copy[n] = '\0';
   return copy;
}

static enum cwiki_snippet_status
allocation_status(void) { return CWIKI_SNIPPET_NO_MEMORY; }

static enum cwiki_snippet_transform
transform_named(const char *name, size_t length)
{
   if (length == 5U && memcmp(name, "upper", 5U) == 0) return CWIKI_SNIPPET_TRANSFORM_UPPER;
   if (length == 5U && memcmp(name, "lower", 5U) == 0) return CWIKI_SNIPPET_TRANSFORM_LOWER;
   if (length == 10U && memcmp(name, "capitalize", 10U) == 0) return CWIKI_SNIPPET_TRANSFORM_CAPITALIZE;
   if (length == 22U && memcmp(name, "match-bracket-backward", 22U) == 0) return CWIKI_SNIPPET_TRANSFORM_MATCH_BRACKET_BACKWARD;
   if (length == 24U && memcmp(name, "space-unless-punctuation", 24U) == 0) return CWIKI_SNIPPET_TRANSFORM_SPACE_UNLESS_PUNCTUATION;
   return CWIKI_SNIPPET_TRANSFORM_NONE;
}

static int
append_utf8(char **out, size_t *length, size_t *capacity, utf8proc_int32_t cp)
{
   utf8proc_uint8_t encoded[4];
   utf8proc_ssize_t used = utf8proc_encode_char(cp, encoded);
   if (used <= 0) return -1;
   if (reserve((void **)out, 1U, capacity, *length + (size_t)used + 1U) != 0) return -1;
   memcpy(*out + *length, encoded, (size_t)used);
   *length += (size_t)used;
   return 0;
}

enum cwiki_snippet_status
cwiki_snippet_transform_apply(enum cwiki_snippet_transform transform,
    const char *input, size_t input_length, char **output, size_t *output_length)
{
   char *result = NULL;
   size_t length = 0U, capacity = 0U, offset = 0U;
   bool first_cased = true;
   if (output == NULL || output_length == NULL || (input == NULL && input_length != 0U) ||
       !cwiki_utf8_validate(input, input_length) || transform == CWIKI_SNIPPET_TRANSFORM_NONE ||
       transform > CWIKI_SNIPPET_TRANSFORM_SPACE_UNLESS_PUNCTUATION) return CWIKI_SNIPPET_INVALID;
   *output = NULL; *output_length = 0U;
   if (transform == CWIKI_SNIPPET_TRANSFORM_MATCH_BRACKET_BACKWARD) {
      char value;
      if (input_length != 1U || (input[0] != ')' && input[0] != ']' && input[0] != '}')) return CWIKI_SNIPPET_INVALID;
      value = input[0] == ')' ? '(' : input[0] == ']' ? '[' : '{';
      result = copy_bytes(&value, 1U); length = 1U;
   } else if (transform == CWIKI_SNIPPET_TRANSFORM_SPACE_UNLESS_PUNCTUATION) {
      unsigned char last = input_length == 0U ? 0U : (unsigned char)input[input_length - 1U];
      bool punctuation = last < 0x80U && ispunct((int)last) != 0;
      result = copy_bytes(" ", punctuation ? 0U : 1U); length = punctuation ? 0U : 1U;
   } else {
      while (offset < input_length) {
         utf8proc_int32_t cp;
         utf8proc_ssize_t used = utf8proc_iterate((const utf8proc_uint8_t *)input + offset,
             (utf8proc_ssize_t)(input_length - offset), &cp);
         const utf8proc_property_t *property;
         if (used <= 0) { free(result); return CWIKI_SNIPPET_INVALID; }
         property = utf8proc_get_property(cp);
         if (transform == CWIKI_SNIPPET_TRANSFORM_UPPER) cp = utf8proc_toupper(cp);
         else if (transform == CWIKI_SNIPPET_TRANSFORM_LOWER) cp = utf8proc_tolower(cp);
         else if (property->category == UTF8PROC_CATEGORY_LU || property->category == UTF8PROC_CATEGORY_LL || property->category == UTF8PROC_CATEGORY_LT) {
            cp = first_cased ? utf8proc_toupper(cp) : utf8proc_tolower(cp);
            first_cased = false;
         }
         if (append_utf8(&result, &length, &capacity, cp) != 0) { free(result); return allocation_status(); }
         offset += (size_t)used;
      }
      if (result == NULL) result = copy_bytes("", 0U);
      else result[length] = '\0';
   }
   if (result == NULL) return allocation_status();
   *output = result; *output_length = length; return CWIKI_SNIPPET_OK;
}

static void
token_free(struct body_token *token) { free(token->text); }

void
cwiki_snippet_body_free(struct cwiki_snippet_body *body)
{
   size_t i;
   if (body == NULL) return;
   for (i = 0U; i < body->count; i++) token_free(&body->tokens[i]);
   free(body->tokens); free(body);
}

static int
add_token(struct cwiki_snippet_body *body, struct body_token token)
{
   if (reserve((void **)&body->tokens, sizeof(*body->tokens), &body->capacity,
       body->count + 1U) != 0) return -1;
   body->tokens[body->count++] = token; return 0;
}

static bool
parse_number(const char *p, size_t n, unsigned *value, size_t *used)
{
   size_t i = 0U; unsigned result = 0U;
   if (n == 0U || !isdigit((unsigned char)p[0])) return false;
   while (i < n && isdigit((unsigned char)p[i])) {
      unsigned digit = (unsigned)(p[i] - '0');
      if (result > (MAX_STOP - digit) / 10U) return false;
      result = result * 10U + digit; i++;
   }
   *value = result; *used = i; return true;
}

static bool
default_conflicts(const struct cwiki_snippet_body *body, unsigned number,
    const char *text, size_t length)
{
   size_t i;
   for (i = 0U; i < body->count; i++) {
      const struct body_token *t = &body->tokens[i];
      if (t->kind == TOKEN_STOP && t->number == number && t->has_default &&
          (t->length != length || memcmp(t->text, text, length) != 0)) return true;
   }
   return false;
}

static enum cwiki_snippet_status
parse_braced(struct cwiki_snippet_body *body, const char *p, size_t n,
    size_t capture_count, size_t *used)
{
   const char *close = memchr(p, '}', n);
   struct body_token token = {0};
   const char *bar;
   size_t inside, prefix, digits;
   if (close == NULL) return CWIKI_SNIPPET_INVALID;
   inside = (size_t)(close - p); *used = inside + 1U;
   if (inside == 6U && memcmp(p, "visual", 6U) == 0) { token.kind = TOKEN_VISUAL; return add_token(body, token) == 0 ? CWIKI_SNIPPET_OK : allocation_status(); }
   if (inside >= 8U && memcmp(p, "capture:", 8U) == 0) { token.kind = TOKEN_CAPTURE; prefix = 8U; }
   else if (inside >= 5U && memcmp(p, "stop:", 5U) == 0) { token.kind = TOKEN_STOP; prefix = 5U; }
   else if (parse_number(p, inside, &token.number, &digits)) {
      token.kind = TOKEN_STOP;
      if (digits == inside) return add_token(body, token) == 0 ? CWIKI_SNIPPET_OK : allocation_status();
      if (p[digits] != ':' || token.number == 0U) return CWIKI_SNIPPET_INVALID;
      token.has_default = true; token.length = inside - digits - 1U;
      token.text = copy_bytes(p + digits + 1U, token.length);
      if (token.text == NULL) return allocation_status();
      if (default_conflicts(body, token.number, token.text, token.length) || add_token(body, token) != 0) { token_free(&token); return CWIKI_SNIPPET_INVALID; }
      return CWIKI_SNIPPET_OK;
   } else return CWIKI_SNIPPET_INVALID;
   if (!parse_number(p + prefix, inside - prefix, &token.number, &digits) || token.number == 0U) return CWIKI_SNIPPET_INVALID;
   if (token.kind == TOKEN_CAPTURE && (size_t)token.number >= capture_count) return CWIKI_SNIPPET_INVALID;
   if (prefix + digits < inside) {
      if (p[prefix + digits] != '|') return CWIKI_SNIPPET_INVALID;
      bar = p + prefix + digits + 1U;
      token.transform = transform_named(bar, inside - prefix - digits - 1U);
      if (token.transform == CWIKI_SNIPPET_TRANSFORM_NONE) return CWIKI_SNIPPET_INVALID;
   }
   return add_token(body, token) == 0 ? CWIKI_SNIPPET_OK : allocation_status();
}

enum cwiki_snippet_status
cwiki_snippet_body_compile(struct cwiki_snippet_body **output,
    const char *source, size_t length, size_t capture_count)
{
   struct cwiki_snippet_body *body;
   size_t cursor = 0U, text = 0U;
   bool final_seen = false;
   if (output == NULL) return CWIKI_SNIPPET_INVALID;
   *output = NULL;
   if ((source == NULL && length != 0U) || !cwiki_utf8_validate(source, length)) return CWIKI_SNIPPET_INVALID;
   body = s_calloc(1U, sizeof(*body)); if (body == NULL) return allocation_status();
   while (cursor < length) {
      struct body_token token = {0};
      size_t used = 0U;
      enum cwiki_snippet_status status;
      if (source[cursor] != '$') { cursor++; continue; }
      if (cursor > text) {
         token.kind = TOKEN_TEXT; token.length = cursor - text; token.text = copy_bytes(source + text, token.length);
         if (token.text == NULL || add_token(body, token) != 0) { token_free(&token); cwiki_snippet_body_free(body); return allocation_status(); }
         memset(&token, 0, sizeof(token));
      }
      if (cursor + 1U >= length) { cwiki_snippet_body_free(body); return CWIKI_SNIPPET_INVALID; }
      if (source[cursor + 1U] == '$') {
         token.kind = TOKEN_TEXT; token.text = copy_bytes("$", 1U); token.length = 1U;
         if (token.text == NULL || add_token(body, token) != 0) { token_free(&token); cwiki_snippet_body_free(body); return allocation_status(); }
         cursor += 2U;
      } else if (source[cursor + 1U] == '{') {
         status = parse_braced(body, source + cursor + 2U, length - cursor - 2U, capture_count, &used);
         if (status != CWIKI_SNIPPET_OK) { cwiki_snippet_body_free(body); return status; }
         cursor += used + 2U;
      } else if (isdigit((unsigned char)source[cursor + 1U])) {
         if (!parse_number(source + cursor + 1U, length - cursor - 1U, &token.number, &used)) { cwiki_snippet_body_free(body); return CWIKI_SNIPPET_INVALID; }
         token.kind = TOKEN_STOP;
         if (token.number == 0U) { if (final_seen) { cwiki_snippet_body_free(body); return CWIKI_SNIPPET_INVALID; } final_seen = true; body->final_index = body->count; }
         if (add_token(body, token) != 0) { cwiki_snippet_body_free(body); return allocation_status(); }
         cursor += used + 1U;
      } else { cwiki_snippet_body_free(body); return CWIKI_SNIPPET_INVALID; }
      text = cursor;
   }
   if (cursor > text) {
      struct body_token token = {TOKEN_TEXT, NULL, cursor - text, 0U, CWIKI_SNIPPET_TRANSFORM_NONE, false};
      token.text = copy_bytes(source + text, token.length);
      if (token.text == NULL || add_token(body, token) != 0) { token_free(&token); cwiki_snippet_body_free(body); return allocation_status(); }
   }
   if (!final_seen) { cwiki_snippet_body_free(body); return CWIKI_SNIPPET_INVALID; }
   *output = body; return CWIKI_SNIPPET_OK;
}

bool
cwiki_snippet_body_valid(const struct cwiki_snippet_body *body, size_t capture_count)
{
   size_t i, finals = 0U;
   if (body == NULL || body->count == 0U || body->final_index >= body->count) return false;
   for (i = 0U; i < body->count; i++) {
      const struct body_token *t = &body->tokens[i];
      if (t->kind > TOKEN_VISUAL || (t->kind == TOKEN_CAPTURE && ((size_t)t->number >= capture_count || t->number == 0U))) return false;
      if (t->kind == TOKEN_STOP && t->number == 0U) finals++;
      if (t->text != NULL && !cwiki_utf8_validate(t->text, t->length)) return false;
   }
   return finals == 1U;
}

static void
trie_free(struct trie_node *node)
{
   struct trie_edge *edge = node->edges;
   while (edge != NULL) {
      struct trie_edge *next = edge->next;
      trie_free(edge->child); free(edge->child); free(edge); edge = next;
   }
   free(node->definitions);
}

static void
definition_free(struct definition *definition)
{
   size_t i;
   if (definition == NULL) return;
   for (i = 0U; i < definition->body_count; i++) cwiki_snippet_body_free(definition->bodies[i].body);
   free(definition->bodies); free(definition->trigger); free(definition->subject);
   cwiki_regex_free(definition->regex); free(definition);
}

enum cwiki_snippet_status
cwiki_snippet_registry_init(struct cwiki_snippet_registry **output)
{
   struct cwiki_snippet_registry *registry;
   if (output == NULL) return CWIKI_SNIPPET_INVALID;
   *output = NULL; registry = s_calloc(1U, sizeof(*registry));
   if (registry == NULL) return allocation_status();
   *output = registry; return CWIKI_SNIPPET_OK;
}

void
cwiki_snippet_registry_free(struct cwiki_snippet_registry *registry)
{
   size_t i, zone, bucket;
   if (registry == NULL) return;
   for (i = 0U; i < registry->count; i++) definition_free(registry->definitions[i]);
   trie_free(&registry->literal_root); free(registry->definitions);
   for (zone = 0U; zone <= CWIKI_ZONE_CUSTOM; zone++)
      for (bucket = 0U; bucket < REGEX_BUCKETS; bucket++)
         free(registry->regex_buckets[zone][bucket].definitions);
   free(registry->subject); free(registry);
}

static bool
decode_final_literal(const char *pattern, size_t length, uint32_t *result)
{
   size_t offset = 0U, last = 0U, slash_count = 0U;
   utf8proc_int32_t cp = 0;
   while (offset < length) {
      utf8proc_ssize_t used;
      last = offset;
      used = utf8proc_iterate((const utf8proc_uint8_t *)pattern + offset,
          (utf8proc_ssize_t)(length - offset), &cp);
      if (used <= 0) return false;
      offset += (size_t)used;
   }
   if (length == 0U) return false;
   while (last > 0U && pattern[last - 1U] == '\\') { slash_count++; last--; }
   if (cp < 0x80 && strchr(".^$*+?()[]{}|", (int)cp) != NULL && slash_count % 2U == 0U) return false;
   if (cp == '\\' && slash_count % 2U == 0U) return false;
   *result = (uint32_t)cp; return true;
}

static struct trie_node *
trie_child(struct trie_node *node, unsigned char byte, bool create)
{
   struct trie_edge *edge;
   for (edge = node->edges; edge != NULL; edge = edge->next) if (edge->byte == byte) return edge->child;
   if (!create) return NULL;
   edge = s_calloc(1U, sizeof(*edge));
   if (edge == NULL) return NULL;
   edge->child = s_calloc(1U, sizeof(*edge->child));
   if (edge->child == NULL) { free(edge); return NULL; }
   edge->byte = byte; edge->next = node->edges; node->edges = edge;
   return edge->child;
}

static int
trie_add(struct cwiki_snippet_registry *registry, struct definition *definition)
{
   struct trie_node *node = &registry->literal_root;
   size_t i;
   for (i = definition->trigger_length; i > 0U; i--) {
      node = trie_child(node, (unsigned char)definition->trigger[i - 1U], true);
      if (node == NULL) return -1;
   }
   if (reserve((void **)&node->definitions, sizeof(*node->definitions),
       &node->capacity, node->count + 1U) != 0) return -1;
   node->definitions[node->count++] = definition;
   if (definition->trigger_length > registry->longest_literal) registry->longest_literal = definition->trigger_length;
   return 0;
}

static enum cwiki_snippet_status
compile_regex(struct definition *definition, const struct cwiki_snippet_spec *spec)
{
   struct cwiki_regex_compile_error error;
   enum cwiki_regex_compile_status status;
   char *wrapped;
   size_t length;
   uint32_t match_limit = spec->match_limit == 0U ? CWIKI_SNIPPET_DEFAULT_MATCH_LIMIT : spec->match_limit;
   uint32_t depth_limit = spec->depth_limit == 0U ? CWIKI_SNIPPET_DEFAULT_DEPTH_LIMIT : spec->depth_limit;
   if (!decode_final_literal(spec->trigger, spec->trigger_length, &definition->final_codepoint) || spec->trigger_length > SIZE_MAX - 7U) return CWIKI_SNIPPET_INVALID;
   length = spec->trigger_length + 5U; wrapped = s_malloc(length + 1U);
   if (wrapped == NULL) return allocation_status();
   memcpy(wrapped, "(?:", 3U); memcpy(wrapped + 3U, spec->trigger, spec->trigger_length);
   memcpy(wrapped + 3U + spec->trigger_length, ")$", 3U);
   status = cwiki_regex_compile(&definition->regex, wrapped, length, 0U,
       match_limit, depth_limit, &error);
   free(wrapped);
   return status == CWIKI_REGEX_COMPILE_OK ? CWIKI_SNIPPET_OK :
       status == CWIKI_REGEX_COMPILE_NO_MEMORY ? allocation_status() : CWIKI_SNIPPET_REGEX_ERROR;
}

static bool
valid_spec(const struct cwiki_snippet_spec *spec)
{
   uint32_t expand = CWIKI_SNIPPET_TRIGGER_AUTO | CWIKI_SNIPPET_TRIGGER_EXPLICIT;
   size_t i;
   bool required_body = false;
   if (spec == NULL || spec->bodies == NULL) return false;
   for (i = 0U; i < spec->body_count; i++)
      required_body = required_body || spec->bodies[i].zone == spec->required_zone;
   return spec->trigger != NULL && spec->trigger_length != 0U &&
       cwiki_utf8_validate(spec->trigger, spec->trigger_length) &&
       spec->required_zone <= CWIKI_ZONE_CUSTOM && spec->bodies != NULL &&
       spec->body_count != 0U && required_body &&
       (spec->subject != NULL || spec->subject_length == 0U) &&
       cwiki_utf8_validate(spec->subject, spec->subject_length) &&
       (spec->flags & expand) != 0U && (spec->flags & ~((uint32_t)expand |
       CWIKI_SNIPPET_TRIGGER_WORD_BOUNDARY | CWIKI_SNIPPET_TRIGGER_BEGINNING_OF_LINE)) == 0U;
}

enum cwiki_snippet_status
cwiki_snippet_registry_add(struct cwiki_snippet_registry *registry,
    const struct cwiki_snippet_spec *spec)
{
   struct definition *definition;
   enum cwiki_snippet_status status = CWIKI_SNIPPET_OK;
   size_t captures = 1U, i;
   if (registry == NULL || !valid_spec(spec)) return CWIKI_SNIPPET_INVALID;
   definition = s_calloc(1U, sizeof(*definition)); if (definition == NULL) return allocation_status();
   definition->kind = spec->kind; definition->required_zone = spec->required_zone;
   definition->flags = spec->flags; definition->priority = spec->priority;
   definition->trigger_length = spec->trigger_length; definition->subject_length = spec->subject_length;
   definition->trigger = copy_bytes(spec->trigger, spec->trigger_length);
   definition->subject = copy_bytes(spec->subject == NULL ? "" : spec->subject, spec->subject_length);
   definition->bodies = s_calloc(spec->body_count, sizeof(*definition->bodies));
   if (definition->trigger == NULL || definition->subject == NULL || definition->bodies == NULL) { definition_free(definition); return allocation_status(); }
   if (spec->kind == CWIKI_SNIPPET_REGEX) {
      status = compile_regex(definition, spec);
      if (status != CWIKI_SNIPPET_OK) { definition_free(definition); return status; }
      captures = cwiki_regex_capture_count(definition->regex);
   } else if (spec->kind != CWIKI_SNIPPET_LITERAL) { definition_free(definition); return CWIKI_SNIPPET_INVALID; }
   for (i = 0U; i < spec->body_count; i++) {
      size_t j;
      if (spec->bodies[i].zone > CWIKI_ZONE_CUSTOM) { status = CWIKI_SNIPPET_INVALID; break; }
      for (j = 0U; j < i; j++) if (spec->bodies[j].zone == spec->bodies[i].zone) { status = CWIKI_SNIPPET_INVALID; break; }
      if (status != CWIKI_SNIPPET_OK) break;
      definition->bodies[i].zone = spec->bodies[i].zone;
      status = cwiki_snippet_body_compile(&definition->bodies[i].body,
          spec->bodies[i].body, spec->bodies[i].body_length, captures);
      if (status != CWIKI_SNIPPET_OK) break;
      definition->body_count++;
   }
   if (status != CWIKI_SNIPPET_OK || reserve((void **)&registry->definitions,
       sizeof(*registry->definitions), &registry->capacity, registry->count + 1U) != 0) {
      definition_free(definition); return status == CWIKI_SNIPPET_OK ? allocation_status() : status;
   }
   definition->order = registry->count;
   if (definition->kind == CWIKI_SNIPPET_LITERAL) {
      if (trie_add(registry, definition) != 0) { definition_free(definition); return allocation_status(); }
   } else {
      size_t bucket_index = definition->final_codepoint % REGEX_BUCKETS;
      for (i = 0U; i < definition->body_count; i++) {
         struct regex_bucket *bucket = &registry->regex_buckets[definition->bodies[i].zone][bucket_index];
         if (reserve((void **)&bucket->definitions, sizeof(*bucket->definitions),
             &bucket->capacity, bucket->count + 1U) != 0) {
            size_t added;
            for (added = 0U; added < i; added++) {
               struct regex_bucket *previous = &registry->regex_buckets[definition->bodies[added].zone][bucket_index];
               previous->count--;
            }
            definition_free(definition); return allocation_status();
         }
         bucket->definitions[bucket->count++] = definition;
      }
   }
   registry->definitions[registry->count++] = definition;
   return CWIKI_SNIPPET_OK;
}

enum cwiki_snippet_status
cwiki_snippet_registry_set_subject(struct cwiki_snippet_registry *registry,
    const char *subject, size_t subject_length)
{
   char *copy;
   if (registry == NULL || (subject == NULL && subject_length != 0U) || !cwiki_utf8_validate(subject, subject_length)) return CWIKI_SNIPPET_INVALID;
   copy = copy_bytes(subject == NULL ? "" : subject, subject_length);
   if (copy == NULL) return allocation_status();
   free(registry->subject); registry->subject = copy; registry->subject_length = subject_length;
   return CWIKI_SNIPPET_OK;
}

static bool
subject_active(const struct cwiki_snippet_registry *registry,
    const struct definition *definition)
{
   return definition->subject_length == 0U ||
       (definition->subject_length == registry->subject_length &&
       memcmp(definition->subject, registry->subject, registry->subject_length) == 0);
}

static bool
has_body(const struct definition *definition, enum cwiki_zone_kind zone)
{
   size_t i;
   for (i = 0U; i < definition->body_count; i++) if (definition->bodies[i].zone == zone) return true;
   return false;
}

static bool
kind_active(const struct definition *definition, enum cwiki_snippet_expand_kind kind)
{
   return kind == CWIKI_SNIPPET_AUTO ?
       (definition->flags & CWIKI_SNIPPET_TRIGGER_AUTO) != 0U :
       (definition->flags & CWIKI_SNIPPET_TRIGGER_EXPLICIT) != 0U;
}

static bool
word_codepoint(utf8proc_int32_t cp)
{
   utf8proc_category_t category = utf8proc_category(cp);
   return cp == '_' || (category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_LO) ||
       (category >= UTF8PROC_CATEGORY_ND && category <= UTF8PROC_CATEGORY_NO);
}

static bool
boundary_ok(const struct definition *definition, const struct cwiki_line *line,
    size_t start)
{
   size_t offset = 0U, previous = 0U;
   utf8proc_int32_t cp = 0;
   if ((definition->flags & CWIKI_SNIPPET_TRIGGER_BEGINNING_OF_LINE) != 0U && start != 0U) return false;
   if ((definition->flags & CWIKI_SNIPPET_TRIGGER_WORD_BOUNDARY) == 0U || start == 0U) return true;
   while (offset < start) {
      utf8proc_ssize_t used;
      previous = offset;
      used = utf8proc_iterate((const utf8proc_uint8_t *)line->bytes + offset,
          (utf8proc_ssize_t)(start - offset), &cp);
      if (used <= 0) return false;
      offset += (size_t)used;
   }
   return offset == start && previous < start && !word_codepoint(cp);
}

static bool
better(const struct definition *candidate, size_t consumed,
    const struct definition *best, size_t best_consumed)
{
   bool candidate_subject, best_subject;
   if (best == NULL) return true;
   if (candidate->priority != best->priority) return candidate->priority > best->priority;
   if (consumed != best_consumed) return consumed > best_consumed;
   candidate_subject = candidate->subject_length != 0U; best_subject = best->subject_length != 0U;
   if (candidate_subject != best_subject) return candidate_subject;
   return candidate->order < best->order;
}

static bool
eligible(const struct cwiki_snippet_registry *registry,
    const struct definition *definition, enum cwiki_zone_kind zone,
    enum cwiki_snippet_expand_kind kind, const struct cwiki_line *line,
    size_t start)
{
   return has_body(definition, zone) &&
       subject_active(registry, definition) && kind_active(definition, kind) &&
       boundary_ok(definition, line, start);
}

static bool
final_codepoint(const struct cwiki_line *line, size_t cursor, uint32_t *cp)
{
   size_t offset = 0U;
   utf8proc_int32_t value = 0;
   if (cursor == 0U) return false;
   while (offset < cursor) {
      utf8proc_ssize_t used = utf8proc_iterate((const utf8proc_uint8_t *)line->bytes + offset,
          (utf8proc_ssize_t)(cursor - offset), &value);
      if (used <= 0) return false;
      offset += (size_t)used;
   }
   if (offset != cursor) return false;
   *cp = (uint32_t)value; return true;
}

enum cwiki_snippet_status
cwiki_snippet_match(struct cwiki_snippet_registry *registry,
    struct cwiki_zone_engine *zones, const struct cwiki_buffer *buffer,
    struct cwiki_position cursor, enum cwiki_snippet_expand_kind kind,
    uint32_t input_flags, struct cwiki_snippet_match *match)
{
   const struct cwiki_line *line;
   struct cwiki_zone zone;
   struct definition *best = NULL;
   const struct cwiki_regex_capture *best_captures = NULL;
   size_t best_count = 0U, best_consumed = 0U, walked = 0U;
   struct trie_node *node;
   uint32_t cp;
   if (match == NULL) return CWIKI_SNIPPET_INVALID;
   memset(match, 0, sizeof(*match));
   if (registry == NULL || zones == NULL || buffer == NULL || cursor.line >= buffer->line_count ||
       cursor.byte > buffer->lines[cursor.line].length || kind > CWIKI_SNIPPET_EXPLICIT ||
       (input_flags & ~(uint32_t)CWIKI_SNIPPET_INPUT_PASTE) != 0U) return CWIKI_SNIPPET_INVALID;
   if ((input_flags & CWIKI_SNIPPET_INPUT_PASTE) != 0U) return CWIKI_SNIPPET_NO_MATCH;
   if (cwiki_zone_at(zones, buffer, cursor.line, cursor.byte, &zone) != 0) return CWIKI_SNIPPET_INVALID;
   line = &buffer->lines[cursor.line]; node = &registry->literal_root;
   while (walked < registry->longest_literal && walked < cursor.byte) {
      size_t i;
      node = trie_child(node, (unsigned char)line->bytes[cursor.byte - walked - 1U], false);
      if (node == NULL) break;
      walked++;
      for (i = 0U; i < node->count; i++) {
         struct definition *candidate = node->definitions[i];
         if (eligible(registry, candidate, zone.kind, kind, line, cursor.byte - walked) &&
             better(candidate, walked, best, best_consumed)) {
            best = candidate; best_consumed = walked; best_captures = NULL; best_count = 0U;
         }
      }
   }
   if (final_codepoint(line, cursor.byte, &cp)) {
      struct regex_bucket *bucket = &registry->regex_buckets[zone.kind][cp % REGEX_BUCKETS];
      size_t bucket_index;
      for (bucket_index = 0U; bucket_index < bucket->count; bucket_index++) {
         struct definition *candidate = bucket->definitions[bucket_index];
         struct cwiki_regex_result result;
         size_t consumed;
         if (candidate->disabled || candidate->final_codepoint != cp || !subject_active(registry, candidate) ||
             !kind_active(candidate, kind) || !has_body(candidate, zone.kind)) continue;
         result = cwiki_regex_execute(candidate->regex, line->bytes, cursor.byte, 0U, false);
         if (result.status == CWIKI_REGEX_MATCH_LIMIT || result.status == CWIKI_REGEX_DEPTH_LIMIT || result.status == CWIKI_REGEX_JIT_STACK_LIMIT) {
            candidate->disabled = true; candidate->diagnostic_pending = true; continue;
         }
         if (result.status != CWIKI_REGEX_MATCH || result.capture_count == 0U ||
             result.captures[0].end != cursor.byte || result.captures[0].start == CWIKI_REGEX_UNSET) continue;
         consumed = cursor.byte - result.captures[0].start;
         if (eligible(registry, candidate, zone.kind, kind, line, result.captures[0].start) &&
             better(candidate, consumed, best, best_consumed)) {
            best = candidate; best_consumed = consumed; best_captures = result.captures; best_count = result.capture_count;
         }
      }
   }
   if (best == NULL) return CWIKI_SNIPPET_NO_MATCH;
   if (best_count != 0U) {
      match->captures = s_malloc(best_count * sizeof(*match->captures));
      if (match->captures == NULL) return allocation_status();
      memcpy(match->captures, best_captures, best_count * sizeof(*match->captures));
   }
   match->definition = best; match->zone = zone.kind; match->line = cursor.line;
   match->start_byte = cursor.byte - best_consumed; match->end_byte = cursor.byte;
   match->capture_count = best_count; return CWIKI_SNIPPET_OK;
}

void
cwiki_snippet_match_free(struct cwiki_snippet_match *match)
{
   if (match == NULL) return;
   free(match->captures); memset(match, 0, sizeof(*match));
}

bool
cwiki_snippet_take_limit_diagnostic(struct cwiki_snippet_registry *registry)
{
   size_t i;
   if (registry == NULL) return false;
   for (i = 0U; i < registry->count; i++) if (registry->definitions[i]->diagnostic_pending) {
      registry->definitions[i]->diagnostic_pending = false; return true;
   }
   return false;
}

struct rendered_occurrence {
   unsigned number;
   enum cwiki_snippet_transform transform;
   bool primary;
   size_t start;
   size_t end;
};

struct rendered_body {
   char *bytes;
   size_t length;
   size_t capacity;
   struct rendered_occurrence *occurrences;
   size_t occurrence_count;
   size_t occurrence_capacity;
   size_t final;
};

struct stop_value { const char *bytes; size_t length; bool primary_seen; };

static int
append_bytes(struct rendered_body *rendered, const char *bytes, size_t length)
{
   if (length > SIZE_MAX - rendered->length || reserve((void **)&rendered->bytes,
       1U, &rendered->capacity, rendered->length + length + 1U) != 0) return -1;
   if (length != 0U) memcpy(rendered->bytes + rendered->length, bytes, length);
   rendered->length += length; rendered->bytes[rendered->length] = '\0'; return 0;
}

static int
render_occurrence(struct rendered_body *rendered, unsigned number,
    enum cwiki_snippet_transform transform, bool primary, size_t start)
{
   struct rendered_occurrence occurrence = {number, transform, primary, start, rendered->length};
   if (reserve((void **)&rendered->occurrences, sizeof(*rendered->occurrences),
       &rendered->occurrence_capacity, rendered->occurrence_count + 1U) != 0) return -1;
   rendered->occurrences[rendered->occurrence_count++] = occurrence; return 0;
}

static const struct cwiki_snippet_body *
body_for(const struct definition *definition, enum cwiki_zone_kind zone)
{
   size_t i;
   for (i = 0U; i < definition->body_count; i++) if (definition->bodies[i].zone == zone) return definition->bodies[i].body;
   return NULL;
}

static enum cwiki_snippet_status
render_body(const struct cwiki_snippet_body *body,
    const struct cwiki_snippet_match *match, const struct cwiki_line *source,
    const char *visual, size_t visual_length, struct rendered_body *rendered)
{
   struct stop_value *values;
   size_t i;
   values = s_calloc(MAX_STOP + 1U, sizeof(*values));
   if (values == NULL) return allocation_status();
   for (i = 0U; i < body->count; i++) {
      const struct body_token *token = &body->tokens[i];
      if (token->kind == TOKEN_STOP && token->number != 0U && token->has_default && values[token->number].bytes == NULL) {
         values[token->number].bytes = token->text; values[token->number].length = token->length;
      }
   }
   for (i = 0U; i < body->count; i++) {
      const struct body_token *token = &body->tokens[i];
      const char *bytes = NULL;
      size_t length = 0U, start = rendered->length;
      char *transformed = NULL;
      enum cwiki_snippet_status status;
      if (token->kind == TOKEN_TEXT) { bytes = token->text; length = token->length; }
      else if (token->kind == TOKEN_VISUAL) { bytes = visual; length = visual_length; }
      else if (token->kind == TOKEN_CAPTURE) {
         const struct cwiki_regex_capture *capture;
         if ((size_t)token->number >= match->capture_count) { free(values); return CWIKI_SNIPPET_INVALID; }
         capture = &match->captures[token->number];
         if (capture->start != CWIKI_REGEX_UNSET) {
            if (capture->start > capture->end || capture->end > source->length ||
                (source->bytes == NULL && capture->end != 0U)) {
               free(values); return CWIKI_SNIPPET_INVALID;
            }
            bytes = capture->start == 0U ? source->bytes :
                source->bytes + capture->start;
            length = capture->end - capture->start;
         }
      } else if (token->number == 0U) {
         rendered->final = rendered->length; continue;
      } else { bytes = values[token->number].bytes; length = values[token->number].length; }
      if (bytes == NULL) bytes = "";
      if (token->transform != CWIKI_SNIPPET_TRANSFORM_NONE) {
         status = cwiki_snippet_transform_apply(token->transform, bytes, length, &transformed, &length);
         if (status != CWIKI_SNIPPET_OK) { free(values); return status; }
         bytes = transformed;
      }
      if (append_bytes(rendered, bytes, length) != 0) { free(transformed); free(values); return allocation_status(); }
      free(transformed);
      if (token->kind == TOKEN_STOP) {
         bool primary = token->transform == CWIKI_SNIPPET_TRANSFORM_NONE && !values[token->number].primary_seen;
         if (primary) values[token->number].primary_seen = true;
         if (render_occurrence(rendered, token->number, token->transform, primary, start) != 0) { free(values); return allocation_status(); }
      }
   }
   free(values); return CWIKI_SNIPPET_OK;
}

static void
rendered_free(struct rendered_body *rendered)
{
   free(rendered->bytes); free(rendered->occurrences); memset(rendered, 0, sizeof(*rendered));
}

static int
position_compare(struct cwiki_position left, struct cwiki_position right)
{
   if (left.line != right.line) return left.line < right.line ? -1 : 1;
   if (left.byte != right.byte) return left.byte < right.byte ? -1 : 1;
   return 0;
}

static struct cwiki_position
offset_position(struct cwiki_position base, const char *bytes, size_t offset)
{
   struct cwiki_position result = base;
   size_t i, after_newline = 0U;
   for (i = 0U; i < offset; i++) if (bytes[i] == '\n') { result.line++; after_newline = i + 1U; }
   result.byte = result.line == base.line ? base.byte + offset : offset - after_newline;
   return result;
}

static int
undo_insert_text(struct cwiki_undo *undo, struct cwiki_position start,
    const char *bytes, size_t length)
{
   size_t offset = 0U;
   struct cwiki_position cursor = start;
   if (length == 0U) return 0;
   while (offset <= length) {
      const char *newline = memchr(bytes + offset, '\n', length - offset);
      size_t segment = newline == NULL ? length - offset : (size_t)(newline - (bytes + offset));
      if (cwiki_undo_insert(undo, cursor.line, cursor.byte, bytes + offset, segment) != 0) return -1;
      cursor.byte += segment; offset += segment;
      if (newline == NULL) break;
      if (cwiki_undo_split(undo, cursor.line, cursor.byte) != 0) return -1;
      cursor.line++; cursor.byte = 0U; offset++;
   }
   return 0;
}

static int
undo_delete_range(struct cwiki_undo *undo, struct cwiki_position start,
    struct cwiki_position end)
{
   size_t joins;
   if (position_compare(start, end) > 0) return -1;
   if (start.line == end.line) return cwiki_undo_delete(undo, start.line, start.byte, end.byte - start.byte);
   if (cwiki_undo_delete(undo, start.line, start.byte,
       undo->buffer->lines[start.line].length - start.byte) != 0 ||
       cwiki_undo_delete(undo, end.line, 0U, end.byte) != 0) return -1;
   joins = end.line - start.line;
   while (joins-- != 0U) if (cwiki_undo_join(undo, start.line) != 0) return -1;
   return 0;
}

static void
session_unregister(struct cwiki_snippet_engine *engine, struct session *session)
{
   size_t i;
   for (i = 0U; i < session->occurrence_count; i++) {
      cwiki_buffer_unregister_position(engine->buffer, &session->occurrences[i].start);
      cwiki_buffer_unregister_position(engine->buffer, &session->occurrences[i].end);
   }
   cwiki_buffer_unregister_position(engine->buffer, &session->extent_start);
   cwiki_buffer_unregister_position(engine->buffer, &session->extent_end);
   cwiki_buffer_unregister_position(engine->buffer, &session->final);
}

static void
session_clear(struct cwiki_snippet_engine *engine, struct session *session)
{
   session_unregister(engine, session); free(session->occurrences); free(session->stops);
   memset(session, 0, sizeof(*session));
}

enum cwiki_snippet_status
cwiki_snippet_engine_init(struct cwiki_snippet_engine **output,
    struct cwiki_buffer *buffer, struct cwiki_undo *undo)
{
   struct cwiki_snippet_engine *engine;
   if (output == NULL || buffer == NULL || undo == NULL || undo->buffer != buffer) return CWIKI_SNIPPET_INVALID;
   *output = NULL; engine = s_calloc(1U, sizeof(*engine));
   if (engine == NULL) return allocation_status();
   engine->buffer = buffer; engine->undo = undo; *output = engine; return CWIKI_SNIPPET_OK;
}

void
cwiki_snippet_engine_free(struct cwiki_snippet_engine *engine)
{
   if (engine == NULL) return;
   while (engine->depth != 0U) { engine->depth--; session_clear(engine, &engine->sessions[engine->depth]); }
   free(engine);
}

static int
add_stop(struct session *session, unsigned number)
{
   unsigned *stops;
   size_t i;
   for (i = 0U; i < session->stop_count; i++) if (session->stops[i] == number) return 0;
   if (session->stop_count == SIZE_MAX / sizeof(*session->stops)) return -1;
   stops = s_realloc(session->stops,
       (session->stop_count + 1U) * sizeof(*session->stops));
   if (stops == NULL) return -1;
   session->stops = stops;
   i = session->stop_count;
   while (i > 0U && session->stops[i - 1U] > number) { session->stops[i] = session->stops[i - 1U]; i--; }
   session->stops[i] = number; session->stop_count++; return 0;
}

static int
register_session(struct cwiki_snippet_engine *engine, struct session *session)
{
   size_t i;
   if (cwiki_buffer_register_position(engine->buffer, &session->extent_start) != 0 ||
       cwiki_buffer_register_position(engine->buffer, &session->extent_end) != 0 ||
       cwiki_buffer_register_position(engine->buffer, &session->final) != 0) return -1;
   for (i = 0U; i < session->occurrence_count; i++) {
      if (cwiki_buffer_register_position(engine->buffer, &session->occurrences[i].start) != 0 ||
          cwiki_buffer_register_position(engine->buffer, &session->occurrences[i].end) != 0) return -1;
   }
   return 0;
}

enum cwiki_snippet_status
cwiki_snippet_expand(struct cwiki_snippet_engine *engine,
    const struct cwiki_snippet_match *match, const char *visual,
    size_t visual_length, uint64_t timestamp, struct cwiki_position *cursor)
{
   const struct definition *definition;
   const struct cwiki_snippet_body *body;
   struct rendered_body rendered = {0};
   struct session *session;
   struct cwiki_position base;
   struct occurrence *outer_primary[CWIKI_SNIPPET_SESSION_MAX_DEPTH] = {0};
   bool outer_extent_at_base[CWIKI_SNIPPET_SESSION_MAX_DEPTH] = {false};
   size_t i, outer;
   enum cwiki_snippet_status status;
   if (engine == NULL || match == NULL || match->definition == NULL || cursor == NULL ||
       (visual == NULL && visual_length != 0U) || !cwiki_utf8_validate(visual, visual_length) ||
       engine->depth >= CWIKI_SNIPPET_SESSION_MAX_DEPTH || match->line >= engine->buffer->line_count ||
       match->start_byte > match->end_byte || match->end_byte > engine->buffer->lines[match->line].length)
      return engine != NULL && engine->depth >= CWIKI_SNIPPET_SESSION_MAX_DEPTH ? CWIKI_SNIPPET_STACK_FULL : CWIKI_SNIPPET_INVALID;
   definition = match->definition;
   /* Matching already selected an exact-zone body; infer it from the definition gate. */
   body = body_for(definition, match->zone);
   if (body == NULL) return CWIKI_SNIPPET_INVALID;
   status = render_body(body, match, &engine->buffer->lines[match->line], visual, visual_length, &rendered);
   if (status != CWIKI_SNIPPET_OK) { rendered_free(&rendered); return status; }
   session = &engine->sessions[engine->depth]; memset(session, 0, sizeof(*session));
   session->occurrences = s_calloc(rendered.occurrence_count, sizeof(*session->occurrences));
   if (session->occurrences == NULL && rendered.occurrence_count != 0U) { rendered_free(&rendered); return allocation_status(); }
   session->occurrence_count = rendered.occurrence_count; base.line = match->line; base.byte = match->start_byte;
   for (i = 0U; i < rendered.occurrence_count; i++) {
      session->occurrences[i].number = rendered.occurrences[i].number;
      session->occurrences[i].transform = rendered.occurrences[i].transform;
      session->occurrences[i].primary = rendered.occurrences[i].primary;
      session->occurrences[i].start = offset_position(base, rendered.bytes, rendered.occurrences[i].start);
      session->occurrences[i].end = offset_position(base, rendered.bytes, rendered.occurrences[i].end);
      if (add_stop(session, rendered.occurrences[i].number) != 0) { free(session->occurrences); free(session->stops); memset(session, 0, sizeof(*session)); rendered_free(&rendered); return allocation_status(); }
   }
   session->extent_start = base;
   session->extent_end = offset_position(base, rendered.bytes, rendered.length);
   session->final = offset_position(base, rendered.bytes, rendered.final);
   for (outer = 0U; outer < engine->depth; outer++) {
      struct session *parent = &engine->sessions[outer];
      outer_extent_at_base[outer] = position_compare(parent->extent_start, base) == 0;
      if (parent->current < parent->stop_count) {
         struct occurrence *primary = primary_for(parent, parent->stops[parent->current]);
         if (primary != NULL && position_compare(primary->start, base) == 0)
            outer_primary[outer] = primary;
      }
   }
   if (cwiki_undo_begin(engine->undo, timestamp) != 0) { session_clear(engine, session); rendered_free(&rendered); return CWIKI_SNIPPET_INVALID; }
   if (cwiki_undo_delete(engine->undo, match->line, match->start_byte, match->end_byte - match->start_byte) != 0 ||
       undo_insert_text(engine->undo, base, rendered.bytes, rendered.length) != 0 || register_session(engine, session) != 0 ||
       cwiki_undo_commit(engine->undo) != 0) {
      session_unregister(engine, session); (void)cwiki_undo_cancel(engine->undo);
      free(session->occurrences); free(session->stops); memset(session, 0, sizeof(*session)); rendered_free(&rendered);
      return allocation_status();
   }
   for (outer = 0U; outer < engine->depth; outer++) {
      if (outer_extent_at_base[outer]) engine->sessions[outer].extent_start = base;
      if (outer_primary[outer] != NULL) outer_primary[outer]->start = base;
   }
   engine->depth++; *cursor = session->stop_count == 0U ? session->final : session->occurrences[0].start;
   if (session->stop_count != 0U) {
      for (i = 0U; i < session->occurrence_count; i++) if (session->occurrences[i].primary && session->occurrences[i].number == session->stops[0]) { *cursor = session->occurrences[i].start; break; }
   }
   rendered_free(&rendered); return CWIKI_SNIPPET_OK;
}

size_t
cwiki_snippet_session_depth(const struct cwiki_snippet_engine *engine)
{
   return engine == NULL ? 0U : engine->depth;
}

static struct occurrence *
primary_for(struct session *session, unsigned number)
{
   size_t i;
   for (i = 0U; i < session->occurrence_count; i++)
      if (session->occurrences[i].primary && session->occurrences[i].number == number) return &session->occurrences[i];
   return NULL;
}

int
cwiki_snippet_current_stop(const struct cwiki_snippet_engine *engine,
    struct cwiki_position *start, struct cwiki_position *end)
{
   struct session *session;
   struct occurrence *primary;
   if (engine == NULL || engine->depth == 0U || start == NULL || end == NULL) { errno = EINVAL; return -1; }
   session = (struct session *)&engine->sessions[engine->depth - 1U];
   if (session->current >= session->stop_count) { *start = session->final; *end = session->final; return 0; }
   primary = primary_for(session, session->stops[session->current]);
   if (primary == NULL) { errno = EINVAL; return -1; }
   *start = primary->start; *end = primary->end; return 0;
}

static void
pop_session(struct cwiki_snippet_engine *engine)
{
   if (engine->depth == 0U) return;
   engine->depth--; session_clear(engine, &engine->sessions[engine->depth]);
}

int
cwiki_snippet_next_stop(struct cwiki_snippet_engine *engine,
    struct cwiki_position *cursor)
{
   struct session *session;
   struct occurrence *primary;
   if (engine == NULL || cursor == NULL || engine->depth == 0U) { errno = EINVAL; return -1; }
   session = &engine->sessions[engine->depth - 1U];
   if (session->current < session->stop_count) session->current++;
   else { pop_session(engine); return 1; }
   if (session->current == session->stop_count) { *cursor = session->final; return 0; }
   primary = primary_for(session, session->stops[session->current]);
   if (primary == NULL) { errno = EINVAL; return -1; }
   *cursor = primary->start; return 0;
}

int
cwiki_snippet_previous_stop(struct cwiki_snippet_engine *engine,
    struct cwiki_position *cursor)
{
   struct session *session;
   struct occurrence *primary;
   if (engine == NULL || cursor == NULL || engine->depth == 0U) { errno = EINVAL; return -1; }
   session = &engine->sessions[engine->depth - 1U];
   if (session->stop_count == 0U) { *cursor = session->final; return 0; }
   if (session->current != 0U) session->current--;
   primary = primary_for(session, session->stops[session->current]);
   if (primary == NULL) { errno = EINVAL; return -1; }
   *cursor = primary->start; return 0;
}

static int
range_bytes(const struct cwiki_buffer *buffer, struct cwiki_position start,
    struct cwiki_position end, char **output, size_t *output_length)
{
   size_t total = 0U, line, offset = 0U;
   char *bytes;
   if (start.line >= buffer->line_count || end.line >= buffer->line_count ||
       position_compare(start, end) > 0 || start.byte > buffer->lines[start.line].length ||
       end.byte > buffer->lines[end.line].length) return -1;
   for (line = start.line; line <= end.line; line++) {
      size_t from = line == start.line ? start.byte : 0U;
      size_t to = line == end.line ? end.byte : buffer->lines[line].length;
      if (to - from > SIZE_MAX - total) return -1;
      total += to - from;
      if (line != end.line) { if (total == SIZE_MAX) return -1; total++; }
   }
   bytes = s_malloc(total + 1U); if (bytes == NULL) return -1;
   for (line = start.line; line <= end.line; line++) {
      size_t from = line == start.line ? start.byte : 0U;
      size_t to = line == end.line ? end.byte : buffer->lines[line].length;
      if (to != from) memcpy(bytes + offset, buffer->lines[line].bytes + from, to - from);
      offset += to - from;
      if (line != end.line) bytes[offset++] = '\n';
   }
   bytes[total] = '\0'; *output = bytes; *output_length = total; return 0;
}

static size_t
relative_offset(const struct cwiki_buffer *buffer, struct cwiki_position base,
    struct cwiki_position point)
{
   size_t line, result = 0U;
   if (base.line == point.line) return point.byte - base.byte;
   result = buffer->lines[base.line].length - base.byte + 1U;
   for (line = base.line + 1U; line < point.line; line++) result += buffer->lines[line].length + 1U;
   return result + point.byte;
}

static enum cwiki_snippet_status
replace_plain(struct cwiki_snippet_engine *engine, struct cwiki_position start,
    struct cwiki_position end, const char *bytes, size_t length, uint64_t timestamp)
{
   if (cwiki_undo_begin(engine->undo, timestamp) != 0) return CWIKI_SNIPPET_INVALID;
   if (undo_delete_range(engine->undo, start, end) != 0 ||
       undo_insert_text(engine->undo, start, bytes, length) != 0 ||
       cwiki_undo_commit(engine->undo) != 0) {
      if (cwiki_undo_transaction_active(engine->undo)) (void)cwiki_undo_cancel(engine->undo);
      return CWIKI_SNIPPET_NO_MEMORY;
   }
   return CWIKI_SNIPPET_OK;
}

enum cwiki_snippet_status
cwiki_snippet_edit(struct cwiki_snippet_engine *engine,
    struct cwiki_position start, struct cwiki_position end, const char *bytes,
    size_t length, uint64_t timestamp, struct cwiki_position *cursor)
{
   struct session *session;
   struct occurrence *primary;
   struct occurrence *saved_occurrences = NULL;
   struct cwiki_position saved_extent_start, saved_extent_end, saved_final;
   char *old = NULL, *updated = NULL;
   char **mirror_bytes = NULL;
   size_t *mirror_lengths = NULL;
   size_t old_length, before, after, updated_length, i;
   enum cwiki_snippet_status status = CWIKI_SNIPPET_OK;
   if (engine == NULL || cursor == NULL || (bytes == NULL && length != 0U) ||
       !cwiki_utf8_validate(bytes, length) || position_compare(start, end) > 0) return CWIKI_SNIPPET_INVALID;
   if (engine->depth == 0U) {
      status = replace_plain(engine, start, end, bytes, length, timestamp);
      if (status == CWIKI_SNIPPET_OK) *cursor = offset_position(start, bytes, length);
      return status;
   }
   session = &engine->sessions[engine->depth - 1U];
   if (session->current >= session->stop_count) { pop_session(engine); return cwiki_snippet_edit(engine, start, end, bytes, length, timestamp, cursor); }
   primary = primary_for(session, session->stops[session->current]);
   if (primary == NULL || position_compare(start, primary->start) < 0 || position_compare(end, primary->end) > 0) {
      pop_session(engine); return cwiki_snippet_edit(engine, start, end, bytes, length, timestamp, cursor);
   }
   if (range_bytes(engine->buffer, primary->start, primary->end, &old, &old_length) != 0) return allocation_status();
   before = relative_offset(engine->buffer, primary->start, start);
   after = old_length - relative_offset(engine->buffer, primary->start, end);
   if (before > SIZE_MAX - length || before + length > SIZE_MAX - after) { free(old); return CWIKI_SNIPPET_NO_MEMORY; }
   updated_length = before + length + after; updated = s_malloc(updated_length + 1U);
   if (updated == NULL) { free(old); return allocation_status(); }
   memcpy(updated, old, before); if (length != 0U) memcpy(updated + before, bytes, length);
   memcpy(updated + before + length, old + old_length - after, after); updated[updated_length] = '\0';
   mirror_bytes = s_calloc(session->occurrence_count, sizeof(*mirror_bytes));
   mirror_lengths = s_calloc(session->occurrence_count, sizeof(*mirror_lengths));
   saved_occurrences = s_malloc(session->occurrence_count * sizeof(*saved_occurrences));
   if (mirror_bytes == NULL || mirror_lengths == NULL || saved_occurrences == NULL) { status = allocation_status(); goto done; }
   memcpy(saved_occurrences, session->occurrences, session->occurrence_count * sizeof(*saved_occurrences));
   saved_extent_start = session->extent_start; saved_extent_end = session->extent_end; saved_final = session->final;
   for (i = 0U; i < session->occurrence_count; i++) {
      struct occurrence *occurrence = &session->occurrences[i];
      if (occurrence == primary || occurrence->number != primary->number) continue;
      if (occurrence->transform == CWIKI_SNIPPET_TRANSFORM_NONE) {
         mirror_bytes[i] = copy_bytes(updated, updated_length); mirror_lengths[i] = updated_length;
         if (mirror_bytes[i] == NULL) { status = allocation_status(); goto done; }
      } else {
         status = cwiki_snippet_transform_apply(occurrence->transform, updated,
             updated_length, &mirror_bytes[i], &mirror_lengths[i]);
         if (status != CWIKI_SNIPPET_OK) goto done;
      }
   }
   engine->updating = true;
   if (cwiki_undo_begin(engine->undo, timestamp) != 0) { status = CWIKI_SNIPPET_INVALID; goto restore_guard; }
   {
      struct cwiki_position primary_start = primary->start;
      if (undo_delete_range(engine->undo, primary->start, primary->end) != 0 ||
          undo_insert_text(engine->undo, primary_start, updated, updated_length) != 0) { status = CWIKI_SNIPPET_NO_MEMORY; goto rollback; }
      primary->start = primary_start; primary->end = offset_position(primary_start, updated, updated_length);
   }
   for (i = session->occurrence_count; i > 0U; i--) {
      struct occurrence *occurrence = &session->occurrences[i - 1U];
      struct cwiki_position occurrence_start;
      if (mirror_bytes[i - 1U] == NULL) continue;
      occurrence_start = occurrence->start;
      if (undo_delete_range(engine->undo, occurrence->start, occurrence->end) != 0 ||
          undo_insert_text(engine->undo, occurrence_start, mirror_bytes[i - 1U], mirror_lengths[i - 1U]) != 0) { status = CWIKI_SNIPPET_NO_MEMORY; goto rollback; }
      occurrence->start = occurrence_start;
      occurrence->end = offset_position(occurrence_start, mirror_bytes[i - 1U], mirror_lengths[i - 1U]);
   }
   if (cwiki_undo_commit(engine->undo) != 0) { status = CWIKI_SNIPPET_NO_MEMORY; goto rollback; }
   session->extent_start = saved_extent_start;
   *cursor = offset_position(primary->start, updated, before + length);
   engine->updating = false; goto done;
rollback:
   if (cwiki_undo_transaction_active(engine->undo)) (void)cwiki_undo_cancel(engine->undo);
   memcpy(session->occurrences, saved_occurrences, session->occurrence_count * sizeof(*saved_occurrences));
   session->extent_start = saved_extent_start; session->extent_end = saved_extent_end; session->final = saved_final;
restore_guard:
   engine->updating = false;
done:
   if (mirror_bytes != NULL) for (i = 0U; i < session->occurrence_count; i++) free(mirror_bytes[i]);
   free(mirror_bytes); free(mirror_lengths); free(saved_occurrences); free(updated); free(old);
   return status;
}

void
cwiki_snippet_cursor_moved(struct cwiki_snippet_engine *engine,
    struct cwiki_position cursor)
{
   struct session *session;
   if (engine == NULL || engine->depth == 0U || engine->updating) return;
   session = &engine->sessions[engine->depth - 1U];
   if (position_compare(cursor, session->extent_start) < 0 ||
       position_compare(cursor, session->extent_end) > 0 ||
       position_compare(cursor, session->final) > 0) pop_session(engine);
}

void
cwiki_snippet_expansion_undone(struct cwiki_snippet_engine *engine)
{
   if (engine != NULL) pop_session(engine);
}
