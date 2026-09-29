#include "key_record.h"

#include "input.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const unsigned char recording_magic[8] = {
   'C', 'W', 'K', 'E', 'Y', 'R', 'E', 'C'
};

#ifdef CWIKI_KEY_RECORD_TESTING
static cwiki_key_record_test_write_fn record_write = write;

void
cwiki_key_record_test_set_write(cwiki_key_record_test_write_fn write_fn)
{
   record_write = write_fn == NULL ? write : write_fn;
}
#else
#define record_write write
#endif

static void
put_u32(unsigned char *bytes, uint32_t value)
{
   size_t i;

   for (i = 0U; i < 4U; i++) {
      bytes[i] = (unsigned char)(value >> (i * 8U));
   }
}

static void
put_u64(unsigned char *bytes, uint64_t value)
{
   size_t i;

   for (i = 0U; i < 8U; i++) {
      bytes[i] = (unsigned char)(value >> (i * 8U));
   }
}

static uint32_t
get_u32(const unsigned char *bytes)
{
   size_t i;
   uint32_t value = 0U;

   for (i = 0U; i < 4U; i++) {
      value |= (uint32_t)bytes[i] << (i * 8U);
   }
   return value;
}

static uint64_t
get_u64(const unsigned char *bytes)
{
   size_t i;
   uint64_t value = 0U;

   for (i = 0U; i < 8U; i++) {
      value |= (uint64_t)bytes[i] << (i * 8U);
   }
   return value;
}

static size_t
first_span_length(const struct cwiki_key_record *record)
{
   size_t available;

   if (record->length == 0U) {
      return 0U;
   }
   available = record->capacity - record->head;
   return record->length < available ? record->length : available;
}

static void
update_header(struct cwiki_key_record *record)
{
   (void)memcpy(record->header, recording_magic, sizeof(recording_magic));
   put_u32(record->header + 8U, CWIKI_KEY_RECORD_VERSION);
   put_u32(record->header + 12U, CWIKI_KEY_RECORD_HEADER_SIZE);
   put_u64(record->header + 16U, (uint64_t)record->length);
   put_u64(record->header + 24U, record->dropped);
   put_u64(record->header + 32U, (uint64_t)first_span_length(record));
}

int
cwiki_key_record_init(struct cwiki_key_record *record, unsigned char *storage,
    size_t capacity)
{
   if (record == NULL || (storage == NULL && capacity != 0U) ||
       (storage != NULL && capacity == 0U)) {
      errno = EINVAL;
      return -1;
   }
   (void)memset(record, 0, sizeof(*record));
   record->crash_fd = -1;
   if (storage == NULL) {
      storage = malloc(CWIKI_KEY_RECORD_DEFAULT_CAPACITY);
      if (storage == NULL) {
         return -1;
      }
      capacity = CWIKI_KEY_RECORD_DEFAULT_CAPACITY;
      record->owns_storage = 1;
   }
   record->storage = storage;
   record->capacity = capacity;
   update_header(record);
   return 0;
}

void
cwiki_key_record_destroy(struct cwiki_key_record *record)
{
   if (record == NULL) {
      return;
   }
   record->crash_active = 0;
   if (record->owns_storage != 0) {
      free(record->storage);
   }
   (void)memset(record, 0, sizeof(*record));
   record->crash_fd = -1;
}

int
cwiki_key_record_append(struct cwiki_key_record *record,
    const unsigned char *bytes, size_t length)
{
   size_t available;
   size_t to_drop;
   size_t i;

   if (record == NULL || record->storage == NULL || record->capacity == 0U ||
       (length != 0U && bytes == NULL)) {
      errno = EINVAL;
      return -1;
   }
   available = record->capacity - record->length;
   to_drop = length > available ? length - available : 0U;
   if ((uint64_t)to_drop > UINT64_MAX - record->dropped) {
      errno = EOVERFLOW;
      return -1;
   }
   for (i = 0U; i < length; i++) {
      size_t tail;

      if (record->length == record->capacity) {
         record->head = (record->head + 1U) % record->capacity;
      } else {
         record->length++;
      }
      tail = (record->head + record->length - 1U) % record->capacity;
      record->storage[tail] = bytes[i];
   }
   record->dropped += (uint64_t)to_drop;
   update_header(record);
   return 0;
}

