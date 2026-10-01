#include "app.h"

#include "clue_render.h"
#include "conceal.h"
#include "config.h"
#include "editor_input.h"
#include "float.h"
#include "highlight.h"
#include "key_record.h"
#include "layout.h"
#include "picker.h"
#include "picker_render.h"
#include "render.h"
#include "terminal.h"
#include "zone.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define SAVE_IDLE_NS UINT64_C(1000000000)

enum app_action_id {
   APP_ACTION_TOGGLE_WRAP,
   APP_ACTION_TOGGLE_NUMBERS,
   APP_ACTION_TOGGLE_CURSORLINE,
   APP_ACTION_TOGGLE_CONCEAL,
   APP_ACTION_LIST_SNIPPETS,
   APP_ACTION_LIST_UNDO,
   APP_ACTION_PICKER_CLOSE,
   APP_ACTION_PICKER_NEXT,
   APP_ACTION_PICKER_PREVIOUS,
   APP_ACTION_PICKER_LINE_START,
   APP_ACTION_PICKER_LINE_BACKWARD,
   APP_ACTION_PICKER_LINE_DELETE,
   APP_ACTION_PICKER_LINE_END,
   APP_ACTION_PICKER_LINE_FORWARD,
   APP_ACTION_PICKER_BACKSPACE,
   APP_ACTION_PICKER_KILL_END,
   APP_ACTION_PICKER_KILL_WORD,
   APP_ACTION_COUNT
};

struct app_action_context {
   struct app *app;
   enum app_action_id id;
};

struct app {
   struct cwiki_document document;
   struct cwiki_editor editor;
   struct cwiki_editor_input *input;
   struct cwiki_zone_engine *zones;
   struct cwiki_conceal_table conceal;
   struct cwiki_layout_window layout;
   struct cwiki_render_viewport viewport;
   struct cwiki_motion_viewport motion_viewport;
   struct cwiki_config_settings settings;
   struct cwiki_config *configs[CWIKI_CONFIG_SOURCE_MAX];
   size_t config_count;
   struct cwiki_config_error config_error;
   enum cwiki_config_status config_status;
   const char *config_path;
   struct cwiki_picker *picker;
   struct cwiki_input_event picker_pending[CWIKI_KEYMAP_MAX_SEQUENCE];
   size_t picker_pending_count;
   uint64_t last_input_ns;
   struct app_action_context action_contexts[APP_ACTION_COUNT];
   const char *status_message;
   int error;
   unsigned int statuses;
   bool snippet_overlay;
};

struct undo_picker_text {
   char id[32];
   char label[48];
   char detail[128];
};

#ifdef CWIKI_APP_TESTING
static ssize_t (*frame_write)(int, const void *, size_t) = write;

void
cwiki_app_test_set_write(ssize_t (*write_fn)(int, const void *, size_t))
{
   frame_write = write_fn == NULL ? write : write_fn;
}
#else
#define frame_write write
#endif

static int
refresh(struct app *app)
{
   struct cwiki_layout_options options = {0};
   size_t gutter;
   size_t row;
   size_t column;
   size_t height = app->viewport.rows > 1U ? app->viewport.rows - 1U : 1U;

   app->viewport.line_numbers = app->settings.line_numbers;
   app->viewport.cursorline = app->settings.cursorline;
   app->viewport.status_message = app->status_message;
   gutter = app->viewport.line_numbers ? cwiki_render_line_number_width(
       app->document.buffer.line_count, app->viewport.columns) : 0U;
   options.content_width = app->viewport.columns - gutter;
   options.wrap = app->settings.wrap;
   options.break_indent = app->settings.break_indent;
   options.continuation_marker = app->settings.continuation_marker;
   options.conceal_categories = app->settings.module_syntax &&
       app->settings.conceal ?
       app->settings.conceal_categories : 0U;
   options.concealcursor_modes = app->settings.concealcursor_modes;
   options.mode = app->editor.mode == CWIKI_EDITOR_COMMAND ?
       CWIKI_CONCEAL_MODE_COMMAND :
       (app->editor.mode == CWIKI_EDITOR_NORMAL ? CWIKI_CONCEAL_MODE_NORMAL :
       CWIKI_CONCEAL_MODE_INSERT);
   options.cursor = app->editor.motion.cursor;
   options.reveal_line = app->editor.reveal_line;
   options.reveal = app->editor.reveal;
   /* ponytail: rebuild after every event; incremental caching can follow profiling. */
   for (size_t line = 0U; line < app->document.buffer.line_count; line++) {
      if (app->document.buffer.lines[line].zone_dirty &&
          cwiki_zone_recompute(app->zones, &app->document.buffer, line, NULL) != 0) {
         return -1;
      }
   }
   if (cwiki_layout_rebuild(&app->layout, &app->document.buffer, app->zones,
       &app->conceal, &options) != 0 ||
       cwiki_layout_source_to_display(&app->layout, &app->document.buffer,
       options.cursor, &row, &column) != 0) {
      return -1;
   }
   if (row < app->viewport.first_row) {
      app->viewport.first_row = row;
   } else if (row - app->viewport.first_row >= height) {
      app->viewport.first_row = row - height + 1U;
   }
   if (column < app->viewport.horizontal_offset) {
      app->viewport.horizontal_offset = column;
   } else if (column - app->viewport.horizontal_offset >= options.content_width) {
      app->viewport.horizontal_offset = column - options.content_width + 1U;
   }
   app->motion_viewport.first_display_row = app->viewport.first_row;
   app->motion_viewport.last_display_row = app->viewport.first_row + height - 1U;
   if (app->motion_viewport.last_display_row >= app->layout.row_count) {
      app->motion_viewport.last_display_row = app->layout.row_count - 1U;
   }
   return 0;
}

