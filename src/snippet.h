#ifndef CWIKI_SNIPPET_H
#define CWIKI_SNIPPET_H

#include "buffer.h"
#include "regex.h"
#include "zone.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CWIKI_SNIPPET_SESSION_MAX_DEPTH 8U
#define CWIKI_SNIPPET_DEFAULT_MATCH_LIMIT 100000U
#define CWIKI_SNIPPET_DEFAULT_DEPTH_LIMIT 1000U

struct cwiki_undo;
struct cwiki_snippet_body;
struct cwiki_snippet_registry;
struct cwiki_snippet_engine;

enum cwiki_snippet_status {
   CWIKI_SNIPPET_OK,
   CWIKI_SNIPPET_INVALID,
   CWIKI_SNIPPET_NO_MEMORY,
   CWIKI_SNIPPET_REGEX_ERROR,
   CWIKI_SNIPPET_NO_MATCH,
   CWIKI_SNIPPET_LIMIT_DISABLED,
   CWIKI_SNIPPET_STACK_FULL
};

enum cwiki_snippet_trigger_kind {
   CWIKI_SNIPPET_LITERAL,
   CWIKI_SNIPPET_REGEX
};

enum cwiki_snippet_expand_kind {
   CWIKI_SNIPPET_AUTO,
   CWIKI_SNIPPET_EXPLICIT
};

enum cwiki_snippet_input_flags {
   CWIKI_SNIPPET_INPUT_NONE = 0U,
   CWIKI_SNIPPET_INPUT_PASTE = 1U << 0
};

enum cwiki_snippet_trigger_flags {
   CWIKI_SNIPPET_TRIGGER_AUTO = 1U << 0,
   CWIKI_SNIPPET_TRIGGER_EXPLICIT = 1U << 1,
   CWIKI_SNIPPET_TRIGGER_WORD_BOUNDARY = 1U << 2,
   CWIKI_SNIPPET_TRIGGER_BEGINNING_OF_LINE = 1U << 3
};

enum cwiki_snippet_transform {
   CWIKI_SNIPPET_TRANSFORM_NONE,
   CWIKI_SNIPPET_TRANSFORM_UPPER,
   CWIKI_SNIPPET_TRANSFORM_LOWER,
   CWIKI_SNIPPET_TRANSFORM_CAPITALIZE,
   CWIKI_SNIPPET_TRANSFORM_MATCH_BRACKET_BACKWARD,
   CWIKI_SNIPPET_TRANSFORM_SPACE_UNLESS_PUNCTUATION
};

struct cwiki_snippet_body_spec {
   enum cwiki_zone_kind zone;
   const char *body;
   size_t body_length;
};

struct cwiki_snippet_spec {
   enum cwiki_snippet_trigger_kind kind;
   const char *trigger;
   size_t trigger_length;
   enum cwiki_zone_kind required_zone;
   const struct cwiki_snippet_body_spec *bodies;
   size_t body_count;
   const char *subject; /* NULL means the global layer. */
   size_t subject_length;
   uint32_t flags;
   int priority;
   uint32_t match_limit;
   uint32_t depth_limit;
};

struct cwiki_snippet_match {
   const void *definition;
   enum cwiki_zone_kind zone;
   size_t line;
   size_t start_byte;
   size_t end_byte;
   struct cwiki_regex_capture *captures;
   size_t capture_count;
};

/*
 * The transform set is deliberately closed. upper/lower use utf8proc's simple
 * one-codepoint Unicode mappings (no locale and no multi-codepoint expansion).
 * capitalize uppercases the first cased codepoint and lowercases subsequent
 * cased codepoints. match-bracket-backward accepts exactly one ASCII closing
 * bracket and returns its ASCII opener: ')', ']' or '}' -> '(', '[' or '{'.
 * space-unless-punctuation returns one ASCII space unless the input's final
 * codepoint is ASCII punctuation, in which case it returns an empty string.
 */
enum cwiki_snippet_status cwiki_snippet_transform_apply(
    enum cwiki_snippet_transform transform, const char *input,
    size_t input_length, char **output, size_t *output_length);

enum cwiki_snippet_status cwiki_snippet_body_compile(
    struct cwiki_snippet_body **body, const char *source, size_t length,
    size_t capture_count);
void cwiki_snippet_body_free(struct cwiki_snippet_body *body);
bool cwiki_snippet_body_valid(const struct cwiki_snippet_body *body,
    size_t capture_count);

enum cwiki_snippet_status cwiki_snippet_registry_init(
    struct cwiki_snippet_registry **registry);
void cwiki_snippet_registry_free(struct cwiki_snippet_registry *registry);
enum cwiki_snippet_status cwiki_snippet_registry_add(
    struct cwiki_snippet_registry *registry,
    const struct cwiki_snippet_spec *spec);
enum cwiki_snippet_status cwiki_snippet_registry_set_subject(
    struct cwiki_snippet_registry *registry, const char *subject,
    size_t subject_length);
enum cwiki_snippet_status cwiki_snippet_match(
    struct cwiki_snippet_registry *registry, struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *buffer, struct cwiki_position cursor,
    enum cwiki_snippet_expand_kind kind, uint32_t input_flags,
    struct cwiki_snippet_match *match);
void cwiki_snippet_match_free(struct cwiki_snippet_match *match);
/* Returns true once for each regex disabled by a resource limit. */
bool cwiki_snippet_take_limit_diagnostic(
    struct cwiki_snippet_registry *registry);

enum cwiki_snippet_status cwiki_snippet_engine_init(
    struct cwiki_snippet_engine **engine, struct cwiki_buffer *buffer,
    struct cwiki_undo *undo);
void cwiki_snippet_engine_free(struct cwiki_snippet_engine *engine);
enum cwiki_snippet_status cwiki_snippet_expand(
    struct cwiki_snippet_engine *engine,
    const struct cwiki_snippet_match *match, const char *visual,
    size_t visual_length, uint64_t timestamp, struct cwiki_position *cursor);
size_t cwiki_snippet_session_depth(const struct cwiki_snippet_engine *engine);
int cwiki_snippet_current_stop(const struct cwiki_snippet_engine *engine,
    struct cwiki_position *start, struct cwiki_position *end);
int cwiki_snippet_next_stop(struct cwiki_snippet_engine *engine,
    struct cwiki_position *cursor);
int cwiki_snippet_previous_stop(struct cwiki_snippet_engine *engine,
    struct cwiki_position *cursor);
enum cwiki_snippet_status cwiki_snippet_edit(
    struct cwiki_snippet_engine *engine, struct cwiki_position start,
    struct cwiki_position end, const char *bytes, size_t length,
    uint64_t timestamp, struct cwiki_position *cursor);
/* Adds the edit to the caller's active undo transaction. */
enum cwiki_snippet_status cwiki_snippet_edit_pending(
    struct cwiki_snippet_engine *engine, struct cwiki_position start,
    struct cwiki_position end, const char *bytes, size_t length,
    struct cwiki_position *cursor);
void cwiki_snippet_cursor_moved(struct cwiki_snippet_engine *engine,
    struct cwiki_position cursor);
void cwiki_snippet_expansion_undone(struct cwiki_snippet_engine *engine);
/* History traversal must clear sessions: range boundary bias is not reversible. */
void cwiki_snippet_clear_sessions(struct cwiki_snippet_engine *engine);

#ifdef CWIKI_SNIPPET_TESTING
void cwiki_snippet_test_fail_allocation_after(size_t successful_allocations);
void cwiki_snippet_test_reset_allocation(void);
#endif

#endif
