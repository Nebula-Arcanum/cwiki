#ifndef CWIKI_RENDER_H
#define CWIKI_RENDER_H

#include <stddef.h>
#include <stdbool.h>

struct cwiki_editor;
struct cwiki_highlight_line;
struct cwiki_layout_window;

struct cwiki_render_viewport {
   size_t rows; /* Includes the persistent final status row; both sizes > 0. */
   size_t columns; /* Terminal columns, including any line-number gutter. */
   size_t first_row; /* Absolute display row, not source line. */
   size_t horizontal_offset; /* Applies only to unwrapped/code/raw lines. */
   bool line_numbers;
   bool cursorline;
};

/* Leaves at least one content column; returns 0 when columns cannot fit both. */
size_t cwiki_render_line_number_width(size_t source_lines, size_t columns);

/*
 * Build a complete ANSI frame, without I/O or allocation. The application owns
 * cwiki_terminal_begin_update/end_update around writing this payload, including
 * ending the update on write failure. No synchronized-update bytes occur here.
 * Layout and highlights must describe the current document; highlights contains
 * one entry per source line. NULL highlights selects unstyled source (raw rows
 * remain visibly marked). Inputs must not alias output and must remain stable.
 * layout.content_width must equal viewport.columns minus the line-number width
 * when line_numbers is enabled, and viewport.columns otherwise.
 *
 * NULL bytes with capacity 0 queries the required byte length (excluding NUL).
 * Otherwise capacity must be > length. On any error (-1, errno), output and
 * *length remain unchanged. On success output is NUL-terminated. Dimensions are
 * limited to 4096 cells each. No terminal state changes occur until the caller
 * writes the completed payload.
 *
 * Status: MODE basename [+] line:column, with 1-based source-cell coordinates.
 * Command mode replaces it with ':' and the command tail, keeping its end cursor
 * visible. Off-viewport source cursors are hidden; EOL at the right edge clamps
 * to the last cell. Controls and standalone zero-width clusters are suppressed
 * (matching layout's zero-cell widths); text never supplies terminal escapes.
 */
int cwiki_render_frame(const struct cwiki_layout_window *layout,
    const struct cwiki_highlight_line *highlights,
    const struct cwiki_editor *editor,
    const struct cwiki_render_viewport *viewport,
    char *bytes, size_t capacity, size_t *length);

#endif
