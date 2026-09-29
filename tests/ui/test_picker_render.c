#include "float.h"
#include "picker.h"
#include "picker_render.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIDE "\x1b[?25l"
#define SHOW "\x1b[?25h"
#define RESET "\x1b[0m"
#define DIM "\x1b[0;90m"
#define CYAN "\x1b[0;36m"
#define INVERSE "\x1b[0;7m"

static struct cwiki_picker *
picker(void)
{
   const struct cwiki_picker_spec items[] = {
      {"alpha", "Alpha", "Physics"},
      {"beta", "Beta", "Chemistry"},
      {"gamma", "Gamma", "Math"}
   };
   struct cwiki_picker *created = NULL;

   assert(cwiki_picker_init(&created, items,
       sizeof(items) / sizeof(items[0])) == CWIKI_PICKER_OK);
   return created;
}

static void
placement(void)
{
   struct cwiki_float_options options = {
      {2U, 3U, 10U, 20U}, 6U, 12U, CWIKI_FLOAT_CENTER, 0, 0
   };
   struct cwiki_float_area placed = {0};

   assert(cwiki_float_place(&options, &placed) == 0);
   assert(placed.top == 4U && placed.left == 7U &&
       placed.rows == 6U && placed.columns == 12U);
   options.anchor = CWIKI_FLOAT_SOUTH_EAST;
   options.row_offset = -1;
   options.column_offset = -2;
   assert(cwiki_float_place(&options, &placed) == 0);
   assert(placed.top == 5U && placed.left == 9U);
   options.rows = 99U;
   options.columns = 99U;
   options.anchor = CWIKI_FLOAT_NORTH_WEST;
   options.row_offset = 0;
   options.column_offset = 0;
   assert(cwiki_float_place(&options, &placed) == 0 &&
       placed.top == 2U && placed.left == 3U &&
       placed.rows == 10U && placed.columns == 20U);
   options.row_offset = 1;
   assert(cwiki_float_place(&options, &placed) == -1 && errno == EINVAL);
   options.row_offset = 0;
   options.area.rows = 0U;
   assert(cwiki_float_place(&options, &placed) == -1 && errno == EINVAL);
}

static void
expect_overlay(const struct cwiki_picker *model,
    const struct cwiki_float_area *area, enum cwiki_float_border border,
    const char *expected)
{
   char output[4096];
   char before[4096];
   size_t needed = 0U;
   size_t length = 777U;
   size_t capacity;

   assert(cwiki_picker_render_overlay(model, area, border, NULL, 0U,
       &needed) == 0);
   assert(needed == strlen(expected));
   (void)memset(output, '!', sizeof(output));
   (void)memcpy(before, output, sizeof(output));
   for (capacity = 0U; capacity <= needed; capacity++) {
      assert(cwiki_picker_render_overlay(model, area, border, output,
          capacity, &length) == -1 && errno == ENOSPC);
      assert(length == 777U && memcmp(output, before, sizeof(output)) == 0);
   }
   assert(cwiki_picker_render_overlay(model, area, border, output,
       needed + 1U, &length) == 0);
   assert(length == needed && strcmp(output, expected) == 0);
   assert(output[length + 1U] == '!');
}

static void
bordered_picker(void)
{
   struct cwiki_picker *model = picker();
   const struct cwiki_float_area area = {2U, 4U, 5U, 18U};
   const char *expected = HIDE
       "\x1b[2;4H" DIM "┌────────────────┐" RESET
       "\x1b[3;4H" DIM "│" RESET CYAN "> " RESET "a             "
       RESET DIM "│" RESET
       "\x1b[4;4H" DIM "│" RESET RESET "Alpha  Physics  " RESET
       DIM "│" RESET
       "\x1b[5;4H" DIM "│" RESET INVERSE "Gamma  Math     " RESET
       DIM "│" RESET
       "\x1b[6;4H" DIM "└────────────────┘" RESET
       "\x1b[3;8H" SHOW;

   assert(cwiki_picker_insert(model, "a", 1U) == CWIKI_PICKER_OK);
   assert(cwiki_picker_select_next(model) == CWIKI_PICKER_OK);
   expect_overlay(model, &area, CWIKI_FLOAT_BORDER_SINGLE, expected);
   cwiki_picker_free(model);
}

