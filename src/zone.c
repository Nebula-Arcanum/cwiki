#include "zone.h"

#include "buffer.h"
#include "regex.h"
#include "unicode.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define UNESCAPED "(?<!\\\\)(?:\\\\\\\\)*\\K"
#define BIT(index) CWIKI_ZONE_REGION_BIT(index)

enum builtin_region {
   REGION_CODE,
   REGION_COMMENT_BLOCK,
   REGION_COMMENT_INLINE,
   REGION_COMMENT_HTML,
   REGION_MATH_DISPLAY_DOLLAR,
   REGION_MATH_DISPLAY_BRACKET,
   REGION_MATH_INLINE_DOLLAR,
   REGION_MATH_INLINE_PAREN,
   REGION_TIKZ,
   REGION_LATEX,
   REGION_CHEMISTRY,
   REGION_TEXT,
   REGION_INTERTEXT,
   REGION_REFERENCE,
   REGION_COMMENT_PERCENT,
   REGION_COUNT
};

#define EVERYWHERE_COMMENTS \
   (BIT(REGION_COMMENT_BLOCK) | BIT(REGION_COMMENT_INLINE) | \
    BIT(REGION_COMMENT_HTML))
#define MATH_CHILDREN \
   (EVERYWHERE_COMMENTS | BIT(REGION_CHEMISTRY) | BIT(REGION_TEXT) | \
    BIT(REGION_INTERTEXT) | BIT(REGION_REFERENCE) | \
    BIT(REGION_COMMENT_PERCENT) | BIT(REGION_TIKZ) | BIT(REGION_LATEX))
#define LATEX_CHILDREN \
   (EVERYWHERE_COMMENTS | BIT(REGION_MATH_DISPLAY_DOLLAR) | \
    BIT(REGION_MATH_DISPLAY_BRACKET) | BIT(REGION_MATH_INLINE_DOLLAR) | \
    BIT(REGION_MATH_INLINE_PAREN) | BIT(REGION_REFERENCE) | \
    BIT(REGION_COMMENT_PERCENT) | BIT(REGION_TIKZ) | BIT(REGION_LATEX))

