#include "render.h"

#include "editor.h"
#include "highlight.h"
#include "layout.h"
#include "zone.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define START "\x1b[?25l\x1b[0m\x1b[2J\x1b[H"
#define RESET "\x1b[0m"
#define STATUS "\x1b[0;7m"
#define SHOW "\x1b[?25h"

struct fixture {
   struct cwiki_document document;
   struct cwiki_editor editor;
   struct cwiki_zone_engine *zones;
   struct cwiki_conceal_table conceal;
   struct cwiki_layout_window layout;
   struct cwiki_layout_options options;
   struct cwiki_highlight_line highlights[16];
   struct cwiki_render_viewport view;
};

static void
build(struct fixture *f)
{
   size_t line;

   assert(f->document.buffer.line_count <= 16U);
   for (line = 0U; line < f->document.buffer.line_count; line++) {
      cwiki_highlight_line_free(&f->highlights[line]);
      assert(cwiki_highlight_build_line(f->zones, &f->document.buffer,
          line, &f->highlights[line]) == 0);
   }
   f->options.content_width = f->view.columns;
   f->options.cursor = f->editor.motion.cursor;
   assert(cwiki_layout_rebuild(&f->layout, &f->document.buffer, f->zones,
       &f->conceal, &f->options) == 0);
}

static void
init(struct fixture *f, const char *source, size_t rows, size_t columns)
{
   const struct cwiki_zone_region *regions;
   size_t count;
   size_t scanned;
   uint64_t top;

   (void)memset(f, 0, sizeof(*f));
   assert(cwiki_buffer_init(&f->document.buffer) == 0);
   assert(cwiki_buffer_load(&f->document.buffer, source, strlen(source)) == 0);
   f->document.path = "/fixture/n.md";
   f->editor.document = &f->document;
   regions = cwiki_zone_builtin_regions(&count, &top);
   assert(cwiki_zone_engine_init(&f->zones, regions, count, top) == 0);
   assert(cwiki_zone_recompute(f->zones, &f->document.buffer, 0U,
       &scanned) == 0);
   assert(cwiki_conceal_table_init_builtin(&f->conceal) == 0);
   f->options.wrap = true;
   f->options.break_indent = true;
   f->options.continuation_marker = ">";
   f->options.conceal_categories = CWIKI_CONCEAL_DEFAULT_MASK;
   f->options.concealcursor_modes = CWIKI_CONCEALCURSOR_DEFAULT;
   f->view.rows = rows;
   f->view.columns = columns;
   build(f);
}

static void
finish(struct fixture *f)
{
   size_t line;

   for (line = 0U; line < f->document.buffer.line_count; line++) {
      cwiki_highlight_line_free(&f->highlights[line]);
   }
   cwiki_layout_window_free(&f->layout);
   cwiki_conceal_table_free(&f->conceal);
   cwiki_zone_engine_free(f->zones);
   cwiki_buffer_free(&f->document.buffer);
}

static void
expect(struct fixture *f, const char *expected)
{
   char actual[8192];
   char before[8192];
   size_t needed = 0U;
   size_t length = 999U;

   assert(cwiki_render_frame(&f->layout, f->highlights, &f->editor,
       &f->view, NULL, 0U, &needed) == 0);
   assert(needed + 1U < sizeof(actual));
   (void)memset(actual, '!', sizeof(actual));
   (void)memcpy(before, actual, sizeof(actual));
   /* No allocation: every insufficient-capacity boundary is transactional. */
   for (size_t capacity = 0U; capacity <= needed; capacity++) {
      assert(cwiki_render_frame(&f->layout, f->highlights, &f->editor,
          &f->view, actual, capacity, &length) == -1);
      assert(errno == ENOSPC && length == 999U);
      assert(memcmp(actual, before, sizeof(actual)) == 0);
   }
   assert(cwiki_render_frame(&f->layout, f->highlights, &f->editor,
       &f->view, actual, needed + 1U, &length) == 0);
   if (length != strlen(expected) || strcmp(actual, expected) != 0) {
      (void)fprintf(stderr, "expected: %s\nactual:   %s\n", expected, actual);
      abort();
   }
   assert(actual[length + 1U] == '!');
   assert(strstr(actual, "2026") == NULL);
}

static void
modes(void)
{
   struct fixture f;

   init(&f, "abc", 2U, 24U);
   f.editor.motion.cursor.byte = 1U;
   expect(&f, START "\x1b[1;1H" RESET "abc" RESET
       "                     " "\x1b[2;1H" STATUS
       "NORMAL n.md 1:2         " RESET "\x1b[1;2H" SHOW);
   f.editor.mode = CWIKI_EDITOR_INSERT;
   f.document.dirty = true;
   expect(&f, START "\x1b[1;1H" RESET "abc" RESET
       "                     " "\x1b[2;1H" STATUS
       "INSERT n.md [+] 1:2     " RESET "\x1b[1;2H" SHOW);
   f.editor.mode = CWIKI_EDITOR_REPLACE;
   expect(&f, START "\x1b[1;1H" RESET "abc" RESET
       "                     " "\x1b[2;1H" STATUS
       "REPLACE n.md [+] 1:2    " RESET "\x1b[1;2H" SHOW);
   f.editor.mode = CWIKI_EDITOR_COMMAND;
   f.editor.command = "wq";
   f.editor.command_length = 2U;
   expect(&f, START "\x1b[1;1H" RESET "abc" RESET
       "                     " "\x1b[2;1H" STATUS
       ":wq                     " RESET "\x1b[2;4H" SHOW);
   finish(&f);
}

