#include "input.h"
#include "key_record.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OUTPUT_CAPACITY 8192U
#define EVENT_CAPACITY 8U
#define EVENT_TEXT_CAPACITY 128U

enum writer_mode {
   WRITER_COMPLETE,
   WRITER_EINTR_THEN_SHORT,
   WRITER_FAIL,
   WRITER_SHORT_ONCE,
   WRITER_EINTR_ALWAYS
};

struct captured_event {
   struct cwiki_input_event event;
   unsigned char text[EVENT_TEXT_CAPACITY];
};

struct event_capture {
   struct captured_event events[EVENT_CAPACITY];
   size_t count;
};

static unsigned char output[OUTPUT_CAPACITY];
static size_t output_length;
static size_t write_calls;
static size_t write_lengths[16];
static enum writer_mode writer_mode;
static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static void
reset_writer(enum writer_mode mode)
{
   (void)memset(output, 0, sizeof(output));
   (void)memset(write_lengths, 0, sizeof(write_lengths));
   output_length = 0U;
   write_calls = 0U;
   writer_mode = mode;
}

static ssize_t
injected_write(int fd, const void *bytes, size_t length)
{
   size_t written = length;

   (void)fd;
   if (write_calls < sizeof(write_lengths) / sizeof(write_lengths[0])) {
      write_lengths[write_calls] = length;
   }
   write_calls++;
   if (writer_mode == WRITER_EINTR_ALWAYS ||
       (writer_mode == WRITER_EINTR_THEN_SHORT && write_calls == 1U)) {
      errno = EINTR;
      return -1;
   }
   if (writer_mode == WRITER_FAIL && write_calls == 2U) {
      errno = ENOSPC;
      return -1;
   }
   if (writer_mode == WRITER_SHORT_ONCE) {
      written = length == 0U ? 0U : length - 1U;
   } else if (writer_mode == WRITER_EINTR_THEN_SHORT && length > 3U) {
      written = 3U;
   }
   if (written > sizeof(output) - output_length) {
      errno = ENOSPC;
      return -1;
   }
   if (written != 0U) {
      (void)memcpy(output + output_length, bytes, written);
      output_length += written;
   }
   return (ssize_t)written;
}

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

static void
capture_event(const struct cwiki_input_event *event, void *opaque)
{
   struct event_capture *capture = opaque;
   struct captured_event *saved;

   if (capture->count == EVENT_CAPACITY ||
       event->text_len > EVENT_TEXT_CAPACITY) {
      failures++;
      return;
   }
   saved = &capture->events[capture->count++];
   saved->event = *event;
   if (event->text_len != 0U) {
      (void)memcpy(saved->text, event->text, event->text_len);
      saved->event.text = saved->text;
   }
}

static void
parse_events(const unsigned char *bytes, size_t length, bool fragmented,
    struct event_capture *capture)
{
   struct cwiki_input_parser parser;
   size_t i;

   cwiki_input_parser_init(&parser);
   if (fragmented) {
      for (i = 0U; i < length; i++) {
         check(cwiki_input_parser_feed(&parser, bytes + i, 1U, capture_event,
             capture) == 0, "feed replay one byte at a time");
      }
   } else {
      check(cwiki_input_parser_feed(&parser, bytes, length, capture_event,
          capture) == 0, "feed replay as one chunk");
   }
   check(cwiki_input_parser_rejected(&parser) == 0U,
       "replay fixture has no rejected sequences");
   cwiki_input_parser_destroy(&parser);
}

static void
check_ring_case(const unsigned char *bytes, size_t length, size_t capacity,
    const unsigned char *expected, size_t expected_length,
    uint64_t expected_dropped, const char *message)
{
   unsigned char storage[16];
   struct cwiki_key_record record;
   struct cwiki_key_recording_view view;

   check(capacity <= sizeof(storage), "ring test capacity fits storage");
   check(cwiki_key_record_init(&record, storage, capacity) == 0, message);
   check(cwiki_key_record_append(&record, bytes, length) == 0, message);
   reset_writer(WRITER_COMPLETE);
   check(cwiki_key_record_flush(&record, 10) == 0, message);
   check(cwiki_key_record_read(output, output_length, &view) == 0, message);
   check(view.length == expected_length && view.dropped == expected_dropped &&
       memcmp(view.bytes, expected, expected_length) == 0, message);
   cwiki_key_record_destroy(&record);
}