static const struct cwiki_zone_region builtin_regions[REGION_COUNT] = {
   {CWIKI_ZONE_CODE, "fenced code", "^[ \\t]{0,3}```[ \\t]*([^ `\\t]*)[^\\r\\n]*$",
       "^[ \\t]{0,3}```+[ \\t]*$", NULL, 0U, 0U, 1U, 0U, false},
   {CWIKI_ZONE_COMMENT_NOTE, "note block comment", "^[ \\t]*%%[ \\t]*$",
       "^[ \\t]*%%[ \\t]*$", NULL, 0U, 0U, 0U, 0U, false},
   {CWIKI_ZONE_COMMENT_NOTE, "note inline comment", UNESCAPED "%%",
       UNESCAPED "%%", NULL, 0U, 0U, 0U, 0U, false},
   {CWIKI_ZONE_COMMENT_HTML, "HTML comment", "<!--", "-->", NULL, 0U, 0U,
       0U, 0U, false},
   {CWIKI_ZONE_MATH_DISPLAY, "display math dollars", UNESCAPED "\\$\\$",
       UNESCAPED "\\$\\$", NULL, MATH_CHILDREN, 0U, 0U, 0U, false},
   {CWIKI_ZONE_MATH_DISPLAY, "display math brackets", UNESCAPED "\\\\\\[",
       UNESCAPED "\\\\\\]", NULL, MATH_CHILDREN, 0U, 0U, 0U, false},
   {CWIKI_ZONE_MATH_INLINE, "inline math dollars", UNESCAPED "\\$",
       UNESCAPED "\\$", NULL, MATH_CHILDREN, 0U, 0U, 0U, false},
   {CWIKI_ZONE_MATH_INLINE, "inline math parentheses", UNESCAPED "\\\\\\(",
       UNESCAPED "\\\\\\)", NULL, MATH_CHILDREN, 0U, 0U, 0U, false},
   {CWIKI_ZONE_TIKZ, "TikZ environment",
       UNESCAPED "\\\\begin\\{(tikzpicture)\\}",
       UNESCAPED "\\\\end\\{(tikzpicture)\\}", NULL, LATEX_CHILDREN, 0U,
       1U, 1U, false},
   {CWIKI_ZONE_LATEX, "LaTeX environment",
       UNESCAPED "\\\\begin\\{([A-Za-z*][A-Za-z0-9*@:-]*)\\}",
       UNESCAPED "\\\\end\\{([A-Za-z*][A-Za-z0-9*@:-]*)\\}", NULL,
       LATEX_CHILDREN, 0U, 1U, 1U, false},
   {CWIKI_ZONE_CHEMISTRY, "chemistry", UNESCAPED "\\\\ce\\{",
       UNESCAPED "\\}", NULL,
       EVERYWHERE_COMMENTS | BIT(REGION_COMMENT_PERCENT), 0U, 0U, 0U, false},
   {CWIKI_ZONE_TEXT, "math text hole", UNESCAPED "\\\\text\\{",
       UNESCAPED "\\}", NULL,
       EVERYWHERE_COMMENTS | BIT(REGION_MATH_DISPLAY_DOLLAR) |
       BIT(REGION_MATH_DISPLAY_BRACKET) | BIT(REGION_MATH_INLINE_DOLLAR) |
       BIT(REGION_MATH_INLINE_PAREN), 0U, 0U, 0U, false},
   {CWIKI_ZONE_TEXT, "math intertext hole", UNESCAPED "\\\\intertext\\{",
       UNESCAPED "\\}", NULL,
       EVERYWHERE_COMMENTS | BIT(REGION_MATH_DISPLAY_DOLLAR) |
       BIT(REGION_MATH_DISPLAY_BRACKET) | BIT(REGION_MATH_INLINE_DOLLAR) |
       BIT(REGION_MATH_INLINE_PAREN), 0U, 0U, 0U, false},
   {CWIKI_ZONE_REFERENCE, "reference hole",
       UNESCAPED "\\\\(?:label|ref|eqref)\\{", UNESCAPED "\\}", NULL,
       EVERYWHERE_COMMENTS, 0U, 0U, 0U, false},
   {CWIKI_ZONE_COMMENT_PERCENT, "LaTeX line comment", UNESCAPED "%", NULL,
       NULL, 0U, 0U, 0U, 0U, true}
};

struct compiled_pattern {
   struct cwiki_regex *regex;
};

struct compiled_region {
   struct compiled_pattern start;
   struct compiled_pattern end;
   struct compiled_pattern skip;
};

struct cwiki_zone_engine {
   const struct cwiki_zone_region *regions;
   struct compiled_region *compiled;
   size_t region_count;
   uint64_t top_level;
   char **details;
   size_t detail_count;
   size_t detail_capacity;
};

struct match {
   size_t start;
   size_t end;
   const char *detail;
   size_t detail_length;
   bool found;
};

static size_t
next_codepoint(const char *bytes, size_t length, size_t offset)
{
   unsigned char lead = (unsigned char)bytes[offset];
   size_t width;
   size_t index;

   if (lead < 0x80U) {
      return offset + 1U;
   }
   if (lead >= 0xc2U && lead <= 0xdfU) {
      width = 2U;
   } else if (lead >= 0xe0U && lead <= 0xefU) {
      width = 3U;
   } else if (lead >= 0xf0U && lead <= 0xf4U) {
      width = 4U;
   } else {
      return SIZE_MAX;
   }
   if (width > length - offset) {
      return SIZE_MAX;
   }
   for (index = 1U; index < width; index++) {
      unsigned char continuation = (unsigned char)bytes[offset + index];

      if ((continuation & 0xc0U) != 0x80U) {
         return SIZE_MAX;
      }
   }
   return offset + width;
}

static void
compiled_pattern_free(struct compiled_pattern *pattern)
{
   cwiki_regex_free(pattern->regex);
   memset(pattern, 0, sizeof(*pattern));
}

