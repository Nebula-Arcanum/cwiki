#include "conceal.h"

#include "buffer.h"
#include "unicode.h"
#include "zone.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <utf8proc.h>

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define ALL_CONTEXTS ((uint32_t)(CWIKI_CONCEAL_CONTEXT_MATH_INLINE | \
    CWIKI_CONCEAL_CONTEXT_MATH_DISPLAY | \
    CWIKI_CONCEAL_CONTEXT_CHEMISTRY | CWIKI_CONCEAL_CONTEXT_LATEX | \
    CWIKI_CONCEAL_CONTEXT_TIKZ))
#define MATH_CONTEXTS ((uint32_t)(CWIKI_CONCEAL_CONTEXT_MATH_INLINE | \
    CWIKI_CONCEAL_CONTEXT_MATH_DISPLAY | CWIKI_CONCEAL_CONTEXT_CHEMISTRY))

static const char *const category_names[CWIKI_CONCEAL_CATEGORY_COUNT] = {
   "accents", "Greek", "math symbols", "ligatures", "fractions",
   "math bounds", "size-modified delimiters", "sub/superscripts",
   "styles", "environments", "item markers", "citations", "spacing",
   "sections"
};

static const struct cwiki_conceal_entry builtin_entries[] = {
   {"\\^a", "â", CWIKI_CONCEAL_ACCENTS, ALL_CONTEXTS},
   {"\\alpha", "𝛼", CWIKI_CONCEAL_GREEK, ALL_CONTEXTS},
   {"\\beta", "𝛽", CWIKI_CONCEAL_GREEK, ALL_CONTEXTS},
   {"\\pi", "𝜋", CWIKI_CONCEAL_GREEK, ALL_CONTEXTS},
   {"\\le", "⩽", CWIKI_CONCEAL_MATH_SYMBOLS, MATH_CONTEXTS},
   {"\\ge", "⩾", CWIKI_CONCEAL_MATH_SYMBOLS, MATH_CONTEXTS},
   {"\\notin", "∉", CWIKI_CONCEAL_MATH_SYMBOLS, MATH_CONTEXTS},
   {"\\AA", "Å", CWIKI_CONCEAL_LIGATURES, ALL_CONTEXTS},
   {"\\frac{1}{10}", "⅒", CWIKI_CONCEAL_FRACTIONS, MATH_CONTEXTS},
   {"\\)", "⁆", CWIKI_CONCEAL_MATH_BOUNDS, MATH_CONTEXTS},
   {"\\Bigl\\langle", "⟨", CWIKI_CONCEAL_SIZE_DELIMITERS, MATH_CONTEXTS},
   {"\\Bigr\\rangle", "⟩", CWIKI_CONCEAL_SIZE_DELIMITERS, MATH_CONTEXTS},
   {"^5", "⁵", CWIKI_CONCEAL_SUB_SUPERSCRIPTS, MATH_CONTEXTS},
   {"_0", "₀", CWIKI_CONCEAL_SUB_SUPERSCRIPTS, MATH_CONTEXTS},
   {"\\mathbf", "𝐁", CWIKI_CONCEAL_STYLES, ALL_CONTEXTS},
   {"\\end{equation}", "❭", CWIKI_CONCEAL_ENVIRONMENTS, ALL_CONTEXTS},
   {"\\item", "◦", CWIKI_CONCEAL_ITEM_MARKERS, ALL_CONTEXTS},
   {"\\cite", "📖", CWIKI_CONCEAL_CITATIONS, ALL_CONTEXTS},
   {"\\quad", " ", CWIKI_CONCEAL_SPACING, ALL_CONTEXTS},
   {"\\section", "❧", CWIKI_CONCEAL_SECTIONS, ALL_CONTEXTS}
};

static int
replacement_width(const char *replacement)
{
   size_t length;
   size_t offset = 0U;
   int width;

   if (replacement == NULL) {
      return -1;
   }
   length = strlen(replacement);
   width = cwiki_grapheme_width(replacement, length);
   if (width != 1 && width != 2) {
      return -1;
   }
   while (offset < length) {
      utf8proc_int32_t codepoint;
      utf8proc_ssize_t used = utf8proc_iterate(
          (const utf8proc_uint8_t *)replacement + offset,
          (utf8proc_ssize_t)(length - offset), &codepoint);

      if (used <= 0 || utf8proc_charwidth_ambiguous(codepoint) != 0) {
         return -1;
      }
      offset += (size_t)used;
   }
   return width;
}