static void
scroll_empty_and_unicode_clip(void)
{
   struct cwiki_picker *model = picker();
   struct cwiki_float_area area = {1U, 1U, 4U, 14U};
   char output[2048];
   size_t length = 0U;

   assert(cwiki_picker_select_next(model) == CWIKI_PICKER_OK);
   assert(cwiki_picker_select_next(model) == CWIKI_PICKER_OK);
   assert(cwiki_picker_render_overlay(model, &area,
       CWIKI_FLOAT_BORDER_SINGLE, output, sizeof(output), &length) == 0);
   assert(strstr(output, "Gamma  Math") != NULL);
   assert(strstr(output, "Alpha") == NULL && strstr(output, "Beta") == NULL);
   assert(cwiki_picker_insert(model, "zzz", 3U) == CWIKI_PICKER_OK);
   assert(cwiki_picker_render_overlay(model, &area,
       CWIKI_FLOAT_BORDER_SINGLE, output, sizeof(output), &length) == 0);
   assert(strstr(output, "No matches") != NULL);
   assert(cwiki_picker_control(model, 1U) == CWIKI_PICKER_OK);
   assert(cwiki_picker_control(model, 11U) == CWIKI_PICKER_OK);
   assert(cwiki_picker_insert(model, "界abc", strlen("界abc")) ==
       CWIKI_PICKER_OK);
   area = (struct cwiki_float_area){1U, 1U, 2U, 6U};
   assert(cwiki_picker_render_overlay(model, &area,
       CWIKI_FLOAT_BORDER_NONE, output, sizeof(output), &length) == 0);
   assert(strstr(output, "abc ") != NULL && strstr(output, "界") == NULL);
   assert(strstr(output, "\x1b[1;6H" SHOW) != NULL);
   cwiki_picker_free(model);
}

static void
errors(void)
{
   struct cwiki_picker *model = picker();
   struct cwiki_float_area area = {1U, 1U, 3U, 4U};
   char output[32] = "unchanged";
   size_t length = 42U;

   assert(cwiki_picker_render_overlay(model, &area,
       CWIKI_FLOAT_BORDER_SINGLE, output, sizeof(output), &length) == -1 &&
       errno == EINVAL && strcmp(output, "unchanged") == 0 && length == 42U);
   area = (struct cwiki_float_area){4096U, 1U, 2U, 6U};
   assert(cwiki_picker_render_overlay(model, &area,
       CWIKI_FLOAT_BORDER_NONE, output, sizeof(output), &length) == -1 &&
       errno == EINVAL && strcmp(output, "unchanged") == 0 && length == 42U);
   cwiki_picker_free(model);
}

static void
demo(void)
{
   struct cwiki_picker *model = picker();
   const struct cwiki_float_options options = {
      {1U, 1U, 12U, 60U}, 7U, 38U, CWIKI_FLOAT_CENTER, 0, 0
   };
   struct cwiki_float_area area;
   char output[4096];
   size_t length;

   assert(cwiki_picker_insert(model, "a", 1U) == CWIKI_PICKER_OK);
   assert(cwiki_picker_select_next(model) == CWIKI_PICKER_OK);
   assert(cwiki_float_place(&options, &area) == 0);
   (void)fputs("\x1b[2J\x1b[H\x1b[0m# Chemistry / calculus\n\n"
       "Energy: $E = mc^2$\nReaction: 2H2 + O2 -> 2H2O\n\n"
       "Use the picker to jump to a note.\n", stdout);
   assert(cwiki_picker_render_overlay(model, &area,
       CWIKI_FLOAT_BORDER_SINGLE, output, sizeof(output), &length) == 0);
   assert(fwrite(output, 1U, length, stdout) == length);
   cwiki_picker_free(model);
}

int
main(int argc, char **argv)
{
   if (argc == 2 && strcmp(argv[1], "--demo") == 0) {
      demo();
      return EXIT_SUCCESS;
   }
   placement();
   bordered_picker();
   scroll_empty_and_unicode_clip();
   errors();
   (void)puts("picker render tests: ok");
   return EXIT_SUCCESS;
}