static int
compiled_pattern_init(struct compiled_pattern *compiled, const char *pattern,
    uint8_t detail_capture, uint32_t match_limit, uint32_t depth_limit)
{
   struct cwiki_regex_compile_error error;
   enum cwiki_regex_compile_status status;

   if (pattern == NULL) {
      return 0;
   }
   status = cwiki_regex_compile(&compiled->regex, pattern, strlen(pattern), 0U,
       match_limit, depth_limit, &error);
   if (status != CWIKI_REGEX_COMPILE_OK ||
       (detail_capture != 0U &&
       (size_t)detail_capture >= cwiki_regex_capture_count(compiled->regex))) {
      compiled_pattern_free(compiled);
      errno = status == CWIKI_REGEX_COMPILE_NO_MEMORY ? ENOMEM : EINVAL;
      return -1;
   }
   return 0;
}

static bool
resource_limit(enum cwiki_regex_status status)
{
   return status == CWIKI_REGEX_MATCH_LIMIT ||
       status == CWIKI_REGEX_DEPTH_LIMIT ||
       status == CWIKI_REGEX_JIT_STACK_LIMIT;
}

static int
pattern_match(struct compiled_pattern *pattern, const char *bytes,
    size_t length, size_t limit, size_t offset, uint8_t detail_capture,
    struct match *match, bool *degraded)
{
   struct cwiki_regex_result result;
   const char *subject = bytes == NULL ? "" : bytes;

   memset(match, 0, sizeof(*match));
   if (pattern->regex == NULL) {
      return 0;
   }
   result = cwiki_regex_execute(pattern->regex, subject, length, offset, true);
   if (result.status == CWIKI_REGEX_NO_MATCH) {
      return 0;
   }
   if (resource_limit(result.status)) {
      *degraded = true;
      return 0;
   }
   if (result.status != CWIKI_REGEX_MATCH || result.capture_count == 0U ||
       result.captures == NULL) {
      errno = EINVAL;
      return -1;
   }
   if (result.captures[0].end <= offset) {
      errno = EINVAL;
      return -1;
   }
   if (result.captures[0].end > limit) {
      return 0;
   }
   match->start = result.captures[0].start;
   match->end = result.captures[0].end;
   match->found = true;
   if (detail_capture != 0U && (size_t)detail_capture < result.capture_count &&
       result.captures[detail_capture].start != CWIKI_REGEX_UNSET) {
      size_t start = result.captures[detail_capture].start;
      size_t end = result.captures[detail_capture].end;

      match->detail = subject + start;
      match->detail_length = end - start;
   }
   return 0;
}

static int
reserve_details(struct cwiki_zone_engine *engine, size_t needed)
{
   size_t capacity = engine->detail_capacity == 0U ? 8U :
       engine->detail_capacity;
   char **details;

   if (needed <= engine->detail_capacity) {
      return 0;
   }
   while (capacity < needed) {
      if (capacity > SIZE_MAX / 2U) {
         errno = ENOMEM;
         return -1;
      }
      capacity *= 2U;
   }
   if (capacity > SIZE_MAX / sizeof(*details)) {
      errno = ENOMEM;
      return -1;
   }
   details = realloc(engine->details, capacity * sizeof(*details));
   if (details == NULL) {
      return -1;
   }
   engine->details = details;
   engine->detail_capacity = capacity;
   return 0;
}

static int
intern_detail(struct cwiki_zone_engine *engine, const char *detail,
    size_t length, uint16_t *identifier)
{
   size_t index;
   char *copy;

   if (detail == NULL || length == 0U) {
      *identifier = 0U;
      return 0;
   }
   for (index = 0U; index < engine->detail_count; index++) {
      if (strlen(engine->details[index]) == length &&
          memcmp(engine->details[index], detail, length) == 0) {
         *identifier = (uint16_t)(index + 1U);
         return 0;
      }
   }
   if (engine->detail_count >= UINT16_MAX ||
       reserve_details(engine, engine->detail_count + 1U) != 0) {
      if (engine->detail_count >= UINT16_MAX) {
         errno = EOVERFLOW;
      }
      return -1;
   }
   copy = malloc(length + 1U);
   if (copy == NULL) {
      return -1;
   }
   memcpy(copy, detail, length);
   copy[length] = '\0';
   engine->details[engine->detail_count++] = copy;
   *identifier = (uint16_t)engine->detail_count;
   return 0;
}