int
cwiki_key_record_activate_crash_fd(struct cwiki_key_record *record,
    int crash_fd)
{
   if (record == NULL || record->storage == NULL || crash_fd < 0) {
      errno = EINVAL;
      return -1;
   }
   record->crash_fd = crash_fd;
   record->crash_status = CWIKI_KEY_RECORD_CRASH_NOT_ATTEMPTED;
   record->crash_errno = 0;
   record->crash_write_calls = 0;
   record->crash_active = 1;
   return 0;
}

void
cwiki_key_record_deactivate_crash_fd(struct cwiki_key_record *record)
{
   if (record != NULL) {
      record->crash_active = 0;
   }
}

static int
crash_write_once(struct cwiki_key_record *record, const unsigned char *bytes,
    size_t length)
{
   ssize_t written;

   if (length == 0U) {
      return 0;
   }
   record->crash_write_calls++;
   written = record_write(record->crash_fd, bytes, length);
   if (written < 0) {
      record->crash_errno = (sig_atomic_t)errno;
      record->crash_status = CWIKI_KEY_RECORD_CRASH_ERROR;
      return -1;
   }
   if ((size_t)written != length) {
      record->crash_status = CWIKI_KEY_RECORD_CRASH_PARTIAL;
      return -1;
   }
   return 0;
}

void
cwiki_key_record_crash_flush(struct cwiki_key_record *record)
{
   int saved_errno = errno;
   size_t first;

   if (record == NULL || record->crash_active == 0) {
      if (record != NULL) {
         record->crash_status = CWIKI_KEY_RECORD_CRASH_INACTIVE;
      }
      errno = saved_errno;
      return;
   }
   record->crash_status = CWIKI_KEY_RECORD_CRASH_COMPLETE;
   record->crash_errno = 0;
   record->crash_write_calls = 0;
   first = first_span_length(record);
   if (crash_write_once(record, record->header, sizeof(record->header)) != 0 ||
       crash_write_once(record, record->storage + record->head, first) != 0 ||
       crash_write_once(record, record->storage, record->length - first) != 0) {
      errno = saved_errno;
      return;
   }
   errno = saved_errno;
}

static int
write_all(int fd, const unsigned char *bytes, size_t length)
{
   size_t offset = 0U;

   while (offset < length) {
      ssize_t written = record_write(fd, bytes + offset, length - offset);

      if (written > 0) {
         if ((size_t)written > length - offset) {
            errno = EIO;
            return -1;
         }
         offset += (size_t)written;
      } else if (written < 0 && errno == EINTR) {
         continue;
      } else {
         if (written == 0) {
            errno = EIO;
         }
         return -1;
      }
   }
   return 0;
}

int
cwiki_key_record_flush(struct cwiki_key_record *record, int fd)
{
   size_t first;

   if (record == NULL || record->storage == NULL || fd < 0) {
      errno = EINVAL;
      return -1;
   }
   first = first_span_length(record);
   if (write_all(fd, record->header, sizeof(record->header)) != 0 ||
       write_all(fd, record->storage + record->head, first) != 0 ||
       write_all(fd, record->storage, record->length - first) != 0) {
      return -1;
   }
   return 0;
}

void
cwiki_key_record_rotate(struct cwiki_key_record *record)
{
   if (record == NULL || record->storage == NULL) {
      return;
   }
   record->head = 0U;
   record->length = 0U;
   record->dropped = 0U;
   update_header(record);
}

int
cwiki_key_record_read(const unsigned char *recording, size_t recording_len,
    struct cwiki_key_recording_view *view)
{
   uint32_t version;
   uint32_t header_size;
   uint64_t retained;
   uint64_t first;

