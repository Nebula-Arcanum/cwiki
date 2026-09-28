#include "terminal.h"

#include "capabilities.h"
#include "input.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static const char queries[] = CWIKI_CAPABILITIES_KEYBOARD_QUERY
    CWIKI_CAPABILITIES_GRAPHICS_QUERY CWIKI_CAPABILITIES_DA1_QUERY;
static const char supported_reply[] =
    "\x1b[?29u\x1b_Gi=1129797963;OK\x1b\\\x1b[?1;2c";
static const char startup[] = CWIKI_INPUT_KEYBOARD_PUSH
    CWIKI_TERMINAL_ALT_ENTER CWIKI_TERMINAL_CURSOR_HIDE
    CWIKI_INPUT_PASTE_ENABLE;

static int failures;

struct pty_pair {
   int master;
   int slave;
};

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static struct pty_pair
open_pty(void)
{
   struct pty_pair pair = {-1, -1};
   char *name;

   pair.master = posix_openpt(O_RDWR | O_NOCTTY);
   if (pair.master < 0 || grantpt(pair.master) != 0 ||
       unlockpt(pair.master) != 0) {
      (void)perror("open pseudo-terminal");
      exit(2);
   }
   name = ptsname(pair.master);
   if (name == NULL) {
      (void)perror("ptsname");
      exit(2);
   }
   pair.slave = open(name, O_RDWR | O_NOCTTY);
   if (pair.slave < 0) {
      (void)perror("open pseudo-terminal slave");
      exit(2);
   }
   return pair;
}

static bool
read_exact(int fd, char *bytes, size_t length)
{
   size_t offset = 0U;

   while (offset < length) {
      ssize_t count = read(fd, bytes + offset, length - offset);

      if (count > 0) {
         offset += (size_t)count;
      } else if (count < 0 && errno == EINTR) {
         continue;
      } else {
         return false;
      }
   }
   return true;
}

static bool
write_exact(int fd, const char *bytes, size_t length)
{
   size_t offset = 0U;

   while (offset < length) {
      ssize_t count = write(fd, bytes + offset, length - offset);

      if (count > 0) {
         offset += (size_t)count;
      } else if (count < 0 && errno == EINTR) {
         continue;
      } else {
         return false;
      }
   }
   return true;
}

static bool
no_pending_output(int fd)
{
   int flags = fcntl(fd, F_GETFL);
   unsigned char byte;
   ssize_t count;
   int read_errno;

   if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
      return false;
   }
   count = read(fd, &byte, 1U);
   read_errno = errno;
   if (fcntl(fd, F_SETFL, flags) != 0) {
      return false;
   }
   return count < 0 && (read_errno == EAGAIN || read_errno == EWOULDBLOCK);
}

static pid_t
spawn_reply(int master, int slave, const char *reply, size_t reply_len)
{
   pid_t child = fork();

   if (child < 0) {
      (void)perror("fork");
      exit(2);
   }
   if (child == 0) {
      char received[sizeof(queries) - 1U];
      bool ok;

      (void)close(slave);
      ok = read_exact(master, received, sizeof(received)) &&
          memcmp(received, queries, sizeof(received)) == 0 &&
          write_exact(master, reply, reply_len);
      _exit(ok ? 0 : 1);
   }
   return child;
}

static void
wait_ok(pid_t child, const char *message)
{
   int status;

   check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
       WEXITSTATUS(status) == 0, message);
}

static bool
same_termios(const struct termios *left, const struct termios *right)
{
   tcflag_t ignored_lflag = 0U;

#ifdef PENDIN
   ignored_lflag |= PENDIN;
#endif
#ifdef FLUSHO
   ignored_lflag |= FLUSHO;
#endif
#if defined(__APPLE__) && !defined(PENDIN)
   /* Darwin hides kernel-maintained PENDIN under strict POSIX feature flags. */
   ignored_lflag |= (tcflag_t)0x20000000U;
#endif
   return left->c_iflag == right->c_iflag &&
       left->c_oflag == right->c_oflag &&
       left->c_cflag == right->c_cflag &&
       (left->c_lflag & (tcflag_t)~ignored_lflag) ==
       (right->c_lflag & (tcflag_t)~ignored_lflag) &&
       memcmp(left->c_cc, right->c_cc, sizeof(left->c_cc)) == 0 &&
       cfgetispeed(left) == cfgetispeed(right) &&
       cfgetospeed(left) == cfgetospeed(right);
}