static void
test_ring_ordering(void)
{
   static const unsigned char no_wrap[] = {0x41U, 0x00U, 0xffU};
   static const unsigned char exact_wrap[] = {1U, 2U, 3U, 4U, 5U};
   static const unsigned char asymmetric[] = {
      0x00U, 0xffU, 0x10U, 0x20U, 0x30U, 0x40U, 0x50U
   };
   static const unsigned char asymmetric_expected[] = {
      0x10U, 0x20U, 0x30U, 0x40U, 0x50U
   };
   static const unsigned char multi[] = {
      0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U
   };
   static const unsigned char multi_expected[] = {9U, 10U, 11U, 12U};
   struct cwiki_key_record default_record;

   check(cwiki_key_record_init(&default_record, NULL, 0U) == 0 &&
       default_record.capacity == CWIKI_KEY_RECORD_DEFAULT_CAPACITY,
       "default initialization allocates the fixed default ring");
   cwiki_key_record_destroy(&default_record);
   check_ring_case(no_wrap, sizeof(no_wrap), 7U, no_wrap, sizeof(no_wrap), 0U,
       "no-wrap ring preserves asymmetric NUL data");
   check_ring_case(exact_wrap, sizeof(exact_wrap), 5U, exact_wrap,
       sizeof(exact_wrap), 0U, "exactly full ring preserves ordering");
   check_ring_case(asymmetric, sizeof(asymmetric), 5U, asymmetric_expected,
       sizeof(asymmetric_expected), 2U,
       "wrapped ring retains newest bytes and exact dropped count");
   check_ring_case(multi, sizeof(multi), 4U, multi_expected,
       sizeof(multi_expected), 9U,
       "multi-wrap ring retains chronological newest bytes");
}

static void
test_normal_flush_and_rotate(void)
{
   static const unsigned char bytes[] = {8U, 0U, 7U, 6U, 5U, 4U};
   static const unsigned char expected[] = {0U, 7U, 6U, 5U, 4U};
   unsigned char storage[5];
   struct cwiki_key_record record;
   struct cwiki_key_recording_view view;

   check(cwiki_key_record_init(&record, storage, sizeof(storage)) == 0,
       "initialize normal flush ring");
   check(cwiki_key_record_append(&record, bytes, sizeof(bytes)) == 0,
       "append normal flush bytes");
   reset_writer(WRITER_EINTR_THEN_SHORT);
   check(cwiki_key_record_flush(&record, 11) == 0,
       "normal flush retries EINTR and short writes");
   check(write_calls > 3U, "normal flush needed deterministic retries");
   check(cwiki_key_record_read(output, output_length, &view) == 0 &&
       view.length == sizeof(expected) && view.dropped == 1U &&
       memcmp(view.bytes, expected, sizeof(expected)) == 0,
       "normal retry output remains a valid chronological recording");

   reset_writer(WRITER_FAIL);
   errno = 0;
   check(cwiki_key_record_flush(&record, 11) == -1 && errno == ENOSPC,
       "normal flush reports injected write error");

   cwiki_key_record_rotate(&record);
   reset_writer(WRITER_COMPLETE);
   check(cwiki_key_record_flush(&record, 11) == 0 &&
       cwiki_key_record_read(output, output_length, &view) == 0 &&
       view.length == 0U && view.dropped == 0U,
       "rotate starts a fresh empty recording");
   cwiki_key_record_destroy(&record);
}

