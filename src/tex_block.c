#include "tex_block.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IGNORE_DETAIL UINT16_MAX

struct cursor {
   struct cwiki_tex_extraction *out;
   size_t block_capacity;
   size_t report_capacity;
   bool open;
   bool untrusted;
};

static int
grow(void **items, size_t count, size_t *capacity, size_t size)
{
   void *grown;
   size_t wanted;

   if (count < *capacity) {
      return 0;
   }
   wanted = *capacity == 0U ? 8U : *capacity;
   if (wanted > SIZE_MAX / 2U / size) {
      errno = ENOMEM;
      return -1;
   }
   if (count >= wanted) {
      wanted *= 2U;
   }
   grown = realloc(*items, wanted * size);
   if (grown == NULL) {
      return -1;
   }
   *items = grown;
   *capacity = wanted;
   return 0;
}

static char *
copy_string(const char *text)
{
   size_t length = strlen(text) + 1U;
   char *copy = malloc(length);

   if (copy != NULL) {
      memcpy(copy, text, length);
   }
   return copy;
}

static bool
renderable(enum cwiki_zone_kind kind, enum cwiki_tex_block_kind *block_kind)
{
   switch (kind) {
   case CWIKI_ZONE_MATH_INLINE:
      *block_kind = CWIKI_TEX_BLOCK_INLINE;
      return true;
   case CWIKI_ZONE_MATH_DISPLAY:
      *block_kind = CWIKI_TEX_BLOCK_DISPLAY;
      return true;
   case CWIKI_ZONE_CHEMISTRY:
      *block_kind = CWIKI_TEX_BLOCK_CHEMISTRY;
      return true;
   case CWIKI_ZONE_TIKZ:
   case CWIKI_ZONE_LATEX:
      *block_kind = CWIKI_TEX_BLOCK_ENVIRONMENT;
      return true;
   default:
      return false;
   }
}

static int
start_block(struct cursor *cursor, struct cwiki_zone_engine *zones,
    enum cwiki_tex_block_kind kind, uint16_t detail, size_t line, size_t byte)
{
   struct cwiki_tex_block *block;

   if (grow((void **)&cursor->out->blocks, cursor->out->block_count,
       &cursor->block_capacity, sizeof(*cursor->out->blocks)) != 0) {
      return -1;
   }
   block = &cursor->out->blocks[cursor->out->block_count];
   memset(block, 0, sizeof(*block));
   block->kind = kind;
   block->first_line = line;
   block->first_byte = byte;
   block->last_line = line;
   if (kind == CWIKI_TEX_BLOCK_ENVIRONMENT) {
      const char *name = cwiki_zone_detail(zones, detail);

      block->env = copy_string(name == NULL ? "" : name);
      if (block->env == NULL) {
         return -1;
      }
   }
   cursor->out->block_count++;
   cursor->open = true;
   cursor->untrusted = false;
   return 0;
}

static int
report(struct cursor *cursor, const struct cwiki_tex_block *block,
    enum cwiki_tex_report_reason reason)
{
   struct cwiki_tex_report *entry;

   if (grow((void **)&cursor->out->reports, cursor->out->report_count,
       &cursor->report_capacity, sizeof(*cursor->out->reports)) != 0) {
      return -1;
   }
   entry = &cursor->out->reports[cursor->out->report_count];
   entry->reason = reason;
   entry->line = block->first_line;
   entry->name = block->env == NULL ? NULL : copy_string(block->env);
   if (block->env != NULL && entry->name == NULL) {
      return -1;
   }
   cursor->out->report_count++;
   return 0;
}

/* Closes the open block; a block that did not end properly is reported. */
static int
finish_block(struct cursor *cursor, size_t line, size_t end, bool terminated)
{
   struct cwiki_tex_block *block =
       &cursor->out->blocks[cursor->out->block_count - 1U];

   block->last_line = line;
   block->last_end = end;
   cursor->open = false;
   if (!terminated) {
      block->compilable = false;
      return report(cursor, block, CWIKI_TEX_REPORT_UNTERMINATED);
   }
   block->compilable = !cursor->untrusted;
   if (cursor->untrusted) {
      return report(cursor, block, CWIKI_TEX_REPORT_UNTRUSTED);
   }
   return 0;
}

static bool
counts_in_depth(const struct cwiki_zone_span *span)
{
   /* A `%` comment ends with its line and never emits a close span. */
   return !(span->zone.kind == CWIKI_ZONE_COMMENT_PERCENT &&
       span->zone.detail != IGNORE_DETAIL);
}