static void
test_success_raw_handoff_and_cleanup(void)
{
   static const char typed[] = "\x1b[97;1u";
   char reply[sizeof(supported_reply) + sizeof(typed)];
   char output[sizeof(startup) - 1U + sizeof(CWIKI_TERMINAL_RESTORE) - 1U];
   struct pty_pair pair = open_pty();
   struct termios before;
   struct termios raw;
   struct termios after;
   struct cwiki_terminal terminal;
   struct cwiki_terminal_result result;
   pid_t responder;

   (void)memcpy(reply, supported_reply, sizeof(supported_reply) - 1U);
   (void)memcpy(reply + sizeof(supported_reply) - 1U, typed,
       sizeof(typed) - 1U);
   check(tcgetattr(pair.slave, &before) == 0, "capture original tty flags");
   responder = spawn_reply(pair.master, pair.slave, reply,
       sizeof(supported_reply) - 1U + sizeof(typed) - 1U);
   result = cwiki_terminal_start_timeout(&terminal, pair.slave, pair.slave,
       250L);
   wait_ok(responder, "capability responder observes exact query ordering");
   check(result.status == CWIKI_TERMINAL_SUCCESS,
       "supported terminal starts successfully");
   check(result.pending_input_len == sizeof(typed) - 1U &&
       memcmp(result.pending_input, typed, sizeof(typed) - 1U) == 0,
       "bytes after DA1 are handed to the caller");
   check(tcgetattr(pair.slave, &raw) == 0 &&
       (raw.c_lflag & (ECHO | ECHONL | ICANON | IEXTEN | ISIG)) == 0U &&
       (raw.c_iflag & (IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
       ICRNL | IXON)) == 0U && (raw.c_oflag & OPOST) == 0U &&
       (raw.c_cflag & (CSIZE | PARENB)) == CS8 &&
       raw.c_cc[VMIN] == 1U && raw.c_cc[VTIME] == 0U,
       "startup applies raw mode with tty-generated signals disabled");

   cwiki_terminal_cleanup(&terminal);
   cwiki_terminal_cleanup(&terminal);
   check(read_exact(pair.master, output, sizeof(output)) &&
       memcmp(output, startup, sizeof(startup) - 1U) == 0 &&
       memcmp(output + sizeof(startup) - 1U, CWIKI_TERMINAL_RESTORE,
       sizeof(CWIKI_TERMINAL_RESTORE) - 1U) == 0,
       "startup and cleanup emit exact bytes in order only once");
   check(tcgetattr(pair.slave, &after) == 0 &&
       same_termios(&after, &before),
       "normal cleanup restores the saved termios");
   (void)close(pair.slave);
   (void)close(pair.master);
}

static enum cwiki_terminal_status
run_capability_case(const char *reply, size_t reply_len)
{
   struct pty_pair pair = open_pty();
   struct termios before;
   struct termios after;
   struct cwiki_terminal terminal;
   struct cwiki_terminal_result result;
   pid_t responder;

   check(tcgetattr(pair.slave, &before) == 0,
       "capture missing-capability termios");
   responder = spawn_reply(pair.master, pair.slave, reply, reply_len);
   result = cwiki_terminal_start_timeout(&terminal, pair.slave, pair.slave,
       250L);
   wait_ok(responder, "missing-capability responder completes");
   check(no_pending_output(pair.master),
       "missing capability emits no unowned restoration bytes");
   check(tcgetattr(pair.slave, &after) == 0 &&
       same_termios(&after, &before),
       "missing capability restores termios");
   (void)close(pair.slave);
   (void)close(pair.master);
   return result.status;
}