static void
test_crash_flush(void)
{
   static const unsigned char bytes[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U};
   static const unsigned char expected[] = {2U, 3U, 4U, 5U, 6U};
   unsigned char storage[5];
   struct cwiki_key_record record;
   struct cwiki_key_recording_view view;

   check(cwiki_key_record_init(&record, storage, sizeof(storage)) == 0,
       "initialize crash flush ring");
   check(cwiki_key_record_append(&record, bytes, sizeof(bytes)) == 0 &&
       cwiki_key_record_activate_crash_fd(&record, 12) == 0,
       "prepare crash destination before activation");
   reset_writer(WRITER_COMPLETE);
   errno = EDOM;
   cwiki_key_record_crash_flush(&record);
   check(errno == EDOM && record.crash_status == CWIKI_KEY_RECORD_CRASH_COMPLETE,
       "crash flush preserves errno and reports complete status");
   check(record.crash_write_calls == 3 && write_calls == 3U &&
       write_lengths[0] == CWIKI_KEY_RECORD_HEADER_SIZE &&
       write_lengths[1] == 3U && write_lengths[2] == 2U,
       "wrapped crash flush is bounded to header and two spans");
   check(cwiki_key_record_read(output, output_length, &view) == 0 &&
       view.length == sizeof(expected) && view.dropped == 2U &&
       view.first_span_length == 3U &&
       memcmp(view.bytes, expected, sizeof(expected)) == 0,
       "crash output records wrapped span ordering");

   reset_writer(WRITER_SHORT_ONCE);
   cwiki_key_record_crash_flush(&record);
   check(write_calls == 1U && record.crash_write_calls == 1 &&
       record.crash_status == CWIKI_KEY_RECORD_CRASH_PARTIAL,
       "crash flush never retries a partial header write");

   reset_writer(WRITER_EINTR_ALWAYS);
   cwiki_key_record_crash_flush(&record);
   check(write_calls == 1U && record.crash_write_calls == 1 &&
       record.crash_status == CWIKI_KEY_RECORD_CRASH_ERROR &&
       record.crash_errno == EINTR,
       "crash flush never retries EINTR and retains signal-safe error status");
   cwiki_key_record_deactivate_crash_fd(&record);
   reset_writer(WRITER_COMPLETE);
   cwiki_key_record_crash_flush(&record);
   check(write_calls == 0U &&
       record.crash_status == CWIKI_KEY_RECORD_CRASH_INACTIVE,
       "inactive crash destination performs no write");
   cwiki_key_record_destroy(&record);
}

static void
test_reader_validation(void)
{
   static const unsigned char bytes[] = {1U, 2U, 3U};
   unsigned char storage[5];
   unsigned char valid[CWIKI_KEY_RECORD_HEADER_SIZE + sizeof(bytes) + 1U];
   unsigned char damaged[sizeof(valid)];
   size_t valid_length;
   struct cwiki_key_record record;
   struct cwiki_key_recording_view view;

   check(cwiki_key_record_init(&record, storage, sizeof(storage)) == 0 &&
       cwiki_key_record_append(&record, bytes, sizeof(bytes)) == 0,
       "prepare validator recording");
   reset_writer(WRITER_COMPLETE);
   check(cwiki_key_record_flush(&record, 13) == 0,
       "flush validator recording");
   valid_length = output_length;
   (void)memcpy(valid, output, valid_length);
   check(cwiki_key_record_read(valid, valid_length, &view) == 0,
       "reader accepts valid versioned recording");
   check(cwiki_key_record_read(valid, CWIKI_KEY_RECORD_HEADER_SIZE - 1U,
       &view) == -1, "reader rejects truncated header");
   check(cwiki_key_record_read(valid, valid_length - 1U, &view) == -1,
       "reader rejects truncated payload");
   valid[valid_length] = 0U;
   check(cwiki_key_record_read(valid, valid_length + 1U, &view) == -1,
       "reader rejects trailing bytes");

   (void)memcpy(damaged, output, valid_length);
   damaged[0] ^= 1U;
   check(cwiki_key_record_read(damaged, valid_length, &view) == -1,
       "reader rejects corrupt magic");
   (void)memcpy(damaged, output, valid_length);
   put_u32(damaged + 8U, 2U);
   errno = 0;
   check(cwiki_key_record_read(damaged, valid_length, &view) == -1 &&
       errno == ENOTSUP, "reader rejects unsupported version distinctly");
   (void)memcpy(damaged, output, valid_length);
   put_u32(damaged + 12U, CWIKI_KEY_RECORD_HEADER_SIZE + 1U);
   check(cwiki_key_record_read(damaged, valid_length, &view) == -1,
       "reader rejects inconsistent header length");
   (void)memcpy(damaged, output, valid_length);
   put_u64(damaged + 16U, UINT64_MAX);
   check(cwiki_key_record_read(damaged, valid_length, &view) == -1,
       "reader rejects overflowing retained length");
   (void)memcpy(damaged, output, valid_length);
   put_u64(damaged + 32U, 4U);
   check(cwiki_key_record_read(damaged, valid_length, &view) == -1,
       "reader rejects span longer than retained bytes");
   (void)memcpy(damaged, output, valid_length);
   put_u64(damaged + 32U, 0U);
   check(cwiki_key_record_read(damaged, valid_length, &view) == -1,
       "reader rejects empty first span with retained bytes");
   cwiki_key_record_destroy(&record);
}

