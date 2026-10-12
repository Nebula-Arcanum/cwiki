#ifndef CWIKI_TEX_BLOCK_H
#define CWIKI_TEX_BLOCK_H

#include "buffer.h"
#include "zone.h"

#include <stdbool.h>
#include <stddef.h>

enum cwiki_tex_block_kind {
   CWIKI_TEX_BLOCK_INLINE,
   CWIKI_TEX_BLOCK_DISPLAY,
   CWIKI_TEX_BLOCK_CHEMISTRY,
   CWIKI_TEX_BLOCK_ENVIRONMENT
};

/* One renderable zone. Lines are 0-based note line indices; the block runs
 * from first_byte on first_line to last_end (exclusive) on last_line. */
struct cwiki_tex_block {
   enum cwiki_tex_block_kind kind;
   char *env; /* environment name; NULL for other kinds */
   size_t first_line;
   size_t first_byte;
   size_t last_line;
   size_t last_end;
   bool compilable;
};

enum cwiki_tex_report_reason {
   CWIKI_TEX_REPORT_UNTERMINATED,
   CWIKI_TEX_REPORT_UNTRUSTED
};

struct cwiki_tex_report {
   enum cwiki_tex_report_reason reason;
   char *name; /* environment name when known, else NULL */
   size_t line; /* 0-based note line where the block starts */
};

struct cwiki_tex_extraction {
   struct cwiki_tex_block *blocks;
   size_t block_count;
   struct cwiki_tex_report *reports;
   size_t report_count;
};

/*
 * Finds the outermost renderable zones (R2.3, R2.9.1) in the note body, the
 * lines from body_first_line on, using only the zone engine's data. The body
 * is scanned from an empty zone stack so frontmatter text cannot leak zone
 * state. Unterminated blocks and blocks touching a degraded line are kept,
 * not compilable, and reported (R2.9.7). Returns 0, or -1 with errno set.
 */
int cwiki_tex_extract(struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *note, size_t body_first_line,
    struct cwiki_tex_extraction *out);
void cwiki_tex_extraction_free(struct cwiki_tex_extraction *extraction);

/* Copies the block's exact note bytes (lines joined by LF, NUL-terminated). */
int cwiki_tex_block_bytes(const struct cwiki_buffer *note,
    const struct cwiki_tex_block *block, char **bytes, size_t *length);

#endif
