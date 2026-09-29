#ifndef CWIKI_CONFIG_H
#define CWIKI_CONFIG_H

#include "action.h"
#include "keymap.h"

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
   char message[160];
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

const char *cwiki_config_clue_group(const struct cwiki_config *config,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count);

#endif