static enum cwiki_picker_status
dispatch_picker_match(struct app *app, const struct cwiki_keymap_match *match)
{
   int action_result = (int)CWIKI_EDITOR_INVALID;
   enum cwiki_action_status action_status = cwiki_action_dispatch(
       cwiki_editor_input_actions(app->input), match->action.name, NULL,
       &action_result);

   if (action_status == CWIKI_ACTION_NO_MEMORY ||
       action_result == (int)CWIKI_EDITOR_NO_MEMORY) {
      return CWIKI_PICKER_NO_MEMORY;
   }
   return action_status == CWIKI_ACTION_OK &&
       action_result == (int)CWIKI_EDITOR_OK ?
       CWIKI_PICKER_OK : CWIKI_PICKER_INVALID;
}

static void
event(const struct cwiki_input_event *event_value, void *context)
{
   struct app *app = context;
   struct timespec now;
   struct cwiki_editor_input_context input_context = {0};
   enum cwiki_editor_mode before = app->editor.mode;
   enum cwiki_editor_status status;

   if (app->error != 0 || app->editor.quit_requested) {
      return;
   }
   if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      app->error = errno;
      return;
   }
   if (app->picker != NULL) {
      enum cwiki_picker_status picker_status = CWIKI_PICKER_NOT_HANDLED;

      if (event_value->kind == CWIKI_INPUT_PASTE) {
         picker_status = cwiki_picker_insert(app->picker,
             (const char *)event_value->text, event_value->text_len);
      } else if (event_value->kind == CWIKI_INPUT_KEY &&
          event_value->action != CWIKI_INPUT_RELEASE) {
         struct cwiki_keymap_match match;

         if (app->picker_pending_count == CWIKI_KEYMAP_MAX_SEQUENCE) {
            picker_status = CWIKI_PICKER_INVALID;
            app->picker_pending_count = 0U;
         } else {
            app->picker_pending[app->picker_pending_count] = *event_value;
            app->picker_pending[app->picker_pending_count].text = NULL;
            app->picker_pending[app->picker_pending_count].text_len = 0U;
            app->picker_pending_count++;
            if (cwiki_keymap_match(cwiki_editor_input_keymap(app->input),
                CWIKI_KEYMAP_PICKER, app->picker_pending,
                app->picker_pending_count, &match) != CWIKI_KEYMAP_OK) {
               picker_status = CWIKI_PICKER_INVALID;
               app->picker_pending_count = 0U;
            } else if (match.kind == CWIKI_KEYMAP_PREFIX) {
               picker_status = CWIKI_PICKER_OK;
            } else if (match.kind == CWIKI_KEYMAP_COMPLETE) {
               app->picker_pending_count = 0U;
               picker_status = dispatch_picker_match(app, &match);
            } else {
               bool had_prefix = app->picker_pending_count > 1U;

               app->picker_pending_count = 0U;
               if (had_prefix) {
                  app->picker_pending[0] = *event_value;
                  app->picker_pending[0].text = NULL;
                  app->picker_pending[0].text_len = 0U;
                  app->picker_pending_count = 1U;
                  if (cwiki_keymap_match(
                      cwiki_editor_input_keymap(app->input),
                      CWIKI_KEYMAP_PICKER, app->picker_pending, 1U,
                      &match) != CWIKI_KEYMAP_OK) {
                     picker_status = CWIKI_PICKER_INVALID;
                     app->picker_pending_count = 0U;
                  } else if (match.kind == CWIKI_KEYMAP_PREFIX) {
                     picker_status = CWIKI_PICKER_OK;
                  } else if (match.kind == CWIKI_KEYMAP_COMPLETE) {
                     app->picker_pending_count = 0U;
                     picker_status = dispatch_picker_match(app, &match);
                  } else {
                     app->picker_pending_count = 0U;
                     if (event_value->text_len != 0U) {
                        picker_status = cwiki_picker_insert(app->picker,
                            (const char *)event_value->text,
                            event_value->text_len);
                     }
                  }
               } else if (event_value->text_len != 0U) {
                  picker_status = cwiki_picker_insert(app->picker,
                      (const char *)event_value->text, event_value->text_len);
               }
            }
         }
      }
      app->last_input_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
          (uint64_t)now.tv_nsec;
      if (picker_status == CWIKI_PICKER_NO_MEMORY) {
         app->error = ENOMEM;
      } else if (picker_status == CWIKI_PICKER_INVALID) {
         app->error = EINVAL;
      }
      return;
   }
   app->snippet_overlay = false;
   app->status_message = NULL;
   app->viewport.status_message = NULL;
   input_context.layout = &app->layout;
   input_context.zones = app->zones;
   input_context.viewport = &app->motion_viewport;
   input_context.timestamp = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
       (uint64_t)now.tv_nsec;
   input_context.tabstop = app->settings.tabstop;
   input_context.expandtab = app->settings.expandtab;
   app->last_input_ns = input_context.timestamp;
   status = cwiki_editor_input_handle(app->input, event_value, &input_context);
   if (status == CWIKI_EDITOR_OK &&
       (before == CWIKI_EDITOR_INSERT || before == CWIKI_EDITOR_REPLACE) &&
       app->editor.mode == CWIKI_EDITOR_NORMAL && app->document.dirty &&
       app->settings.save_policy == CWIKI_SAVE_INSERT_LEAVE) {
      status = cwiki_editor_save(&app->editor);
   }
   app->statuses |= 1U << (unsigned int)status;
   if (status == CWIKI_EDITOR_NO_MEMORY) {
      app->error = ENOMEM;
   } else {
      if (status == CWIKI_EDITOR_SAVE_FAILED) {
         app->status_message = "save failed; buffer remains dirty";
      } else if (status == CWIKI_EDITOR_DURABILITY_UNCERTAIN) {
         app->status_message = "durability uncertain; buffer remains dirty";
      }
      if (refresh(app) != 0) {
         app->error = errno;
      }
   }
}

