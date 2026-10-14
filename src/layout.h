#ifndef CWIKI_LAYOUT_H
#define CWIKI_LAYOUT_H

#include "buffer.h"
#include "conceal.h"
#include "zone.h"

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
   /* Per-line layout inputs beyond the source bytes; a cached line is reused
    * only while these, the bytes and the window-wide options all match. */
   struct cwiki_zone_stack start_zones;
   bool concealed;
   struct cwiki_conceal_reveal reveal;
   /* Nonzero, unique within the window's lifetime, kept while the line is
    * reused: derived per-line state (highlights) may be cached by it. */
   uint64_t id;
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
   const struct cwiki_zone_engine *zones;
   const struct cwiki_conceal_table *conceal;
   uint64_t last_id;
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
/* Lines [*first_line, *end_line) own a row in [first_row, first_row + rows). */
void cwiki_layout_visible_lines(const struct cwiki_layout_window *window,
    size_t first_row, size_t rows, size_t *first_line, size_t *end_line);
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
