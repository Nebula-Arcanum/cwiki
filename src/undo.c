#include "undo.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum operation_kind {
   OP_INSERT,
   OP_DELETE,
   OP_SPLIT,
   OP_JOIN
};

enum transition_kind {
   TRANSITION_NONE,
   TRANSITION_UNDO,
   TRANSITION_REDO
};

struct undo_operation {
   enum operation_kind kind;
   size_t line;
   size_t byte;
   size_t length;
   char *bytes;
};

struct cwiki_undo_node {
   struct cwiki_undo_node *parent;
   struct cwiki_undo_node **children;
   struct undo_operation *operations;
   size_t child_count;
   size_t child_capacity;
   size_t operation_count;
   size_t operation_capacity;
   size_t applied_operations;
   size_t depth;
   uint64_t sequence;
   uint64_t timestamp;
};

#ifdef CWIKI_UNDO_TESTING
static size_t allocations_before_failure;
static bool allocation_failure_enabled;

void
cwiki_undo_test_fail_allocation_after(size_t successful_allocations)
{
   allocations_before_failure = successful_allocations;
   allocation_failure_enabled = true;
}

void
cwiki_undo_test_reset_allocation(void)
{
   allocation_failure_enabled = false;
}

static bool
fail_allocation(void)
{
   if (!allocation_failure_enabled) {
      return false;
   }
   if (allocations_before_failure == 0U) {
      errno = ENOMEM;
      return true;
   }
   allocations_before_failure--;
   return false;
}
#endif

static void *
undo_malloc(size_t size)
{
#ifdef CWIKI_UNDO_TESTING
   if (fail_allocation()) {
      return NULL;
   }
#endif
   return malloc(size);
}

static void *
undo_calloc(size_t count, size_t size)
{
#ifdef CWIKI_UNDO_TESTING
   if (fail_allocation()) {
      return NULL;
   }
#endif
   return calloc(count, size);
}

static void *
undo_realloc(void *memory, size_t size)
{
#ifdef CWIKI_UNDO_TESTING
   if (fail_allocation()) {
      return NULL;
   }
#endif
   return realloc(memory, size);
}

static int
reserve(void **memory, size_t element_size, size_t *capacity, size_t needed)
{
   size_t grown;
   void *replacement;

   if (needed <= *capacity) {
      return 0;
   }
   grown = *capacity == 0U ? 4U : *capacity;
   while (grown < needed) {
      if (grown > SIZE_MAX / 2U) {
         grown = needed;
         break;
      }
      grown *= 2U;
   }
   if (element_size != 0U && grown > SIZE_MAX / element_size) {
      errno = ENOMEM;
      return -1;
   }
   replacement = undo_realloc(*memory, grown * element_size);
   if (replacement == NULL) {
      return -1;
   }
   *memory = replacement;
   *capacity = grown;
   return 0;
}

static bool
valid_byte(const struct cwiki_buffer *buffer, size_t line, size_t byte)
{
   unsigned char value;

   if (buffer == NULL || line >= buffer->line_count ||
       byte > buffer->lines[line].length) {
      return false;
   }
   if (byte == buffer->lines[line].length) {
      return true;
   }
   value = (unsigned char)buffer->lines[line].bytes[byte];
   return (value & 0xc0U) != 0x80U;
}

static bool
ready_for_edit(const struct cwiki_undo *undo)
{
   return undo != NULL && undo->buffer != NULL && undo->pending != NULL &&
       undo->pending->applied_operations == undo->pending->operation_count &&
       undo->transition_kind == TRANSITION_NONE;
}

static int
prepare_operation(struct cwiki_undo *undo, struct undo_operation **operation)
{
   struct cwiki_undo_node *pending;

   if (!ready_for_edit(undo)) {
      errno = EINVAL;
      return -1;
   }
   pending = undo->pending;
   if (reserve((void **)&pending->operations, sizeof(*pending->operations),
       &pending->operation_capacity, pending->operation_count + 1U) != 0) {
      return -1;
   }
   *operation = &pending->operations[pending->operation_count];
   memset(*operation, 0, sizeof(**operation));
   return 0;
}

