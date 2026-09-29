#include "float.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

static bool
valid_anchor(enum cwiki_float_anchor anchor)
{
   return anchor >= CWIKI_FLOAT_NORTH_WEST &&
       anchor <= CWIKI_FLOAT_SOUTH_EAST;
}

static size_t
vertical_position(const struct cwiki_float_options *options, size_t rows)
{
   size_t spare = options->area.rows - rows;

   if (options->anchor <= CWIKI_FLOAT_NORTH_EAST) {
      return options->area.top;
   }
   if (options->anchor <= CWIKI_FLOAT_EAST) {
      return options->area.top + spare / 2U;
   }
   return options->area.top + spare;
}

static size_t
horizontal_position(const struct cwiki_float_options *options, size_t columns)
{
   size_t spare = options->area.columns - columns;
   unsigned int horizontal = (unsigned int)options->anchor % 3U;

   if (horizontal == 0U) {
      return options->area.left;
   }
   if (horizontal == 1U) {
      return options->area.left + spare / 2U;
   }
   return options->area.left + spare;
}

static bool
offset_position(size_t value, ptrdiff_t offset, size_t minimum,
    size_t maximum, size_t *result)
{
   size_t moved;

   if (offset >= 0) {
      size_t amount = (size_t)offset;

      if (amount > maximum - value) {
         return false;
      }
      moved = value + amount;
   } else {
      size_t amount = (size_t)(-(offset + 1)) + 1U;

      if (amount > value - minimum) {
         return false;
      }
      moved = value - amount;
   }
   *result = moved;
   return true;
}

int
cwiki_float_place(const struct cwiki_float_options *options,
    struct cwiki_float_area *placed)
{
   size_t rows;
   size_t columns;
   size_t maximum_top;
   size_t maximum_left;
   size_t top;
   size_t left;

   if (options == NULL || placed == NULL || options->area.top == 0U ||
       options->area.left == 0U || options->area.rows == 0U ||
       options->area.columns == 0U || options->rows == 0U ||
       options->columns == 0U || !valid_anchor(options->anchor) ||
       options->area.top > SIZE_MAX - options->area.rows ||
       options->area.left > SIZE_MAX - options->area.columns) {
      errno = EINVAL;
      return -1;
   }
   rows = options->rows < options->area.rows ? options->rows :
       options->area.rows;
   columns = options->columns < options->area.columns ? options->columns :
       options->area.columns;
   maximum_top = options->area.top + options->area.rows - rows;
   maximum_left = options->area.left + options->area.columns - columns;
   top = vertical_position(options, rows);
   left = horizontal_position(options, columns);
   if (!offset_position(top, options->row_offset, options->area.top,
       maximum_top, &top) ||
       !offset_position(left, options->column_offset, options->area.left,
       maximum_left, &left)) {
      errno = EINVAL;
      return -1;
   }
   placed->top = top;
   placed->left = left;
   placed->rows = rows;
   placed->columns = columns;
   return 0;
}
