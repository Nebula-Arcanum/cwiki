#ifndef CWIKI_PICKER_H
#define CWIKI_PICKER_H

#include "line_edit.h"

#include <stddef.h>
#include <stdint.h>

struct cwiki_picker;

struct cwiki_picker_spec {
   const char *id;
   const char *label;
   /* Optional secondary text; NULL is stored as an empty string. */
   const char *detail;
};

struct cwiki_picker_item {
   const char *id;
   const char *label;
   const char *detail;
   size_t score;
};

enum cwiki_picker_status {
   CWIKI_PICKER_OK,
   CWIKI_PICKER_INVALID,
   CWIKI_PICKER_NO_MEMORY,
   CWIKI_PICKER_NOT_HANDLED,
   CWIKI_PICKER_NOT_FOUND
};

enum cwiki_picker_status cwiki_picker_init(struct cwiki_picker **picker,
    const struct cwiki_picker_spec *items, size_t count);
void cwiki_picker_free(struct cwiki_picker *picker);

const char *cwiki_picker_query(const struct cwiki_picker *picker);
size_t cwiki_picker_query_length(const struct cwiki_picker *picker);
size_t cwiki_picker_query_cursor(const struct cwiki_picker *picker);
enum cwiki_picker_status cwiki_picker_insert(struct cwiki_picker *picker,
    const char *text, size_t length);
enum cwiki_picker_status cwiki_picker_control(struct cwiki_picker *picker,
    uint32_t key);

size_t cwiki_picker_count(const struct cwiki_picker *picker);
enum cwiki_picker_status cwiki_picker_at(const struct cwiki_picker *picker,
    size_t index, struct cwiki_picker_item *item);
size_t cwiki_picker_selected(const struct cwiki_picker *picker);
enum cwiki_picker_status cwiki_picker_select_next(struct cwiki_picker *picker);
enum cwiki_picker_status cwiki_picker_select_previous(
    struct cwiki_picker *picker);

#endif
