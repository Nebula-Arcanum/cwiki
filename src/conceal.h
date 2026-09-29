#ifndef CWIKI_CONCEAL_H
#define CWIKI_CONCEAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cwiki_buffer;
struct cwiki_zone_engine;

enum cwiki_conceal_category {
   CWIKI_CONCEAL_ACCENTS,
   CWIKI_CONCEAL_GREEK,
   CWIKI_CONCEAL_MATH_SYMBOLS,
   CWIKI_CONCEAL_LIGATURES,
   CWIKI_CONCEAL_FRACTIONS,
   CWIKI_CONCEAL_MATH_BOUNDS,
   CWIKI_CONCEAL_SIZE_DELIMITERS,
   CWIKI_CONCEAL_SUB_SUPERSCRIPTS,
   CWIKI_CONCEAL_STYLES,
   CWIKI_CONCEAL_ENVIRONMENTS,
   CWIKI_CONCEAL_ITEM_MARKERS,
   CWIKI_CONCEAL_CITATIONS,
   CWIKI_CONCEAL_SPACING,
   CWIKI_CONCEAL_SECTIONS,
   CWIKI_CONCEAL_CATEGORY_COUNT
};

#define CWIKI_CONCEAL_CATEGORY_BIT(category) \
   (UINT32_C(1) << (unsigned int)(category))
#define CWIKI_CONCEAL_DEFAULT_MASK \
   (CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_ACCENTS) | \
    CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_GREEK) | \
    CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_MATH_SYMBOLS) | \
    CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_LIGATURES) | \
    CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_FRACTIONS) | \
    CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_SIZE_DELIMITERS) | \
    CWIKI_CONCEAL_CATEGORY_BIT(CWIKI_CONCEAL_SUB_SUPERSCRIPTS))

enum cwiki_conceal_context {
   CWIKI_CONCEAL_CONTEXT_MATH_INLINE = UINT32_C(1) << 0,
   CWIKI_CONCEAL_CONTEXT_MATH_DISPLAY = UINT32_C(1) << 1,
   CWIKI_CONCEAL_CONTEXT_CHEMISTRY = UINT32_C(1) << 2,
   CWIKI_CONCEAL_CONTEXT_LATEX = UINT32_C(1) << 3,
   CWIKI_CONCEAL_CONTEXT_TIKZ = UINT32_C(1) << 4
};

enum cwiki_conceal_mode {
   CWIKI_CONCEAL_MODE_NORMAL,
   CWIKI_CONCEAL_MODE_INSERT,
   CWIKI_CONCEAL_MODE_VISUAL,
   CWIKI_CONCEAL_MODE_COMMAND,
   CWIKI_CONCEAL_MODE_COUNT
};

#define CWIKI_CONCEAL_MODE_BIT(mode) \
   (UINT32_C(1) << (unsigned int)(mode))
#define CWIKI_CONCEALCURSOR_DEFAULT \
   (CWIKI_CONCEAL_MODE_BIT(CWIKI_CONCEAL_MODE_NORMAL) | \
    CWIKI_CONCEAL_MODE_BIT(CWIKI_CONCEAL_MODE_COMMAND))

struct cwiki_conceal_entry {
   const char *source;
   const char *replacement;
   enum cwiki_conceal_category category;
   uint32_t contexts;
};

struct cwiki_conceal_table {
   struct cwiki_conceal_entry *entries;
   size_t count;
};

struct cwiki_conceal_reveal {
   size_t source_start;
   size_t source_end;
   bool active;
};

struct cwiki_conceal_run {
   size_t source_start;
   size_t source_end;
   size_t display_start;
   size_t display_end;
   size_t column_start;
   size_t column_end;
   bool concealed;
};

struct cwiki_conceal_line {
   char *display;
   size_t display_length;
   size_t columns;
   struct cwiki_conceal_run *runs;
   size_t run_count;
};

const char *cwiki_conceal_category_name(enum cwiki_conceal_category category);
int cwiki_conceal_table_init(struct cwiki_conceal_table *table,
    const struct cwiki_conceal_entry *entries, size_t count);
int cwiki_conceal_table_init_builtin(struct cwiki_conceal_table *table);
void cwiki_conceal_table_free(struct cwiki_conceal_table *table);
int cwiki_conceal_build_line(const struct cwiki_conceal_table *table,
    struct cwiki_zone_engine *zones, const struct cwiki_buffer *buffer,
    size_t line, const char *source, size_t source_length,
    uint32_t category_mask, bool cursor_line, enum cwiki_conceal_mode mode,
    uint32_t concealcursor_modes,
    const struct cwiki_conceal_reveal *reveal,
    struct cwiki_conceal_line *result);
void cwiki_conceal_line_free(struct cwiki_conceal_line *line);
int cwiki_conceal_source_to_display(const struct cwiki_conceal_line *line,
    const char *source, size_t source_length, size_t source_byte,
    size_t *display_column);
int cwiki_conceal_display_to_source(const struct cwiki_conceal_line *line,
    const char *source, size_t source_length, size_t display_column,
    size_t *source_byte, size_t *run_index);

#endif