static void
unicode_conceal(void)
{
   struct fixture f;

   init(&f, "é界é $\\alpha$!", 2U, 24U);
   f.editor.motion.cursor.byte = strlen("é界é $\\alpha");
   expect(&f, START "\x1b[1;1H" RESET "é界é " "\x1b[1;33m$"
       "\x1b[0;33m𝛼" "\x1b[1;33m$" RESET "!" RESET "               "
       "\x1b[2;1H" STATUS
       "NORMAL n.md 1:13        " RESET "\x1b[1;8H" SHOW);
   finish(&f);
}

static void
wrap_scroll(void)
{
   struct fixture f;

   init(&f, "  ab cd ef\nZ", 4U, 6U);
   f.editor.motion.cursor.byte = 8U;
   expect(&f, START "\x1b[1;1H" RESET "  ab " RESET " "
       "\x1b[2;1H\x1b[0;90m  >" RESET "cd " RESET
       "\x1b[3;1H\x1b[0;90m  >" RESET "ef" RESET " "
       "\x1b[4;1H" STATUS "NORMAL" RESET "\x1b[3;4H" SHOW);
   f.view.first_row = 2U;
   f.view.horizontal_offset = 3U; /* Wrapped prose ignores horizontal scroll. */
   expect(&f, START "\x1b[1;1H\x1b[0;90m  >" RESET "ef" RESET " "
       "\x1b[2;1H" RESET "Z" RESET "     "
       "\x1b[3;1H" RESET "      "
       "\x1b[4;1H" STATUS "NORMAL" RESET "\x1b[1;4H" SHOW);
   f.editor.motion.cursor.byte = 0U;
   expect(&f, START "\x1b[1;1H\x1b[0;90m  >" RESET "ef" RESET " "
       "\x1b[2;1H" RESET "Z" RESET "     "
       "\x1b[3;1H" RESET "      "
       "\x1b[4;1H" STATUS "NORMAL" RESET);
   finish(&f);
}

static void
clipping_raw(void)
{
   struct fixture f;

   init(&f, "A界BC界D", 2U, 4U);
   f.options.wrap = false;
   f.view.horizontal_offset = 2U;
   f.editor.motion.cursor.byte = 4U;
   build(&f);
   expect(&f, START "\x1b[1;1H " RESET "BC " RESET
       "\x1b[2;1H" STATUS "NORM" RESET "\x1b[1;2H" SHOW);
   f.document.buffer.lines[0].zone_degraded = true;
   f.options.wrap = true;
   build(&f);
   expect(&f, START "\x1b[1;1H \x1b[0;31mBC " RESET
       "\x1b[2;1H" STATUS "NORM" RESET "\x1b[1;2H" SHOW);
   f.view.horizontal_offset = 99U;
   expect(&f, START "\x1b[1;1H" RESET "    "
       "\x1b[2;1H" STATUS "NORM" RESET);
   finish(&f);

   init(&f, "```\nabcdef\n```", 2U, 4U);
   f.editor.motion.cursor = (struct cwiki_position){ 1U, 3U };
   f.view.first_row = 1U;
   f.view.horizontal_offset = 2U;
   expect(&f, START "\x1b[1;1H\x1b[0;36mcdef" RESET
       "\x1b[2;1H" STATUS "NORM" RESET "\x1b[1;2H" SHOW);
   finish(&f);
}

static void
small_and_controls(void)
{
   struct fixture f;

   init(&f, "界", 2U, 1U);
   expect(&f, START "\x1b[1;1H " RESET "\x1b[2;1H" STATUS
       "N" RESET "\x1b[1;1H" SHOW);
   f.view.rows = 1U;
   expect(&f, START "\x1b[1;1H" STATUS "N" RESET);
   f.editor.mode = CWIKI_EDITOR_COMMAND;
   f.editor.command = "abcdef";
   f.editor.command_length = 6U;
   expect(&f, START "\x1b[1;1H" STATUS ":" RESET "\x1b[1;1H" SHOW);
   f.view.columns = 4U;
   build(&f);
   expect(&f, START "\x1b[1;1H" STATUS ":ef " RESET "\x1b[1;4H" SHOW);
   f.editor.command = "界ab";
   f.editor.command_length = strlen(f.editor.command);
   expect(&f, START "\x1b[1;1H" STATUS ":ab " RESET "\x1b[1;4H" SHOW);
   finish(&f);

   init(&f, "a\x1b[31m\t\x7f\xc2\x9b" "b", 2U, 8U);
   expect(&f, START "\x1b[1;1H" RESET "a[31mb" RESET "  "
       "\x1b[2;1H" STATUS "NORMAL n" RESET "\x1b[1;1H" SHOW);
   finish(&f);

   init(&f, "abcd", 2U, 4U);
   f.editor.motion.cursor.byte = 4U;
   expect(&f, START "\x1b[1;1H" RESET "abcd" RESET
       "\x1b[2;1H" STATUS "NORM" RESET "\x1b[1;4H" SHOW);
   finish(&f);
}