static bool
valid_stack(const struct cwiki_zone_engine *engine,
    const struct cwiki_zone_stack *stack)
{
   size_t index;

   if (stack == NULL || stack->depth > CWIKI_ZONE_MAX_DEPTH) {
      return false;
   }
   for (index = 0U; index < stack->depth; index++) {
      if (stack->zones[index].region >= engine->region_count ||
          stack->zones[index].kind !=
          engine->regions[stack->zones[index].region].kind) {
         return false;
      }
   }
   return true;
}

int
cwiki_zone_engine_init(struct cwiki_zone_engine **result,
    const struct cwiki_zone_region *regions, size_t region_count,
    uint64_t top_level)
{
   return cwiki_zone_engine_init_with_limits(result, regions, region_count,
       top_level, CWIKI_ZONE_DEFAULT_MATCH_LIMIT,
       CWIKI_ZONE_DEFAULT_DEPTH_LIMIT);
}

int
cwiki_zone_engine_init_with_limits(struct cwiki_zone_engine **result,
    const struct cwiki_zone_region *regions, size_t region_count,
    uint64_t top_level, uint32_t match_limit, uint32_t depth_limit)
{
   struct cwiki_zone_engine *engine;
   size_t index;

   if (result == NULL || regions == NULL || region_count == 0U ||
       region_count > CWIKI_ZONE_MAX_REGIONS || match_limit == 0U ||
       depth_limit == 0U ||
       (region_count < CWIKI_ZONE_MAX_REGIONS &&
       (top_level >> region_count) != 0U)) {
      errno = EINVAL;
      return -1;
   }
   engine = calloc(1U, sizeof(*engine));
   if (engine == NULL) {
      return -1;
   }
   engine->compiled = calloc(region_count, sizeof(*engine->compiled));
   if (engine->compiled == NULL) {
      free(engine);
      return -1;
   }
   engine->regions = regions;
   engine->region_count = region_count;
   engine->top_level = top_level;
   for (index = 0U; index < region_count; index++) {
      const struct cwiki_zone_region *region = &regions[index];

      if (region->name == NULL || region->start == NULL ||
          (!region->ends_at_line && region->end == NULL) ||
          (region_count < CWIKI_ZONE_MAX_REGIONS &&
          (region->contains >> region_count) != 0U)) {
         cwiki_zone_engine_free(engine);
         errno = EINVAL;
         return -1;
      }
      if (compiled_pattern_init(&engine->compiled[index].start, region->start,
          region->start_detail_capture, match_limit, depth_limit) != 0 ||
          compiled_pattern_init(&engine->compiled[index].end, region->end,
          region->end_detail_capture, match_limit, depth_limit) != 0 ||
          compiled_pattern_init(&engine->compiled[index].skip, region->skip,
          0U, match_limit, depth_limit) != 0) {
         int saved_errno = errno;

         cwiki_zone_engine_free(engine);
         errno = saved_errno;
         return -1;
      }
   }
   *result = engine;
   return 0;
}

void
cwiki_zone_engine_free(struct cwiki_zone_engine *engine)
{
   size_t index;

   if (engine == NULL) {
      return;
   }
   for (index = 0U; index < engine->region_count; index++) {
      compiled_pattern_free(&engine->compiled[index].start);
      compiled_pattern_free(&engine->compiled[index].end);
      compiled_pattern_free(&engine->compiled[index].skip);
   }
   for (index = 0U; index < engine->detail_count; index++) {
      free(engine->details[index]);
   }
   free(engine->details);
   free(engine->compiled);
   free(engine);
}