static void
test_missing_timeout_and_errors(void)
{
   static const char keyboard_only[] = "\x1b[?29u\x1b[?1;2c";
   static const char graphics_only[] =
       "\x1b_Gi=1129797963;OK\x1b\\\x1b[?1;2c";
   static const char barrier_only[] = "\x1b[?1;2c";
   struct pty_pair pair;
   struct cwiki_terminal terminal;
   struct cwiki_terminal_result result;
   char observed_queries[sizeof(queries) - 1U];

   check(run_capability_case(keyboard_only, sizeof(keyboard_only) - 1U) ==
       CWIKI_TERMINAL_MISSING_GRAPHICS,
       "DA1 distinctly reports missing graphics support");
   check(run_capability_case(graphics_only, sizeof(graphics_only) - 1U) ==
       CWIKI_TERMINAL_MISSING_KEYBOARD,
       "DA1 distinctly reports missing keyboard support");
   check(run_capability_case(barrier_only, sizeof(barrier_only) - 1U) ==
       CWIKI_TERMINAL_MISSING_KEYBOARD_AND_GRAPHICS,
       "DA1 reports both missing capabilities");

   pair = open_pty();
   result = cwiki_terminal_start_timeout(&terminal, pair.slave, pair.slave,
       10L);
   check(result.status == CWIKI_TERMINAL_TIMEOUT,
       "silent terminal hits the short test timeout");
   check(read_exact(pair.master, observed_queries, sizeof(observed_queries)) &&
       memcmp(observed_queries, queries, sizeof(observed_queries)) == 0 &&
       no_pending_output(pair.master),
       "timeout output contains capability queries only");
   (void)close(pair.slave);
   (void)close(pair.master);

   result = cwiki_terminal_start_timeout(&terminal, -1, -1, 10L);
   check(result.status == CWIKI_TERMINAL_SYSTEM_ERROR &&
       result.system_errno != 0, "invalid tty is a structured system error");
   check(strstr(cwiki_terminal_status_message(
       CWIKI_TERMINAL_MISSING_KEYBOARD), "keyboard") != NULL &&
       strstr(cwiki_terminal_status_message(
       CWIKI_TERMINAL_MISSING_GRAPHICS), "graphics") != NULL &&
       strstr(cwiki_terminal_status_message(CWIKI_TERMINAL_TIMEOUT),
       "2 seconds") != NULL,
       "statuses provide clear requirement and timeout text");
}

static unsigned char fake_output[256];
static size_t fake_output_len;
static int fake_calls;
static int startup_failure;
static int startup_failure_phase;

static ssize_t
short_write(int fd, const void *bytes, size_t length)
{
   size_t count;

   (void)fd;
   fake_calls++;
   if (fake_calls == 1) {
      errno = EINTR;
      return -1;
   }
   count = length > 2U ? 2U : length;
   (void)memcpy(fake_output + fake_output_len, bytes, count);
   fake_output_len += count;
   return (ssize_t)count;
}

static ssize_t
startup_fault_write(int fd, const void *bytes, size_t length)
{
   if (startup_failure_phase == 0 &&
       length == sizeof(CWIKI_INPUT_KEYBOARD_PUSH) - 1U &&
       memcmp(bytes, CWIKI_INPUT_KEYBOARD_PUSH, length) == 0) {
      if (startup_failure == 1) {
         if (!write_exact(fd, bytes, 2U)) {
            return -1;
         }
         startup_failure_phase = 1;
         return 2;
      }
      if (!write_exact(fd, bytes, length)) {
         return -1;
      }
      startup_failure_phase = 1;
      return (ssize_t)length;
   }
   if (startup_failure_phase == 1) {
      startup_failure_phase = 2;
      errno = EIO;
      return -1;
   }
   return write(fd, bytes, length);
}