const char *
cwiki_conceal_category_name(enum cwiki_conceal_category category)
{
   if (category < 0 || category >= CWIKI_CONCEAL_CATEGORY_COUNT) {
      return NULL;
   }
   return category_names[category];
}

int
cwiki_conceal_table_init(struct cwiki_conceal_table *table,
    const struct cwiki_conceal_entry *entries, size_t count)
{
   size_t i;

   if (table == NULL || (entries == NULL && count != 0U) ||
       count > SIZE_MAX / sizeof(*entries)) {
      errno = EINVAL;
      return -1;
   }
   table->entries = NULL;
   table->count = 0U;
   for (i = 0U; i < count; i++) {
      size_t j;
      size_t source_length;

      if (entries[i].category < 0 ||
          entries[i].category >= CWIKI_CONCEAL_CATEGORY_COUNT ||
          entries[i].source == NULL || entries[i].source[0] == '\0' ||
          entries[i].contexts == 0U ||
          (entries[i].contexts & ~ALL_CONTEXTS) != 0U ||
          replacement_width(entries[i].replacement) < 0) {
         errno = EINVAL;
         return -1;
      }
      source_length = strlen(entries[i].source);
      if (!cwiki_utf8_validate(entries[i].source, source_length)) {
         errno = EINVAL;
         return -1;
      }
      for (j = 0U; j < i; j++) {
         if (strcmp(entries[i].source, entries[j].source) == 0) {
            errno = EINVAL;
            return -1;
         }
      }
   }
   if (count != 0U) {
      table->entries = malloc(count * sizeof(*entries));
      if (table->entries == NULL) {
         return -1;
      }
      (void)memcpy(table->entries, entries, count * sizeof(*entries));
   }
   table->count = count;
   return 0;
}

int
cwiki_conceal_table_init_builtin(struct cwiki_conceal_table *table)
{
   return cwiki_conceal_table_init(table, builtin_entries,
       ARRAY_COUNT(builtin_entries));
}

void
cwiki_conceal_table_free(struct cwiki_conceal_table *table)
{
   if (table != NULL) {
      free(table->entries);
      table->entries = NULL;
      table->count = 0U;
   }
}

static uint32_t
zone_context(enum cwiki_zone_kind kind)
{
   static const uint32_t contexts[] = {
      0U, 0U, CWIKI_CONCEAL_CONTEXT_MATH_INLINE,
      CWIKI_CONCEAL_CONTEXT_MATH_DISPLAY, CWIKI_CONCEAL_CONTEXT_CHEMISTRY,
      0U, 0U, CWIKI_CONCEAL_CONTEXT_LATEX, CWIKI_CONCEAL_CONTEXT_TIKZ,
      0U, 0U, 0U, 0U
   };

   if (kind < 0 || (size_t)kind >= ARRAY_COUNT(contexts)) {
      return 0U;
   }
   return contexts[kind];
}