static bool
app_action_available(void *context)
{
   const struct app_action_context *action = context;

   if (action->id >= APP_ACTION_PICKER_CLOSE) {
      return action->app->picker != NULL;
   }
   if (action->app->editor.mode != CWIKI_EDITOR_NORMAL ||
       action->app->picker != NULL) {
      return false;
   }
   if (action->id == APP_ACTION_TOGGLE_CONCEAL) {
      return action->app->settings.module_syntax;
   }
   if (action->id == APP_ACTION_LIST_SNIPPETS) {
      return action->app->settings.module_snippets;
   }
   if (action->id == APP_ACTION_LIST_UNDO) {
      return action->app->settings.module_picker;
   }
   return true;
}

static enum cwiki_picker_status
open_undo_picker(struct app *app)
{
   size_t count = cwiki_undo_state_count(&app->editor.undo);
   struct cwiki_picker_spec *specs = calloc(count, sizeof(*specs));
   struct undo_picker_text *text = calloc(count, sizeof(*text));
   enum cwiki_picker_status status = CWIKI_PICKER_NO_MEMORY;

   if (specs == NULL || text == NULL) {
      goto done;
   }
   for (size_t i = 0U; i < count; i++) {
      struct cwiki_undo_state_info info;
      size_t sequence = count - i - 1U;
      int id_length;
      int label_length;
      int detail_length;

      if (cwiki_undo_state_info(&app->editor.undo, sequence, &info) != 0) {
         status = CWIKI_PICKER_INVALID;
         goto done;
      }
      id_length = snprintf(text[i].id, sizeof(text[i].id), "%" PRIu64,
          info.sequence);
      label_length = snprintf(text[i].label, sizeof(text[i].label),
          "%sstate %" PRIu64, info.current ? "* " : "", info.sequence);
      detail_length = snprintf(text[i].detail, sizeof(text[i].detail),
          "timestamp %" PRIu64 " ns · parent %" PRIu64 " · %zu children",
          info.timestamp, info.parent_sequence, info.child_count);
      if (id_length < 0 || (size_t)id_length >= sizeof(text[i].id) ||
          label_length < 0 ||
          (size_t)label_length >= sizeof(text[i].label) ||
          detail_length < 0 ||
          (size_t)detail_length >= sizeof(text[i].detail)) {
         status = CWIKI_PICKER_INVALID;
         goto done;
      }
      specs[i] = (struct cwiki_picker_spec){
         text[i].id, text[i].label, text[i].detail
      };
   }
   cwiki_picker_free(app->picker);
   app->picker = NULL;
   status = cwiki_picker_init(&app->picker, specs, count);
   app->picker_pending_count = 0U;
done:
   free(text);
   free(specs);
   return status;
}

static int
handle_app_action(void *context, const struct cwiki_action_argument *argument)
{
   struct app_action_context *action = context;
   struct app *app = action->app;

   (void)argument;
   switch (action->id) {
   case APP_ACTION_TOGGLE_WRAP:
      app->settings.wrap = !app->settings.wrap;
      app->status_message = app->settings.wrap ? "wrap on" : "wrap off";
      break;
   case APP_ACTION_TOGGLE_NUMBERS:
      app->settings.line_numbers = !app->settings.line_numbers;
      app->status_message = app->settings.line_numbers ?
          "numbers on" : "numbers off";
      break;
   case APP_ACTION_TOGGLE_CURSORLINE:
      app->settings.cursorline = !app->settings.cursorline;
      app->status_message = app->settings.cursorline ?
          "cursorline on" : "cursorline off";
      break;
   case APP_ACTION_TOGGLE_CONCEAL:
      app->settings.conceal = !app->settings.conceal;
      app->status_message = app->settings.conceal ?
          "conceal on" : "conceal off";
      break;
   case APP_ACTION_LIST_SNIPPETS:
      app->snippet_overlay = true;
      break;
   case APP_ACTION_LIST_UNDO:
      switch (open_undo_picker(app)) {
      case CWIKI_PICKER_OK:
         break;
      case CWIKI_PICKER_NO_MEMORY:
         return (int)CWIKI_EDITOR_NO_MEMORY;
      case CWIKI_PICKER_INVALID:
      case CWIKI_PICKER_NOT_HANDLED:
      case CWIKI_PICKER_NOT_FOUND:
         return (int)CWIKI_EDITOR_INVALID;
      }
      break;
   case APP_ACTION_PICKER_CLOSE:
      cwiki_picker_free(app->picker);
      app->picker = NULL;
      app->picker_pending_count = 0U;
      break;
   case APP_ACTION_PICKER_NEXT:
      return (int)(cwiki_picker_select_next(app->picker) ==
          CWIKI_PICKER_INVALID ? CWIKI_EDITOR_INVALID : CWIKI_EDITOR_OK);
   case APP_ACTION_PICKER_PREVIOUS:
      return (int)(cwiki_picker_select_previous(app->picker) ==
          CWIKI_PICKER_INVALID ? CWIKI_EDITOR_INVALID : CWIKI_EDITOR_OK);
   case APP_ACTION_PICKER_LINE_START:
   case APP_ACTION_PICKER_LINE_BACKWARD:
   case APP_ACTION_PICKER_LINE_DELETE:
   case APP_ACTION_PICKER_LINE_END:
   case APP_ACTION_PICKER_LINE_FORWARD:
   case APP_ACTION_PICKER_BACKSPACE:
   case APP_ACTION_PICKER_KILL_END:
   case APP_ACTION_PICKER_KILL_WORD: {
      static const uint32_t controls[] = {1U, 2U, 4U, 5U, 6U, 8U, 11U, 23U};
      size_t index = (size_t)(action->id - APP_ACTION_PICKER_LINE_START);
      enum cwiki_picker_status status = cwiki_picker_control(app->picker,
          controls[index]);

      return (int)(status == CWIKI_PICKER_OK ? CWIKI_EDITOR_OK :
          (status == CWIKI_PICKER_NO_MEMORY ? CWIKI_EDITOR_NO_MEMORY :
          CWIKI_EDITOR_INVALID));
   }
   case APP_ACTION_COUNT:
      return (int)CWIKI_EDITOR_INVALID;
   }
   return (int)CWIKI_EDITOR_OK;
}