static void
run_startup_failure(int failure, const char *expected, size_t expected_len,
    const char *output_message)
{
   struct pty_pair pair = open_pty();
   struct termios before;
   struct termios after;
   struct cwiki_terminal terminal;
   struct cwiki_terminal_result result;
   unsigned char output[128];
   pid_t responder;

   check(expected_len <= sizeof(output), "startup failure fixture fits");
   check(tcgetattr(pair.slave, &before) == 0,
       "capture startup-failure termios");
   responder = spawn_reply(pair.master, pair.slave, supported_reply,
       sizeof(supported_reply) - 1U);
   startup_failure = failure;
   startup_failure_phase = 0;
   cwiki_terminal_test_set_write(startup_fault_write);
   result = cwiki_terminal_start_timeout(&terminal, pair.slave, pair.slave,
       250L);
   cwiki_terminal_test_set_write(NULL);
   wait_ok(responder, "startup-failure responder completes");
   check(result.status == CWIKI_TERMINAL_SYSTEM_ERROR &&
       result.system_errno == EIO, "startup write failure is a system error");
   check(read_exact(pair.master, (char *)output, expected_len) &&
       memcmp(output, expected, expected_len) == 0 &&
       no_pending_output(pair.master), output_message);
   check(tcgetattr(pair.slave, &after) == 0 &&
       same_termios(&after, &before),
       "startup write failure restores termios");
   (void)close(pair.slave);
   (void)close(pair.master);
}

static void
test_transactional_startup_failures(void)
{
   static const char after_push[] = CWIKI_INPUT_KEYBOARD_PUSH
       CWIKI_TERMINAL_RESTORE;

   run_startup_failure(1, CWIKI_INPUT_KEYBOARD_PUSH, 2U,
       "partial keyboard push does not emit an unowned pop");
   run_startup_failure(2, after_push, sizeof(after_push) - 1U,
       "failure after keyboard push emits full restoration");
}

static void
test_complete_normal_writes_and_reset(void)
{
   struct cwiki_terminal terminal;
   char expected[sizeof(CWIKI_TERMINAL_SYNC_BEGIN) - 1U +
       sizeof(CWIKI_TERMINAL_SYNC_END) - 1U +
       sizeof(CWIKI_TERMINAL_RESTORE) - 1U];
   size_t length = 0U;

   (void)memset(&terminal, 0, sizeof(terminal));
   terminal.active = 1;
   terminal.output_fd = 17;
   fake_output_len = 0U;
   fake_calls = 0;
   cwiki_terminal_test_set_write(short_write);
   check(cwiki_terminal_begin_update(&terminal) == 0 &&
       cwiki_terminal_end_update(&terminal) == 0 &&
       cwiki_terminal_reset(17) == 0,
       "normal writes tolerate EINTR and short writes");
   cwiki_terminal_test_set_write(NULL);
   (void)memcpy(expected + length, CWIKI_TERMINAL_SYNC_BEGIN,
       sizeof(CWIKI_TERMINAL_SYNC_BEGIN) - 1U);
   length += sizeof(CWIKI_TERMINAL_SYNC_BEGIN) - 1U;
   (void)memcpy(expected + length, CWIKI_TERMINAL_SYNC_END,
       sizeof(CWIKI_TERMINAL_SYNC_END) - 1U);
   length += sizeof(CWIKI_TERMINAL_SYNC_END) - 1U;
   (void)memcpy(expected + length, CWIKI_TERMINAL_RESTORE,
       sizeof(CWIKI_TERMINAL_RESTORE) - 1U);
   length += sizeof(CWIKI_TERMINAL_RESTORE) - 1U;
   check(fake_output_len == length &&
       memcmp(fake_output, expected, length) == 0,
       "sync and reset helpers emit complete fixed sequences");
}

