#include "unicode.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define RESPONSE_CAPACITY 128U
#define RESPONSE_TIMEOUT_MS 2000L

struct corpus_entry {
   const char *name;
   const char *bytes;
};

static const struct corpus_entry corpus[] = {
   {"combining acute", "e\xcc\x81"},
   {"East Asian wide", "\xe7\x95\x8c"},
   {"emoji text presentation", "\xe2\x9d\xa4"},
   {"emoji VS16 presentation", "\xe2\x9d\xa4\xef\xb8\x8f"},
   {"emoji ZWJ sequence", "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x94\xac"},
   {"regional-indicator flag", "\xf0\x9f\x87\xba\xf0\x9f\x87\xb3"},
   {"soft hyphen", "\xc2\xad"},
   {"word joiner", "\xe2\x81\xa0"},
   {"zero-width space", "\xe2\x80\x8b"}
};

static const char enter_screen[] = "\033[?1049h\033[2J\033[H";
static const char leave_screen[] = "\033[2J\033[H\033[?1049l";
static struct termios saved_termios;
static bool termios_saved;
static bool screen_entered;

static bool
write_all(int fd, const char *bytes, size_t length)
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

static void
cleanup(void)
{
   if (screen_entered) {
      (void)write_all(STDOUT_FILENO, leave_screen, sizeof(leave_screen) - 1U);
      screen_entered = false;
   }
   if (termios_saved) {
      (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
      termios_saved = false;
   }
}

static long
milliseconds_until(const struct timespec *deadline)
{
   struct timespec now;
   long seconds;
   long nanoseconds;

   if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      return 0L;
   }
   seconds = (long)(deadline->tv_sec - now.tv_sec);
   nanoseconds = deadline->tv_nsec - now.tv_nsec;
   if (nanoseconds < 0L) {
      seconds--;
      nanoseconds += 1000000000L;
   }
   if (seconds < 0L) {
      return 0L;
   }
   if (seconds > (LONG_MAX - nanoseconds / 1000000L) / 1000L) {
      return LONG_MAX;
   }
   return seconds * 1000L + nanoseconds / 1000000L;
}

static bool
parse_cursor_report(const unsigned char *bytes, size_t length,
    unsigned long *row, unsigned long *column)
{
   size_t i;

   for (i = 0U; i + 5U <= length; i++) {
      size_t cursor;
      unsigned long parsed_row = 0UL;
      unsigned long parsed_column = 0UL;

      if (bytes[i] != 0x1bU || bytes[i + 1U] != (unsigned char)'[') {
         continue;
      }
      cursor = i + 2U;
      if (cursor >= length || bytes[cursor] < (unsigned char)'0' ||
          bytes[cursor] > (unsigned char)'9') {
         continue;
      }
      while (cursor < length && bytes[cursor] >= (unsigned char)'0' &&
          bytes[cursor] <= (unsigned char)'9') {
         parsed_row = parsed_row * 10UL +
             (unsigned long)(bytes[cursor] - (unsigned char)'0');
         cursor++;
      }
      if (cursor >= length || bytes[cursor++] != (unsigned char)';') {
         continue;
      }
      if (cursor >= length || bytes[cursor] < (unsigned char)'0' ||
          bytes[cursor] > (unsigned char)'9') {
         continue;
      }
      while (cursor < length && bytes[cursor] >= (unsigned char)'0' &&
          bytes[cursor] <= (unsigned char)'9') {
         parsed_column = parsed_column * 10UL +
             (unsigned long)(bytes[cursor] - (unsigned char)'0');
         cursor++;
      }
      if (cursor < length && bytes[cursor] == (unsigned char)'R') {
         *row = parsed_row;
         *column = parsed_column;
         return true;
      }
   }
   return false;
}