static bool
is_ascii_letter(char byte)
{
   return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

static bool
source_matches(const char *source, size_t source_length, size_t offset,
    const char *pattern, size_t pattern_length)
{
   size_t slashes = 0U;
   size_t before = offset;

   if (pattern_length > source_length - offset ||
       memcmp(source + offset, pattern, pattern_length) != 0) {
      return false;
   }
   while (before != 0U && source[before - 1U] == '\\') {
      slashes++;
      before--;
   }
   if (pattern[0] == '\\' && (slashes % 2U) != 0U) {
      return false;
   }
   return pattern[0] != '\\' || !is_ascii_letter(pattern[pattern_length - 1U]) ||
       offset + pattern_length == source_length ||
       !is_ascii_letter(source[offset + pattern_length]);
}

static const struct cwiki_conceal_entry *
match_entry(const struct cwiki_conceal_table *table,
    struct cwiki_zone_engine *zones, const struct cwiki_buffer *buffer,
    size_t line, const char *source, size_t source_length, size_t offset,
    uint32_t category_mask)
{
   const struct cwiki_conceal_entry *best = NULL;
   size_t best_length = 0U;
   size_t i;
   struct cwiki_zone zone;
   uint32_t context;

   if (cwiki_zone_at(zones, buffer, line, offset, &zone) != 0) {
      return NULL;
   }
   context = zone_context(zone.kind);
   for (i = 0U; i < table->count; i++) {
      const struct cwiki_conceal_entry *entry = &table->entries[i];
      size_t length = strlen(entry->source);

      if ((category_mask & CWIKI_CONCEAL_CATEGORY_BIT(entry->category)) == 0U ||
          (entry->contexts & context) == 0U || length <= best_length ||
          !source_matches(source, source_length, offset, entry->source, length) ||
          !cwiki_grapheme_boundary(source, source_length, offset + length)) {
         continue;
      }
      best = entry;
      best_length = length;
   }
   return best;
}

static int
reserve(void **items, size_t item_size, size_t needed, size_t *capacity)
{
   size_t next;
   void *grown;

   if (needed <= *capacity) {
      return 0;
   }
   next = *capacity == 0U ? 16U : *capacity;
   while (next < needed) {
      if (next > SIZE_MAX / 2U) {
         errno = ENOMEM;
         return -1;
      }
      next *= 2U;
   }
   if (next > SIZE_MAX / item_size) {
      errno = ENOMEM;
      return -1;
   }
   grown = realloc(*items, next * item_size);
   if (grown == NULL) {
      return -1;
   }
   *items = grown;
   *capacity = next;
   return 0;
}

static int
append_run(struct cwiki_conceal_line *result, size_t *display_capacity,
    size_t *run_capacity, const char *bytes, size_t length, size_t source_start,
    size_t source_end, size_t width, bool concealed)
{
   struct cwiki_conceal_run *run;

   if (length > SIZE_MAX - result->display_length - 1U ||
       reserve((void **)&result->display, sizeof(*result->display),
       result->display_length + length + 1U, display_capacity) != 0 ||
       reserve((void **)&result->runs, sizeof(*result->runs),
       result->run_count + 1U, run_capacity) != 0) {
      return -1;
   }
   run = &result->runs[result->run_count++];
   run->source_start = source_start;
   run->source_end = source_end;
   run->display_start = result->display_length;
   run->display_end = result->display_length + length;
   run->column_start = result->columns;
   run->column_end = result->columns + width;
   run->concealed = concealed;
   (void)memcpy(result->display + result->display_length, bytes, length);
   result->display_length += length;
   result->columns += width;
   result->display[result->display_length] = '\0';
   return 0;
}

int
cwiki_conceal_build_line(const struct cwiki_conceal_table *table,
    struct cwiki_zone_engine *zones, const struct cwiki_buffer *buffer,
    size_t line, const char *source, size_t source_length,
    uint32_t category_mask, bool cursor_line, enum cwiki_conceal_mode mode,
    uint32_t concealcursor_modes,
    const struct cwiki_conceal_reveal *reveal,
    struct cwiki_conceal_line *result)
{
   size_t offset = 0U;
   size_t display_capacity = 0U;
   size_t run_capacity = 0U;
   bool conceal;

   if (table == NULL || zones == NULL || buffer == NULL || result == NULL ||
       line >= buffer->line_count || mode < 0 || mode >= CWIKI_CONCEAL_MODE_COUNT ||
       (source == NULL && source_length != 0U) ||
       source_length != buffer->lines[line].length ||
       (source_length != 0U && memcmp(source, buffer->lines[line].bytes,
       source_length) != 0) || !cwiki_utf8_validate(source, source_length) ||
       buffer->lines[line].zone_dirty ||
       (reveal != NULL && reveal->active &&
       (reveal->source_start >= reveal->source_end ||
       reveal->source_end > source_length ||
       !cwiki_grapheme_boundary(source, source_length, reveal->source_start) ||
       !cwiki_grapheme_boundary(source, source_length, reveal->source_end)))) {
      errno = EINVAL;
      return -1;
   }
   (void)memset(result, 0, sizeof(*result));
   conceal = !cursor_line ||
       (concealcursor_modes & CWIKI_CONCEAL_MODE_BIT(mode)) != 0U;
   conceal = conceal && !buffer->lines[line].zone_degraded;
   while (offset < source_length) {
      const struct cwiki_conceal_entry *entry = NULL;
      size_t end;
      bool revealed = false;

      if (conceal) {
         entry = match_entry(table, zones, buffer, line, source,
             source_length, offset, category_mask);
      }
      if (entry != NULL) {
         end = offset + strlen(entry->source);
         revealed = reveal != NULL && reveal->active &&
             reveal->source_start == offset && reveal->source_end == end;
         if (!revealed) {
            size_t replacement_length = strlen(entry->replacement);
            int width = replacement_width(entry->replacement);

            if (width < 0 || append_run(result, &display_capacity,
                &run_capacity, entry->replacement, replacement_length,
                offset, end, (size_t)width, true) != 0) {
               cwiki_conceal_line_free(result);
               return -1;
            }
            offset = end;
            continue;
         }
      } else {
         end = cwiki_grapheme_next(source, source_length, offset);
      }
      if (revealed) {
         size_t cursor = offset;
         size_t width = 0U;

         while (cursor < end) {
            size_t next = cwiki_grapheme_next(source, source_length, cursor);
            int cluster_width = cwiki_grapheme_width(source + cursor,
                next - cursor);

            if (next == SIZE_MAX || cluster_width < 0) {
               cwiki_conceal_line_free(result);
               errno = EINVAL;
               return -1;
            }
            width += (size_t)cluster_width;
            cursor = next;
         }
         if (append_run(result, &display_capacity, &run_capacity,
             source + offset, end - offset, offset, end, width, false) != 0) {
            cwiki_conceal_line_free(result);
            return -1;
         }
         offset = end;
      } else {
         int width;

         if (end == SIZE_MAX) {
            cwiki_conceal_line_free(result);
            errno = EINVAL;
            return -1;
         }
         width = cwiki_grapheme_width(source + offset, end - offset);
         if (width < 0 || append_run(result, &display_capacity, &run_capacity,
             source + offset, end - offset, offset, end, (size_t)width,
             false) != 0) {
            cwiki_conceal_line_free(result);
            return -1;
         }
         offset = end;
      }
   }
   if (result->display == NULL) {
      result->display = malloc(1U);
      if (result->display == NULL) {
         return -1;
      }
      result->display[0] = '\0';
   }
   return 0;
}

void
cwiki_conceal_line_free(struct cwiki_conceal_line *line)
{
   if (line != NULL) {
      free(line->display);
      free(line->runs);
      (void)memset(line, 0, sizeof(*line));
   }
}

static int
width_between(const char *source, size_t source_length, size_t start,
    size_t end, size_t *width)
{
   size_t cursor = start;
   size_t columns = 0U;

   while (cursor < end) {
      size_t next = cwiki_grapheme_next(source, source_length, cursor);
      int cluster_width;

      if (next == SIZE_MAX || next > end) {
         errno = EINVAL;
         return -1;
      }
      cluster_width = cwiki_grapheme_width(source + cursor, next - cursor);
      if (cluster_width < 0) {
         errno = EINVAL;
         return -1;
      }
      columns += (size_t)cluster_width;
      cursor = next;
   }
   *width = columns;
   return 0;
}

int
cwiki_conceal_source_to_display(const struct cwiki_conceal_line *line,
    const char *source, size_t source_length, size_t source_byte,
    size_t *display_column)
{
   size_t i;

   if (line == NULL || display_column == NULL || source_byte > source_length ||
       !cwiki_grapheme_boundary(source, source_length, source_byte)) {
      errno = EINVAL;
      return -1;
   }
   if (source_byte == source_length) {
      *display_column = line->columns;
      return 0;
   }
   for (i = 0U; i < line->run_count; i++) {
      const struct cwiki_conceal_run *run = &line->runs[i];

      if (source_byte < run->source_start || source_byte >= run->source_end) {
         continue;
      }
      if (run->concealed) {
         *display_column = run->column_start;
      } else {
         size_t width;

         if (width_between(source, source_length, run->source_start,
             source_byte, &width) != 0) {
            return -1;
         }
         *display_column = run->column_start + width;
      }
      return 0;
   }
   errno = EINVAL;
   return -1;
}

int
cwiki_conceal_display_to_source(const struct cwiki_conceal_line *line,
    const char *source, size_t source_length, size_t display_column,
    size_t *source_byte, size_t *run_index)
{
   size_t i;

   if (line == NULL || source_byte == NULL || display_column > line->columns ||
       !cwiki_utf8_validate(source, source_length)) {
      errno = EINVAL;
      return -1;
   }
   if (display_column == line->columns) {
      *source_byte = source_length;
      if (run_index != NULL) {
         *run_index = line->run_count == 0U ? SIZE_MAX : line->run_count - 1U;
      }
      return 0;
   }
   for (i = 0U; i < line->run_count; i++) {
      const struct cwiki_conceal_run *run = &line->runs[i];

      if (display_column < run->column_start ||
          display_column >= run->column_end) {
         continue;
      }
      if (run_index != NULL) {
         *run_index = i;
      }
      if (run->concealed) {
         *source_byte = run->source_start;
      } else {
         size_t cursor = run->source_start;
         size_t column = run->column_start;

         while (cursor < run->source_end) {
            size_t next = cwiki_grapheme_next(source, source_length, cursor);
            int width = cwiki_grapheme_width(source + cursor, next - cursor);

            if (width < 0 || display_column < column + (size_t)width) {
               break;
            }
            column += (size_t)width;
            cursor = next;
         }
         *source_byte = cursor;
      }
      return 0;
   }
   errno = EINVAL;
   return -1;
}
