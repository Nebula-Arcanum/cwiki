#include "app.h"

#include "clue_render.h"
#include "conceal.h"
#include "editor_input.h"
#include "float.h"
#include "highlight.h"
#include "key_record.h"
#include "layout.h"
#include "render.h"
#include "terminal.h"
#include "zone.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct app {
   struct cwiki_document document;
   struct cwiki_editor editor;
   struct cwiki_editor_input *input;
   struct cwiki_zone_engine *zones;
   struct cwiki_conceal_table conceal;
   struct cwiki_layout_window layout;
   struct cwiki_render_viewport viewport;
   struct cwiki_motion_viewport motion_viewport;
   int error;
   unsigned int statuses;
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
   size_t row;
   size_t column;
   size_t height = app->viewport.rows > 1U ? app->viewport.rows - 1U : 1U;

   options.content_width = app->viewport.columns;
   options.wrap = true;
   options.break_indent = true;
   options.continuation_marker = "";
   options.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   options.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
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
   input_context.layout = &app->layout;
   input_context.zones = app->zones;
   input_context.viewport = &app->motion_viewport;
   input_context.timestamp = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
       (uint64_t)now.tv_nsec;
   status = cwiki_editor_input_handle(app->input, event_value, &input_context);
   if (status == CWIKI_EDITOR_OK &&
       (before == CWIKI_EDITOR_INSERT || before == CWIKI_EDITOR_REPLACE) &&
       app->editor.mode == CWIKI_EDITOR_NORMAL && app->document.dirty) {
      status = cwiki_editor_begin_command(&app->editor);
      if (status == CWIKI_EDITOR_OK) {
         status = cwiki_editor_command_insert(&app->editor, "w", 1U);
      }
      if (status == CWIKI_EDITOR_OK) {
         status = cwiki_editor_execute_command(&app->editor);
      }
      if (status != CWIKI_EDITOR_OK) {
         (void)cwiki_editor_escape(&app->editor);
      }
   }
   app->statuses |= 1U << (unsigned int)status;
   if (status == CWIKI_EDITOR_NO_MEMORY) {
      app->error = ENOMEM;
   } else if (refresh(app) != 0) {
      app->error = errno;
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
   struct cwiki_highlight_line *highlights = calloc(count, sizeof(*highlights));
   const struct cwiki_input_event *pending;
   struct cwiki_float_area clue_area = {0};
   char *bytes = NULL;
   size_t pending_count;
   size_t frame_length = 0U;
   size_t clue_length = 0U;
   size_t length = 0U;
   size_t offset = 0U;
   int error_number = 0;

   if (highlights == NULL) {
      return -1;
   }
   for (size_t i = 0U; i < count; i++) {
      if (cwiki_highlight_build_line(app->zones, &app->document.buffer, i,
          &highlights[i]) != 0) {
         goto fail;
      }
   }
   pending = cwiki_editor_input_pending(app->input, &pending_count);
   if (pending_count != 0U && app->viewport.rows >= 4U &&
       app->viewport.columns >= 5U) {
      size_t continuation_count = cwiki_keymap_continuation_count(
          cwiki_editor_input_keymap(app->input), CWIKI_KEYMAP_NORMAL, pending,
          pending_count);
      struct cwiki_float_options clue_options = {
         {1U, 1U, app->viewport.rows - 1U, app->viewport.columns},
         continuation_count + 2U, 36U, CWIKI_FLOAT_SOUTH_EAST, 0, 0
      };

      if (continuation_count != 0U &&
          cwiki_float_place(&clue_options, &clue_area) != 0) {
         goto fail;
      }
      if (continuation_count != 0U && clue_area.rows >= 3U &&
          clue_area.columns >= 5U &&
          cwiki_clue_render_overlay(cwiki_editor_input_keymap(app->input),
          cwiki_editor_input_actions(app->input), CWIKI_KEYMAP_NORMAL, pending,
          pending_count, &clue_area, CWIKI_FLOAT_BORDER_SINGLE, NULL, 0U,
          &clue_length) != 0) {
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
       (clue_length != 0U &&
       cwiki_clue_render_overlay(cwiki_editor_input_keymap(app->input),
       cwiki_editor_input_actions(app->input), CWIKI_KEYMAP_NORMAL, pending,
       pending_count, &clue_area, CWIKI_FLOAT_BORDER_SINGLE,
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
   for (size_t i = 0U; i < count; i++) {
      cwiki_highlight_line_free(&highlights[i]);
   }
   free(highlights);
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
   const struct cwiki_zone_region *regions;
   size_t region_count;
   uint64_t top_level;
   bool activated = false;
   bool redraw = true;
   int result = 1;

   if (options == NULL) {
      errno = EINVAL;
      return 1;
   }
   record = options->record == NULL ? &local_record : options->record;
   cwiki_input_parser_init(&parser);
   cwiki_layout_window_init(&app.layout);
   if ((options->record == NULL && cwiki_key_record_init(record, NULL, 0U) != 0) ||
       (cwiki_document_load(&app.document, path) != 0 &&
       (errno != ENOENT || cwiki_document_init(&app.document, path) != 0))) {
      goto system_error;
   }
   if (cwiki_editor_init(&app.editor, &app.document) != CWIKI_EDITOR_OK ||
       cwiki_editor_input_init(&app.input, &app.editor) != CWIKI_EDITOR_OK) {
      errno = ENOMEM;
      goto system_error;
   }
   regions = cwiki_zone_builtin_regions(&region_count, &top_level);
   if (cwiki_zone_engine_init(&app.zones, regions, region_count, top_level) != 0 ||
       cwiki_conceal_table_init_builtin(&app.conceal) != 0) {
      goto system_error;
   }
   if (options->crash_fd >= 0) {
      if (cwiki_terminal_key_record_activate(record, options->crash_fd) != 0) {
         goto system_error;
      }
      activated = true;
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
   if (start.status != CWIKI_TERMINAL_SUCCESS) {
      (void)dprintf(options->error_fd, "cwiki: %s\n",
          cwiki_terminal_status_message(start.status));
   }
   if (app.error != 0) {
      (void)dprintf(options->error_fd, "cwiki: %s\n", strerror(app.error));
   }
   if ((app.statuses & (1U << CWIKI_EDITOR_DIRTY)) != 0U) {
      (void)dprintf(options->error_fd, "cwiki: unsaved changes; :q refused\n");
   }
   if ((app.statuses & (1U << CWIKI_EDITOR_SAVE_FAILED)) != 0U) {
      (void)dprintf(options->error_fd,
          "cwiki: save failed or durability uncertain; buffer remains dirty\n");
   }
   if ((app.statuses & (1U << CWIKI_EDITOR_INVALID)) != 0U) {
      (void)dprintf(options->error_fd, "cwiki: unsupported command or input\n");
   }
   cwiki_input_parser_destroy(&parser);
   cwiki_editor_input_free(app.input);
   cwiki_layout_window_free(&app.layout);
   cwiki_conceal_table_free(&app.conceal);
   cwiki_zone_engine_free(app.zones);
   cwiki_editor_free(&app.editor);
   cwiki_document_free(&app.document);
   if (options->record == NULL) {
      cwiki_key_record_destroy(&local_record);
   }
   return result;
}