static bool
read_cursor_report(unsigned long *row, unsigned long *column)
{
   unsigned char response[RESPONSE_CAPACITY];
   size_t used = 0U;
   struct timespec deadline;

   if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
      return false;
   }
   deadline.tv_sec += RESPONSE_TIMEOUT_MS / 1000L;
   deadline.tv_nsec += (RESPONSE_TIMEOUT_MS % 1000L) * 1000000L;
   if (deadline.tv_nsec >= 1000000000L) {
      deadline.tv_sec++;
      deadline.tv_nsec -= 1000000000L;
   }
   while (used < sizeof(response)) {
      struct pollfd descriptor = {STDIN_FILENO, POLLIN, 0};
      long remaining = milliseconds_until(&deadline);
      int ready;
      ssize_t count;

      if (remaining <= 0L) {
         return false;
      }
      ready = poll(&descriptor, 1U,
          remaining > (long)INT_MAX ? INT_MAX : (int)remaining);
      if (ready < 0 && errno == EINTR) {
         continue;
      }
      if (ready <= 0 || (descriptor.revents & POLLIN) == 0) {
         return false;
      }
      count = read(STDIN_FILENO, response + used, sizeof(response) - used);
      if (count <= 0) {
         return false;
      }
      used += (size_t)count;
      if (parse_cursor_report(response, used, row, column)) {
         return true;
      }
   }
   return false;
}

int
main(void)
{
   const char *kitty_window_id = getenv("KITTY_WINDOW_ID");
   struct termios raw;
   unsigned long row;
   unsigned long column;
   unsigned long predicted_column = 1UL;
   size_t i;
   int status = 1;

   if (kitty_window_id == NULL || kitty_window_id[0] == '\0') {
      (void)fprintf(stderr,
          "FAIL: kitty width conformance test must run inside Kitty\n");
      return 1;
   }
   if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
      (void)fprintf(stderr, "FAIL: standard input and output must be a tty\n");
      return 1;
   }
   if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
      (void)perror("FAIL: tcgetattr");
      return 1;
   }
   termios_saved = true;
   raw = saved_termios;
   raw.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
   raw.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
       IGNCR | ICRNL | IXON);
   raw.c_oflag &= (tcflag_t)~OPOST;
   raw.c_cflag &= (tcflag_t)~(CSIZE | PARENB);
   raw.c_cflag |= CS8;
   raw.c_cc[VMIN] = 0;
   raw.c_cc[VTIME] = 0;
   if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
      (void)perror("FAIL: tcsetattr");
      cleanup();
      return 1;
   }
   if (atexit(cleanup) != 0) {
      (void)fprintf(stderr, "FAIL: could not register terminal cleanup\n");
      cleanup();
      return 1;
   }
   screen_entered = true;
   if (!write_all(STDOUT_FILENO, enter_screen, sizeof(enter_screen) - 1U)) {
      (void)fprintf(stderr, "FAIL: could not initialize Kitty display\n");
      cleanup();
      return 1;
   }

   for (i = 0U; i < sizeof(corpus) / sizeof(corpus[0]); i++) {
      size_t length = strlen(corpus[i].bytes);
      int width = cwiki_grapheme_width(corpus[i].bytes, length);

      if (width < 0) {
         (void)fprintf(stderr, "FAIL: cwiki rejected corpus entry: %s\n",
             corpus[i].name);
         goto done;
      }
      predicted_column += (unsigned long)width;
      if (!write_all(STDOUT_FILENO, corpus[i].bytes, length)) {
         (void)fprintf(stderr, "FAIL: could not write corpus entry: %s\n",
             corpus[i].name);
         goto done;
      }
      if (!write_all(STDOUT_FILENO, "\033[6n", sizeof("\033[6n") - 1U)) {
         (void)fprintf(stderr, "FAIL: could not request cursor position\n");
         goto done;
      }
      if (!read_cursor_report(&row, &column)) {
         (void)fprintf(stderr,
             "FAIL: Kitty did not return a cursor-position report within "
             "%ld ms after %s\n", RESPONSE_TIMEOUT_MS, corpus[i].name);
         goto done;
      }
      if (row != 1UL || column != predicted_column) {
         (void)fprintf(stderr,
             "FAIL: width mismatch after %s: Kitty cursor is at %lu;%lu; "
             "cwiki predicted 1;%lu\n", corpus[i].name, row, column,
             predicted_column);
         goto done;
      }
   }
   status = 0;

done:
   cleanup();
   if (status == 0) {
      (void)printf("kitty width conformance: ok (%lu columns)\n",
          predicted_column - 1UL);
   }
   return status;
}
