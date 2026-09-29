#ifndef CWIKI_EDITOR_H
#define CWIKI_EDITOR_H

#include "document.h"
#include "motion.h"
#include "undo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cwiki_layout_window;
struct cwiki_zone_engine;

enum cwiki_editor_mode {
   CWIKI_EDITOR_NORMAL,
   CWIKI_EDITOR_INSERT,
   CWIKI_EDITOR_REPLACE,
   CWIKI_EDITOR_COMMAND
};

enum cwiki_editor_operator {
   CWIKI_EDITOR_NO_OPERATOR,
   CWIKI_EDITOR_DELETE,
   CWIKI_EDITOR_CHANGE,
   CWIKI_EDITOR_YANK
};

enum cwiki_editor_status {
   CWIKI_EDITOR_OK,
   CWIKI_EDITOR_INVALID,
   CWIKI_EDITOR_NO_MEMORY,
   CWIKI_EDITOR_NOTHING,
   CWIKI_EDITOR_DIRTY,
   CWIKI_EDITOR_SAVE_FAILED
};

struct cwiki_editor_yank {
   char *bytes;
   size_t length;
   bool linewise;
};

struct cwiki_editor {
   struct cwiki_document *document;
   struct cwiki_undo undo;
   struct cwiki_motion_state motion;
   struct cwiki_editor_yank yank;
   char *command;
   size_t command_length;
   size_t command_capacity;
   uint64_t pending_timestamp;
   enum cwiki_editor_mode mode;
   enum cwiki_editor_operator pending_operator;
   bool quit_requested;
};

enum cwiki_editor_status cwiki_editor_init(struct cwiki_editor *editor,
    struct cwiki_document *document);
void cwiki_editor_free(struct cwiki_editor *editor);

enum cwiki_editor_status cwiki_editor_enter_insert(struct cwiki_editor *editor,
    bool append, uint64_t timestamp);
enum cwiki_editor_status cwiki_editor_enter_replace(
    struct cwiki_editor *editor, uint64_t timestamp);
enum cwiki_editor_status cwiki_editor_escape(struct cwiki_editor *editor);
enum cwiki_editor_status cwiki_editor_insert(struct cwiki_editor *editor,
    const char *bytes, size_t length);
enum cwiki_editor_status cwiki_editor_enter(struct cwiki_editor *editor);
enum cwiki_editor_status cwiki_editor_backspace(struct cwiki_editor *editor);

enum cwiki_editor_status cwiki_editor_start_operator(
    struct cwiki_editor *editor, enum cwiki_editor_operator operator_kind,
    uint64_t timestamp);
enum cwiki_editor_status cwiki_editor_apply_motion(
    struct cwiki_editor *editor, const struct cwiki_layout_window *layout,
    struct cwiki_zone_engine *zones, enum cwiki_motion motion,
    const struct cwiki_motion_viewport *viewport);
enum cwiki_editor_status cwiki_editor_put(struct cwiki_editor *editor,
    bool before, uint64_t timestamp);

enum cwiki_editor_status cwiki_editor_undo(struct cwiki_editor *editor);
enum cwiki_editor_status cwiki_editor_redo(struct cwiki_editor *editor);
enum cwiki_editor_status cwiki_editor_older(struct cwiki_editor *editor);
enum cwiki_editor_status cwiki_editor_newer(struct cwiki_editor *editor);

enum cwiki_editor_status cwiki_editor_begin_command(
    struct cwiki_editor *editor);
enum cwiki_editor_status cwiki_editor_command_insert(
    struct cwiki_editor *editor, const char *bytes, size_t length);
enum cwiki_editor_status cwiki_editor_command_backspace(
    struct cwiki_editor *editor);
enum cwiki_editor_status cwiki_editor_execute_command(
    struct cwiki_editor *editor);

#endif