static void
finish_operation(struct cwiki_undo_node *pending)
{
   pending->operation_count++;
   pending->applied_operations++;
}

static int
apply_forward(struct cwiki_buffer *buffer,
    const struct undo_operation *operation)
{
   switch (operation->kind) {
   case OP_INSERT:
      return cwiki_buffer_insert(buffer, operation->line, operation->byte,
          operation->bytes, operation->length);
   case OP_DELETE:
      return cwiki_buffer_delete(buffer, operation->line, operation->byte,
          operation->length);
   case OP_SPLIT:
      return cwiki_buffer_split(buffer, operation->line, operation->byte);
   case OP_JOIN:
      return cwiki_buffer_join(buffer, operation->line);
   }
   errno = EINVAL;
   return -1;
}

static int
apply_reverse(struct cwiki_buffer *buffer,
    const struct undo_operation *operation)
{
   switch (operation->kind) {
   case OP_INSERT:
      return cwiki_buffer_delete(buffer, operation->line, operation->byte,
          operation->length);
   case OP_DELETE:
      return cwiki_buffer_insert(buffer, operation->line, operation->byte,
          operation->bytes, operation->length);
   case OP_SPLIT:
      return cwiki_buffer_join(buffer, operation->line);
   case OP_JOIN:
      return cwiki_buffer_split(buffer, operation->line, operation->byte);
   }
   errno = EINVAL;
   return -1;
}

static void
free_node(struct cwiki_undo_node *node)
{
   size_t index;

   if (node == NULL) {
      return;
   }
   for (index = 0U; index < node->operation_count; index++) {
      free(node->operations[index].bytes);
   }
   free(node->operations);
   free(node->children);
   free(node);
}

int
cwiki_undo_init(struct cwiki_undo *undo, struct cwiki_buffer *buffer)
{
   struct cwiki_undo_node *root;
   struct cwiki_undo_node **states;

   if (undo == NULL || buffer == NULL || buffer->line_count == 0U ||
       buffer->lines == NULL) {
      errno = EINVAL;
      return -1;
   }
   root = undo_calloc(1U, sizeof(*root));
   if (root == NULL) {
      return -1;
   }
   states = undo_malloc(4U * sizeof(*states));
   if (states == NULL) {
      free(root);
      return -1;
   }
   memset(undo, 0, sizeof(*undo));
   root->sequence = 0U;
   states[0] = root;
   undo->buffer = buffer;
   undo->root = root;
   undo->current = root;
   undo->states = states;
   undo->state_count = 1U;
   undo->state_capacity = 4U;
   return 0;
}

void
cwiki_undo_free(struct cwiki_undo *undo)
{
   size_t index;

   if (undo == NULL) {
      return;
   }
   free_node(undo->pending);
   for (index = 0U; index < undo->state_count; index++) {
      free_node(undo->states[index]);
   }
   free(undo->states);
   memset(undo, 0, sizeof(*undo));
}

int
cwiki_undo_begin(struct cwiki_undo *undo, uint64_t timestamp)
{
   struct cwiki_undo_node *pending;

   if (undo == NULL || undo->buffer == NULL || undo->current == NULL) {
      errno = EINVAL;
      return -1;
   }
   if (undo->pending != NULL || undo->transition_kind != TRANSITION_NONE ||
       undo->destination != NULL) {
      errno = EBUSY;
      return -1;
   }
   pending = undo_calloc(1U, sizeof(*pending));
   if (pending == NULL) {
      return -1;
   }
   if (reserve((void **)&undo->current->children,
       sizeof(*undo->current->children), &undo->current->child_capacity,
       undo->current->child_count + 1U) != 0 ||
       reserve((void **)&undo->states, sizeof(*undo->states),
       &undo->state_capacity, undo->state_count + 1U) != 0) {
      free(pending);
      return -1;
   }
   pending->parent = undo->current;
   pending->depth = undo->current->depth + 1U;
   pending->timestamp = timestamp;
   undo->pending = pending;
   return 0;
}

