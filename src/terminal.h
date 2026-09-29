#ifndef CWIKI_TERMINAL_H
#define CWIKI_TERMINAL_H

#include <stddef.h>
#include <termios.h>

struct cwiki_key_record;

#define CWIKI_TERMINAL_SYNC_BEGIN "\x1b[?2026h"
#define CWIKI_TERMINAL_SYNC_END "\x1b[?2026l"
#define CWIKI_TERMINAL_ALT_ENTER "\x1b[?1049h"
#define CWIKI_TERMINAL_ALT_LEAVE "\x1b[?1049l"
#define CWIKI_TERMINAL_CURSOR_HIDE "\x1b[?25l"
#define CWIKI_TERMINAL_CURSOR_SHOW "\x1b[?25h"

/*
 * Once startup modes are owned, the signal handler emits this in one write and
 * cannot recover a partial write. During capability probing it restores only
 * termios. cwiki_terminal_cleanup and cwiki_terminal_reset retry partial writes
 * and EINTR. Keep the ordering synchronized-update, keyboard, paste, alternate
 * screen, cursor.
 */
#define CWIKI_TERMINAL_RESTORE \
   CWIKI_TERMINAL_SYNC_END "\x1b[<u\x1b[?2004l" \
   CWIKI_TERMINAL_ALT_LEAVE CWIKI_TERMINAL_CURSOR_SHOW

enum cwiki_terminal_status {
   CWIKI_TERMINAL_SUCCESS,
   CWIKI_TERMINAL_MISSING_KEYBOARD,
   CWIKI_TERMINAL_MISSING_GRAPHICS,
   CWIKI_TERMINAL_MISSING_KEYBOARD_AND_GRAPHICS,
   CWIKI_TERMINAL_TIMEOUT,
   CWIKI_TERMINAL_SYSTEM_ERROR
};

struct cwiki_terminal_result {
   enum cwiki_terminal_status status;
   int system_errno;
   unsigned char pending_input[256];
   size_t pending_input_len;
};

struct cwiki_terminal {
   int input_fd;
   int output_fd;
   struct termios saved_termios;
   int termios_saved;
   int active;
   int modes_owned;
   struct cwiki_key_record *key_record;
};

struct cwiki_terminal_result cwiki_terminal_start(
    struct cwiki_terminal *terminal, int input_fd, int output_fd);
struct cwiki_terminal_result cwiki_terminal_start_recording(
    struct cwiki_terminal *terminal, int input_fd, int output_fd,
    struct cwiki_key_record *record);
int cwiki_terminal_record_input(struct cwiki_terminal *terminal,
    const unsigned char *bytes, size_t length);
int cwiki_terminal_key_record_activate(struct cwiki_key_record *record,
    int crash_fd);
int cwiki_terminal_key_record_deactivate(struct cwiki_key_record *record);
int cwiki_terminal_key_record_rotate(struct cwiki_key_record *record);
void cwiki_terminal_cleanup(struct cwiki_terminal *terminal);
int cwiki_terminal_begin_update(const struct cwiki_terminal *terminal);
int cwiki_terminal_end_update(const struct cwiki_terminal *terminal);
int cwiki_terminal_reset(int output_fd);
const char *cwiki_terminal_status_message(enum cwiki_terminal_status status);

/*
 * R1.5.9 integrates its async-signal-safe key-ring flush in terminal.c at the
 * marked point after tcsetattr and before restoring the default disposition.
 */

#ifdef CWIKI_TERMINAL_TESTING
#include <sys/types.h>

typedef ssize_t (*cwiki_terminal_test_write_fn)(int, const void *, size_t);
struct cwiki_terminal_result cwiki_terminal_start_timeout(
    struct cwiki_terminal *terminal, int input_fd, int output_fd,
    long timeout_ms);
struct cwiki_terminal_result cwiki_terminal_start_recording_timeout(
    struct cwiki_terminal *terminal, int input_fd, int output_fd,
    struct cwiki_key_record *record, long timeout_ms);
void cwiki_terminal_test_set_write(cwiki_terminal_test_write_fn write_fn);
#endif

#endif
