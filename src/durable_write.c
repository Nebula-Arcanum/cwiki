#include "durable_write.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct write_control {
#ifdef CWIKI_DURABLE_WRITE_TESTING
   const struct cwiki_durable_write_test_fault *fault;
   size_t occurrences[CWIKI_DURABLE_WRITE_DIRECTORY_SYNC + 1U];
#endif
   int unused;
};

static int
inject_failure(struct write_control *control,
    enum cwiki_durable_write_operation operation)
{
#ifdef CWIKI_DURABLE_WRITE_TESTING
   const struct cwiki_durable_write_test_fault *fault = control->fault;

   control->occurrences[operation]++;
   if (fault != NULL && fault->operation == operation &&
       fault->occurrence == control->occurrences[operation]) {
      errno = fault->error_number;
      return 1;
   }
#else
   (void)control;
   (void)operation;
#endif
   return 0;
}

static size_t
write_size(const struct write_control *control, size_t remaining)
{
#ifdef CWIKI_DURABLE_WRITE_TESTING
   if (control->fault != NULL && control->fault->write_limit != 0U &&
       remaining > control->fault->write_limit) {
      return control->fault->write_limit;
   }
#else
   (void)control;
#endif
   return remaining;
}

static struct cwiki_durable_write_result
result(enum cwiki_durable_write_status status,
    enum cwiki_durable_write_operation operation, int error_number)
{
   struct cwiki_durable_write_result write_result;

   write_result.status = status;
   write_result.operation = operation;
   write_result.error_number = error_number;
   return write_result;
}

static int
make_paths(const char *path, char **directory, char **temporary)
{
   static const char suffix[] = "/.cwiki-save-XXXXXX";
   const char *slash = strrchr(path, '/');
   size_t directory_length;
   size_t allocation_length;

   if (slash == NULL) {
      directory_length = 1U;
   } else if (slash == path) {
      directory_length = 1U;
   } else {
      directory_length = (size_t)(slash - path);
   }
   if (directory_length > SIZE_MAX - sizeof(suffix)) {
      errno = ENAMETOOLONG;
      return -1;
   }
   allocation_length = directory_length + sizeof(suffix);
   *directory = malloc(directory_length + 1U);
   *temporary = malloc(allocation_length);
   if (*directory == NULL || *temporary == NULL) {
      free(*directory);
      free(*temporary);
      *directory = NULL;
      *temporary = NULL;
      return -1;
   }
   if (slash == NULL) {
      (*directory)[0] = '.';
   } else if (slash == path) {
      (*directory)[0] = '/';
   } else {
      memcpy(*directory, path, directory_length);
   }
   (*directory)[directory_length] = '\0';
   memcpy(*temporary, *directory, directory_length);
   memcpy(*temporary + directory_length, suffix, sizeof(suffix));
   return 0;
}

static struct cwiki_durable_write_result
durable_write(const char *path, const void *bytes, size_t length,
    struct write_control *control)
{
   struct stat destination_status;
   const unsigned char *source = bytes;
   char *directory = NULL;
   char *temporary = NULL;
   size_t written = 0U;
   int destination_exists = 0;
   int temporary_exists = 0;
   int file_descriptor = -1;
   int directory_descriptor = -1;
   int renamed = 0;
   enum cwiki_durable_write_operation failed_operation = CWIKI_DURABLE_WRITE_NONE;
   int failure_errno = 0;

   if (path == NULL || path[0] == '\0' || (bytes == NULL && length != 0U)) {
      errno = EINVAL;
      return result(CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE,
          CWIKI_DURABLE_WRITE_NONE, EINVAL);
   }
   if (make_paths(path, &directory, &temporary) != 0) {
      return result(CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE,
          CWIKI_DURABLE_WRITE_CREATE_TEMP, errno);
   }

   if (inject_failure(control, CWIKI_DURABLE_WRITE_STAT)) {
      failed_operation = CWIKI_DURABLE_WRITE_STAT;
      goto fail;
   }
   if (stat(path, &destination_status) == 0) {
      destination_exists = 1;
   } else if (errno != ENOENT) {
      failed_operation = CWIKI_DURABLE_WRITE_STAT;
      goto fail;
   }

