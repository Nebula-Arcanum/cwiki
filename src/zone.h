#ifndef CWIKI_ZONE_H
#define CWIKI_ZONE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CWIKI_ZONE_MAX_DEPTH 16U
#define CWIKI_ZONE_MAX_REGIONS 64U
#define CWIKI_ZONE_DEFAULT_MATCH_LIMIT 100000U
#define CWIKI_ZONE_DEFAULT_DEPTH_LIMIT 1000U
#define CWIKI_ZONE_REGION_BIT(index) (UINT64_C(1) << (index))

enum cwiki_zone_kind {
   CWIKI_ZONE_PROSE,
   CWIKI_ZONE_CODE,
   CWIKI_ZONE_MATH_INLINE,
   CWIKI_ZONE_MATH_DISPLAY,
   CWIKI_ZONE_CHEMISTRY,
   CWIKI_ZONE_TEXT,
   CWIKI_ZONE_REFERENCE,
   CWIKI_ZONE_LATEX,
   CWIKI_ZONE_TIKZ,
   CWIKI_ZONE_COMMENT_PERCENT,
   CWIKI_ZONE_COMMENT_NOTE,
   CWIKI_ZONE_COMMENT_HTML,
   CWIKI_ZONE_CUSTOM
};

struct cwiki_zone {
   enum cwiki_zone_kind kind;
   uint16_t detail;
   uint8_t region;
};

struct cwiki_zone_stack {
   struct cwiki_zone zones[CWIKI_ZONE_MAX_DEPTH];
   uint8_t depth;
};

struct cwiki_zone_region {
   enum cwiki_zone_kind kind;
   const char *name;
   const char *start;
   const char *end;
   const char *skip;
   uint64_t contains;
   uint16_t detail;
   uint8_t start_detail_capture;
   uint8_t end_detail_capture;
   bool ends_at_line;
};

struct cwiki_zone_engine;
struct cwiki_buffer;

enum cwiki_zone_token {
   CWIKI_ZONE_CONTENT,
   CWIKI_ZONE_OPEN,
   CWIKI_ZONE_CLOSE
};

struct cwiki_zone_span {
   size_t source_start;
   size_t source_end;
   struct cwiki_zone zone;
   enum cwiki_zone_token token;
};

struct cwiki_zone_line {
   const struct cwiki_zone_span *spans;
   size_t span_count;
   bool degraded;
};

/* Immutable owned snapshot; free with cwiki_zone_line_free. Clean caches are
 * required. Trusted spans partition the line into half-open grapheme ranges;
 * a cluster takes its first byte's role. Degraded lines have no trusted spans.
 * Open/close spans cover the grammar's whole match (including fence info).
 * Skips and any prefix excluded by regex \K remain content. */
int cwiki_zone_build_line(struct cwiki_zone_engine *engine,
    const struct cwiki_buffer *buffer, size_t line,
    struct cwiki_zone_line *result);
void cwiki_zone_line_free(struct cwiki_zone_line *line);

int cwiki_zone_engine_init(struct cwiki_zone_engine **engine,
    const struct cwiki_zone_region *regions, size_t region_count,
    uint64_t top_level);
int cwiki_zone_engine_init_with_limits(struct cwiki_zone_engine **engine,
    const struct cwiki_zone_region *regions, size_t region_count,
    uint64_t top_level, uint32_t match_limit, uint32_t depth_limit);
void cwiki_zone_engine_free(struct cwiki_zone_engine *engine);
const struct cwiki_zone_region *cwiki_zone_builtin_regions(size_t *count,
    uint64_t *top_level);
const char *cwiki_zone_detail(const struct cwiki_zone_engine *engine,
    uint16_t detail);
bool cwiki_zone_stack_equal(const struct cwiki_zone_stack *left,
    const struct cwiki_zone_stack *right);
int cwiki_zone_scan_line(struct cwiki_zone_engine *engine, const char *bytes,
    size_t length, const struct cwiki_zone_stack *start,
    struct cwiki_zone_stack *end);
int cwiki_zone_recompute(struct cwiki_zone_engine *engine,
    struct cwiki_buffer *buffer, size_t first_line, size_t *lines_scanned);
/* Cursor context after consuming the prefix before byte, not a token query.
 * Use cwiki_zone_build_line for ownership of complete delimiters. */
int cwiki_zone_at(struct cwiki_zone_engine *engine,
    const struct cwiki_buffer *buffer, size_t line, size_t byte,
    struct cwiki_zone *zone);

#endif
