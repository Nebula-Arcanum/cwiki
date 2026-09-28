#include "buffer.h"
#include "undo.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void
check(int condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static void
check_encoding(const struct cwiki_buffer *buffer, const char *expected,
    size_t expected_length, const char *message)
{
   char *actual = NULL;
   size_t actual_length = 0U;

   if (cwiki_buffer_encode(buffer, &actual, &actual_length) != 0) {
      check(0, message);
      return;
   }
   check(actual_length == expected_length &&
       memcmp(actual, expected, expected_length) == 0, message);
   free(actual);
}

static int
load_tree(struct cwiki_buffer *buffer, struct cwiki_undo *undo,
    const char *bytes)
{
   if (cwiki_buffer_init(buffer) != 0 ||
       cwiki_buffer_load(buffer, bytes, strlen(bytes)) != 0 ||
       cwiki_undo_init(undo, buffer) != 0) {
      check(0, "initialize undo fixture");
      return -1;
   }
   return 0;
}

static void
test_grouped_round_trip(void)
{
   struct cwiki_buffer buffer;
   struct cwiki_undo undo;
   struct cwiki_position boundary = {0U, 2U};
   struct cwiki_position later = {1U, 2U};
   struct cwiki_undo_state_info info;

   if (load_tree(&buffer, &undo, "abcd\nXYZ") != 0) {
      return;
   }
   check(cwiki_buffer_register_position(&buffer, &boundary) == 0 &&
       cwiki_buffer_register_position(&buffer, &later) == 0,
       "register positions for grouped transaction");
   check(cwiki_undo_begin(&undo, UINT64_C(101)) == 0,
       "begin grouped transaction");
   check(cwiki_undo_insert(&undo, 0U, 2U, "PQ", 2U) == 0,
       "record insert operation");
   check(cwiki_undo_delete(&undo, 1U, 1U, 1U) == 0,
       "record delete operation");
   check(cwiki_undo_split(&undo, 0U, 4U) == 0,
       "record split operation");
   check(cwiki_undo_join(&undo, 1U) == 0, "record join operation");
   check(cwiki_undo_commit(&undo) == 0, "commit grouped transaction");
   check_encoding(&buffer, "abPQ\ncdXZ", 9U,
       "four asymmetric edits produce exact expected bytes");
   check(boundary.line == 1U && boundary.byte == 0U &&
       later.line == 1U && later.byte == 3U,
       "forward edits use buffer position fixups");
   check(cwiki_undo_state_count(&undo) == 2U &&
       cwiki_undo_state_info(&undo, 1U, &info) == 0 &&
       info.timestamp == UINT64_C(101) && info.operation_count == 4U,
       "one multi-edit transaction creates one timestamped state");

   check(cwiki_undo_to_parent(&undo) == 0,
       "one undo reverses the whole transaction");
   check_encoding(&buffer, "abcd\nXYZ", 8U,
       "undo restores exact original bytes");
   check(boundary.line == 0U && boundary.byte == 2U &&
       later.line == 1U && later.byte == 2U,
       "inverse operations run registered-position fixups");
   check(cwiki_undo_redo_child(&undo, 0U) == 0,
       "redo grouped transaction");
   check_encoding(&buffer, "abPQ\ncdXZ", 9U,
       "redo restores exact edited bytes");
   check(cwiki_undo_to_parent(&undo) == 0,
       "second undo remains deterministic");
   check_encoding(&buffer, "abcd\nXYZ", 8U,
       "repeated round trip restores exact original bytes");

   cwiki_undo_free(&undo);
   cwiki_buffer_free(&buffer);
}

static void
append_step(struct cwiki_undo *undo, const char *text, uint64_t timestamp,
    const char *message)
{
   size_t line = undo->buffer->line_count - 1U;
   size_t byte = undo->buffer->lines[line].length;

   check(cwiki_undo_begin(undo, timestamp) == 0 &&
       cwiki_undo_insert(undo, line, byte, text, strlen(text)) == 0 &&
       cwiki_undo_commit(undo) == 0, message);
}

static void
test_branches_and_chronology(void)
{
   struct cwiki_buffer buffer;
   struct cwiki_undo undo;
   struct cwiki_undo_state_info branch;
   struct cwiki_undo_state_info old_child;
   struct cwiki_undo_state_info new_child;

   if (load_tree(&buffer, &undo, "root") != 0) {
      return;
   }
   append_step(&undo, "A", UINT64_C(110), "commit first branch ancestor");
   append_step(&undo, "B", UINT64_C(220), "commit old branch child");
   check(cwiki_undo_to_parent(&undo) == 0, "undo to branch point");
   append_step(&undo, "C", UINT64_C(330), "commit new branch child");
   check_encoding(&buffer, "rootAC", 6U,
       "undo then edit selects new branch without old bytes");
   check(cwiki_undo_state_count(&undo) == 4U &&
       cwiki_undo_state_info(&undo, 1U, &branch) == 0 &&
       cwiki_undo_state_info(&undo, 2U, &old_child) == 0 &&
       cwiki_undo_state_info(&undo, 3U, &new_child) == 0 &&
       branch.child_count == 2U && old_child.parent_sequence == 1U &&
       new_child.parent_sequence == 1U &&
       old_child.timestamp == UINT64_C(220) &&
       new_child.timestamp == UINT64_C(330),
       "branch metadata retains parents, children, order, and timestamps");

   check(cwiki_undo_to_parent(&undo) == 0 &&
       cwiki_undo_redo_child(&undo, 0U) == 0,
       "select old redo child explicitly");
   check_encoding(&buffer, "rootAB", 6U,
       "old branch remains reachable after undo and edit");
   check(cwiki_undo_to_parent(&undo) == 0 &&
       cwiki_undo_redo_child(&undo, 1U) == 0,
       "select new redo child explicitly");
   check_encoding(&buffer, "rootAC", 6U,
       "new branch is independently selectable");

   check(cwiki_undo_older(&undo) == 0, "g- crosses to older sibling state");
   check_encoding(&buffer, "rootAB", 6U,
       "chronological older traversal crosses branches");
   check(cwiki_undo_older(&undo) == 0, "g- reaches common ancestor");
   check_encoding(&buffer, "rootA", 5U,
       "chronological older traversal reaches ancestor state");
   check(cwiki_undo_older(&undo) == 0, "g- reaches initial state");
   check_encoding(&buffer, "root", 4U,
       "chronological traversal includes the initial state");
   check(cwiki_undo_older(&undo) == -1 && errno == ENOENT,
       "g- reports the oldest boundary");
   check(cwiki_undo_newer(&undo) == 0 &&
       cwiki_undo_newer(&undo) == 0,
       "g+ advances in commit chronology");
   check_encoding(&buffer, "rootAB", 6U,
       "g+ reaches old child before later sibling");
   check(cwiki_undo_newer(&undo) == 0,
       "g+ crosses from old child to later sibling");
   check_encoding(&buffer, "rootAC", 6U,
       "g+ reaches newest retained state");
   check(cwiki_undo_newer(&undo) == -1 && errno == ENOENT,
       "g+ reports the newest boundary");

   cwiki_undo_free(&undo);
   cwiki_buffer_free(&buffer);
}

static void
test_cancel_and_empty_transaction(void)
{
   struct cwiki_buffer buffer;
   struct cwiki_undo undo;
   struct cwiki_position first = {0U, 1U};
   struct cwiki_position second = {1U, 3U};

   if (load_tree(&buffer, &undo, "left\nRIGHT") != 0) {
      return;
   }
   check(cwiki_buffer_register_position(&buffer, &first) == 0 &&
       cwiki_buffer_register_position(&buffer, &second) == 0,
       "register cancel positions");
   check(cwiki_undo_begin(&undo, UINT64_C(410)) == 0 &&
       cwiki_undo_insert(&undo, 0U, 1U, "12", 2U) == 0 &&
       cwiki_undo_delete(&undo, 1U, 1U, 2U) == 0 &&
       cwiki_undo_split(&undo, 0U, 3U) == 0 &&
       cwiki_undo_join(&undo, 1U) == 0,
       "apply cancellable mixed transaction");
   check(cwiki_undo_cancel(&undo) == 0,
       "cancel rolls back every applied operation");
   check_encoding(&buffer, "left\nRIGHT", 10U,
       "cancel restores exact initial bytes");
   check(first.line == 0U && first.byte == 1U &&
       second.line == 1U && second.byte == 3U,
       "cancel rollback preserves reversible position fixups");
   check(cwiki_undo_state_count(&undo) == 1U,
       "cancel creates no retained state");

   check(cwiki_undo_begin(&undo, UINT64_C(420)) == 0 &&
       cwiki_undo_commit(&undo) == 0,
       "commit an empty transaction");
   check(cwiki_undo_state_count(&undo) == 1U,
       "empty transaction creates no state");

   cwiki_undo_free(&undo);
   cwiki_buffer_free(&buffer);
}

static void
test_failures_leave_state_coherent(void)
{
   struct cwiki_buffer buffer;
   struct cwiki_undo undo;
   struct cwiki_undo uninitialized;

   check(cwiki_buffer_init(&buffer) == 0 &&
       cwiki_buffer_load(&buffer, "asym\nTAIL", 9U) == 0,
       "initialize failure fixture");
   cwiki_undo_test_fail_allocation_after(0U);
   check(cwiki_undo_init(&uninitialized, &buffer) == -1 && errno == ENOMEM,
       "initial allocation failure is reported");
   cwiki_undo_test_reset_allocation();
   check(cwiki_undo_init(&undo, &buffer) == 0,
       "initialize tree after injected failure");

   cwiki_undo_test_fail_allocation_after(0U);
   check(cwiki_undo_begin(&undo, UINT64_C(500)) == -1 && errno == ENOMEM &&
       !cwiki_undo_transaction_active(&undo) &&
       cwiki_undo_state_count(&undo) == 1U,
       "begin allocation failure leaves tree unchanged");
   cwiki_undo_test_reset_allocation();

   check(cwiki_undo_begin(&undo, UINT64_C(510)) == 0,
       "begin range-failure transaction");
   check(cwiki_undo_insert(&undo, 0U, 1U, NULL, 1U) == -1 &&
       errno == EINVAL,
       "nonempty null insert fails without copying");
   check(cwiki_undo_insert(&undo, 0U, 99U, "q", 1U) == -1 &&
       errno == EINVAL,
       "invalid insert range fails");
   check(cwiki_undo_delete(&undo, 1U, 3U, 9U) == -1 && errno == EINVAL,
       "invalid delete range fails");
   check(cwiki_undo_join(&undo, 1U) == -1 && errno == EINVAL,
       "invalid join range fails");
   check_encoding(&buffer, "asym\nTAIL", 9U,
       "range failures leave exact text unchanged");
   check(cwiki_undo_commit(&undo) == 0 &&
       cwiki_undo_state_count(&undo) == 1U,
       "only failed edits still form an empty transaction");

   check(cwiki_undo_begin(&undo, UINT64_C(520)) == 0,
       "begin allocation-failure transaction");
   cwiki_undo_test_fail_allocation_after(0U);
   check(cwiki_undo_insert(&undo, 0U, 2U, "JK", 2U) == -1 &&
       errno == ENOMEM,
       "operation-array allocation failure is reported");
   cwiki_undo_test_fail_allocation_after(1U);
   check(cwiki_undo_delete(&undo, 1U, 1U, 2U) == -1 &&
       errno == ENOMEM,
       "deleted-byte allocation failure is reported");
   cwiki_undo_test_reset_allocation();
   check_encoding(&buffer, "asym\nTAIL", 9U,
       "allocation failures leave exact text unchanged");
   check(cwiki_undo_insert(&undo, 0U, 2U, "JK", 2U) == 0 &&
       cwiki_undo_commit(&undo) == 0,
       "transaction remains usable after allocation failures");
   check_encoding(&buffer, "asJKym\nTAIL", 11U,
       "successful retry records only the applied edit");
   check(cwiki_undo_to_parent(&undo) == 0,
       "successful retry remains undoable");
   check_encoding(&buffer, "asym\nTAIL", 9U,
       "undo after failed attempts restores exact bytes");

   cwiki_undo_free(&undo);
   cwiki_buffer_free(&buffer);
}

int
main(void)
{
   test_grouped_round_trip();
   test_branches_and_chronology();
   test_cancel_and_empty_transaction();
   test_failures_leave_state_coherent();
   if (failures != 0) {
      (void)fprintf(stderr, "%d undo test(s) failed\n", failures);
      return 1;
   }
   (void)printf("undo tests passed\n");
   return 0;
}
