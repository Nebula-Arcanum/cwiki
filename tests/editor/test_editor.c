#include "document.h"
#include "editor.h"
#include "motion.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

struct fixture {
   struct cwiki_document document;
   struct cwiki_editor editor;
   char path[64];
};

static void
write_all(int descriptor, const char *bytes, size_t length)
{
   size_t offset = 0U;

   while (offset < length) {
      ssize_t written = write(descriptor, bytes + offset, length - offset);

      check(written > 0, "write fixture bytes");
      if (written <= 0) {
         return;
      }
      offset += (size_t)written;
   }
}

static void
fixture_init(struct fixture *fixture, const char *bytes)
{
   int descriptor;

   (void)memset(fixture, 0, sizeof(*fixture));
   (void)strcpy(fixture->path, "/tmp/cwiki-editor-XXXXXX");
   descriptor = mkstemp(fixture->path);
   check(descriptor >= 0, "create editor fixture");
   if (descriptor >= 0) {
      write_all(descriptor, bytes, strlen(bytes));
      check(close(descriptor) == 0, "close editor fixture");
   }
   check(cwiki_document_load(&fixture->document, fixture->path) == 0,
       "load editor fixture document");
   check(cwiki_editor_init(&fixture->editor, &fixture->document) ==
       CWIKI_EDITOR_OK, "initialize editor");
}

static void
fixture_free(struct fixture *fixture)
{
   cwiki_editor_free(&fixture->editor);
   cwiki_document_free(&fixture->document);
   (void)unlink(fixture->path);
}

static bool
content_is(struct fixture *fixture, const char *expected)
{
   char *bytes = NULL;
   size_t length = 0U;
   size_t expected_length = strlen(expected);
   bool equal;

   if (cwiki_buffer_encode(&fixture->document.buffer, &bytes, &length) != 0) {
      return false;
   }
   equal = length == expected_length &&
       memcmp(bytes, expected, expected_length) == 0;
   free(bytes);
   return equal;
}

static void
test_insert_replace_backspace_and_undo(void)
{
   struct fixture fixture;

   fixture_init(&fixture, "ab\ncd");
   check(cwiki_editor_enter_insert(&fixture.editor, true, 10U) ==
       CWIKI_EDITOR_OK, "a starts one insert transaction");
   check(cwiki_editor_insert(&fixture.editor, "界", strlen("界")) ==
       CWIKI_EDITOR_OK, "insert one multibyte grapheme");
   check(cwiki_editor_enter(&fixture.editor) == CWIKI_EDITOR_OK,
       "Enter splits the current line");
   check(cwiki_editor_insert(&fixture.editor, "e\xcc\x81", 3U) ==
       CWIKI_EDITOR_OK, "insert one combining grapheme");
   check(cwiki_editor_backspace(&fixture.editor) == CWIKI_EDITOR_OK,
       "Backspace removes a whole combining grapheme");
   check(cwiki_editor_backspace(&fixture.editor) == CWIKI_EDITOR_OK,
       "Backspace at BOL joins with the preceding line");
   check(cwiki_editor_escape(&fixture.editor) == CWIKI_EDITOR_OK &&
       fixture.editor.mode == CWIKI_EDITOR_NORMAL &&
       content_is(&fixture, "a界b\ncd"),
       "Escape commits the complete insert session as one state");
   check(cwiki_undo_state_count(&fixture.editor.undo) == 2U,
       "the insert session creates exactly one undo state");
   check(cwiki_editor_undo(&fixture.editor) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "ab\ncd"), "one undo restores the whole session");
   check(cwiki_editor_redo(&fixture.editor) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "a界b\ncd"), "redo restores the whole session");
   fixture_free(&fixture);

   fixture_init(&fixture, "a界c");
   check(cwiki_editor_enter_replace(&fixture.editor, 20U) == CWIKI_EDITOR_OK &&
       cwiki_editor_insert(&fixture.editor, "βZQ!", strlen("βZQ!")) ==
       CWIKI_EDITOR_OK && cwiki_editor_escape(&fixture.editor) ==
       CWIKI_EDITOR_OK && content_is(&fixture, "βZQ!"),
       "Replace overwrites graphemes then inserts after EOL");
   check(cwiki_editor_undo(&fixture.editor) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "a界c"), "replace session is one undo step");
   fixture_free(&fixture);
}

