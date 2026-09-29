#ifndef CWIKI_LAYOUT_H
#define CWIKI_LAYOUT_H

#include "buffer.h"
#include "conceal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cwiki_zone_engine;

struct cwiki_layout_options {
   size_t content_width;
   bool wrap;
   bool break_indent;
   const char *continuation_marker;
   uint32_t conceal_categories;
   enum cwiki_conceal_mode mode;
   uint32_t concealcursor_modes;
   struct cwiki_position cursor;
   size_t reveal_line;
   struct cwiki_conceal_reveal reveal;
};

struct cwiki_layout_row {
   size_t line;
   size_t display_start;
   size_t display_end;
   size_t source_start;
   size_t source_end;
   size_t column_start;
   size_t column_end;
   size_t prefix_columns;
   size_t horizontal_extent;
   bool continuation;
   bool degraded;
};

struct cwiki_layout_line {
   char *source;
   size_t source_length;
   struct cwiki_conceal_line display;
   struct cwiki_layout_row *rows;
   size_t row_count;
   size_t first_row;
   size_t horizontal_extent;
   bool degraded;
   bool code;
};

struct cwiki_layout_window {
   struct cwiki_layout_line *lines;
   size_t line_count;
   size_t row_count;
   size_t content_width;
   bool wrap;
   bool break_indent;
   char *continuation_marker;
   size_t marker_columns;
   uint32_t conceal_categories;
   enum cwiki_conceal_mode mode;
   uint32_t concealcursor_modes;
   struct cwiki_position cursor;
   size_t reveal_line;
   struct cwiki_conceal_reveal reveal;
   bool valid;
};

enum cwiki_layout_range_kind {
   CWIKI_LAYOUT_RANGE_DISPLAY_ROWS,
   CWIKI_LAYOUT_RANGE_SOURCE_LINES
};

struct cwiki_layout_range {
   struct cwiki_position start;
   struct cwiki_position end;
   enum cwiki_layout_range_kind kind;
};

void cwiki_layout_window_init(struct cwiki_layout_window *window);
void cwiki_layout_window_free(struct cwiki_layout_window *window);
bool cwiki_layout_needs_rebuild(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer,
    const struct cwiki_layout_options *options);
int cwiki_layout_rebuild(struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, struct cwiki_zone_engine *zones,
    const struct cwiki_conceal_table *conceal,
    const struct cwiki_layout_options *options);
const struct cwiki_layout_row *cwiki_layout_row_at(
    const struct cwiki_layout_window *window, size_t row);
int cwiki_layout_source_to_display(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, struct cwiki_position source,
    size_t *display_row, size_t *display_column);
int cwiki_layout_display_to_source(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, size_t display_row,
    size_t display_column, struct cwiki_position *source);
int cwiki_layout_move_display_row(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, struct cwiki_position from, bool down,
    size_t *desired_display_column, struct cwiki_position *to,
    struct cwiki_layout_range *range);
int cwiki_layout_move_source_line(const struct cwiki_layout_window *window,
    const struct cwiki_buffer *buffer, struct cwiki_position from, bool down,
    size_t *desired_source_column, struct cwiki_position *to,
    struct cwiki_layout_range *range);

#endif