static void
test_handled_signal_restores_and_reraises(void)
{
   struct pty_pair pair = open_pty();
   struct termios before;
   struct termios after;
   int ready_pipe[2];
   pid_t child;
   char observed_queries[sizeof(queries) - 1U];
   char observed_startup[sizeof(startup) - 1U];
   char observed_restore[sizeof(CWIKI_TERMINAL_RESTORE) - 1U];
   char ready;
   int status;

   check(tcgetattr(pair.slave, &before) == 0, "capture signal-test termios");
   if (pipe(ready_pipe) != 0) {
      (void)perror("pipe");
      exit(2);
   }
   child = fork();
   if (child < 0) {
      (void)perror("fork");
      exit(2);
   }
   if (child == 0) {
      struct cwiki_terminal terminal;
      struct cwiki_terminal_result result;

      (void)close(pair.master);
      (void)close(ready_pipe[0]);
      result = cwiki_terminal_start_timeout(&terminal, pair.slave, pair.slave,
          500L);
      if (result.status != CWIKI_TERMINAL_SUCCESS ||
          write(ready_pipe[1], "x", 1U) != 1) {
         _exit(3);
      }
      for (;;) {
         (void)pause();
      }
   }
   (void)close(ready_pipe[1]);
   check(read_exact(pair.master, observed_queries, sizeof(observed_queries)) &&
       memcmp(observed_queries, queries, sizeof(observed_queries)) == 0,
       "signal-test child sends capability queries");
   check(write_exact(pair.master, supported_reply,
       sizeof(supported_reply) - 1U), "signal-test terminal sends replies");
   check(read_exact(pair.master, observed_startup, sizeof(observed_startup)) &&
       memcmp(observed_startup, startup, sizeof(observed_startup)) == 0 &&
       read_exact(ready_pipe[0], &ready, 1U),
       "signal-test child enters terminal session");
   check(kill(child, SIGTERM) == 0, "send handled signal to child");
   check(read_exact(pair.master, observed_restore, sizeof(observed_restore)) &&
       memcmp(observed_restore, CWIKI_TERMINAL_RESTORE,
       sizeof(observed_restore)) == 0,
       "signal handler emits one exact restoration string");
   check(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
       WTERMSIG(status) == SIGTERM,
       "signal handler restores default disposition and re-raises");
   check(tcgetattr(pair.slave, &after) == 0 &&
       same_termios(&after, &before),
       "signal handler restores saved termios");
   (void)close(ready_pipe[0]);
   (void)close(pair.slave);
   (void)close(pair.master);
}

static void
test_signal_during_probe_does_not_pop_modes(void)
{
   struct pty_pair pair = open_pty();
   struct termios before;
   struct termios after;
   pid_t child;
   char observed_queries[sizeof(queries) - 1U];
   int status;

   check(tcgetattr(pair.slave, &before) == 0,
       "capture probe-signal termios");
   child = fork();
   if (child < 0) {
      (void)perror("fork");
      exit(2);
   }
   if (child == 0) {
      struct cwiki_terminal terminal;

      (void)close(pair.master);
      (void)cwiki_terminal_start_timeout(&terminal, pair.slave, pair.slave,
          5000L);
      _exit(3);
   }
   check(read_exact(pair.master, observed_queries, sizeof(observed_queries)) &&
       memcmp(observed_queries, queries, sizeof(observed_queries)) == 0,
       "probe-signal child sends capability queries");
   check(kill(child, SIGTERM) == 0, "signal child during capability probe");
   check(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
       WTERMSIG(status) == SIGTERM,
       "probe signal restores default disposition and re-raises");
   check(no_pending_output(pair.master),
       "probe signal emits no unowned restoration bytes");
   check(tcgetattr(pair.slave, &after) == 0 &&
       same_termios(&after, &before),
       "probe signal restores saved termios");
   (void)close(pair.slave);
   (void)close(pair.master);
}

int
main(void)
{
   test_success_raw_handoff_and_cleanup();
   test_missing_timeout_and_errors();
   test_complete_normal_writes_and_reset();
   test_transactional_startup_failures();
   test_handled_signal_restores_and_reraises();
   test_signal_during_probe_does_not_pop_modes();
   if (failures != 0) {
      return 1;
   }
   (void)puts("terminal lifecycle: ok");
   return 0;
}
