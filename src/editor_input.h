#ifndef CWIKI_EDITOR_INPUT_H
#define CWIKI_EDITOR_INPUT_H

#include "action.h"
#include "editor.h"
#include "input.h"
#include "keymap.h"

#include <stdint.h>

struct cwiki_editor_input;
struct cwiki_layout_window;
struct cwiki_snippet_registry;
struct cwiki_zone_engine;

struct cwiki_editor_input_context {
   const struct cwiki_layout_window *layout;
   struct cwiki_zone_engine *zones;
   const struct cwiki_motion_viewport *viewport;
   uint64_t timestamp;
};

enum cwiki_editor_status cwiki_editor_input_init(
    struct cwiki_editor_input **input, struct cwiki_editor *editor);
void cwiki_editor_input_free(struct cwiki_editor_input *input);
/* Takes ownership of a complete candidate keymap. */
enum cwiki_editor_status cwiki_editor_input_replace_keymap(
    struct cwiki_editor_input *input, struct cwiki_keymap *candidate);
/* Takes ownership of a complete candidate snippet registry. */
enum cwiki_editor_status cwiki_editor_input_replace_snippets(
    struct cwiki_editor_input *input,
    struct cwiki_snippet_registry *candidate);

enum cwiki_editor_status cwiki_editor_input_handle(
    struct cwiki_editor_input *input, const struct cwiki_input_event *event,
    const struct cwiki_editor_input_context *context);

struct cwiki_action_registry *cwiki_editor_input_actions(
    struct cwiki_editor_input *input);
struct cwiki_keymap *cwiki_editor_input_keymap(struct cwiki_editor_input *input);
/* Returned events remain owned by input and valid until its next handle call. */
const struct cwiki_input_event *cwiki_editor_input_pending(
    const struct cwiki_editor_input *input, size_t *count);

#endif