   if (inject_failure(control, CWIKI_DURABLE_WRITE_CREATE_TEMP)) {
      failed_operation = CWIKI_DURABLE_WRITE_CREATE_TEMP;
      goto fail;
   }
   file_descriptor = mkstemp(temporary);
   if (file_descriptor == -1) {
      failed_operation = CWIKI_DURABLE_WRITE_CREATE_TEMP;
      goto fail;
   }
   temporary_exists = 1;

   while (written < length) {
      ssize_t count;
      size_t requested;

      if (inject_failure(control, CWIKI_DURABLE_WRITE_WRITE)) {
         failed_operation = CWIKI_DURABLE_WRITE_WRITE;
         goto fail;
      }
      requested = write_size(control, length - written);
      count = write(file_descriptor, source + written, requested);
      if (count < 0) {
         if (errno == EINTR) {
            continue;
         }
         failed_operation = CWIKI_DURABLE_WRITE_WRITE;
         goto fail;
      }
      if (count == 0) {
         errno = EIO;
         failed_operation = CWIKI_DURABLE_WRITE_WRITE;
         goto fail;
      }
      written += (size_t)count;
   }

   if (destination_exists &&
       (inject_failure(control, CWIKI_DURABLE_WRITE_SET_MODE) ||
       fchmod(file_descriptor, destination_status.st_mode & 07777) != 0)) {
      failed_operation = CWIKI_DURABLE_WRITE_SET_MODE;
      goto fail;
   }
   if (inject_failure(control, CWIKI_DURABLE_WRITE_FILE_SYNC) ||
       fsync(file_descriptor) != 0) {
      failed_operation = CWIKI_DURABLE_WRITE_FILE_SYNC;
      goto fail;
   }
   if (inject_failure(control, CWIKI_DURABLE_WRITE_FILE_CLOSE)) {
      failed_operation = CWIKI_DURABLE_WRITE_FILE_CLOSE;
      goto fail;
   }
   if (close(file_descriptor) != 0) {
      file_descriptor = -1;
      failed_operation = CWIKI_DURABLE_WRITE_FILE_CLOSE;
      goto fail;
   }
   file_descriptor = -1;

   if (inject_failure(control, CWIKI_DURABLE_WRITE_DIRECTORY_OPEN)) {
      failed_operation = CWIKI_DURABLE_WRITE_DIRECTORY_OPEN;
      goto fail;
   }
   directory_descriptor = open(directory, O_RDONLY);
   if (directory_descriptor == -1) {
      failed_operation = CWIKI_DURABLE_WRITE_DIRECTORY_OPEN;
      goto fail;
   }

   if (inject_failure(control, CWIKI_DURABLE_WRITE_RENAME) ||
       rename(temporary, path) != 0) {
      failed_operation = CWIKI_DURABLE_WRITE_RENAME;
      goto fail;
   }
   temporary_exists = 0;
   renamed = 1;

   if (inject_failure(control, CWIKI_DURABLE_WRITE_DIRECTORY_SYNC) ||
       fsync(directory_descriptor) != 0) {
      failed_operation = CWIKI_DURABLE_WRITE_DIRECTORY_SYNC;
      goto fail;
   }
   (void)close(directory_descriptor);

   free(temporary);
   free(directory);
   return result(CWIKI_DURABLE_WRITE_SUCCESS, CWIKI_DURABLE_WRITE_NONE, 0);

fail:
   failure_errno = errno;
   if (file_descriptor != -1) {
      (void)close(file_descriptor);
   }
   if (temporary_exists) {
      (void)unlink(temporary);
   }
   if (directory_descriptor != -1) {
      (void)close(directory_descriptor);
   }
   free(temporary);
   free(directory);
   errno = failure_errno;
   return result(renamed ? CWIKI_DURABLE_WRITE_DURABILITY_UNCERTAIN :
       CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE, failed_operation, failure_errno);
}

struct cwiki_durable_write_result
cwiki_durable_write(const char *path, const void *bytes, size_t length)
{
   struct write_control control = {0};

   return durable_write(path, bytes, length, &control);
}

#ifdef CWIKI_DURABLE_WRITE_TESTING
struct cwiki_durable_write_result
cwiki_durable_write_test(const char *path, const void *bytes, size_t length,
    const struct cwiki_durable_write_test_fault *fault)
{
   struct write_control control = {0};

   control.fault = fault;
   return durable_write(path, bytes, length, &control);
}
#endif