static int
register_app_actions(struct app *app)
{
   static const struct {
      const char *name;
      const char *label;
      const char *description;
   } definitions[] = {
      {"option.toggle-wrap", "Toggle wrap", "Toggle soft wrap"},
      {"option.toggle-line-numbers", "Toggle numbers",
          "Toggle relative line numbers"},
      {"option.toggle-cursorline", "Toggle cursorline",
          "Toggle the smart cursorline"},
      {"option.toggle-conceal", "Toggle conceal",
          "Toggle source conceal substitutions"},
      {"snippet.list-active", "Active snippets",
          "List snippets active at the cursor"},
      {"history.list-branches", "Undo history",
          "List retained undo states and branch timestamps"},
      {"picker.close", "Close picker", "Close the active picker"},
      {"picker.next", "Next picker item", "Select the next picker item"},
      {"picker.previous", "Previous picker item",
          "Select the previous picker item"},
      {"picker.line-start", "Picker line start",
          "Move to the start of the picker query"},
      {"picker.line-backward", "Picker backward",
          "Move left in the picker query"},
      {"picker.line-delete", "Picker delete",
          "Delete at the picker query cursor"},
      {"picker.line-end", "Picker line end",
          "Move to the end of the picker query"},
      {"picker.line-forward", "Picker forward",
          "Move right in the picker query"},
      {"picker.backspace", "Picker backspace",
          "Delete before the picker query cursor"},
      {"picker.kill-end", "Picker kill to end",
          "Delete the picker query through its end"},
      {"picker.kill-word", "Picker kill previous word",
          "Delete the previous picker query word"}
   };
   static const struct {
      uint32_t key;
      unsigned int modifiers;
      const char *action;
   } picker_bindings[] = {
      {27U, 0U, "picker.close"},
      {13U, 0U, "picker.close"},
      {10U, 0U, "picker.close"},
      {'n', CWIKI_INPUT_CTRL, "picker.next"},
      {'p', CWIKI_INPUT_CTRL, "picker.previous"},
      {'a', CWIKI_INPUT_CTRL, "picker.line-start"},
      {'b', CWIKI_INPUT_CTRL, "picker.line-backward"},
      {'d', CWIKI_INPUT_CTRL, "picker.line-delete"},
      {'e', CWIKI_INPUT_CTRL, "picker.line-end"},
      {'f', CWIKI_INPUT_CTRL, "picker.line-forward"},
      {'h', CWIKI_INPUT_CTRL, "picker.backspace"},
      {127U, 0U, "picker.backspace"},
      {8U, 0U, "picker.backspace"},
      {'k', CWIKI_INPUT_CTRL, "picker.kill-end"},
      {'w', CWIKI_INPUT_CTRL, "picker.kill-word"}
   };
   static const char leader_keys[] = {'w', 'n', 'l', 'c', 's', 'u'};
   struct cwiki_action_registry *actions =
       cwiki_editor_input_actions(app->input);
   struct cwiki_keymap *keymap = cwiki_editor_input_keymap(app->input);

   for (size_t i = 0U; i < APP_ACTION_COUNT; i++) {
      struct cwiki_action_spec spec = {
         definitions[i].name, definitions[i].label, definitions[i].description,
         CWIKI_ACTION_PARAMETER_NONE, NULL, handle_app_action,
         app_action_available, &app->action_contexts[i]
      };

      app->action_contexts[i].app = app;
      app->action_contexts[i].id = (enum app_action_id)i;
      if (cwiki_action_register(actions, &spec) != CWIKI_ACTION_OK) {
         return -1;
      }
   }
   for (size_t i = 0U; i < sizeof(leader_keys); i++) {
      struct cwiki_key_sequence sequence = {
         {{'\\', 0U}, {(uint32_t)leader_keys[i], 0U}}, 2U
      };

      if (cwiki_keymap_bind(keymap, CWIKI_KEYMAP_NORMAL, &sequence,
          definitions[i].name) != CWIKI_KEYMAP_OK) {
         return -1;
      }
   }
   for (size_t i = 0U; i < sizeof(picker_bindings) /
       sizeof(picker_bindings[0]); i++) {
      struct cwiki_key_sequence sequence = {
         {{picker_bindings[i].key, picker_bindings[i].modifiers}}, 1U
      };

      if (cwiki_keymap_bind(keymap, CWIKI_KEYMAP_PICKER, &sequence,
          picker_bindings[i].action) != CWIKI_KEYMAP_OK) {
         return -1;
      }
   }
   return 0;
}

