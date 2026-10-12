#ifndef CWIKI_TEX_SOURCE_H
#define CWIKI_TEX_SOURCE_H

#include "buffer.h"
#include "config.h"
#include "tex_block.h"

#include <stddef.h>

/* Where a generated line came from (R1.8.14). */
enum cwiki_tex_origin {
   CWIKI_TEX_ORIGIN_TEMPLATE,
   CWIKI_TEX_ORIGIN_VAULT_PREAMBLE, /* source_line: preamble.tex line */
   CWIKI_TEX_ORIGIN_PACKAGES, /* source_line: index in `packages:` */
   CWIKI_TEX_ORIGIN_NOTE_PREAMBLE, /* source_line: line in `preamble:` text */
   CWIKI_TEX_ORIGIN_BLOCK /* source_line: note line */
};

/* All source_line values are 0-based; meaningless for the template. */
struct cwiki_tex_line {
   enum cwiki_tex_origin origin;
   size_t source_line;
};

struct cwiki_tex_source {
   char *text; /* NUL-terminated */
   size_t length;
   struct cwiki_tex_line *lines; /* one per generated line */
   size_t line_count;
   size_t *pages; /* pages[i] = index of the block on output page i */
   size_t page_count;
};

/*
 * Reads <vault_root>/.cwiki/preamble.tex (R2.8.4, R2.8.6). A missing file
 * yields *bytes == NULL and returns 0. A file that is unreadable or over
 * 1 MiB returns -1 and writes a message naming its path into `message`.
 */
int cwiki_tex_vault_preamble_read(const char *vault_root, char **bytes,
    size_t *length, char *message, size_t message_size);

/*
 * Builds one TeX document for one run: vault preamble, one \usepackage per
 * note package, the note `preamble:`, then the chosen blocks (indices into
 * `blocks`, caller order), one output page each. Block bytes are copied
 * unmodified. Fails with EINVAL for a block that is not compilable or an
 * out-of-range index. `vault` may be NULL.
 */
int cwiki_tex_generate(const struct cwiki_buffer *note,
    const struct cwiki_config_note_tex *tex, const char *vault,
    size_t vault_length, const struct cwiki_tex_extraction *blocks,
    const size_t *chosen, size_t chosen_count, struct cwiki_tex_source *out);
void cwiki_tex_source_free(struct cwiki_tex_source *source);

#endif
