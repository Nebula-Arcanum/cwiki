#include "terminal.h"

#include "capabilities.h"
#include "input.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TERMINAL_TIMEOUT_MS 2000L

static const int handled_signals[] = {
   SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGHUP, SIGINT
};

static int signal_output_fd = -1;
static int signal_input_fd = -1;
static struct termios signal_termios;
static volatile sig_atomic_t signal_session_active;
static volatile sig_atomic_t signal_modes_owned;
static struct sigaction old_actions[
   sizeof(handled_signals) / sizeof(handled_signals[0])];
static size_t installed_actions;
static int atexit_installed;

#ifdef CWIKI_TERMINAL_TESTING
static cwiki_terminal_test_write_fn terminal_write = write;

void
cwiki_terminal_test_set_write(cwiki_terminal_test_write_fn write_fn)
{
   terminal_write = write_fn == NULL ? write : write_fn;
}
#else
#define terminal_write write
#endif

static int
write_all(int fd, const char *bytes, size_t length)
{
   size_t written = 0U;

   while (written < length) {
      ssize_t result = terminal_write(fd, bytes + written, length - written);

      if (result > 0) {
         written += (size_t)result;
      } else if (result < 0 && errno == EINTR) {
         continue;
      } else {
         if (result == 0) {
            errno = EIO;
         }
         return -1;
      }
   }
   return 0;
}

static void
terminal_signal_handler(int signo)
{
   static const char restoration[] = CWIKI_TERMINAL_RESTORE;

   if (signal_session_active != 0) {
      if (signal_modes_owned != 0) {
         (void)write(signal_output_fd, restoration,
             sizeof(restoration) - 1U);
      }
      (void)tcsetattr(signal_input_fd, TCSANOW, &signal_termios);
      /* R1.5.9: flush the fixed, pre-opened key-recording ring here. */
   }
   (void)signal(signo, SIG_DFL);
   (void)raise(signo);
}

static void
restore_handlers(void)
{
   while (installed_actions > 0U) {
      size_t index = installed_actions - 1U;

      (void)sigaction(handled_signals[index], &old_actions[index], NULL);
      installed_actions = index;
   }
}

static void
atexit_cleanup(void)
{
   static const char restoration[] = CWIKI_TERMINAL_RESTORE;

   if (signal_session_active != 0) {
      if (signal_modes_owned != 0) {
         (void)write_all(signal_output_fd, restoration,
             sizeof(restoration) - 1U);
      }
      (void)tcsetattr(signal_input_fd, TCSANOW, &signal_termios);
      signal_modes_owned = 0;
      signal_session_active = 0;
      restore_handlers();
   }
}

static int
install_handlers(void)
{
   struct sigaction action;
   size_t i;

   (void)memset(&action, 0, sizeof(action));
   action.sa_handler = terminal_signal_handler;
   (void)sigemptyset(&action.sa_mask);
   for (i = 0U; i < sizeof(handled_signals) / sizeof(handled_signals[0]); i++) {
      (void)sigaddset(&action.sa_mask, handled_signals[i]);
   }
   for (i = 0U; i < sizeof(handled_signals) / sizeof(handled_signals[0]); i++) {
      if (sigaction(handled_signals[i], &action, &old_actions[i]) != 0) {
         restore_handlers();
         return -1;
      }
      installed_actions++;
   }
   if (atexit_installed == 0) {
      if (atexit(atexit_cleanup) != 0) {
         restore_handlers();
         errno = ENOMEM;
         return -1;
      }
      atexit_installed = 1;
   }
   return 0;
}

static int
set_raw_mode(struct cwiki_terminal *terminal)
{
   struct termios raw;

   if (tcgetattr(terminal->input_fd, &terminal->saved_termios) != 0) {
      return -1;
   }
   terminal->termios_saved = 1;
   raw = terminal->saved_termios;
   raw.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
       IGNCR | ICRNL | IXON);
   raw.c_oflag &= (tcflag_t)~OPOST;
   raw.c_cflag &= (tcflag_t)~(CSIZE | PARENB);
   raw.c_cflag |= CS8;
   raw.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
   raw.c_cc[VMIN] = 1;
   raw.c_cc[VTIME] = 0;
   return tcsetattr(terminal->input_fd, TCSAFLUSH, &raw);
}

static void
deactivate(struct cwiki_terminal *terminal)
{
   signal_modes_owned = 0;
   signal_session_active = 0;
   restore_handlers();
   terminal->modes_owned = 0;
   terminal->active = 0;
}

