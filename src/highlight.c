#include "highlight.h"

#include "buffer.h"
#include "unicode.h"
#include "zone.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char *const role_names[CWIKI_HIGHLIGHT_ROLE_COUNT] = {
   "prose", "code", "inline math", "display math", "chemistry",
   "text hole", "reference", "LaTeX", "TikZ", "comment", "custom",
   "raw"
};

static const enum cwiki_highlight_role zone_roles[] = {
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
   CWIKI_HIGHLIGHT_COMMENT,
   CWIKI_HIGHLIGHT_COMMENT,
   CWIKI_HIGHLIGHT_CUSTOM
};

const char *
cwiki_highlight_role_name(enum cwiki_highlight_role role)
{
   if (role < 0 || role >= CWIKI_HIGHLIGHT_ROLE_COUNT) {
      return NULL;
   }
   return role_names[role];
}

static int
raw_line(size_t length, struct cwiki_highlight_line *result)
{
   struct cwiki_highlight_run *run;

   result->degraded = true;
   if (length == 0U) {
      return 0;
   }
   run = malloc(sizeof(*run));
   if (run == NULL) {
      return -1;
   }
   run->source_start = 0U;
   run->source_end = length;
   run->role = CWIKI_HIGHLIGHT_RAW;
   result->runs = run;
   result->run_count = 1U;
   return 0;
}

int
cwiki_highlight_build_line(struct cwiki_zone_engine *zones,
    const struct cwiki_buffer *buffer, size_t line,
    struct cwiki_highlight_line *result)
{
   struct cwiki_highlight_run *runs;
   const struct cwiki_line *source;
   size_t offset = 0U;
   size_t count = 0U;

   if (zones == NULL || buffer == NULL || result == NULL ||
       line >= buffer->line_count || buffer->lines[line].zone_dirty ||
       (line != 0U && buffer->lines[line - 1U].zone_dirty)) {
      errno = EINVAL;
      return -1;
   }
   (void)memset(result, 0, sizeof(*result));
   source = &buffer->lines[line];
   if (cwiki_buffer_line_degraded(buffer, line)) {
      return raw_line(source->length, result);
   }
   if (source->length == 0U) {
      return 0;
   }
   if (source->length > SIZE_MAX / sizeof(*runs)) {
      errno = ENOMEM;
      return -1;
   }
   runs = malloc(source->length * sizeof(*runs));
   if (runs == NULL) {
      return -1;
   }
   while (offset < source->length) {
      struct cwiki_zone zone;
      enum cwiki_highlight_role role;
      size_t next = cwiki_grapheme_next(source->bytes, source->length, offset);

      if (next == SIZE_MAX || cwiki_zone_at(zones, buffer, line, offset,
          &zone) != 0 || zone.kind < 0 ||
          (size_t)zone.kind >= sizeof(zone_roles) / sizeof(zone_roles[0])) {
         free(runs);
         errno = EINVAL;
         return -1;
      }
      role = zone_roles[zone.kind];
      if (count != 0U && runs[count - 1U].role == role) {
         runs[count - 1U].source_end = next;
      } else {
         runs[count].source_start = offset;
         runs[count].source_end = next;
         runs[count].role = role;
         count++;
      }
      offset = next;
   }
   result->runs = runs;
   result->run_count = count;
   return 0;
}

void
cwiki_highlight_line_free(struct cwiki_highlight_line *line)
{
   if (line != NULL) {
      free((void *)line->runs);
      (void)memset(line, 0, sizeof(*line));
   }
}