int
cwiki_undo_commit(struct cwiki_undo *undo)
{
   struct cwiki_undo_node *pending;

   if (undo == NULL || undo->pending == NULL) {
      errno = EINVAL;
      return -1;
   }
   pending = undo->pending;
   if (pending->applied_operations != pending->operation_count) {
      errno = EBUSY;
      return -1;
   }
   if (pending->operation_count == 0U) {
      free_node(pending);
      undo->pending = NULL;
      return 0;
   }
   if (undo->state_count > UINT64_MAX) {
      errno = EOVERFLOW;
      return -1;
   }
   pending->sequence = (uint64_t)undo->state_count;
   pending->parent->children[pending->parent->child_count++] = pending;
   undo->states[undo->state_count++] = pending;
   undo->current = pending;
   undo->pending = NULL;
   return 0;
}

int
cwiki_undo_cancel(struct cwiki_undo *undo)
{
   struct cwiki_undo_node *pending;

   if (undo == NULL || undo->pending == NULL) {
      errno = EINVAL;
      return -1;
   }
   pending = undo->pending;
   while (pending->applied_operations != 0U) {
      size_t index = pending->applied_operations - 1U;

      if (apply_reverse(undo->buffer, &pending->operations[index]) != 0) {
         return -1;
      }
      pending->applied_operations--;
   }
   free_node(pending);
   undo->pending = NULL;
   return 0;
}

bool
cwiki_undo_transaction_active(const struct cwiki_undo *undo)
{
   return undo != NULL && undo->pending != NULL;
}

int
cwiki_undo_checkpoint(const struct cwiki_undo *undo,
    struct cwiki_undo_checkpoint *checkpoint)
{
   if (!ready_for_edit(undo) || checkpoint == NULL) {
      errno = EINVAL;
      return -1;
   }
   checkpoint->operation_count = undo->pending->operation_count;
   return 0;
}

int
cwiki_undo_rollback(struct cwiki_undo *undo,
    const struct cwiki_undo_checkpoint *checkpoint)
{
   struct cwiki_undo_node *pending;

   if (!ready_for_edit(undo) || checkpoint == NULL ||
       checkpoint->operation_count > undo->pending->operation_count) {
      errno = EINVAL;
      return -1;
   }
   pending = undo->pending;
   while (pending->applied_operations > checkpoint->operation_count) {
      size_t index = pending->applied_operations - 1U;

      if (apply_reverse(undo->buffer, &pending->operations[index]) != 0) {
         return -1;
      }
      free(pending->operations[index].bytes);
      memset(&pending->operations[index], 0,
          sizeof(pending->operations[index]));
      pending->applied_operations--;
      pending->operation_count--;
   }
   return 0;
}

int
cwiki_undo_insert(struct cwiki_undo *undo, size_t line, size_t byte,
    const char *bytes, size_t length)
{
   struct undo_operation *operation;
   char *copy;

   if (!ready_for_edit(undo)) {
      errno = EINVAL;
      return -1;
   }
   if (bytes == NULL && length != 0U) {
      errno = EINVAL;
      return -1;
   }
   if (length == 0U) {
      return cwiki_buffer_insert(undo->buffer, line, byte, bytes, length);
   }
   if (prepare_operation(undo, &operation) != 0) {
      return -1;
   }
   copy = undo_malloc(length);
   if (copy == NULL) {
      return -1;
   }
   memcpy(copy, bytes, length);
   operation->kind = OP_INSERT;
   operation->line = line;
   operation->byte = byte;
   operation->length = length;
   operation->bytes = copy;
   if (cwiki_buffer_insert(undo->buffer, line, byte, bytes, length) != 0) {
      free(copy);
      memset(operation, 0, sizeof(*operation));
      return -1;
   }
   finish_operation(undo->pending);
   return 0;
}