static int
set_handled_signal_mask(int how, sigset_t *previous)
{
   sigset_t signals;
   size_t i;

   (void)sigemptyset(&signals);
   for (i = 0U; i < sizeof(handled_signals) / sizeof(handled_signals[0]); i++) {
      (void)sigaddset(&signals, handled_signals[i]);
   }
   return sigprocmask(how, &signals, previous);
}

static int64_t
milliseconds_since(const struct timespec *start, const struct timespec *now)
{
   int64_t seconds = (int64_t)now->tv_sec - (int64_t)start->tv_sec;
   int64_t nanoseconds = (int64_t)now->tv_nsec - (int64_t)start->tv_nsec;

   return seconds * INT64_C(1000) + nanoseconds / INT64_C(1000000);
}

static struct cwiki_terminal_result
result_with(enum cwiki_terminal_status status, int system_errno)
{
   struct cwiki_terminal_result result;

   (void)memset(&result, 0, sizeof(result));
   result.status = status;
   result.system_errno = system_errno;
   return result;
}

static struct cwiki_terminal_result
start_with_timeout(struct cwiki_terminal *terminal, int input_fd, int output_fd,
    long timeout_ms)
{
   static const char queries[] = CWIKI_CAPABILITIES_KEYBOARD_QUERY
       CWIKI_CAPABILITIES_GRAPHICS_QUERY CWIKI_CAPABILITIES_DA1_QUERY;
   static const char startup[] = CWIKI_TERMINAL_ALT_ENTER
       CWIKI_TERMINAL_CURSOR_HIDE CWIKI_INPUT_KEYBOARD_PUSH
       CWIKI_INPUT_PASTE_ENABLE;
   struct cwiki_capabilities_parser parser;
   struct cwiki_capabilities_result capabilities;
   struct cwiki_terminal_result result;
   struct timespec started;

   result = result_with(CWIKI_TERMINAL_SYSTEM_ERROR, 0);
   if (terminal == NULL || signal_session_active != 0 || timeout_ms < 0L) {
      result.system_errno = EINVAL;
      return result;
   }
   (void)memset(terminal, 0, sizeof(*terminal));
   terminal->input_fd = input_fd;
   terminal->output_fd = output_fd;
   if (set_raw_mode(terminal) != 0) {
      result.system_errno = errno;
      return result;
   }
   signal_input_fd = input_fd;
   signal_output_fd = output_fd;
   signal_termios = terminal->saved_termios;
   signal_session_active = 1;
   if (install_handlers() != 0) {
      result.system_errno = errno;
      signal_session_active = 0;
      (void)tcsetattr(input_fd, TCSANOW, &terminal->saved_termios);
      return result;
   }
   terminal->active = 1;
   if (write_all(output_fd, queries, sizeof(queries) - 1U) != 0 ||
       clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
      result.system_errno = errno;
      cwiki_terminal_cleanup(terminal);
      return result;
   }

   cwiki_capabilities_parser_init(&parser);
   for (;;) {
      struct timespec now;
      int64_t elapsed;
      long remaining;
      struct pollfd descriptor;
      int ready;

      if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
         result.system_errno = errno;
         cwiki_terminal_cleanup(terminal);
         return result;
      }
      elapsed = milliseconds_since(&started, &now);
      remaining = timeout_ms - (long)elapsed;
      if (remaining <= 0L) {
         result.status = CWIKI_TERMINAL_TIMEOUT;
         cwiki_terminal_cleanup(terminal);
         return result;
      }
      descriptor.fd = input_fd;
      descriptor.events = POLLIN;
      descriptor.revents = 0;
      do {
         ready = poll(&descriptor, 1U, (int)remaining);
      } while (ready < 0 && errno == EINTR);
      if (ready == 0) {
         result.status = CWIKI_TERMINAL_TIMEOUT;
         cwiki_terminal_cleanup(terminal);
         return result;
      }
      if (ready < 0) {
         result.system_errno = errno;
         cwiki_terminal_cleanup(terminal);
         return result;
      }
      if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
         result.system_errno = EIO;
         cwiki_terminal_cleanup(terminal);
         return result;
      }
      if ((descriptor.revents & POLLIN) != 0) {
         unsigned char bytes[256];
         ssize_t count;
         size_t consumed;

         do {
            count = read(input_fd, bytes, sizeof(bytes));
         } while (count < 0 && errno == EINTR);
         if (count <= 0) {
            result.system_errno = count == 0 ? EIO : errno;
            cwiki_terminal_cleanup(terminal);
            return result;
         }
         consumed = cwiki_capabilities_parser_feed(&parser, bytes,
             (size_t)count);
         capabilities = cwiki_capabilities_parser_result(&parser);
         if (capabilities.complete != 0) {
            result.pending_input_len = (size_t)count - consumed;
            (void)memcpy(result.pending_input, bytes + consumed,
                result.pending_input_len);
            break;
         }
      }
   }

   if (capabilities.keyboard != CWIKI_CAPABILITY_SUPPORTED &&
       capabilities.graphics != CWIKI_CAPABILITY_SUPPORTED) {
      result.status = CWIKI_TERMINAL_MISSING_KEYBOARD_AND_GRAPHICS;
   } else if (capabilities.keyboard != CWIKI_CAPABILITY_SUPPORTED) {
      result.status = CWIKI_TERMINAL_MISSING_KEYBOARD;
   } else if (capabilities.graphics != CWIKI_CAPABILITY_SUPPORTED) {
      result.status = CWIKI_TERMINAL_MISSING_GRAPHICS;
   } else {
      sigset_t previous_mask;

      if (set_handled_signal_mask(SIG_BLOCK, &previous_mask) != 0) {
         result.system_errno = errno;
         cwiki_terminal_cleanup(terminal);
         return result;
      }
      if (write_all(output_fd, startup, sizeof(startup) - 1U) != 0) {
         result.system_errno = errno;
         (void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);
         cwiki_terminal_cleanup(terminal);
         return result;
      }
      terminal->modes_owned = 1;
      signal_modes_owned = 1;
      if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) != 0) {
         result.system_errno = errno;
         cwiki_terminal_cleanup(terminal);
         return result;
      }
      result.status = CWIKI_TERMINAL_SUCCESS;
      return result;
   }
   cwiki_terminal_cleanup(terminal);
   return result;
}

