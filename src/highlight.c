#include "highlight.h"

#include "buffer.h"
#include "zone.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char *const role_names[CWIKI_HIGHLIGHT_ROLE_COUNT] = {
   "prose", "code", "inline math", "display math", "chemistry",
   "text hole", "reference", "LaTeX", "TikZ", "comment", "custom",
   "raw", "code delimiter", "inline math delimiter", "display math delimiter",
   "chemistry delimiter", "text hole delimiter", "reference delimiter",
   "LaTeX delimiter", "TikZ delimiter", "comment delimiter", "custom delimiter"
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

static const enum cwiki_highlight_role delimiter_roles[] = {
   CWIKI_HIGHLIGHT_PROSE,
   CWIKI_HIGHLIGHT_CODE_DELIMITER,
   CWIKI_HIGHLIGHT_MATH_INLINE_DELIMITER,
   CWIKI_HIGHLIGHT_MATH_DISPLAY_DELIMITER,
   CWIKI_HIGHLIGHT_CHEMISTRY_DELIMITER,
   CWIKI_HIGHLIGHT_TEXT_DELIMITER,
   CWIKI_HIGHLIGHT_REFERENCE_DELIMITER,
   CWIKI_HIGHLIGHT_LATEX_DELIMITER,
   CWIKI_HIGHLIGHT_TIKZ_DELIMITER,
   CWIKI_HIGHLIGHT_COMMENT_DELIMITER,
   CWIKI_HIGHLIGHT_COMMENT_DELIMITER,
   CWIKI_HIGHLIGHT_COMMENT_DELIMITER,
   CWIKI_HIGHLIGHT_CUSTOM_DELIMITER
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
   struct cwiki_zone_line parsed;
   size_t index;
   size_t count = 0U;

   if (result == NULL) {
      errno = EINVAL;
      return -1;
   }
   if (cwiki_zone_build_line(zones, buffer, line, &parsed) != 0) {
      return -1;
   }
   (void)memset(result, 0, sizeof(*result));
   if (parsed.degraded) {
      cwiki_zone_line_free(&parsed);
      return raw_line(buffer->lines[line].length, result);
   }
   if (parsed.span_count == 0U) {
      cwiki_zone_line_free(&parsed);
      return 0;
   }
   if (parsed.span_count > SIZE_MAX / sizeof(*runs)) {
      cwiki_zone_line_free(&parsed);
      errno = ENOMEM;
      return -1;
   }
   runs = malloc(parsed.span_count * sizeof(*runs));
   if (runs == NULL) {
      cwiki_zone_line_free(&parsed);
      return -1;
   }
   for (index = 0U; index < parsed.span_count; index++) {
      const struct cwiki_zone_span *span = &parsed.spans[index];
      enum cwiki_highlight_role role;

      if (span->zone.kind < 0 || (size_t)span->zone.kind >=
          sizeof(zone_roles) / sizeof(zone_roles[0])) {
         free(runs);
         cwiki_zone_line_free(&parsed);
         errno = EINVAL;
         return -1;
      }
      role = span->token == CWIKI_ZONE_CONTENT ? zone_roles[span->zone.kind] :
          delimiter_roles[span->zone.kind];
      if (count != 0U && runs[count - 1U].role == role) {
         runs[count - 1U].source_end = span->source_end;
      } else {
         runs[count].source_start = span->source_start;
         runs[count].source_end = span->source_end;
         runs[count].role = role;
         count++;
      }
   }
   cwiki_zone_line_free(&parsed);
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