static size_t
read_descriptor(int fd, unsigned char *bytes, size_t capacity)
{
   size_t length = 0U;

   check(lseek(fd, 0, SEEK_SET) == 0, "rewind sidecar descriptor");
   while (length < capacity) {
      ssize_t count = read(fd, bytes + length, capacity - length);

      if (count > 0) {
         length += (size_t)count;
      } else if (count < 0 && errno == EINTR) {
         continue;
      } else {
         check(count == 0, "read sidecar descriptor");
         break;
      }
   }
   return length;
}

static void
test_sidecar(void)
{
   static const unsigned char key[] = "\033[97::65;6:2;65:34u";
   static const unsigned char paste_begin[] = "\033[200~";
   static const unsigned char paste_end[] = "\033[201~";
   unsigned char raw[128];
   uint64_t timestamps[sizeof(raw) / sizeof(raw[0])];
   unsigned char sidecar[OUTPUT_CAPACITY];
   char timestamp_text[192];
   size_t offset = 0U;
   size_t key_end;
   size_t paste_end_offset;
   size_t i;
   int descriptor;
   FILE *file;

   raw[offset++] = 0U;
   (void)memcpy(raw + offset, key, sizeof(key) - 1U);
   offset += sizeof(key) - 1U;
   key_end = offset - 1U;
   (void)memcpy(raw + offset, paste_begin, sizeof(paste_begin) - 1U);
   offset += sizeof(paste_begin) - 1U;
   raw[offset++] = (unsigned char)'a';
   raw[offset++] = (unsigned char)'\n';
   raw[offset++] = 0U;
   raw[offset++] = (unsigned char)'\\';
   raw[offset++] = (unsigned char)'\"';
   for (i = 5U; i < 40U; i++) {
      raw[offset++] = (unsigned char)('a' + (i % 26U));
   }
   (void)memcpy(raw + offset, paste_end, sizeof(paste_end) - 1U);
   offset += sizeof(paste_end) - 1U;
   paste_end_offset = offset - 1U;
   for (i = 0U; i < offset; i++) {
      timestamps[i] = UINT64_C(1000000) + (uint64_t)i * UINT64_C(17);
   }

   file = tmpfile();
   check(file != NULL, "create temporary sidecar file");
   if (file == NULL) {
      return;
   }
   descriptor = fileno(file);
   cwiki_key_record_test_set_write(NULL);
   check(cwiki_key_record_write_sidecar(descriptor, raw, timestamps, offset,
       23U) == 0, "generate sidecar through real input parser");
   (void)fflush(file);
   i = read_descriptor(descriptor, sidecar, sizeof(sidecar) - 1U);
   sidecar[i] = 0U;
   check(strstr((const char *)sidecar, "dropped bytes=23 retained=") != NULL,
       "sidecar makes dropped-ring count explicit");
   check(strstr((const char *)sidecar,
       "rejected timestamp_ns=1000000 count=1 ending_byte=\"\\0\"") != NULL,
       "sidecar explicitly escapes parser-rejected NUL byte");
   (void)snprintf(timestamp_text, sizeof(timestamp_text),
       "key timestamp_ns=%" PRIu64
       " code=97 modifiers=5 action=repeat shifted=- "
       "base_layout=65 text=\"A\\\"\"",
       timestamps[key_end]);
   check(strstr((const char *)sidecar, timestamp_text) != NULL,
       "fragmented CSI-u event uses completion-byte timestamp and all fields");
   (void)snprintf(timestamp_text, sizeof(timestamp_text),
       "paste timestamp_ns=%" PRIu64
       " bytes=40 prefix=\"a\\n\\0\\\\\\\"",
       timestamps[paste_end_offset]);
   check(strstr((const char *)sidecar, timestamp_text) != NULL &&
       strstr((const char *)sidecar, "truncated=true") != NULL,
       "paste sidecar uses completion timestamp and bounded escaped prefix");
   check(i < 700U, "paste payload is summarized rather than expanded");
   check(fclose(file) == 0, "close temporary sidecar file");
   cwiki_key_record_test_set_write(injected_write);
}