static void
test_operators_characterwise_put_and_change(void)
{
   struct fixture fixture;

   fixture_init(&fixture, "alpha beta");
   check(cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_DELETE,
       30U) == CWIKI_EDITOR_OK &&
       cwiki_editor_apply_motion(&fixture.editor, NULL, NULL,
       CWIKI_MOTION_WORD_FORWARD, NULL) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "beta") &&
       fixture.editor.yank.length == 6U &&
       memcmp(fixture.editor.yank.bytes, "alpha ", 6U) == 0 &&
       !fixture.editor.yank.linewise,
       "dw deletes the exclusive word range into the unnamed yank");
   check(cwiki_editor_put(&fixture.editor, true, 31U) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "alpha beta"),
       "P puts characterwise bytes before the cursor");
   check(cwiki_editor_undo(&fixture.editor) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "beta"), "put is one independent undo step");
   check(cwiki_editor_undo(&fixture.editor) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "alpha beta"), "delete is one independent undo step");

   fixture.editor.motion.cursor = (struct cwiki_position){0U, 0U};
   check(cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_CHANGE,
       32U) == CWIKI_EDITOR_OK &&
       cwiki_editor_apply_motion(&fixture.editor, NULL, NULL,
       CWIKI_MOTION_WORD_FORWARD, NULL) == CWIKI_EDITOR_OK &&
       fixture.editor.mode == CWIKI_EDITOR_INSERT &&
       content_is(&fixture, "beta"),
       "cw uses the ordinary w endpoint with no exceptional rewrite");
   check(cwiki_editor_insert(&fixture.editor, "gamma ", 6U) ==
       CWIKI_EDITOR_OK && cwiki_editor_escape(&fixture.editor) ==
       CWIKI_EDITOR_OK && content_is(&fixture, "gamma beta"),
       "change deletion and inserted replacement share one undo transaction");
   check(cwiki_editor_undo(&fixture.editor) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "alpha beta"),
       "one undo reverses the complete change operation");
   fixture_free(&fixture);
}

static void
test_linewise_yank_put_delete_and_change(void)
{
   struct fixture fixture;

   fixture_init(&fixture, "one\ntwo\nthree");
   fixture.editor.motion.cursor = (struct cwiki_position){1U, 0U};
   check(cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_YANK,
       40U) == CWIKI_EDITOR_OK &&
       cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_YANK,
       40U) == CWIKI_EDITOR_OK && fixture.editor.yank.linewise &&
       fixture.editor.yank.length == 4U &&
       memcmp(fixture.editor.yank.bytes, "two\n", 4U) == 0,
       "yy stores one complete source line with linewise shape");
   check(cwiki_editor_put(&fixture.editor, false, 41U) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "one\ntwo\ntwo\nthree") &&
       fixture.editor.motion.cursor.line == 2U,
       "p inserts a linewise yank after the cursor line");
   check(cwiki_editor_put(&fixture.editor, true, 42U) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "one\ntwo\ntwo\ntwo\nthree") &&
       fixture.editor.motion.cursor.line == 2U,
       "P inserts a linewise yank before the cursor line");

   check(cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_DELETE,
       43U) == CWIKI_EDITOR_OK &&
       cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_DELETE,
       43U) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "one\ntwo\ntwo\nthree"),
       "dd removes exactly the current source line");
   fixture.editor.motion.cursor = (struct cwiki_position){1U, 0U};
   check(cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_CHANGE,
       44U) == CWIKI_EDITOR_OK &&
       cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_CHANGE,
       44U) == CWIKI_EDITOR_OK && fixture.editor.mode == CWIKI_EDITOR_INSERT &&
       content_is(&fixture, "one\n\ntwo\nthree"),
       "cc leaves one empty replacement line rather than merging neighbors");
   check(cwiki_editor_insert(&fixture.editor, "replaced", 8U) ==
       CWIKI_EDITOR_OK && cwiki_editor_escape(&fixture.editor) ==
       CWIKI_EDITOR_OK && content_is(&fixture, "one\nreplaced\ntwo\nthree"),
       "line change inserts into its retained empty line");
   fixture_free(&fixture);
}