   if (recording == NULL || view == NULL ||
       recording_len < CWIKI_KEY_RECORD_HEADER_SIZE) {
      errno = EINVAL;
      return -1;
   }
   if (memcmp(recording, recording_magic, sizeof(recording_magic)) != 0) {
      errno = EINVAL;
      return -1;
   }
   version = get_u32(recording + 8U);
   if (version != CWIKI_KEY_RECORD_VERSION) {
      errno = ENOTSUP;
      return -1;
   }
   header_size = get_u32(recording + 12U);
   retained = get_u64(recording + 16U);
   first = get_u64(recording + 32U);
   if (header_size != CWIKI_KEY_RECORD_HEADER_SIZE || retained > SIZE_MAX ||
       first > retained || (retained == 0U && first != 0U) ||
       (retained != 0U && first == 0U) ||
       retained > (uint64_t)(SIZE_MAX - CWIKI_KEY_RECORD_HEADER_SIZE) ||
       recording_len != CWIKI_KEY_RECORD_HEADER_SIZE + (size_t)retained) {
      errno = EINVAL;
      return -1;
   }
   view->bytes = recording + CWIKI_KEY_RECORD_HEADER_SIZE;
   view->length = (size_t)retained;
   view->dropped = get_u64(recording + 24U);
   view->first_span_length = (size_t)first;
   return 0;
}

struct sidecar_context {
   int fd;
   uint64_t timestamp_ns;
   int failed;
};

static int
sidecar_text(struct sidecar_context *context, const char *text, size_t length)
{
   if (context->failed != 0) {
      return -1;
   }
   if (write_all(context->fd, (const unsigned char *)text, length) != 0) {
      context->failed = 1;
      return -1;
   }
   return 0;
}

static int
sidecar_escaped(struct sidecar_context *context, const unsigned char *bytes,
    size_t length)
{
   size_t i;

   if (sidecar_text(context, "\"", 1U) != 0) {
      return -1;
   }
   for (i = 0U; i < length; i++) {
      char escaped[4];
      size_t escaped_len;

      switch (bytes[i]) {
      case (unsigned char)'\\':
         (void)memcpy(escaped, "\\\\", 2U);
         escaped_len = 2U;
         break;
      case (unsigned char)'\"':
         (void)memcpy(escaped, "\\\"", 2U);
         escaped_len = 2U;
         break;
      case (unsigned char)'\n':
         (void)memcpy(escaped, "\\n", 2U);
         escaped_len = 2U;
         break;
      case (unsigned char)'\r':
         (void)memcpy(escaped, "\\r", 2U);
         escaped_len = 2U;
         break;
      case (unsigned char)'\t':
         (void)memcpy(escaped, "\\t", 2U);
         escaped_len = 2U;
         break;
      case 0U:
         (void)memcpy(escaped, "\\0", 2U);
         escaped_len = 2U;
         break;
      default:
         if (bytes[i] >= 0x20U && bytes[i] <= 0x7eU) {
            escaped[0] = (char)bytes[i];
            escaped_len = 1U;
         } else {
            static const char hexadecimal[] = "0123456789abcdef";

            escaped[0] = '\\';
            escaped[1] = 'x';
            escaped[2] = hexadecimal[bytes[i] >> 4U];
            escaped[3] = hexadecimal[bytes[i] & 0x0fU];
            escaped_len = 4U;
         }
         break;
      }
      if (sidecar_text(context, escaped, escaped_len) != 0) {
         return -1;
      }
   }
   return sidecar_text(context, "\"", 1U);
}

static const char *
action_name(enum cwiki_input_key_action action)
{
   switch (action) {
   case CWIKI_INPUT_PRESS:
      return "press";
   case CWIKI_INPUT_REPEAT:
      return "repeat";
   case CWIKI_INPUT_RELEASE:
      return "release";
   }
   return "unknown";
}

static int
sidecar_optional_key(struct sidecar_context *context, bool present,
    uint32_t value, const char *following)
{
   char buffer[80];
   int length;

   if (!present) {
      if (sidecar_text(context, "-", 1U) != 0) {
         return -1;
      }
   } else {
      length = snprintf(buffer, sizeof(buffer), "%" PRIu32, value);
      if (length < 0 || (size_t)length >= sizeof(buffer) ||
          sidecar_text(context, buffer, (size_t)length) != 0) {
         context->failed = 1;
         return -1;
      }
   }
   return sidecar_text(context, following, strlen(following));
}