static bool
idle_save_due(const struct app *app, uint64_t now)
{
   bool eligible = app->settings.save_policy == CWIKI_SAVE_IDLE ||
       (app->settings.save_policy == CWIKI_SAVE_INSERT_LEAVE &&
       (app->editor.mode == CWIKI_EDITOR_INSERT ||
       app->editor.mode == CWIKI_EDITOR_REPLACE));

   return eligible && app->document.dirty && now >= app->last_input_ns &&
       now - app->last_input_ns >= SAVE_IDLE_NS;
}

static void
save_if_idle(struct app *app)
{
   struct timespec now;
   enum cwiki_editor_status status;
   uint64_t timestamp;

   if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      app->error = errno;
      return;
   }
   timestamp = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
       (uint64_t)now.tv_nsec;
   if (!idle_save_due(app, timestamp)) {
      return;
   }
   app->last_input_ns = timestamp;
   status = cwiki_editor_save(&app->editor);
   app->statuses |= 1U << (unsigned int)status;
   if (status == CWIKI_EDITOR_NO_MEMORY) {
      app->error = ENOMEM;
   } else if (status == CWIKI_EDITOR_SAVE_FAILED) {
      app->status_message = "save failed; buffer remains dirty";
   } else if (status == CWIKI_EDITOR_DURABILITY_UNCERTAIN) {
      app->status_message = "durability uncertain; buffer remains dirty";
   }
}

static int
size_from_tty(struct app *app, int fd)
{
   struct winsize size;

   if (ioctl(fd, TIOCGWINSZ, &size) != 0) {
      return -1;
   }
   if (size.ws_row == 0U || size.ws_col == 0U ||
       size.ws_row > 4096U || size.ws_col > 4096U) {
      errno = EINVAL;
      return -1;
   }
   if (app->viewport.rows == size.ws_row &&
       app->viewport.columns == size.ws_col) {
      return 0;
   }
   app->viewport.rows = size.ws_row;
   app->viewport.columns = size.ws_col;
   return refresh(app) == 0 ? 1 : -1;
}