struct cwiki_terminal_result
cwiki_terminal_start(struct cwiki_terminal *terminal, int input_fd,
    int output_fd)
{
   return start_with_timeout(terminal, input_fd, output_fd,
       TERMINAL_TIMEOUT_MS);
}

#ifdef CWIKI_TERMINAL_TESTING
struct cwiki_terminal_result
cwiki_terminal_start_timeout(struct cwiki_terminal *terminal, int input_fd,
    int output_fd, long timeout_ms)
{
   return start_with_timeout(terminal, input_fd, output_fd, timeout_ms);
}
#endif

void
cwiki_terminal_cleanup(struct cwiki_terminal *terminal)
{
   static const char restoration[] = CWIKI_TERMINAL_RESTORE;

   if (terminal == NULL || terminal->active == 0) {
      return;
   }
   if (terminal->modes_owned != 0) {
      (void)write_all(terminal->output_fd, restoration,
          sizeof(restoration) - 1U);
   }
   if (terminal->termios_saved != 0) {
      (void)tcsetattr(terminal->input_fd, TCSANOW, &terminal->saved_termios);
   }
   deactivate(terminal);
}

int
cwiki_terminal_begin_update(const struct cwiki_terminal *terminal)
{
   if (terminal == NULL || terminal->active == 0) {
      errno = EINVAL;
      return -1;
   }
   return write_all(terminal->output_fd, CWIKI_TERMINAL_SYNC_BEGIN,
       sizeof(CWIKI_TERMINAL_SYNC_BEGIN) - 1U);
}

int
cwiki_terminal_end_update(const struct cwiki_terminal *terminal)
{
   if (terminal == NULL || terminal->active == 0) {
      errno = EINVAL;
      return -1;
   }
   return write_all(terminal->output_fd, CWIKI_TERMINAL_SYNC_END,
       sizeof(CWIKI_TERMINAL_SYNC_END) - 1U);
}

int
cwiki_terminal_reset(int output_fd)
{
   return write_all(output_fd, CWIKI_TERMINAL_RESTORE,
       sizeof(CWIKI_TERMINAL_RESTORE) - 1U);
}

const char *
cwiki_terminal_status_message(enum cwiki_terminal_status status)
{
   switch (status) {
   case CWIKI_TERMINAL_SUCCESS:
      return "terminal ready";
   case CWIKI_TERMINAL_MISSING_KEYBOARD:
      return "terminal lacks the kitty keyboard protocol required by cwiki";
   case CWIKI_TERMINAL_MISSING_GRAPHICS:
      return "terminal lacks the kitty graphics protocol required by cwiki";
   case CWIKI_TERMINAL_MISSING_KEYBOARD_AND_GRAPHICS:
      return "terminal lacks the kitty keyboard and graphics protocols required by cwiki";
   case CWIKI_TERMINAL_TIMEOUT:
      return "terminal did not answer cwiki's capability queries within 2 seconds";
   case CWIKI_TERMINAL_SYSTEM_ERROR:
      return "terminal setup failed because of a system error";
   }
   return "unknown terminal setup result";
}