int
cwiki_undo_delete(struct cwiki_undo *undo, size_t line, size_t byte,
    size_t length)
{
   struct undo_operation *operation;
   char *copy;

   if (!ready_for_edit(undo)) {
      errno = EINVAL;
      return -1;
   }
   if (!valid_byte(undo->buffer, line, byte) ||
       length > undo->buffer->lines[line].length - byte ||
       !valid_byte(undo->buffer, line, byte + length)) {
      errno = EINVAL;
      return -1;
   }
   if (length == 0U) {
      return 0;
   }
   if (prepare_operation(undo, &operation) != 0) {
      return -1;
   }
   copy = undo_malloc(length);
   if (copy == NULL) {
      return -1;
   }
   memcpy(copy, undo->buffer->lines[line].bytes + byte, length);
   operation->kind = OP_DELETE;
   operation->line = line;
   operation->byte = byte;
   operation->length = length;
   operation->bytes = copy;
   if (cwiki_buffer_delete(undo->buffer, line, byte, length) != 0) {
      free(copy);
      memset(operation, 0, sizeof(*operation));
      return -1;
   }
   finish_operation(undo->pending);
   return 0;
}

int
cwiki_undo_split(struct cwiki_undo *undo, size_t line, size_t byte)
{
   struct undo_operation *operation;

   if (prepare_operation(undo, &operation) != 0) {
      return -1;
   }
   operation->kind = OP_SPLIT;
   operation->line = line;
   operation->byte = byte;
   if (cwiki_buffer_split(undo->buffer, line, byte) != 0) {
      memset(operation, 0, sizeof(*operation));
      return -1;
   }
   finish_operation(undo->pending);
   return 0;
}

int
cwiki_undo_join(struct cwiki_undo *undo, size_t line)
{
   struct undo_operation *operation;

   if (!ready_for_edit(undo) || line >= undo->buffer->line_count ||
       line + 1U >= undo->buffer->line_count) {
      errno = EINVAL;
      return -1;
   }
   if (prepare_operation(undo, &operation) != 0) {
      return -1;
   }
   operation->kind = OP_JOIN;
   operation->line = line;
   operation->byte = undo->buffer->lines[line].length;
   if (cwiki_buffer_join(undo->buffer, line) != 0) {
      memset(operation, 0, sizeof(*operation));
      return -1;
   }
   finish_operation(undo->pending);
   return 0;
}

static int
undo_current_node(struct cwiki_undo *undo)
{
   struct cwiki_undo_node *node;

   if (undo->transition_kind == TRANSITION_NONE) {
      if (undo->current == undo->root) {
         errno = ENOENT;
         return -1;
      }
      undo->transition_kind = TRANSITION_UNDO;
      undo->transition_node = undo->current;
      undo->transition_operation = undo->current->operation_count;
   } else if (undo->transition_kind != TRANSITION_UNDO ||
       undo->transition_node != undo->current) {
      errno = EBUSY;
      return -1;
   }
   node = undo->transition_node;
   while (undo->transition_operation != 0U) {
      size_t index = undo->transition_operation - 1U;

      if (apply_reverse(undo->buffer, &node->operations[index]) != 0) {
         return -1;
      }
      undo->transition_operation--;
   }
   undo->current = node->parent;
   undo->transition_node = NULL;
   undo->transition_kind = TRANSITION_NONE;
   return 0;
}

static int
redo_node(struct cwiki_undo *undo, struct cwiki_undo_node *node)
{
   if (undo->transition_kind == TRANSITION_NONE) {
      undo->transition_kind = TRANSITION_REDO;
      undo->transition_node = node;
      undo->transition_operation = 0U;
   } else if (undo->transition_kind != TRANSITION_REDO ||
       undo->transition_node != node) {
      errno = EBUSY;
      return -1;
   }
   while (undo->transition_operation < node->operation_count) {
      if (apply_forward(undo->buffer,
          &node->operations[undo->transition_operation]) != 0) {
         return -1;
      }
      undo->transition_operation++;
   }
   undo->current = node;
   undo->transition_node = NULL;
   undo->transition_kind = TRANSITION_NONE;
   return 0;
}