static int
draw(struct app *app, struct cwiki_terminal *terminal)
{
   size_t count = app->document.buffer.line_count;
   struct cwiki_highlight_line *highlights = NULL;
   const struct cwiki_input_event *pending;
   struct cwiki_float_area clue_area = {0};
   struct cwiki_snippet_info *snippet_infos = NULL;
   struct cwiki_clue_item *snippet_items = NULL;
   char *bytes = NULL;
   size_t pending_count;
   size_t snippet_count = 0U;
   const char *clue_title = NULL;
   size_t frame_length = 0U;
   size_t clue_length = 0U;
   size_t length = 0U;
   size_t offset = 0U;
   int error_number = 0;

   if (app->settings.module_syntax) {
      highlights = calloc(count, sizeof(*highlights));
      if (highlights == NULL) {
         return -1;
      }
      for (size_t i = 0U; i < count; i++) {
         if (cwiki_highlight_build_line(app->zones, &app->document.buffer, i,
             &highlights[i]) != 0) {
            goto fail;
         }
      }
   }
   if (app->picker != NULL && app->viewport.rows >= 5U &&
       app->viewport.columns >= 5U) {
      struct cwiki_float_options picker_options = {
         {1U, 1U, app->viewport.rows - 1U, app->viewport.columns},
         12U, 72U, CWIKI_FLOAT_CENTER, 0, 0
      };

      if (cwiki_float_place(&picker_options, &clue_area) != 0 ||
          cwiki_picker_render_overlay(app->picker, &clue_area,
          CWIKI_FLOAT_BORDER_SINGLE, NULL, 0U, &clue_length) != 0) {
         goto fail;
      }
   } else if (app->snippet_overlay && app->viewport.rows >= 4U &&
       app->viewport.columns >= 5U) {
      struct cwiki_float_options snippet_options;

      if (cwiki_editor_input_list_active_snippets(app->input, app->zones,
          NULL, 0U, &snippet_count) != CWIKI_SNIPPET_OK ||
          snippet_count > SIZE_MAX / sizeof(*snippet_infos) ||
          snippet_count > SIZE_MAX / sizeof(*snippet_items)) {
         errno = EINVAL;
         goto fail;
      }
      if (snippet_count != 0U) {
         snippet_infos = malloc(snippet_count * sizeof(*snippet_infos));
         snippet_items = malloc(snippet_count * sizeof(*snippet_items));
         if (snippet_infos == NULL || snippet_items == NULL) {
            goto fail;
         }
         if (cwiki_editor_input_list_active_snippets(app->input, app->zones,
             snippet_infos, snippet_count, &snippet_count) !=
             CWIKI_SNIPPET_OK) {
            errno = EINVAL;
            goto fail;
         }
         for (size_t i = 0U; i < snippet_count; i++) {
            bool automatic = (snippet_infos[i].flags &
                CWIKI_SNIPPET_TRIGGER_AUTO) != 0U;
            bool explicit = (snippet_infos[i].flags &
                CWIKI_SNIPPET_TRIGGER_EXPLICIT) != 0U;
            const char *label = snippet_infos[i].kind == CWIKI_SNIPPET_REGEX ?
                (automatic && explicit ? "auto/manual regex" :
                (automatic ? "auto regex" : "manual regex")) :
                (automatic && explicit ? "auto/manual" :
                (automatic ? "auto" : "manual"));

            snippet_items[i] = (struct cwiki_clue_item){
               snippet_infos[i].trigger, snippet_infos[i].trigger_length,
               label, strlen(label)
            };
         }
      }
      snippet_options = (struct cwiki_float_options){
         {1U, 1U, app->viewport.rows - 1U, app->viewport.columns},
         snippet_count == 0U ? 3U :
         (snippet_count > 10U ? 10U : snippet_count) + 2U,
         44U, CWIKI_FLOAT_SOUTH_EAST, 0, 0
      };
      if (cwiki_float_place(&snippet_options, &clue_area) != 0 ||
          cwiki_clue_render_list_overlay(snippet_items, snippet_count,
          &clue_area, CWIKI_FLOAT_BORDER_SINGLE, "Active snippets",
          "No active snippets", NULL, 0U, &clue_length) != 0) {
         goto fail;
      }
   }
   pending = cwiki_editor_input_pending(app->input, &pending_count);
   if (app->settings.module_clues && app->picker == NULL &&
       pending_count != 0U &&
       app->viewport.rows >= 4U &&
       app->viewport.columns >= 5U) {
      size_t continuation_count = cwiki_keymap_continuation_count(
          cwiki_editor_input_keymap(app->input), CWIKI_KEYMAP_NORMAL, pending,
          pending_count);
      struct cwiki_float_options clue_options = {
         {1U, 1U, app->viewport.rows - 1U, app->viewport.columns},
         continuation_count + 2U, 36U, CWIKI_FLOAT_SOUTH_EAST, 0, 0
      };

      for (size_t i = app->config_count; i != 0U && clue_title == NULL; i--) {
         clue_title = cwiki_config_clue_group(app->configs[i - 1U],
             CWIKI_KEYMAP_NORMAL, pending, pending_count);
      }
      if (continuation_count != 0U &&
          cwiki_float_place(&clue_options, &clue_area) != 0) {
         goto fail;
      }
      if (continuation_count != 0U && clue_area.rows >= 3U &&
          clue_area.columns >= 5U &&
          cwiki_clue_render_overlay(cwiki_editor_input_keymap(app->input),
          cwiki_editor_input_actions(app->input), CWIKI_KEYMAP_NORMAL, pending,
          pending_count, &clue_area, CWIKI_FLOAT_BORDER_SINGLE, clue_title,
          NULL, 0U, &clue_length) != 0) {
         goto fail;
      }
   }
   if (cwiki_render_frame(&app->layout, highlights, &app->editor,
       &app->viewport, NULL, 0U, &frame_length) != 0 ||
       clue_length == SIZE_MAX ||
       frame_length > SIZE_MAX - clue_length - 1U ||
       (bytes = malloc(frame_length + clue_length + 1U)) == NULL ||
       cwiki_render_frame(&app->layout, highlights, &app->editor,
       &app->viewport, bytes, frame_length + 1U, &frame_length) != 0 ||
       (clue_length != 0U && app->picker != NULL &&
       cwiki_picker_render_overlay(app->picker, &clue_area,
       CWIKI_FLOAT_BORDER_SINGLE, bytes + frame_length, clue_length + 1U,
       &clue_length) != 0) ||
       (clue_length != 0U && app->picker == NULL &&
       !app->snippet_overlay &&
       cwiki_clue_render_overlay(cwiki_editor_input_keymap(app->input),
       cwiki_editor_input_actions(app->input), CWIKI_KEYMAP_NORMAL, pending,
       pending_count, &clue_area, CWIKI_FLOAT_BORDER_SINGLE, clue_title,
       bytes + frame_length, clue_length + 1U, &clue_length) != 0) ||
       (clue_length != 0U && app->picker == NULL && app->snippet_overlay &&
       cwiki_clue_render_list_overlay(snippet_items, snippet_count, &clue_area,
       CWIKI_FLOAT_BORDER_SINGLE, "Active snippets", "No active snippets",
       bytes + frame_length, clue_length + 1U, &clue_length) != 0)) {
      goto fail;
   }
   length = frame_length + clue_length;
   if (cwiki_terminal_begin_update(terminal) != 0) {
      error_number = errno;
   } else {
      while (offset < length) {
         ssize_t written = frame_write(terminal->output_fd, bytes + offset,
             length - offset);
         if (written > 0) {
            offset += (size_t)written;
         } else if (written < 0 && errno == EINTR) {
            continue;
         } else {
            error_number = written == 0 ? EIO : errno;
            break;
         }
      }
   }
   /* End even when begin or payload failed; never strand a synchronized frame. */
   if (cwiki_terminal_end_update(terminal) != 0 && error_number == 0) {
      error_number = errno;
   }
   goto done;
fail:
   error_number = errno;
done:
   if (highlights != NULL) {
      for (size_t i = 0U; i < count; i++) {
         cwiki_highlight_line_free(&highlights[i]);
      }
   }
   free(highlights);
   free(snippet_infos);
   free(snippet_items);
   free(bytes);
   errno = error_number;
   return error_number == 0 ? 0 : -1;
}

