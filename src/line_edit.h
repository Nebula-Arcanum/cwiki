#ifndef CWIKI_LINE_EDIT_H
#define CWIKI_LINE_EDIT_H

#include <stddef.h>
#include <stdint.h>

struct cwiki_line_edit;

enum cwiki_line_edit_status {
   CWIKI_LINE_EDIT_OK,
   CWIKI_LINE_EDIT_INVALID,
   CWIKI_LINE_EDIT_NO_MEMORY,
   CWIKI_LINE_EDIT_NOT_HANDLED
};

enum cwiki_line_edit_status cwiki_line_edit_init(
    struct cwiki_line_edit **edit, const char *initial, size_t length);
void cwiki_line_edit_free(struct cwiki_line_edit *edit);

const char *cwiki_line_edit_bytes(const struct cwiki_line_edit *edit);
size_t cwiki_line_edit_length(const struct cwiki_line_edit *edit);
size_t cwiki_line_edit_cursor(const struct cwiki_line_edit *edit);

/* Text must be valid UTF-8 without ASCII control bytes and must not alias edit. */
enum cwiki_line_edit_status cwiki_line_edit_insert(struct cwiki_line_edit *edit,
    const char *text, size_t length);

/* Handles C-a/C-b/C-d/C-e/C-f/C-h/C-k/C-w by their control-byte values. */
enum cwiki_line_edit_status cwiki_line_edit_control(
    struct cwiki_line_edit *edit, uint32_t key);

#ifdef CWIKI_LINE_EDIT_TESTING
void cwiki_line_edit_test_fail_allocation_after(
    size_t successful_allocations);
void cwiki_line_edit_test_reset_allocation(void);
#endif

#endif