static void
test_replay_equivalence(void)
{
   static const unsigned char raw[] =
       "\033[97;1;97u\033[200~x\000y\033[201~\033[98;3:3;98u";
   unsigned char storage[sizeof(raw)];
   struct cwiki_key_record record;
   struct cwiki_key_recording_view view;
   struct event_capture direct = {0};
   struct event_capture replayed = {0};
   size_t i;

   if (cwiki_key_record_init(&record, storage, sizeof(storage)) != 0) {
      check(false, "initialize replay-equivalence recording");
      return;
   }
   if (cwiki_key_record_append(&record, raw, sizeof(raw) - 1U) != 0) {
      check(false, "record replay-equivalence raw bytes");
      cwiki_key_record_destroy(&record);
      return;
   }
   reset_writer(WRITER_COMPLETE);
   if (cwiki_key_record_flush(&record, 14) != 0 ||
       cwiki_key_record_read(output, output_length, &view) != 0) {
      check(false, "read replay-equivalence recording");
      cwiki_key_record_destroy(&record);
      return;
   }
   parse_events(raw, sizeof(raw) - 1U, false, &direct);
   parse_events(view.bytes, view.length, true, &replayed);
   check(view.length == sizeof(raw) - 1U &&
       memcmp(view.bytes, raw, sizeof(raw) - 1U) == 0,
       "reader returns exact chronological raw stream for replay");
   check(direct.count == replayed.count && direct.count == 3U,
       "direct and recorded replay emit the same event count");
   for (i = 0U; i < direct.count && i < replayed.count; i++) {
      check(direct.events[i].event.kind == replayed.events[i].event.kind &&
          direct.events[i].event.key == replayed.events[i].event.key &&
          direct.events[i].event.modifiers ==
          replayed.events[i].event.modifiers &&
          direct.events[i].event.action == replayed.events[i].event.action &&
          direct.events[i].event.text_len == replayed.events[i].event.text_len &&
          memcmp(direct.events[i].event.text, replayed.events[i].event.text,
          direct.events[i].event.text_len) == 0,
          "recorded replay events equal direct real-parser events");
   }
   cwiki_key_record_destroy(&record);
}

int
main(void)
{
   cwiki_key_record_test_set_write(injected_write);
   test_ring_ordering();
   test_normal_flush_and_rotate();
   test_crash_flush();
   test_reader_validation();
   test_sidecar();
   test_replay_equivalence();
   cwiki_key_record_test_set_write(NULL);
   if (failures != 0) {
      return EXIT_FAILURE;
   }
   (void)puts("key recording: ok");
   return EXIT_SUCCESS;
}