int
cwiki_app_run(const char *path, const struct cwiki_app_options *options)
{
   struct app app = {0};
   struct cwiki_terminal terminal = {0};
   struct cwiki_key_record local_record = {0};
   struct cwiki_key_record *record;
   struct cwiki_input_parser parser;
   struct cwiki_terminal_result start = {0};
   struct cwiki_config_zone_table zone_table = {0};
   struct cwiki_config_subjects subjects = {0};
   bool activated = false;
   bool sidecar_activated = false;
   bool redraw = true;
   int result = 1;

   if (options == NULL) {
      errno = EINVAL;
      return 1;
   }
   record = options->record == NULL ? &local_record : options->record;
   cwiki_input_parser_init(&parser);
   cwiki_layout_window_init(&app.layout);
   cwiki_config_settings_defaults(&app.settings);
   if ((options->record == NULL && cwiki_key_record_init(record, NULL, 0U) != 0) ||
       (cwiki_document_load(&app.document, path) != 0 &&
       (errno != ENOENT || cwiki_document_init(&app.document, path) != 0))) {
      goto system_error;
   }
   if (cwiki_editor_init(&app.editor, &app.document) != CWIKI_EDITOR_OK ||
       cwiki_editor_input_init(&app.input, &app.editor) != CWIKI_EDITOR_OK ||
       register_app_actions(&app) != 0) {
      errno = ENOMEM;
      goto system_error;
   }
   app.config_path = path;
   app.config_status = cwiki_config_parse_note_subjects(&app.document.buffer,
       &subjects, &app.config_error);
   if (app.config_status == CWIKI_CONFIG_NO_MEMORY) {
      errno = ENOMEM;
      goto system_error;
   }
   if (app.config_status != CWIKI_CONFIG_OK) {
      goto done;
   }
   if (options->config_count > CWIKI_CONFIG_SOURCE_MAX ||
       (options->configs == NULL && options->config_count != 0U)) {
      errno = EINVAL;
      goto system_error;
   }
   if (options->config_count != 0U) {
      struct cwiki_keymap *candidate = NULL;
      struct cwiki_snippet_registry *snippet_candidate = NULL;
      const struct cwiki_keymap *base = cwiki_editor_input_keymap(app.input);

      for (size_t i = 0U; i < options->config_count; i++) {
         struct cwiki_keymap *next = NULL;

         app.config_path = options->configs[i].path;
         if (options->configs[i].bytes == NULL) {
            app.config_status = CWIKI_CONFIG_INVALID;
            break;
         }
         app.config_status = cwiki_config_parse(&app.configs[i],
             options->configs[i].bytes, options->configs[i].length,
             cwiki_editor_input_actions(app.input), &app.config_error);
         if (app.config_status == CWIKI_CONFIG_OK) {
            app.config_count++;
            cwiki_config_apply_settings(app.configs[i], &app.settings);
            app.config_status = cwiki_config_build_keymap(app.configs[i], base,
                &next, &app.config_error);
         }
         if (app.config_status != CWIKI_CONFIG_OK) {
            cwiki_keymap_free(next);
            break;
         }
         cwiki_keymap_free(candidate);
         candidate = next;
         base = candidate;
      }
      if (app.config_status == CWIKI_CONFIG_OK) {
         const struct cwiki_config *ordered[CWIKI_CONFIG_SOURCE_MAX];

         for (size_t i = 0U; i < app.config_count; i++) {
            ordered[i] = app.configs[i];
         }
         app.config_status = cwiki_config_build_snippets(ordered,
             app.config_count, &snippet_candidate, &app.config_error);
      }
      if (app.config_status == CWIKI_CONFIG_OK) {
         if (cwiki_editor_input_replace_keymap(app.input, candidate) !=
             CWIKI_EDITOR_OK) {
            cwiki_keymap_free(candidate);
            app.config_status = CWIKI_CONFIG_INVALID;
         }
         candidate = NULL;
      }
      if (app.config_status == CWIKI_CONFIG_OK) {
         if (cwiki_editor_input_replace_snippets(app.input, snippet_candidate) !=
             CWIKI_EDITOR_OK) {
            cwiki_snippet_registry_free(snippet_candidate);
            app.config_status = CWIKI_CONFIG_INVALID;
         }
         snippet_candidate = NULL;
      }
      if (app.config_status == CWIKI_CONFIG_NO_MEMORY) {
         cwiki_keymap_free(candidate);
         cwiki_snippet_registry_free(snippet_candidate);
         errno = ENOMEM;
         goto system_error;
      }
      if (app.config_status != CWIKI_CONFIG_OK) {
         cwiki_keymap_free(candidate);
         cwiki_snippet_registry_free(snippet_candidate);
         goto done;
      }
   }
   if (cwiki_editor_input_set_snippet_subjects(app.input, subjects.items,
       subjects.count) != CWIKI_EDITOR_OK) {
      errno = ENOMEM;
      goto system_error;
   }
   cwiki_editor_input_set_snippets_enabled(app.input,
       app.settings.module_snippets);
   {
      const struct cwiki_config *ordered[CWIKI_CONFIG_SOURCE_MAX];
      const char *paths[CWIKI_CONFIG_SOURCE_MAX];

      if (app.config_count != 0U && options->configs == NULL) {
         errno = EINVAL;
         goto system_error;
      }
      for (size_t i = 0U; i < app.config_count; i++) {
         ordered[i] = app.configs[i];
         paths[i] = options->configs[i].path;
      }
      app.config_status = cwiki_config_build_zones(ordered, app.config_count,
          &zone_table, &app.config_error);
      if (app.config_status != CWIKI_CONFIG_OK &&
          options->configs != NULL &&
          app.config_error.source_index < app.config_count) {
         app.config_path = options->configs[app.config_error.source_index].path;
      }
      if (app.config_status == CWIKI_CONFIG_NO_MEMORY) {
         errno = ENOMEM;
         goto system_error;
      }
      if (app.config_status != CWIKI_CONFIG_OK) {
         goto done;
      }
      if (options->inspect_config) {
         if (cwiki_config_inspect(options->output_fd, ordered,
             app.config_count, paths, &app.settings, &subjects, path,
             cwiki_editor_input_keymap(app.input)) != 0) {
            goto system_error;
         }
         result = 0;
         goto done;
      }
   }
   if (cwiki_zone_engine_init(&app.zones, zone_table.regions, zone_table.count,
       zone_table.top_level) != 0 ||
       cwiki_conceal_table_init_builtin(&app.conceal) != 0) {
      goto system_error;
   }
   if (options->crash_fd >= 0) {
      if (cwiki_terminal_key_record_activate(record, options->crash_fd) != 0) {
         goto system_error;
      }
      activated = true;
   }
   if (options->sidecar_fd >= 0) {
      if (cwiki_terminal_key_record_activate_sidecar(record,
          options->sidecar_fd) != 0) {
         goto system_error;
      }
      sidecar_activated = true;
   }
   start = cwiki_terminal_start_recording(&terminal, options->input_fd,
       options->output_fd, record);
   if (start.status != CWIKI_TERMINAL_SUCCESS) {
      app.error = start.system_errno;
      goto done;
   }
   if (size_from_tty(&app, options->input_fd) < 0 ||
       cwiki_input_parser_feed(&parser, start.pending_input,
       start.pending_input_len, event, &app) != 0) {
      goto system_error;
   }
   while (app.error == 0) {
      struct pollfd input = {options->input_fd, POLLIN, 0};
      int resized = size_from_tty(&app, options->input_fd);
      int ready;

      if (resized < 0) {
         goto system_error;
      }
      redraw = redraw || resized != 0;
      ready = poll(&input, 1U, redraw ? 0 : 100);
      if (ready < 0) {
         if (errno == EINTR) {
            continue;
         }
         goto system_error;
      }
      if (ready > 0 && !app.editor.quit_requested) {
         unsigned char bytes[4096];
         ssize_t length;

         if ((input.revents & (POLLIN | POLLHUP)) == 0) {
            errno = EIO;
            goto system_error;
         }
         length = read(options->input_fd, bytes, sizeof(bytes));
         if (length < 0 && errno == EINTR) {
            continue;
         }
         if (length <= 0) {
            if (length == 0) {
               errno = EPIPE;
            }
            goto system_error;
         }
         if (cwiki_terminal_record_input(&terminal, bytes, (size_t)length) != 0 ||
             cwiki_input_parser_feed(&parser, bytes, (size_t)length,
             event, &app) != 0) {
            goto system_error;
         }
         redraw = true;
         continue;
      }
      save_if_idle(&app);
      if (app.error != 0) {
         break;
      }
      if (redraw && draw(&app, &terminal) != 0) {
         goto system_error;
      }
      redraw = false;
      if (app.editor.quit_requested) {
         result = 0;
         break;
      }
   }
   goto done;
system_error:
   app.error = errno;
done:
   cwiki_terminal_cleanup(&terminal);
   if (activated) {
      (void)cwiki_terminal_key_record_deactivate(record);
   }
   if (sidecar_activated) {
      (void)cwiki_terminal_key_record_deactivate_sidecar(record);
   }
   if (start.status != CWIKI_TERMINAL_SUCCESS) {
      (void)dprintf(options->error_fd, "cwiki: %s\n",
          cwiki_terminal_status_message(start.status));
   }
   if (app.error != 0) {
      (void)dprintf(options->error_fd, "cwiki: %s\n", strerror(app.error));
   }
   if (app.config_status != CWIKI_CONFIG_OK) {
      (void)dprintf(options->error_fd,
          "cwiki: %s:%zu:%zu: %s\n",
          app.config_path == NULL ? "configuration" : app.config_path,
          app.config_error.line, app.config_error.column,
          app.config_error.message);
   }
   if ((app.statuses & (1U << CWIKI_EDITOR_DIRTY)) != 0U) {
      (void)dprintf(options->error_fd, "cwiki: unsaved changes; :q refused\n");
   }
   if ((app.statuses & (1U << CWIKI_EDITOR_SAVE_FAILED)) != 0U) {
      (void)dprintf(options->error_fd,
          "cwiki: save failed; buffer remains dirty\n");
   }
   if ((app.statuses & (1U << CWIKI_EDITOR_DURABILITY_UNCERTAIN)) != 0U) {
      (void)dprintf(options->error_fd,
          "cwiki: durability uncertain; buffer remains dirty\n");
   }
   if ((app.statuses & (1U << CWIKI_EDITOR_INVALID)) != 0U) {
      (void)dprintf(options->error_fd, "cwiki: unsupported command or input\n");
   }
   cwiki_input_parser_destroy(&parser);
   cwiki_picker_free(app.picker);
   cwiki_editor_input_free(app.input);
   cwiki_layout_window_free(&app.layout);
   cwiki_conceal_table_free(&app.conceal);
   cwiki_zone_engine_free(app.zones);
   cwiki_config_zone_table_free(&zone_table);
   cwiki_config_subjects_free(&subjects);
   for (size_t i = 0U; i < app.config_count; i++) {
      cwiki_config_free(app.configs[i]);
   }
   cwiki_editor_free(&app.editor);
   cwiki_document_free(&app.document);
   if (options->record == NULL) {
      cwiki_key_record_destroy(&local_record);
   }
   return result;
}