static void
sidecar_event(const struct cwiki_input_event *event, void *opaque)
{
   struct sidecar_context *context = opaque;
   char buffer[256];
   int length;

   if (context->failed != 0) {
      return;
   }
   if (event->kind == CWIKI_INPUT_PASTE) {
      size_t prefix = event->text_len < CWIKI_KEY_RECORD_PASTE_PREFIX ?
          event->text_len : CWIKI_KEY_RECORD_PASTE_PREFIX;
      const char *suffix = prefix < event->text_len ?
          " truncated=true\n" : " truncated=false\n";

      length = snprintf(buffer, sizeof(buffer),
          "paste timestamp_ns=%" PRIu64 " bytes=%zu prefix=",
          context->timestamp_ns, event->text_len);
      if (length < 0 || (size_t)length >= sizeof(buffer) ||
          sidecar_text(context, buffer, (size_t)length) != 0 ||
          sidecar_escaped(context, event->text, prefix) != 0 ||
          sidecar_text(context, suffix, strlen(suffix)) != 0) {
         context->failed = 1;
      }
      return;
   }
   length = snprintf(buffer, sizeof(buffer),
       "key timestamp_ns=%" PRIu64 " code=%" PRIu32
       " modifiers=%u action=%s shifted=",
       context->timestamp_ns, event->key, event->modifiers,
       action_name(event->action));
   if (length < 0 || (size_t)length >= sizeof(buffer) ||
       sidecar_text(context, buffer, (size_t)length) != 0 ||
       sidecar_optional_key(context, event->has_shifted_key,
       event->shifted_key, " base_layout=") != 0 ||
       sidecar_optional_key(context, event->has_base_layout_key,
       event->base_layout_key, " text=") != 0 ||
       sidecar_escaped(context, event->text, event->text_len) != 0 ||
       sidecar_text(context, "\n", 1U) != 0) {
      context->failed = 1;
   }
}

int
cwiki_key_record_write_sidecar(int fd, const unsigned char *bytes,
    const uint64_t *timestamps_ns, size_t length, uint64_t dropped)
{
   struct cwiki_input_parser parser;
   struct sidecar_context context;
   size_t rejected = 0U;
   size_t i;
   char buffer[160];
   int formatted;

   if (fd < 0 || (length != 0U && (bytes == NULL || timestamps_ns == NULL))) {
      errno = EINVAL;
      return -1;
   }
   context.fd = fd;
   context.timestamp_ns = 0U;
   context.failed = 0;
   formatted = snprintf(buffer, sizeof(buffer),
       "dropped bytes=%" PRIu64 " retained=%zu\n", dropped, length);
   if (formatted < 0 || (size_t)formatted >= sizeof(buffer) ||
       sidecar_text(&context, buffer, (size_t)formatted) != 0) {
      return -1;
   }
   cwiki_input_parser_init(&parser);
   for (i = 0U; i < length && context.failed == 0; i++) {
      size_t now_rejected;

      context.timestamp_ns = timestamps_ns[i];
      if (cwiki_input_parser_feed(&parser, bytes + i, 1U, sidecar_event,
          &context) != 0) {
         context.failed = 1;
         break;
      }
      now_rejected = cwiki_input_parser_rejected(&parser);
      if (now_rejected != rejected) {
         formatted = snprintf(buffer, sizeof(buffer),
             "rejected timestamp_ns=%" PRIu64 " count=%zu ending_byte=",
             context.timestamp_ns, now_rejected - rejected);
         if (formatted < 0 || (size_t)formatted >= sizeof(buffer) ||
             sidecar_text(&context, buffer, (size_t)formatted) != 0 ||
             sidecar_escaped(&context, bytes + i, 1U) != 0 ||
             sidecar_text(&context, "\n", 1U) != 0) {
            context.failed = 1;
         }
         rejected = now_rejected;
      }
   }
   cwiki_input_parser_destroy(&parser);
   if (context.failed != 0) {
      if (errno == 0) {
         errno = EIO;
      }
      return -1;
   }
   return 0;
}
