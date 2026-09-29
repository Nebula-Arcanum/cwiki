#include "document.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
copy_path(char **copy, const char *path)
{
   size_t length;

   if (path == NULL || path[0] == '\0') {
      errno = EINVAL;
      return -1;
   }
   length = strlen(path);
   if (length == SIZE_MAX) {
      errno = ENAMETOOLONG;
      return -1;
   }
   *copy = malloc(length + 1U);
   if (*copy == NULL) {
      return -1;
   }
   memcpy(*copy, path, length + 1U);
   return 0;
}

static int
read_file(const char *path, char **bytes, size_t *length)
{
   char *contents = NULL;
   size_t capacity = 0U;
   size_t used = 0U;
   int descriptor;

   descriptor = open(path, O_RDONLY);
   if (descriptor == -1) {
      return -1;
   }
   for (;;) {
      ssize_t count;

      if (used == capacity) {
         size_t grown = capacity == 0U ? 4096U : capacity * 2U;
         char *replacement;

         if (grown < capacity) {
            errno = ENOMEM;
            goto fail;
         }
         replacement = realloc(contents, grown);
         if (replacement == NULL) {
            goto fail;
         }
         contents = replacement;
         capacity = grown;
      }
      count = read(descriptor, contents + used, capacity - used);
      if (count < 0) {
         if (errno == EINTR) {
            continue;
         }
         goto fail;
      }
      if (count == 0) {
         break;
      }
      used += (size_t)count;
   }
   if (close(descriptor) != 0) {
      free(contents);
      return -1;
   }
   *bytes = contents;
   *length = used;
   return 0;

fail:
   {
      int saved_errno = errno;

      (void)close(descriptor);
      free(contents);
      errno = saved_errno;
      return -1;
   }
}

int
cwiki_document_init(struct cwiki_document *document, const char *path)
{
   struct cwiki_document initialized = {0};

   if (document == NULL) {
      errno = EINVAL;
      return -1;
   }
   memset(document, 0, sizeof(*document));
   if (copy_path(&initialized.path, path) != 0) {
      return -1;
   }
   if (cwiki_buffer_init(&initialized.buffer) != 0) {
      free(initialized.path);
      return -1;
   }
   initialized.dirty = true;
   *document = initialized;
   return 0;
}

int
cwiki_document_load(struct cwiki_document *document, const char *path)
{
   struct cwiki_document loaded = {0};
   char *bytes = NULL;
   size_t length = 0U;

   if (document == NULL) {
      errno = EINVAL;
      return -1;
   }
   memset(document, 0, sizeof(*document));
   if (copy_path(&loaded.path, path) != 0) {
      return -1;
   }
   if (read_file(path, &bytes, &length) != 0 ||
       cwiki_buffer_load(&loaded.buffer, bytes, length) != 0) {
      int saved_errno = errno;

      free(bytes);
      cwiki_document_free(&loaded);
      errno = saved_errno;
      return -1;
   }
   free(bytes);
   *document = loaded;
   return 0;
}

void
cwiki_document_free(struct cwiki_document *document)
{
   if (document == NULL) {
      return;
   }
   free(document->path);
   cwiki_buffer_free(&document->buffer);
   memset(document, 0, sizeof(*document));
}

void
cwiki_document_mark_dirty(struct cwiki_document *document)
{
   if (document != NULL) {
      document->dirty = true;
   }
}

static struct cwiki_durable_write_result
save_result(enum cwiki_durable_write_status status,
    enum cwiki_durable_write_operation operation, int error_number)
{
   struct cwiki_durable_write_result result;

   result.status = status;
   result.operation = operation;
   result.error_number = error_number;
   return result;
}

static struct cwiki_durable_write_result
save_document(struct cwiki_document *document,
    const struct cwiki_durable_write_test_fault *fault)
{
   struct cwiki_durable_write_result result;
   char *bytes;
   size_t length;

   if (document == NULL || document->path == NULL) {
      errno = EINVAL;
      return save_result(CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE,
          CWIKI_DURABLE_WRITE_NONE, EINVAL);
   }
   if (cwiki_buffer_encode(&document->buffer, &bytes, &length) != 0) {
      return save_result(CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE,
          CWIKI_DURABLE_WRITE_NONE, errno);
   }
#ifdef CWIKI_DOCUMENT_TESTING
   result = cwiki_durable_write_test(document->path, bytes, length, fault);
#else
   (void)fault;
   result = cwiki_durable_write(document->path, bytes, length);
#endif
   free(bytes);
   if (result.status == CWIKI_DURABLE_WRITE_SUCCESS) {
      document->dirty = false;
   }
   return result;
}

struct cwiki_durable_write_result
cwiki_document_save(struct cwiki_document *document)
{
   return save_document(document, NULL);
}

#ifdef CWIKI_DOCUMENT_TESTING
struct cwiki_durable_write_result
cwiki_document_save_test(struct cwiki_document *document,
    const struct cwiki_durable_write_test_fault *fault)
{
   return save_document(document, fault);
}
#endif