const struct cwiki_zone_region *
cwiki_zone_builtin_regions(size_t *count, uint64_t *top_level)
{
   if (count != NULL) {
      *count = REGION_COUNT;
   }
   if (top_level != NULL) {
      *top_level = EVERYWHERE_COMMENTS | BIT(REGION_CODE) |
          BIT(REGION_MATH_DISPLAY_DOLLAR) |
          BIT(REGION_MATH_DISPLAY_BRACKET) |
          BIT(REGION_MATH_INLINE_DOLLAR) | BIT(REGION_MATH_INLINE_PAREN) |
          BIT(REGION_TIKZ) | BIT(REGION_LATEX);
   }
   return builtin_regions;
}

const char *
cwiki_zone_detail(const struct cwiki_zone_engine *engine, uint16_t detail)
{
   if (engine == NULL || detail == 0U || detail > engine->detail_count) {
      return NULL;
   }
   return engine->details[detail - 1U];
}

bool
cwiki_zone_stack_equal(const struct cwiki_zone_stack *left,
    const struct cwiki_zone_stack *right)
{
   size_t index;

   if (left == NULL || right == NULL || left->depth != right->depth) {
      return false;
   }
   for (index = 0U; index < left->depth; index++) {
      if (left->zones[index].kind != right->zones[index].kind ||
          left->zones[index].detail != right->zones[index].detail ||
          left->zones[index].region != right->zones[index].region) {
         return false;
      }
   }
   return true;
}

static void
emit_span(struct cwiki_zone_line *line, const char *bytes, size_t length,
    size_t end, struct cwiki_zone zone, enum cwiki_zone_token token)
{
   struct cwiki_zone_span *spans;
   size_t start;
   size_t next;

   if (line == NULL) {
      return;
   }
   spans = (struct cwiki_zone_span *)line->spans;
   start = line->span_count == 0U ? 0U :
       spans[line->span_count - 1U].source_end;
   if (start >= end) {
      return;
   }
   next = start;
   while (next < end) {
      next = cwiki_grapheme_next(bytes, length, next);
   }
   if (line->span_count != 0U) {
      struct cwiki_zone_span *last = &spans[line->span_count - 1U];

      if (token == CWIKI_ZONE_CONTENT && last->token == token &&
          last->zone.kind == zone.kind &&
          last->zone.detail == zone.detail && last->zone.region == zone.region) {
         last->source_end = next;
         return;
      }
   }
   spans[line->span_count++] = (struct cwiki_zone_span){start, next, zone,
       token};
}

static int
scan(struct cwiki_zone_engine *engine, const char *bytes, size_t length,
    size_t limit, const struct cwiki_zone_stack *start,
    struct cwiki_zone_stack *result, bool finish_line, bool *degraded,
    struct cwiki_zone_line *spans)
{
   struct cwiki_zone_stack stack;
   size_t offset = 0U;

