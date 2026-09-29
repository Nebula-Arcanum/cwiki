#ifndef CWIKI_KEY_RECORD_H
#define CWIKI_KEY_RECORD_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#define CWIKI_KEY_RECORD_DEFAULT_CAPACITY (64U * 1024U)
#define CWIKI_KEY_RECORD_VERSION 1U
#define CWIKI_KEY_RECORD_HEADER_SIZE 40U
#define CWIKI_KEY_RECORD_PASTE_PREFIX 32U

enum cwiki_key_record_crash_status {
   CWIKI_KEY_RECORD_CRASH_NOT_ATTEMPTED,
   CWIKI_KEY_RECORD_CRASH_COMPLETE,
   CWIKI_KEY_RECORD_CRASH_PARTIAL,
   CWIKI_KEY_RECORD_CRASH_ERROR,
   CWIKI_KEY_RECORD_CRASH_INACTIVE
};

struct cwiki_key_record {
   unsigned char *storage;
   size_t capacity;
   size_t head;
   size_t length;
   uint64_t dropped;
   int owns_storage;
   int crash_fd;
   volatile sig_atomic_t crash_active;
   volatile sig_atomic_t crash_status;
   volatile sig_atomic_t crash_errno;
   volatile sig_atomic_t crash_write_calls;
   unsigned char header[CWIKI_KEY_RECORD_HEADER_SIZE];
};

struct cwiki_key_recording_view {
   const unsigned char *bytes;
   size_t length;
   uint64_t dropped;
   size_t first_span_length;
};

/*
 * Passing NULL storage and zero capacity allocates the fixed default capacity.
 * Otherwise storage must name capacity caller-owned bytes. The capacity never
 * changes. Calls which mutate or activate a record must be serialized, and the
 * integrating terminal code must block its crash signals around those calls.
 */
int cwiki_key_record_init(struct cwiki_key_record *record,
    unsigned char *storage, size_t capacity);
void cwiki_key_record_destroy(struct cwiki_key_record *record);
int cwiki_key_record_append(struct cwiki_key_record *record,
    const unsigned char *bytes, size_t length);

/* The caller opens, configures, and eventually closes crash_fd in normal code. */
int cwiki_key_record_activate_crash_fd(struct cwiki_key_record *record,
    int crash_fd);
void cwiki_key_record_deactivate_crash_fd(struct cwiki_key_record *record);

/*
 * Async-signal-safe under the serialization rule above. This makes at most
 * three write(2) calls and never retries a short or interrupted write.
 */
void cwiki_key_record_crash_flush(struct cwiki_key_record *record);

/*
 * Normal-code flush. The descriptor remains open on success and failure and
 * must already be positioned/truncated by the caller. The binary format is:
 * 8-byte "CWKEYREC" magic; little-endian u32 version and header size; then
 * little-endian u64 retained length, dropped count, and first ring-span length;
 * then the retained raw bytes in chronological order.
 */
int cwiki_key_record_flush(struct cwiki_key_record *record, int fd);
void cwiki_key_record_rotate(struct cwiki_key_record *record);

/* Validate a complete in-memory recording and expose its chronological bytes. */
int cwiki_key_record_read(const unsigned char *recording, size_t recording_len,
    struct cwiki_key_recording_view *view);

/*
 * Decode chronological bytes through cwiki_input_parser. timestamps_ns contains
 * one caller-supplied monotonic timestamp per raw byte; an event receives the
 * timestamp of the byte which completes it. The sidecar fd remains open.
 */
int cwiki_key_record_write_sidecar(int fd, const unsigned char *bytes,
    const uint64_t *timestamps_ns, size_t length, uint64_t dropped);

#ifdef CWIKI_KEY_RECORD_TESTING
#include <sys/types.h>

typedef ssize_t (*cwiki_key_record_test_write_fn)(int, const void *, size_t);
void cwiki_key_record_test_set_write(cwiki_key_record_test_write_fn write_fn);
#endif

#endif