static int
scan_line(struct cursor *cursor, struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *body, size_t index, size_t offset)
{
   struct cwiki_zone_line spans;
   size_t depth = index == 0U ? 0U : body->lines[index - 1U].end_zones.depth;
   size_t line = index + offset;
   size_t span;
   int status = 0;

   if (cwiki_zone_build_line(zones, body, index, &spans) != 0) {
      return -1;
   }
   if (cursor->open && cwiki_buffer_line_degraded(body, index)) {
      cursor->untrusted = true;
   }
   if (spans.degraded && !cursor->open && depth == 0U &&
       body->lines[index].end_zones.depth != 0U) {
      enum cwiki_tex_block_kind kind;
      const struct cwiki_zone *top = &body->lines[index].end_zones.zones[0];

      if (renderable(top->kind, &kind)) {
         status = start_block(cursor, zones, kind, top->detail, line, 0U);
         cursor->untrusted = true;
      }
   }
   for (span = 0U; status == 0 && span < spans.span_count; span++) {
      const struct cwiki_zone_span *item = &spans.spans[span];
      enum cwiki_tex_block_kind kind;

      if (item->token == CWIKI_ZONE_OPEN) {
         if (cursor->open && item->zone.kind == CWIKI_ZONE_COMMENT_PERCENT &&
             item->zone.detail == IGNORE_DETAIL) {
            /* SynIgnore resets the stack: the open block never closes. */
            size_t previous = line - 1U;

            status = finish_block(cursor, previous,
                body->lines[index - 1U].length, false);
            depth = 0U;
         }
         if (status == 0 && !cursor->open && depth == 0U &&
             renderable(item->zone.kind, &kind)) {
            status = start_block(cursor, zones, kind, item->zone.detail,
                line, item->source_start);
         }
         if (counts_in_depth(item)) {
            depth++;
         }
      } else if (item->token == CWIKI_ZONE_CLOSE) {
         if (depth != 0U) {
            depth--;
         }
         if (status == 0 && cursor->open && depth == 0U) {
            status = finish_block(cursor, line, item->source_end, true);
         }
      }
   }
   cwiki_zone_line_free(&spans);
   if (status == 0 && cursor->open &&
       body->lines[index].end_zones.depth == 0U) {
      /* The stack emptied without a close span (degraded line). */
      status = finish_block(cursor, line, body->lines[index].length, true);
   }
   return status;
}

int
cwiki_tex_extract(struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *note, size_t body_first_line,
    struct cwiki_tex_extraction *out)
{
   struct cursor cursor;
   struct cwiki_buffer body;
   size_t first = body_first_line;
   size_t index;
   size_t total = 0U;
   size_t scanned;
   char *joined;
   size_t at = 0U;
   int status = 0;

   if (zones == NULL || note == NULL || out == NULL) {
      errno = EINVAL;
      return -1;
   }
   memset(out, 0, sizeof(*out));
   memset(&cursor, 0, sizeof(cursor));
   cursor.out = out;
   if (first >= note->line_count) {
      return 0;
   }
   for (index = first; index < note->line_count; index++) {
      if (note->lines[index].length > SIZE_MAX - total - 1U) {
         errno = ENOMEM;
         return -1;
      }
      total += note->lines[index].length + 1U;
   }
   joined = malloc(total);
   if (joined == NULL) {
      return -1;
   }
   for (index = first; index < note->line_count; index++) {
      memcpy(joined + at, note->lines[index].bytes, note->lines[index].length);
      at += note->lines[index].length;
      if (index + 1U < note->line_count) {
         joined[at++] = '\n';
      }
   }
   if (cwiki_buffer_init(&body) != 0) {
      free(joined);
      return -1;
   }
   if (cwiki_buffer_load(&body, joined, at) != 0 ||
       cwiki_zone_recompute(zones, &body, 0U, &scanned) != 0) {
      status = -1;
   }
   free(joined);
   for (index = 0U; status == 0 && index < body.line_count; index++) {
      status = scan_line(&cursor, zones, &body, index, first);
   }
   if (status == 0 && cursor.open) {
      status = finish_block(&cursor, first + body.line_count - 1U,
          body.lines[body.line_count - 1U].length, false);
   }
   cwiki_buffer_free(&body);
   if (status != 0) {
      int saved = errno;

      cwiki_tex_extraction_free(out);
      errno = saved;
   }
   return status;
}

void
cwiki_tex_extraction_free(struct cwiki_tex_extraction *extraction)
{
   size_t index;

   if (extraction == NULL) {
      return;
   }
   for (index = 0U; index < extraction->block_count; index++) {
      free(extraction->blocks[index].env);
   }
   for (index = 0U; index < extraction->report_count; index++) {
      free(extraction->reports[index].name);
   }
   free(extraction->blocks);
   free(extraction->reports);
   memset(extraction, 0, sizeof(*extraction));
}

int
cwiki_tex_block_bytes(const struct cwiki_buffer *note,
    const struct cwiki_tex_block *block, char **bytes, size_t *length)
{
   size_t line;
   size_t total = 0U;
   size_t at = 0U;
   char *out;

   if (note == NULL || block == NULL || bytes == NULL || length == NULL ||
       block->last_line >= note->line_count ||
       block->first_line > block->last_line ||
       block->first_byte > note->lines[block->first_line].length ||
       block->last_end > note->lines[block->last_line].length ||
       (block->first_line == block->last_line &&
       block->first_byte > block->last_end)) {
      errno = EINVAL;
      return -1;
   }
   for (line = block->first_line; line <= block->last_line; line++) {
      size_t from = line == block->first_line ? block->first_byte : 0U;
      size_t to = line == block->last_line ? block->last_end :
          note->lines[line].length;

      total += to - from + 1U;
   }
   out = malloc(total);
   if (out == NULL) {
      return -1;
   }
   for (line = block->first_line; line <= block->last_line; line++) {
      size_t from = line == block->first_line ? block->first_byte : 0U;
      size_t to = line == block->last_line ? block->last_end :
          note->lines[line].length;

      memcpy(out + at, note->lines[line].bytes + from, to - from);
      at += to - from;
      if (line != block->last_line) {
         out[at++] = '\n';
      }
   }
   out[at] = '\0';
   *bytes = out;
   *length = at;
   return 0;
}
