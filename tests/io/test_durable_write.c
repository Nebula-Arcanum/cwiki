#include "durable_write.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int failures;

static const unsigned char old_bytes[] = {
   0xa5U, 'o', 'l', 'd', 0x00U, 0x7fU, '\n'
};
static const unsigned char new_bytes[] = {
   'N', 0x00U, 'e', 'w', '\n', 0xffU, 0x11U, '!', 'x', 0x80U, 'z'
};

static void
check(int condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static int
write_file(const char *path, const unsigned char *bytes, size_t length,
    mode_t mode)
{
   int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
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
   if (close(descriptor) != 0) {
      return -1;
   }
   return chmod(path, mode);
}

static int
file_equals(const char *path, const unsigned char *expected, size_t length)
{
   unsigned char buffer[64];
   int descriptor = open(path, O_RDONLY);
   ssize_t count;

   if (descriptor == -1) {
      return 0;
   }
   count = read(descriptor, buffer, sizeof(buffer));
   if (count < 0 || close(descriptor) != 0) {
      return 0;
   }
   return (size_t)count == length && memcmp(buffer, expected, length) == 0;
}

static int
temporary_count(const char *directory)
{
   DIR *stream = opendir(directory);
   struct dirent *entry;
   int count = 0;

   if (stream == NULL) {
      return -1;
   }
   while ((entry = readdir(stream)) != NULL) {
      if (strncmp(entry->d_name, ".cwiki-save-", 12U) == 0) {
         count++;
      }
   }
   if (closedir(stream) != 0) {
      return -1;
   }
   return count;
}

static void
reset_destination(const char *path)
{
   check(write_file(path, old_bytes, sizeof(old_bytes), 0640) == 0,
       "reset destination bytes and mode");
}

static void
test_pre_rename_failures(const char *directory, const char *path)
{
   static const enum cwiki_durable_write_operation operations[] = {
      CWIKI_DURABLE_WRITE_STAT,
      CWIKI_DURABLE_WRITE_CREATE_TEMP,
      CWIKI_DURABLE_WRITE_SET_MODE,
      CWIKI_DURABLE_WRITE_WRITE,
      CWIKI_DURABLE_WRITE_FILE_SYNC,
      CWIKI_DURABLE_WRITE_FILE_CLOSE,
      CWIKI_DURABLE_WRITE_DIRECTORY_OPEN,
      CWIKI_DURABLE_WRITE_RENAME
   };
   size_t index;

   for (index = 0U; index < sizeof(operations) / sizeof(operations[0]); index++) {
      struct cwiki_durable_write_test_fault fault = {
         operations[index], 1U, EIO, 0U
      };
      struct cwiki_durable_write_result result;

      reset_destination(path);
      result = cwiki_durable_write_test(path, new_bytes, sizeof(new_bytes),
          &fault);
      check(result.status == CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE,
          "pre-rename boundary reports pre-rename failure");
      check(result.operation == operations[index] && result.error_number == EIO,
          "pre-rename failure identifies injected operation and errno");
      check(file_equals(path, old_bytes, sizeof(old_bytes)),
          "pre-rename failure leaves exact old bytes");
      check(temporary_count(directory) == 0,
          "pre-rename failure removes temporary file");
   }
}

static void
test_partial_write_failure(const char *directory, const char *path)
{
   const struct cwiki_durable_write_test_fault fault = {
      CWIKI_DURABLE_WRITE_WRITE, 2U, ENOSPC, 3U
   };
   struct cwiki_durable_write_result result;

   reset_destination(path);
   result = cwiki_durable_write_test(path, new_bytes, sizeof(new_bytes), &fault);
   check(result.status == CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE &&
       result.operation == CWIKI_DURABLE_WRITE_WRITE &&
       result.error_number == ENOSPC,
       "failure after a short write reports the write error");
   check(file_equals(path, old_bytes, sizeof(old_bytes)),
       "failure after a short write leaves exact old bytes");
   check(temporary_count(directory) == 0,
       "failure after a short write removes temporary file");
}

static void
test_post_rename_failures(const char *directory, const char *path)
{
   const struct cwiki_durable_write_test_fault fault = {
      CWIKI_DURABLE_WRITE_DIRECTORY_SYNC, 1U, EIO, 0U
   };
   struct cwiki_durable_write_result result;
   int complete_old;
   int complete_new;

   reset_destination(path);
   result = cwiki_durable_write_test(path, new_bytes, sizeof(new_bytes), &fault);
   check(result.status == CWIKI_DURABLE_WRITE_DURABILITY_UNCERTAIN,
       "directory-sync failure reports durability uncertain");
   check(result.operation == CWIKI_DURABLE_WRITE_DIRECTORY_SYNC &&
       result.error_number == EIO,
       "directory-sync failure identifies the operation and errno");
   complete_old = file_equals(path, old_bytes, sizeof(old_bytes));
   complete_new = file_equals(path, new_bytes, sizeof(new_bytes));
   check(complete_old || complete_new,
       "directory-sync failure leaves exact complete old or new bytes");
   check(temporary_count(directory) == 0,
       "directory-sync failure leaves no temporary file");
}

static void
test_success(const char *directory, const char *path)
{
   const struct cwiki_durable_write_test_fault short_writes = {
      CWIKI_DURABLE_WRITE_NONE, 0U, 0, 3U
   };
   struct cwiki_durable_write_result result;
   struct stat status;

   reset_destination(path);
   result = cwiki_durable_write_test(path, new_bytes, sizeof(new_bytes),
       &short_writes);
   check(result.status == CWIKI_DURABLE_WRITE_SUCCESS &&
       result.operation == CWIKI_DURABLE_WRITE_NONE && result.error_number == 0,
       "success reports success after deterministic short writes");
   check(file_equals(path, new_bytes, sizeof(new_bytes)),
       "success leaves exact asymmetric binary payload");
   check(stat(path, &status) == 0 && (status.st_mode & 07777) == 0640,
       "success preserves destination permission bits");
   check(temporary_count(directory) == 0,
       "success leaves no temporary file");
}

static void
test_absent_destination(const char *directory, const char *path)
{
   struct cwiki_durable_write_result result;

   check(unlink(path) == 0, "remove destination before absent-file test");
   result = cwiki_durable_write(path, new_bytes, sizeof(new_bytes));
   check(result.status == CWIKI_DURABLE_WRITE_SUCCESS,
       "absent destination can be created durably");
   check(file_equals(path, new_bytes, sizeof(new_bytes)),
       "absent destination receives exact binary payload");
   check(temporary_count(directory) == 0,
       "absent-destination success leaves no temporary file");
}

int
main(void)
{
   char directory[] = "/tmp/cwiki-durable-write-XXXXXX";
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

   test_pre_rename_failures(directory, path);
   test_partial_write_failure(directory, path);
   test_post_rename_failures(directory, path);
   test_success(directory, path);
   test_absent_destination(directory, path);

   check(unlink(path) == 0, "remove test destination");
   check(rmdir(directory) == 0, "remove isolated test directory");
   if (failures != 0) {
      return 1;
   }
   (void)puts("durable write: ok");
   return 0;
}