static void
roles_and_status(void)
{
   struct fixture f;
   struct cwiki_highlight_run runs[CWIKI_HIGHLIGHT_ROLE_COUNT];
   struct cwiki_highlight_line saved;
   size_t role;

   init(&f, "abcdefghijkl", 2U, 24U);
   for (role = 0U; role < CWIKI_HIGHLIGHT_ROLE_COUNT; role++) {
      runs[role] = (struct cwiki_highlight_run){ role, role + 1U,
          (enum cwiki_highlight_role)role };
   }
   saved = f.highlights[0];
   f.highlights[0] = (struct cwiki_highlight_line){ runs,
       CWIKI_HIGHLIGHT_ROLE_COUNT, false };
   f.document.path = "/fixture/é\x1b[2J.md";
   expect(&f, START "\x1b[1;1H" RESET "a\x1b[0;36mb\x1b[0;33mc"
       "\x1b[0;33md\x1b[0;32me" RESET "f\x1b[0;34mg\x1b[0;35mh"
       "\x1b[0;35mi\x1b[0;90mj\x1b[0;36mk\x1b[0;31ml" RESET
       "            " "\x1b[2;1H" STATUS "NORMAL é[2J.md 1:1      " RESET
       "\x1b[1;1H" SHOW);
   f.highlights[0] = saved;
   finish(&f);
}

static void
errors(void)
{
   struct fixture f;
   char output[64] = "unchanged";
   size_t length = 42U;

   init(&f, "abc", 2U, 8U);
   f.view.rows = 0U;
   assert(cwiki_render_frame(&f.layout, f.highlights, &f.editor,
       &f.view, output, sizeof(output), &length) == -1 && errno == EINVAL);
   assert(strcmp(output, "unchanged") == 0 && length == 42U);
   f.view.rows = 2U;
   f.view.horizontal_offset = SIZE_MAX;
   assert(cwiki_render_frame(&f.layout, f.highlights, &f.editor,
       &f.view, output, sizeof(output), &length) == -1 && errno == EINVAL);
   assert(strcmp(output, "unchanged") == 0 && length == 42U);
   f.view.horizontal_offset = 0U;
   f.editor.mode = CWIKI_EDITOR_COMMAND;
   f.editor.command = "w\xff";
   f.editor.command_length = 2U;
   /* Failure after source rows have been measured still publishes no bytes. */
   assert(cwiki_render_frame(&f.layout, f.highlights, &f.editor,
       &f.view, output, sizeof(output), &length) == -1 && errno == EINVAL);
   assert(strcmp(output, "unchanged") == 0 && length == 42U);
   f.editor.mode = CWIKI_EDITOR_NORMAL;
   assert(cwiki_buffer_insert(&f.document.buffer, 0U, 0U, "X", 1U) == 0);
   assert(cwiki_render_frame(&f.layout, f.highlights, &f.editor,
       &f.view, output, sizeof(output), &length) == -1 && errno == EINVAL);
   assert(strcmp(output, "unchanged") == 0 && length == 42U);
   finish(&f);
}

static void
demo(bool command)
{
   struct fixture f;
   char output[8192];
   size_t length;

   init(&f, "# Chemistry / calculus\n"
       "Energy: $E = mc^2$, angle $\\alpha + \\beta$.\n"
       "Reaction: $\\ce{2H2 + O2 -> 2H2O}$\n"
       "  A long class-note sentence wraps with its indentation and marker.\n"
       "```c\nreturn energy;\n```", 9U, 48U);
   f.document.path = "/fixture/Class notes.md";
   f.document.dirty = true;
   f.editor.motion.cursor = (struct cwiki_position){ 1U, 9U };
   if (command) {
      f.editor.mode = CWIKI_EDITOR_COMMAND;
      f.editor.command = "wq";
      f.editor.command_length = 2U;
   }
   assert(cwiki_render_frame(&f.layout, f.highlights, &f.editor,
       &f.view, output, sizeof(output), &length) == 0);
   assert(fwrite(output, 1U, length, stdout) == length);
   finish(&f);
}

int
main(int argc, char **argv)
{
   if (argc == 2) {
      demo(strcmp(argv[1], "--command-demo") == 0);
      return EXIT_SUCCESS;
   }
   modes();
   unicode_conceal();
   wrap_scroll();
   clipping_raw();
   small_and_controls();
   roles_and_status();
   errors();
   (void)puts("render: byte-exact modes, Unicode, conceal, wrap, clipping, raw, "
       "scroll, cursor, small sizes, controls and transactional capacity checks passed");
   return EXIT_SUCCESS;
}
