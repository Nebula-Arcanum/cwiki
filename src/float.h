#ifndef CWIKI_FLOAT_H
#define CWIKI_FLOAT_H

#include <stddef.h>
#include <stdint.h>

enum cwiki_float_anchor {
   CWIKI_FLOAT_NORTH_WEST,
   CWIKI_FLOAT_NORTH,
   CWIKI_FLOAT_NORTH_EAST,
   CWIKI_FLOAT_WEST,
   CWIKI_FLOAT_CENTER,
   CWIKI_FLOAT_EAST,
   CWIKI_FLOAT_SOUTH_WEST,
   CWIKI_FLOAT_SOUTH,
   CWIKI_FLOAT_SOUTH_EAST
};

enum cwiki_float_border {
   CWIKI_FLOAT_BORDER_NONE,
   CWIKI_FLOAT_BORDER_SINGLE
};

struct cwiki_float_area {
   size_t top;
   size_t left;
   size_t rows;
   size_t columns;
};

struct cwiki_float_options {
   struct cwiki_float_area area;
   size_t rows;
   size_t columns;
   enum cwiki_float_anchor anchor;
   ptrdiff_t row_offset;
   ptrdiff_t column_offset;
};

/* Requested dimensions are clipped to the containing area. */
int cwiki_float_place(const struct cwiki_float_options *options,
    struct cwiki_float_area *placed);

#endif
