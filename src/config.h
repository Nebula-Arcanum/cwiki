#ifndef CWIKI_CONFIG_H
#define CWIKI_CONFIG_H

#include "action.h"
#include "conceal.h"
#include "keymap.h"
#include "snippet.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct cwiki_config;

enum cwiki_config_status {
   CWIKI_CONFIG_OK,
   CWIKI_CONFIG_INVALID,
   CWIKI_CONFIG_YAML_ERROR,
   CWIKI_CONFIG_SCHEMA_ERROR,
   CWIKI_CONFIG_NO_MEMORY
};

struct cwiki_config_error {
   size_t line;
   size_t column;
   size_t source_index;
   char message[160];
};

enum cwiki_save_policy {
   CWIKI_SAVE_INSERT_LEAVE,
   CWIKI_SAVE_MANUAL,
   CWIKI_SAVE_IDLE
};

struct cwiki_config_settings {
   bool conceal;
   uint32_t conceal_categories;
   uint32_t concealcursor_modes;
   bool wrap;
   bool break_indent;
   const char *continuation_marker;
   enum cwiki_save_policy save_policy;
};

struct cwiki_config_zone_table {
   struct cwiki_zone_region *regions;
   size_t count;
   uint64_t top_level;
};

struct cwiki_config_subjects {
   struct cwiki_snippet_subject *items;
   size_t count;
};

/*
 * M1 schema:
 *
 * keymaps:
 *   normal:
 *     - keys: [g, g]
 *       action: motion.document-first  # null unbinds
 * clue-groups:
 *   normal:
 *     - prefix: [g]
 *       label: Go
 * display:
 *   conceal: true
 *   conceal-categories: [accents, greek, math-symbols]
 *   conceal-cursor: nc
 *   wrap: true
 *   break-indent: true
 *   continuation-marker: ""
 * save-policy: insert-leave
 * snippets:
 *   custom.derivative:
 *     trigger: dv
 *     kind: literal
 *     expand: [explicit]
 *     word-boundary: true
 *     priority: 10
 *     subject: calculus
 *     bodies:
 *       math-inline: '\\frac{d$1}{d$2}$0'
 *       math-display: '\\frac{d$1}{d$2}$0'
 *   vimtex.alpha: null  # disable a builtin
 * zones:
 *   custom.my-math:
 *     kind: math-display
 *     start: '\\begin\\{my-math\\}'
 *     end: '\\end\\{my-math\\}'
 *     top-level: true
 *     parents: [latex-environment]
 *     contains: [note-inline-comment, latex-line-comment]
 */
enum cwiki_config_status cwiki_config_parse(struct cwiki_config **config,
    const unsigned char *bytes, size_t length,
    const struct cwiki_action_registry *actions,
    struct cwiki_config_error *error);
void cwiki_config_free(struct cwiki_config *config);

/* Clone base, apply all bindings, and publish only the complete candidate. */
enum cwiki_config_status cwiki_config_build_keymap(
    const struct cwiki_config *config, const struct cwiki_keymap *base,
    struct cwiki_keymap **candidate, struct cwiki_config_error *error);

/* Compose ordered scopes and publish one complete owned registry. */
enum cwiki_config_status cwiki_config_build_snippets(
    const struct cwiki_config *const *configs, size_t config_count,
    struct cwiki_snippet_registry **candidate,
    struct cwiki_config_error *error);

/* Builtins plus effective custom regions. The engine borrows this table. */
enum cwiki_config_status cwiki_config_build_zones(
    const struct cwiki_config *const *configs, size_t config_count,
    struct cwiki_config_zone_table *table,
    struct cwiki_config_error *error);
void cwiki_config_zone_table_free(struct cwiki_config_zone_table *table);

/* Parse only M1's note-scoped subject list from opening YAML frontmatter. */
enum cwiki_config_status cwiki_config_parse_note_subjects(
    const struct cwiki_buffer *buffer, struct cwiki_config_subjects *subjects,
    struct cwiki_config_error *error);
void cwiki_config_subjects_free(struct cwiki_config_subjects *subjects);

const char *cwiki_config_clue_group(const struct cwiki_config *config,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count);

void cwiki_config_settings_defaults(struct cwiki_config_settings *settings);
/* Apply only values explicitly present in config. String storage is config-owned. */
void cwiki_config_apply_settings(const struct cwiki_config *config,
    struct cwiki_config_settings *settings);

#endif
