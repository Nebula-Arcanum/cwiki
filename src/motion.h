#ifndef CWIKI_MOTION_H
#define CWIKI_MOTION_H

#include "buffer.h"
#include "conceal.h"

#include <stddef.h>

struct cwiki_layout_window;
struct cwiki_zone_engine;

enum cwiki_motion {
   CWIKI_MOTION_LEFT,
   CWIKI_MOTION_RIGHT,
   CWIKI_MOTION_WORD_FORWARD,
   CWIKI_MOTION_WORD_FORWARD_BIG,
   CWIKI_MOTION_WORD_BACKWARD,
   CWIKI_MOTION_WORD_BACKWARD_BIG,
   CWIKI_MOTION_WORD_END,
   CWIKI_MOTION_WORD_END_BIG,
   CWIKI_MOTION_DISPLAY_DOWN,
   CWIKI_MOTION_DISPLAY_UP,
   CWIKI_MOTION_SOURCE_DOWN,
   CWIKI_MOTION_SOURCE_UP,
   CWIKI_MOTION_DOCUMENT_FIRST,
   CWIKI_MOTION_DOCUMENT_LAST,
   CWIKI_MOTION_VIEWPORT_HIGH,
   CWIKI_MOTION_VIEWPORT_MIDDLE,
   CWIKI_MOTION_VIEWPORT_LOW,
   CWIKI_MOTION_LINE_START,
   CWIKI_MOTION_FIRST_NONBLANK,
   CWIKI_MOTION_LINE_END,
   CWIKI_MOTION_PREVIOUS_LINE,
   CWIKI_MOTION_NEXT_LINE,
   CWIKI_MOTION_LINE_FIRST_NONBLANK,
   CWIKI_MOTION_PARAGRAPH_BACKWARD,
   CWIKI_MOTION_PARAGRAPH_FORWARD,
   CWIKI_MOTION_SENTENCE_BACKWARD,
   CWIKI_MOTION_SENTENCE_FORWARD
};

enum cwiki_motion_shape {
   CWIKI_MOTION_CHARACTERWISE,
   CWIKI_MOTION_LINEWISE,
   CWIKI_MOTION_DISPLAY_ROWS
};

struct cwiki_motion_range {
   struct cwiki_position start;
   struct cwiki_position end;
   enum cwiki_motion_shape shape;
};

struct cwiki_motion_state {
   struct cwiki_position cursor;
   /* Initialize both desired columns to SIZE_MAX before the first motion. */
   size_t desired_display_column;
   size_t desired_source_column;
};

struct cwiki_motion_viewport {
   size_t first_display_row;
   size_t last_display_row;
};

struct cwiki_motion_result {
   struct cwiki_position destination;
   struct cwiki_motion_range range;
   size_t reveal_line;
   struct cwiki_conceal_reveal reveal;
};

/*
 * Ranges are normalized source half-open intervals. A linewise range ending
 * at EOF uses {buffer->line_count, 0} as its exclusive sentinel. Display-row
 * ranges contain exactly the source spans owned by the covered layout rows.
 */
int cwiki_motion_apply(const struct cwiki_buffer *buffer,
    const struct cwiki_layout_window *layout, struct cwiki_zone_engine *zones,
    struct cwiki_motion_state *state, enum cwiki_motion motion,
    const struct cwiki_motion_viewport *viewport,
    struct cwiki_motion_result *result);

#endif