   if (engine == NULL || (bytes == NULL && length != 0U) || limit > length ||
       result == NULL || degraded == NULL || !valid_stack(engine, start)) {
      errno = EINVAL;
      return -1;
   }
   *degraded = false;
   stack = *start;
   while (offset < limit) {
      struct match match;
      uint64_t children;
      size_t region_index;
      bool consumed = false;
      struct cwiki_zone content = stack.depth == 0U ?
          (struct cwiki_zone){CWIKI_ZONE_PROSE, 0U, 0U} :
          stack.zones[stack.depth - 1U];

      if (stack.depth != 0U) {
         struct cwiki_zone *top = &stack.zones[stack.depth - 1U];
         const struct cwiki_zone_region *region =
             &engine->regions[top->region];
         struct compiled_region *compiled = &engine->compiled[top->region];

         if (pattern_match(&compiled->skip, bytes, length, limit, offset, 0U,
             &match, degraded) != 0) {
            return -1;
         }
         if (match.found) {
            emit_span(spans, bytes, length, match.end, content,
                CWIKI_ZONE_CONTENT);
            offset = match.end;
            continue;
         }
         if (!region->ends_at_line &&
             pattern_match(&compiled->end, bytes, length, limit, offset,
             region->end_detail_capture, &match, degraded) != 0) {
            return -1;
         }
         if (!region->ends_at_line && match.found &&
             (region->end_detail_capture == 0U ||
             (match.detail != NULL &&
             cwiki_zone_detail(engine, top->detail) != NULL &&
             strlen(cwiki_zone_detail(engine, top->detail)) ==
             match.detail_length && memcmp(cwiki_zone_detail(engine,
             top->detail), match.detail, match.detail_length) == 0))) {
            emit_span(spans, bytes, length, match.start, content,
                CWIKI_ZONE_CONTENT);
            emit_span(spans, bytes, length, match.end, *top, CWIKI_ZONE_CLOSE);
            offset = match.end;
            stack.depth--;
            continue;
         }
         children = region->contains;
      } else {
         children = engine->top_level;
      }
      for (region_index = 0U; region_index < engine->region_count;
          region_index++) {
         const struct cwiki_zone_region *region;
         uint16_t detail;

         if ((children & BIT(region_index)) == 0U) {
            continue;
         }
         region = &engine->regions[region_index];
         if (pattern_match(&engine->compiled[region_index].start, bytes,
             length, limit, offset, region->start_detail_capture, &match,
             degraded) != 0) {
            return -1;
         }
         if (!match.found) {
            continue;
         }
         detail = region->detail;
         if (region->start_detail_capture != 0U &&
             intern_detail(engine, match.detail, match.detail_length,
             &detail) != 0) {
            return -1;
         }
         if (stack.depth < CWIKI_ZONE_MAX_DEPTH) {
            struct cwiki_zone *zone = &stack.zones[stack.depth++];

            zone->kind = region->kind;
            zone->detail = detail;
            zone->region = (uint8_t)region_index;
         }
         emit_span(spans, bytes, length, match.start, content,
             CWIKI_ZONE_CONTENT);
         emit_span(spans, bytes, length, match.end,
             (struct cwiki_zone){region->kind, detail, (uint8_t)region_index},
             CWIKI_ZONE_OPEN);
         offset = match.end;
         consumed = true;
         break;
      }
      if (!consumed) {
         offset = next_codepoint(bytes, length, offset);
         if (offset == SIZE_MAX) {
            errno = EINVAL;
            return -1;
         }
         emit_span(spans, bytes, length, offset, content, CWIKI_ZONE_CONTENT);
      }
   }
   if (finish_line) {
      while (stack.depth != 0U &&
          engine->regions[stack.zones[stack.depth - 1U].region].ends_at_line) {
         stack.depth--;
      }
   }
   *result = stack;
   return 0;
}

int
cwiki_zone_scan_line(struct cwiki_zone_engine *engine, const char *bytes,
    size_t length, const struct cwiki_zone_stack *start,
    struct cwiki_zone_stack *end)
{
   bool degraded;

   if (!cwiki_utf8_validate(bytes, length)) {
      errno = EINVAL;
      return -1;
   }
   return scan(engine, bytes, length, length, start, end, true, &degraded,
       NULL);
}

int
cwiki_zone_recompute(struct cwiki_zone_engine *engine,
    struct cwiki_buffer *buffer, size_t first_line, size_t *lines_scanned)
{
   struct cwiki_zone_stack start = {{{CWIKI_ZONE_PROSE, 0U, 0U}}, 0U};
   size_t line;
   size_t count = 0U;

