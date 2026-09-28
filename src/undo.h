#ifndef CWIKI_UNDO_H
#define CWIKI_UNDO_H

#include "buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cwiki_undo_node;

struct cwiki_undo {
   struct cwiki_buffer *buffer;
   struct cwiki_undo_node *root;
   struct cwiki_undo_node *current;
   struct cwiki_undo_node *pending;
   struct cwiki_undo_node *transition_node;
   struct cwiki_undo_node *destination;
   struct cwiki_undo_node **states;
   size_t state_count;
   size_t state_capacity;
   size_t transition_operation;
   int transition_kind;
};

struct cwiki_undo_state_info {
   uint64_t sequence;
   uint64_t timestamp;
   uint64_t parent_sequence;
   size_t child_count;
   size_t operation_count;
   bool current;
};

int cwiki_undo_init(struct cwiki_undo *undo, struct cwiki_buffer *buffer);
void cwiki_undo_free(struct cwiki_undo *undo);

int cwiki_undo_begin(struct cwiki_undo *undo, uint64_t timestamp);
int cwiki_undo_commit(struct cwiki_undo *undo);
int cwiki_undo_cancel(struct cwiki_undo *undo);
bool cwiki_undo_transaction_active(const struct cwiki_undo *undo);

int cwiki_undo_insert(struct cwiki_undo *undo, size_t line, size_t byte,
    const char *bytes, size_t length);
int cwiki_undo_delete(struct cwiki_undo *undo, size_t line, size_t byte,
    size_t length);
int cwiki_undo_split(struct cwiki_undo *undo, size_t line, size_t byte);
int cwiki_undo_join(struct cwiki_undo *undo, size_t line);

int cwiki_undo_to_parent(struct cwiki_undo *undo);
int cwiki_undo_redo_child(struct cwiki_undo *undo, size_t child_index);
int cwiki_undo_older(struct cwiki_undo *undo);
int cwiki_undo_newer(struct cwiki_undo *undo);

size_t cwiki_undo_state_count(const struct cwiki_undo *undo);
int cwiki_undo_state_info(const struct cwiki_undo *undo, size_t sequence,
    struct cwiki_undo_state_info *info);

#ifdef CWIKI_UNDO_TESTING
void cwiki_undo_test_fail_allocation_after(size_t successful_allocations);
void cwiki_undo_test_reset_allocation(void);
#endif

#endif
