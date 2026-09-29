#include "document.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int failures;

static void
check(int condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static int
write_file(const char *path, const char *bytes, size_t length)
{
   int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
   size_t offset = 0U;

   if (descriptor == -1) {
      return -1;
   }
   while (offset < length) {
      ssize_t count = write(descriptor, bytes + offset, length - offset);

      if (count <= 0) {
         int saved_errno = count == 0 ? EIO : errno;

         (void)close(descriptor);
         errno = saved_errno;
         return -1;
      }
      offset += (size_t)count;
   }
   return close(descriptor);
}

static int
file_equals(const char *path, const char *expected, size_t length)
{
   char actual[256];
   int descriptor = open(path, O_RDONLY);
   ssize_t count;

   if (descriptor == -1) {
      return 0;
   }
   count = read(descriptor, actual, sizeof(actual));
   if (count < 0 || close(descriptor) != 0) {
      return 0;
   }
   return (size_t)count == length && memcmp(actual, expected, length) == 0;
}

static void
test_round_trip(const char *path, const char *bytes, size_t length,
    enum cwiki_line_ending ending)
{
   struct cwiki_document document;
   struct cwiki_durable_write_result result;

   check(write_file(path, bytes, length) == 0, "write round-trip fixture");
   check(cwiki_document_load(&document, path) == 0, "load document");
   if (document.path == NULL) {
      return;
   }
   check(document.path != path && strcmp(document.path, path) == 0,
       "document owns a copied path");
   check(document.buffer.line_ending == ending && !document.dirty,
       "load records line endings and starts clean");
   cwiki_document_mark_dirty(&document);
   check(document.dirty, "explicit hook marks document dirty");
   result = cwiki_document_save(&document);
   check(result.status == CWIKI_DURABLE_WRITE_SUCCESS && !document.dirty,
       "successful save clears dirty state");
   check(file_equals(path, bytes, length), "save preserves exact file bytes");
   cwiki_document_free(&document);
}

static void
test_load_failures(const char *directory, const char *path)
{
   static const char mixed[] = "a\r\nb\nc\r\n";
   static const char invalid_utf8[] = {'a', (char)0xff, '\n'};
   struct cwiki_document document;

   check(write_file(path, mixed, sizeof(mixed) - 1U) == 0,
       "write mixed-ending fixture");
   errno = 0;
   check(cwiki_document_load(&document, path) == -1 && errno == EINVAL,
       "mixed line endings are rejected");
   check(document.path == NULL && document.buffer.lines == NULL,
       "mixed-ending failure leaves no partial ownership");
   cwiki_document_free(&document);

   check(write_file(path, invalid_utf8, sizeof(invalid_utf8)) == 0,
       "write invalid UTF-8 fixture");
   errno = 0;
   check(cwiki_document_load(&document, path) == -1 && errno == EINVAL,
       "invalid UTF-8 is rejected");
   check(document.path == NULL && document.buffer.lines == NULL,
       "invalid UTF-8 failure leaves no partial ownership");
   cwiki_document_free(&document);

   check(unlink(path) == 0, "remove fixture before missing-file test");
   errno = 0;
   check(cwiki_document_load(&document, path) == -1 && errno == ENOENT,
       "missing file is reported instead of silently created");
   check(document.path == NULL && document.buffer.lines == NULL,
       "missing-file failure leaves no partial ownership");
   cwiki_document_free(&document);

   errno = 0;
   check(cwiki_document_load(&document, directory) == -1,
       "read error is reported");
   check(document.path == NULL && document.buffer.lines == NULL,
       "read failure leaves no partial ownership");
   cwiki_document_free(&document);
}

static void
test_new_document(const char *path)
{
   static const char inserted[] = "new\nfile";
   struct cwiki_document document;
   struct cwiki_durable_write_result result;

   check(cwiki_document_init(&document, path) == 0,
       "initialize new document");
   check(document.dirty &&
       document.buffer.line_ending == CWIKI_LINE_ENDING_LF,
       "new document is dirty and defaults to LF");
   check(cwiki_buffer_load(&document.buffer, inserted,
       sizeof(inserted) - 1U) == 0, "edit new document buffer");
   cwiki_document_mark_dirty(&document);
   result = cwiki_document_save(&document);
   check(result.status == CWIKI_DURABLE_WRITE_SUCCESS && !document.dirty,
       "new document saves successfully");
   check(file_equals(path, inserted, sizeof(inserted) - 1U),
       "new document writes exact LF bytes");
   cwiki_document_free(&document);
}

static void
test_durable_results(const char *path)
{
   static const char original[] = "old\n";
   static const char replacement[] = "replacement\n";
   const struct cwiki_durable_write_test_fault pre_rename = {
      CWIKI_DURABLE_WRITE_RENAME, 1U, EIO, 0U
   };
   const struct cwiki_durable_write_test_fault post_rename = {
      CWIKI_DURABLE_WRITE_DIRECTORY_SYNC, 1U, EIO, 0U
   };
   struct cwiki_document document;
   struct cwiki_durable_write_result result;

   check(write_file(path, original, sizeof(original) - 1U) == 0,
       "write durable-result fixture");
   check(cwiki_document_load(&document, path) == 0,
       "load durable-result fixture");
   if (document.path == NULL) {
      return;
   }
   check(cwiki_buffer_load(&document.buffer, replacement,
       sizeof(replacement) - 1U) == 0, "replace document contents");
   cwiki_document_mark_dirty(&document);

   result = cwiki_document_save_test(&document, &pre_rename);
   check(result.status == CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE &&
       result.operation == CWIKI_DURABLE_WRITE_RENAME && document.dirty,
       "pre-rename failure keeps document dirty");
   check(file_equals(path, original, sizeof(original) - 1U),
       "pre-rename failure preserves original bytes");

   result = cwiki_document_save_test(&document, &post_rename);
   check(result.status == CWIKI_DURABLE_WRITE_DURABILITY_UNCERTAIN &&
       result.operation == CWIKI_DURABLE_WRITE_DIRECTORY_SYNC &&
       document.dirty, "durability-uncertain save keeps document dirty");
   check(file_equals(path, replacement, sizeof(replacement) - 1U),
       "post-rename failure leaves complete replacement bytes");

   result = cwiki_document_save_test(&document, NULL);
   check(result.status == CWIKI_DURABLE_WRITE_SUCCESS && !document.dirty,
       "full durable success clears document dirty state");
   cwiki_document_free(&document);
}

static void
test_save_errors(const char *directory)
{
   struct cwiki_document document;
   struct cwiki_durable_write_result result;
   char path[256];

   (void)snprintf(path, sizeof(path), "%s/missing/note.md", directory);
   check(cwiki_document_init(&document, path) == 0,
       "initialize document with unavailable parent");
   result = cwiki_document_save(&document);
   check(result.status == CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE &&
       result.error_number != 0 && document.dirty,
       "ordinary save error is returned and keeps document dirty");
   cwiki_document_free(&document);

   result = cwiki_document_save(NULL);
   check(result.status == CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE &&
       result.operation == CWIKI_DURABLE_WRITE_NONE &&
       result.error_number == EINVAL, "invalid save input reports an error");
}

int
main(void)
{
   static const char lf[] = "alpha\n\0beta";
   static const char crlf[] = "alpha\r\n\0beta";
   char directory[] = "/tmp/cwiki-document-XXXXXX";
   char path[sizeof(directory) + 16U];
   int descriptor;

   descriptor = mkstemp(directory);
   check(descriptor >= 0, "reserve isolated test path");
   if (descriptor >= 0) {
      check(close(descriptor) == 0 && unlink(directory) == 0 &&
          mkdir(directory, 0700) == 0, "create isolated test directory");
   }
   if (failures != 0) {
      return 1;
   }
   (void)snprintf(path, sizeof(path), "%s/note.md", directory);

   test_round_trip(path, lf, sizeof(lf) - 1U, CWIKI_LINE_ENDING_LF);
   test_round_trip(path, crlf, sizeof(crlf) - 1U,
       CWIKI_LINE_ENDING_CRLF);
   test_load_failures(directory, path);
   test_new_document(path);
   test_durable_results(path);
   test_save_errors(directory);

   check(unlink(path) == 0, "remove test document");
   check(rmdir(directory) == 0, "remove isolated test directory");
   if (failures != 0) {
      return 1;
   }
   (void)puts("document: ok");
   return 0;
}