   if (engine == NULL || buffer == NULL || first_line >= buffer->line_count ||
       (first_line != 0U && buffer->lines[first_line - 1U].zone_dirty)) {
      errno = EINVAL;
      return -1;
   }
   if (first_line != 0U) {
      start = buffer->lines[first_line - 1U].end_zones;
   }
   for (line = first_line; line < buffer->line_count; line++) {
      struct cwiki_zone_stack previous = buffer->lines[line].end_zones;
      struct cwiki_zone_stack end;
      bool previous_degraded = buffer->lines[line].zone_degraded;
      bool degraded;
      bool changed;

      if (!cwiki_utf8_validate(buffer->lines[line].bytes,
          buffer->lines[line].length)) {
         errno = EINVAL;
         return -1;
      }
      if (scan(engine, buffer->lines[line].bytes, buffer->lines[line].length,
          buffer->lines[line].length, &start, &end, true, &degraded, NULL) != 0) {
         return -1;
      }
      changed = !cwiki_zone_stack_equal(&previous, &end) ||
          previous_degraded != degraded;
      buffer->lines[line].end_zones = end;
      buffer->lines[line].zone_degraded = degraded;
      buffer->lines[line].zone_dirty = false;
      start = end;
      count++;
      if (!changed && (line + 1U == buffer->line_count ||
          !buffer->lines[line + 1U].zone_dirty)) {
         break;
      }
   }
   if (lines_scanned != NULL) {
      *lines_scanned = count;
   }
   return 0;
}

int
cwiki_zone_at(struct cwiki_zone_engine *engine,
    const struct cwiki_buffer *buffer, size_t line, size_t byte,
    struct cwiki_zone *zone)
{
   struct cwiki_zone_stack start = {{{CWIKI_ZONE_PROSE, 0U, 0U}}, 0U};
   struct cwiki_zone_stack at;
   bool degraded;

   if (engine == NULL || buffer == NULL || zone == NULL ||
       line >= buffer->line_count || byte > buffer->lines[line].length ||
       (byte < buffer->lines[line].length &&
       ((unsigned char)buffer->lines[line].bytes[byte] & 0xc0U) == 0x80U) ||
       buffer->lines[line].zone_dirty ||
       (line != 0U && buffer->lines[line - 1U].zone_dirty)) {
      errno = EINVAL;
      return -1;
   }
   if (line != 0U) {
      start = buffer->lines[line - 1U].end_zones;
   }
   if (scan(engine, buffer->lines[line].bytes, buffer->lines[line].length,
       byte, &start, &at, false, &degraded, NULL) != 0) {
      return -1;
   }
   if (at.depth == 0U) {
      zone->kind = CWIKI_ZONE_PROSE;
      zone->detail = 0U;
      zone->region = 0U;
   } else {
      *zone = at.zones[at.depth - 1U];
   }
   return 0;
}

int
cwiki_zone_build_line(struct cwiki_zone_engine *engine,
    const struct cwiki_buffer *buffer, size_t line,
    struct cwiki_zone_line *result)
{
   struct cwiki_zone_stack start = {{{CWIKI_ZONE_PROSE, 0U, 0U}}, 0U};
   struct cwiki_zone_stack end;
   const struct cwiki_line *source;
   bool degraded;

   if (engine == NULL || buffer == NULL || result == NULL ||
       line >= buffer->line_count || buffer->lines[line].zone_dirty ||
       (line != 0U && buffer->lines[line - 1U].zone_dirty)) {
      errno = EINVAL;
      return -1;
   }
   memset(result, 0, sizeof(*result));
   source = &buffer->lines[line];
   result->degraded = cwiki_buffer_line_degraded(buffer, line);
   if (result->degraded || source->length == 0U) {
      return 0;
   }
   if (source->length > SIZE_MAX / sizeof(*result->spans)) {
      errno = ENOMEM;
      return -1;
   }
   result->spans = calloc(source->length, sizeof(*result->spans));
   if (result->spans == NULL) {
      return -1;
   }
   if (line != 0U) {
      start = buffer->lines[line - 1U].end_zones;
   }
   if (scan(engine, source->bytes, source->length, source->length, &start,
       &end, true, &degraded, result) != 0) {
      cwiki_zone_line_free(result);
      return -1;
   }
   if (degraded) {
      cwiki_zone_line_free(result);
      result->degraded = true;
   }
   return 0;
}

void
cwiki_zone_line_free(struct cwiki_zone_line *line)
{
   if (line != NULL) {
      free((void *)line->spans);
      memset(line, 0, sizeof(*line));
   }
}
