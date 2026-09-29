#ifndef CWIKI_HIGHLIGHT_H
#define CWIKI_HIGHLIGHT_H

#include <stdbool.h>
#include <stddef.h>

struct cwiki_buffer;
struct cwiki_zone_engine;

enum cwiki_highlight_role {
   CWIKI_HIGHLIGHT_PROSE,
   CWIKI_HIGHLIGHT_CODE,
   CWIKI_HIGHLIGHT_MATH_INLINE,
   CWIKI_HIGHLIGHT_MATH_DISPLAY,
   CWIKI_HIGHLIGHT_CHEMISTRY,
   CWIKI_HIGHLIGHT_TEXT,
   CWIKI_HIGHLIGHT_REFERENCE,
   CWIKI_HIGHLIGHT_LATEX,
   CWIKI_HIGHLIGHT_TIKZ,
   CWIKI_HIGHLIGHT_COMMENT,
   CWIKI_HIGHLIGHT_CUSTOM,
   CWIKI_HIGHLIGHT_RAW,
   CWIKI_HIGHLIGHT_CODE_DELIMITER,
   CWIKI_HIGHLIGHT_MATH_INLINE_DELIMITER,
   CWIKI_HIGHLIGHT_MATH_DISPLAY_DELIMITER,
   CWIKI_HIGHLIGHT_CHEMISTRY_DELIMITER,
   CWIKI_HIGHLIGHT_TEXT_DELIMITER,
   CWIKI_HIGHLIGHT_REFERENCE_DELIMITER,
   CWIKI_HIGHLIGHT_LATEX_DELIMITER,
   CWIKI_HIGHLIGHT_TIKZ_DELIMITER,
   CWIKI_HIGHLIGHT_COMMENT_DELIMITER,
   CWIKI_HIGHLIGHT_CUSTOM_DELIMITER,
   CWIKI_HIGHLIGHT_ROLE_COUNT
};

struct cwiki_highlight_run {
   size_t source_start;
   size_t source_end;
   enum cwiki_highlight_role role;
};

struct cwiki_highlight_line {
   const struct cwiki_highlight_run *runs;
   size_t run_count;
   bool degraded;
};

const char *cwiki_highlight_role_name(enum cwiki_highlight_role role);
int cwiki_highlight_build_line(struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *buffer, size_t line,
    struct cwiki_highlight_line *result);
void cwiki_highlight_line_free(struct cwiki_highlight_line *line);

#endif