static void
test_cross_line_and_eof_ranges(void)
{
   struct fixture fixture;

   fixture_init(&fixture, "a\nb\nc");
   check(cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_DELETE,
       45U) == CWIKI_EDITOR_OK &&
       cwiki_editor_apply_motion(&fixture.editor, NULL, NULL,
       CWIKI_MOTION_WORD_FORWARD, NULL) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "b\nc") && fixture.editor.yank.length == 2U &&
       memcmp(fixture.editor.yank.bytes, "a\n", 2U) == 0 &&
       !fixture.editor.yank.linewise,
       "a characterwise operator can remove a source newline exactly");
   check(cwiki_editor_put(&fixture.editor, true, 46U) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "a\nb\nc"),
       "characterwise put restores a yank containing a newline");

   fixture.editor.motion.cursor = (struct cwiki_position){0U, 0U};
   check(cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_DELETE,
       47U) == CWIKI_EDITOR_OK &&
       cwiki_editor_apply_motion(&fixture.editor, NULL, NULL,
       CWIKI_MOTION_DOCUMENT_LAST, NULL) == CWIKI_EDITOR_OK &&
       fixture.document.buffer.line_count == 1U &&
       content_is(&fixture, "") && fixture.editor.yank.linewise &&
       fixture.editor.yank.length == 6U,
       "a linewise EOF sentinel deletes every selected line but keeps a buffer");
   check(cwiki_editor_undo(&fixture.editor) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "a\nb\nc"),
       "one undo restores an all-lines deletion");
   fixture_free(&fixture);

   fixture_init(&fixture, "\nb");
   check(cwiki_editor_start_operator(&fixture.editor, CWIKI_EDITOR_DELETE,
       48U) == CWIKI_EDITOR_OK &&
       cwiki_editor_apply_motion(&fixture.editor, NULL, NULL,
       CWIKI_MOTION_WORD_FORWARD, NULL) == CWIKI_EDITOR_OK &&
       content_is(&fixture, "b") && fixture.editor.yank.length == 1U &&
       fixture.editor.yank.bytes[0] == '\n',
       "a cross-line range safely captures an empty first-line suffix");
   fixture_free(&fixture);
}

static void
test_commands_save_quit_and_cancel(void)
{
   struct fixture fixture;

   fixture_init(&fixture, "note");
   check(cwiki_editor_enter_insert(&fixture.editor, true, 50U) ==
       CWIKI_EDITOR_OK && cwiki_editor_insert(&fixture.editor, "!", 1U) ==
       CWIKI_EDITOR_OK && cwiki_editor_escape(&fixture.editor) ==
       CWIKI_EDITOR_OK && fixture.document.dirty,
       "an edit marks the document dirty");
   check(cwiki_editor_begin_command(&fixture.editor) == CWIKI_EDITOR_OK &&
       cwiki_editor_command_insert(&fixture.editor, "q", 1U) ==
       CWIKI_EDITOR_OK && cwiki_editor_execute_command(&fixture.editor) ==
       CWIKI_EDITOR_DIRTY && !fixture.editor.quit_requested,
       ":q refuses to discard a dirty buffer");
   check(cwiki_editor_escape(&fixture.editor) == CWIKI_EDITOR_OK &&
       fixture.editor.mode == CWIKI_EDITOR_NORMAL,
       "Escape cancels the refused command line");
   check(cwiki_editor_begin_command(&fixture.editor) == CWIKI_EDITOR_OK &&
       cwiki_editor_command_insert(&fixture.editor, "w", 1U) ==
       CWIKI_EDITOR_OK && cwiki_editor_execute_command(&fixture.editor) ==
       CWIKI_EDITOR_OK && !fixture.document.dirty,
       ":w clears dirty only after the durable save succeeds");
   check(cwiki_editor_begin_command(&fixture.editor) == CWIKI_EDITOR_OK &&
       cwiki_editor_command_insert(&fixture.editor, "q", 1U) ==
       CWIKI_EDITOR_OK && cwiki_editor_execute_command(&fixture.editor) ==
       CWIKI_EDITOR_OK && fixture.editor.quit_requested,
       ":q requests exit for a clean document");
   fixture_free(&fixture);

   fixture_init(&fixture, "x");
   check(cwiki_editor_begin_command(&fixture.editor) == CWIKI_EDITOR_OK &&
       cwiki_editor_command_insert(&fixture.editor, "w界", strlen("w界")) ==
       CWIKI_EDITOR_OK && cwiki_editor_command_backspace(&fixture.editor) ==
       CWIKI_EDITOR_OK && fixture.editor.command_length == 1U,
       "command-line Backspace removes one complete grapheme");
   check(cwiki_editor_command_insert(&fixture.editor, "q", 1U) ==
       CWIKI_EDITOR_OK && cwiki_editor_execute_command(&fixture.editor) ==
       CWIKI_EDITOR_OK && fixture.editor.quit_requested,
       ":wq is accepted after grapheme-safe command editing");
   fixture_free(&fixture);
}

int
main(void)
{
   test_insert_replace_backspace_and_undo();
   test_operators_characterwise_put_and_change();
   test_linewise_yank_put_delete_and_change();
   test_cross_line_and_eof_ranges();
   test_commands_save_quit_and_cancel();
   if (failures != 0) {
      (void)fprintf(stderr, "%d editor test(s) failed\n", failures);
      return EXIT_FAILURE;
   }
   (void)puts("editor tests passed");
   return EXIT_SUCCESS;
}