int
cwiki_undo_to_parent(struct cwiki_undo *undo)
{
   if (undo == NULL || undo->pending != NULL || undo->destination != NULL) {
      errno = EBUSY;
      return -1;
   }
   return undo_current_node(undo);
}

int
cwiki_undo_redo_child(struct cwiki_undo *undo, size_t child_index)
{
   struct cwiki_undo_node *node;

   if (undo == NULL || undo->pending != NULL || undo->destination != NULL) {
      errno = EBUSY;
      return -1;
   }
   if (undo->transition_kind == TRANSITION_REDO) {
      node = undo->transition_node;
   } else {
      if (undo->transition_kind != TRANSITION_NONE ||
          child_index >= undo->current->child_count) {
         errno = child_index >= undo->current->child_count ? EINVAL : EBUSY;
         return -1;
      }
      node = undo->current->children[child_index];
   }
   return redo_node(undo, node);
}

static bool
is_ancestor(const struct cwiki_undo_node *ancestor,
    const struct cwiki_undo_node *node)
{
   const struct cwiki_undo_node *cursor = node;

   while (cursor != NULL && cursor->depth > ancestor->depth) {
      cursor = cursor->parent;
   }
   return cursor == ancestor;
}

static struct cwiki_undo_node *
next_descendant(struct cwiki_undo_node *ancestor,
    struct cwiki_undo_node *descendant)
{
   struct cwiki_undo_node *cursor = descendant;

   while (cursor->parent != ancestor) {
      cursor = cursor->parent;
   }
   return cursor;
}

static int
move_to(struct cwiki_undo *undo, struct cwiki_undo_node *target)
{
   if (undo->destination != NULL && undo->destination != target) {
      errno = EBUSY;
      return -1;
   }
   undo->destination = target;
   while (!is_ancestor(undo->current, target)) {
      if (undo_current_node(undo) != 0) {
         return -1;
      }
   }
   while (undo->current != target) {
      struct cwiki_undo_node *next = next_descendant(undo->current, target);

      if (redo_node(undo, next) != 0) {
         return -1;
      }
   }
   undo->destination = NULL;
   return 0;
}

static int
chronological(struct cwiki_undo *undo, bool newer)
{
   size_t sequence;

   if (undo == NULL || undo->pending != NULL) {
      errno = EBUSY;
      return -1;
   }
   if (undo->destination != NULL) {
      return move_to(undo, undo->destination);
   }
   sequence = (size_t)undo->current->sequence;
   if ((!newer && sequence == 0U) ||
       (newer && sequence + 1U >= undo->state_count)) {
      errno = ENOENT;
      return -1;
   }
   sequence = newer ? sequence + 1U : sequence - 1U;
   return move_to(undo, undo->states[sequence]);
}

int
cwiki_undo_older(struct cwiki_undo *undo)
{
   return chronological(undo, false);
}

int
cwiki_undo_newer(struct cwiki_undo *undo)
{
   return chronological(undo, true);
}

size_t
cwiki_undo_state_count(const struct cwiki_undo *undo)
{
   return undo == NULL ? 0U : undo->state_count;
}

int
cwiki_undo_state_info(const struct cwiki_undo *undo, size_t sequence,
    struct cwiki_undo_state_info *info)
{
   const struct cwiki_undo_node *node;

   if (undo == NULL || info == NULL || sequence >= undo->state_count) {
      errno = EINVAL;
      return -1;
   }
   node = undo->states[sequence];
   info->sequence = node->sequence;
   info->timestamp = node->timestamp;
   info->parent_sequence = node->parent == NULL ? 0U :
       node->parent->sequence;
   info->child_count = node->child_count;
   info->operation_count = node->operation_count;
   info->current = node == undo->current &&
       undo->transition_kind == TRANSITION_NONE;
   return 0;
}
