#include "app.h"
#include "capabilities.h"
#include "document.h"
#include "key_record.h"
#include "terminal.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define ESCAPE "\x1b[27u"
#define ENTER "\x1b[13u"

static const char supported[] =
    "\x1b[?29u\x1b_Gi=1129797963;OK\x1b\\\x1b[?1;2c";
static const char queries[] = CWIKI_CAPABILITIES_KEYBOARD_QUERY
    CWIKI_CAPABILITIES_GRAPHICS_QUERY CWIKI_CAPABILITIES_DA1_QUERY;

struct session {
   int master;
   int slave;
   pid_t child;
   FILE *errors;
   FILE *record;
   struct termios original;
};

/* Readable fixture notation -> kitty physical keys with associated text.
 * Already encoded CSI/APC sequences are copied verbatim. */
static const char *
wire(const char *text)
{
   static char bytes[65536];
   size_t used = 0U;

   while (*text != '\0') {
      if ((unsigned char)*text == 27U) {
         bytes[used++] = *text++;
         if (*text == '[') {
            bytes[used++] = *text++;
            while (*text != '\0') {
               unsigned char value = (unsigned char)*text;
               bytes[used++] = *text++;
               if (value >= 0x40U && value <= 0x7eU) {
                  break;
               }
            }
         } else if (*text == '_') {
            while (*text != '\0') {
               char value = *text++;
               bytes[used++] = value;
               if (value == '\\') {
                  break;
               }
            }
         }
      } else {
         unsigned int value = (unsigned char)*text++;
         unsigned int key = value;
         unsigned int modifiers = 1U;
         int count;

         if (value >= 'A' && value <= 'Z') {
            key = value + ('a' - 'A');
            modifiers = 2U;
         } else if (value == ':') {
            key = ';';
            modifiers = 2U;
         }
         count = snprintf(bytes + used, sizeof(bytes) - used,
             "\x1b[%u;%u;%uu", key, modifiers, value);
         assert(count > 0 && (size_t)count < sizeof(bytes) - used);
         used += (size_t)count;
      }
      assert(used + 1U < sizeof(bytes));
   }
   bytes[used] = '\0';
   return bytes;
}

static void
send_bytes(int fd, const char *bytes)
{
   size_t length = strlen(bytes);

   while (length != 0U) {
      ssize_t count = write(fd, bytes, length);
      assert(count > 0);
      bytes += (size_t)count;
      length -= (size_t)count;
   }
}

static void
receive_until(int fd, const char *end, char *bytes, size_t capacity)
{
   size_t length = 0U;

   bytes[0] = '\0';
   while (strstr(bytes, end) == NULL) {
      struct pollfd pending = {fd, POLLIN, 0};
      int ready = poll(&pending, 1U, 5000);

      if (ready != 1) {
         (void)fprintf(stderr, "timeout awaiting %s; received %s\n", end, bytes);
      }
      assert(ready == 1);
      assert(length + 1U < capacity);
      assert(read(fd, bytes + length, 1U) == 1);
      bytes[++length] = '\0';
   }
}

static ssize_t
fail_frame(int fd, const void *bytes, size_t length)
{
   (void)fd;
   (void)bytes;
   (void)length;
   errno = EIO;
   return -1;
}

static struct session
start(const char *path, const char *reply, bool fault)
{
   struct session session;
   struct winsize size = {5U, 40U, 0U, 0U};
   char output[1024];
   char *name;

   session.master = posix_openpt(O_RDWR | O_NOCTTY);
   assert(session.master >= 0);
   assert(grantpt(session.master) == 0 && unlockpt(session.master) == 0);
   name = ptsname(session.master);
   assert(name != NULL);
   session.slave = open(name, O_RDWR | O_NOCTTY);
   assert(session.slave >= 0);
   assert(ioctl(session.slave, TIOCSWINSZ, &size) == 0);
   assert(tcgetattr(session.slave, &session.original) == 0);
   session.errors = tmpfile();
   session.record = tmpfile();
   assert(session.errors != NULL && session.record != NULL);
   session.child = fork();
   assert(session.child >= 0);
   if (session.child == 0) {
      struct cwiki_key_record record;
      struct cwiki_app_options options = {
         session.slave, session.slave, fileno(session.errors),
         fileno(session.record), &record
      };
      int result;

      (void)close(session.master);
      assert(cwiki_key_record_init(&record, NULL, 0U) == 0);
      if (fault) {
         cwiki_app_test_set_write(fail_frame);
      }
      result = cwiki_app_run(path, &options);
      assert(cwiki_key_record_flush(&record, fileno(session.record)) == 0);
      cwiki_key_record_destroy(&record);
      _exit(result);
   }
   receive_until(session.master, CWIKI_CAPABILITIES_DA1_QUERY,
       output, sizeof(output));
   assert(strcmp(output, queries) == 0);
   send_bytes(session.master, wire(reply));
   return session;
}

static void
frame(struct session *session, char *bytes, size_t capacity)
{
   receive_until(session->master, CWIKI_TERMINAL_SYNC_END, bytes, capacity);
   assert(strstr(bytes, CWIKI_TERMINAL_SYNC_BEGIN) != NULL);
}

static void
quiet(struct session *session)
{
   struct pollfd pending = {session->master, POLLIN, 0};

   assert(poll(&pending, 1U, 180) == 0);
}

static void
finish(struct session *session, int expected, bool modes,
    const char *recorded, const char *diagnostic)
{
   struct termios restored;
   char output[4096];
   unsigned char recording[65536];
   struct cwiki_key_recording_view view;
   ssize_t length;
   int status;
   tcflag_t ignored_lflag = 0U;

#ifdef PENDIN
   ignored_lflag |= PENDIN;
#endif
#ifdef FLUSHO
   ignored_lflag |= FLUSHO;
#endif
#if defined(__APPLE__) && !defined(PENDIN)
   /* Same kernel-maintained bit excluded by the terminal module's tests. */
   ignored_lflag |= (tcflag_t)0x20000000U;
#endif

   if (modes) {
      receive_until(session->master, CWIKI_TERMINAL_CURSOR_SHOW,
          output, sizeof(output));
      assert(strstr(output, CWIKI_TERMINAL_RESTORE) != NULL);
   }
   assert(waitpid(session->child, &status, 0) == session->child);
   assert(WIFEXITED(status) && WEXITSTATUS(status) == expected);
   assert(tcgetattr(session->slave, &restored) == 0);
   assert(restored.c_iflag == session->original.c_iflag);
   assert(restored.c_oflag == session->original.c_oflag);
   assert(restored.c_cflag == session->original.c_cflag);
   assert((restored.c_lflag & (tcflag_t)~ignored_lflag) ==
       (session->original.c_lflag & (tcflag_t)~ignored_lflag));
   assert(memcmp(restored.c_cc, session->original.c_cc,
       sizeof(restored.c_cc)) == 0);
   if (!modes) {
      quiet(session);
   }
   length = pread(fileno(session->record), recording, sizeof(recording), 0);
   assert(length >= 0);
   assert(cwiki_key_record_read(recording, (size_t)length, &view) == 0);
   assert(view.length == strlen(wire(recorded)));
   assert(memcmp(view.bytes, wire(recorded), view.length) == 0);
   length = pread(fileno(session->errors), output, sizeof(output) - 1U, 0);
   assert(length >= 0);
   output[(size_t)length] = '\0';
   if (diagnostic == NULL) {
      if (length != 0) {
         (void)fprintf(stderr, "unexpected diagnostics: %s", output);
      }
      assert(length == 0);
   } else {
      assert(strstr(output, diagnostic) != NULL);
   }
   assert(fclose(session->errors) == 0 && fclose(session->record) == 0);
   assert(close(session->master) == 0 && close(session->slave) == 0);
}

static void
content(const char *path, const char *expected, enum cwiki_line_ending ending)
{
   struct cwiki_document document;
   char *bytes;
   size_t length;

   assert(cwiki_document_load(&document, path) == 0);
   assert(document.buffer.line_ending == ending);
   assert(cwiki_buffer_encode(&document.buffer, &bytes, &length) == 0);
   assert(length == strlen(expected) && memcmp(bytes, expected, length) == 0);
   free(bytes);
   cwiki_document_free(&document);
}

static void
write_note(const char *path, const char *bytes)
{
   int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

   assert(fd >= 0);
   send_bytes(fd, bytes);
   assert(close(fd) == 0);
}

int
main(void)
{
   char path[] = "/tmp/cwiki-app-test-XXXXXX";
   char output[32768];
   char transcript[4096];
   struct session session;
   int fd = mkstemp(path);

   assert(fd >= 0 && close(fd) == 0 && unlink(path) == 0);

   /* Pending startup input, new LF file, edits and motions in a single drain.
    * j must see the newly inserted second line; dw must see its latest bytes. */
   assert(snprintf(transcript, sizeof(transcript),
       "%siONE" ENTER "two three" ESCAPE "ggjdw:wq" ENTER,
       supported) > 0);
   session = start(path, transcript, false);
   frame(&session, output, sizeof(output));
   if (strstr(output, "three") == NULL) {
      (void)fprintf(stderr, "unexpected frame: %s\n", output);
   }
   assert(strstr(output, "three") != NULL);
   finish(&session, 0, true, transcript, NULL);
   content(path, "ONE\nthree", CWIKI_LINE_ENDING_LF);
   (void)puts("app: startup handoff, event ordering, LF creation, wq, recording, cleanup passed");

   /* Existing bytes are not normalized on load or clean quit. */
   write_note(path, "first\r\nsecond");
   session = start(path, supported, false);
   frame(&session, output, sizeof(output));
   quiet(&session);
   send_bytes(session.master, wire(":q" ENTER));
   frame(&session, output, sizeof(output));
   assert(snprintf(transcript, sizeof(transcript), "%s:q" ENTER, supported) > 0);
   finish(&session, 0, true, transcript, NULL);
   content(path, "first\r\nsecond", CWIKI_LINE_ENDING_CRLF);

   /* Insert-leave saves without :w; Replace-leave does too. */
   session = start(path, supported, false);
   frame(&session, output, sizeof(output));
   send_bytes(session.master, wire("iX" ESCAPE));
   frame(&session, output, sizeof(output));
   quiet(&session);
   content(path, "Xfirst\r\nsecond", CWIKI_LINE_ENDING_CRLF);
   send_bytes(session.master, wire("0RY" ESCAPE));
   frame(&session, output, sizeof(output));
   quiet(&session);
   content(path, "Yfirst\r\nsecond", CWIKI_LINE_ENDING_CRLF);
   send_bytes(session.master, wire(":q" ENTER));
   frame(&session, output, sizeof(output));
   assert(snprintf(transcript, sizeof(transcript),
       "%siX" ESCAPE "0RY" ESCAPE ":q" ENTER, supported) > 0);
   finish(&session, 0, true, transcript, NULL);
   (void)puts("app: byte-exact reopen, Insert/Replace leave saves, one redraw per drain passed");

   session = start(path, supported, false);
   frame(&session, output, sizeof(output));
   send_bytes(session.master, wire("dd:q" ENTER));
   frame(&session, output, sizeof(output));
   quiet(&session);
   content(path, "Yfirst\r\nsecond", CWIKI_LINE_ENDING_CRLF);
   send_bytes(session.master, wire(ESCAPE ":wq" ENTER));
   frame(&session, output, sizeof(output));
   assert(snprintf(transcript, sizeof(transcript),
       "%sdd:q" ENTER ESCAPE ":wq" ENTER, supported) > 0);
   finish(&session, 0, true, transcript, ":q refused");
   content(path, "second", CWIKI_LINE_ENDING_LF);
   (void)puts("app: dirty q refusal and later durable wq passed");

   write_note(path, "one\ntwo\nthree\nfour\nfive\nsix\nseven");
   session = start(path, supported, false);
   frame(&session, output, sizeof(output));
   send_bytes(session.master, wire("G"));
   frame(&session, output, sizeof(output));
   assert(strstr(output, "seven") != NULL && strstr(output, "one") == NULL);
   quiet(&session);
   {
      struct winsize size = {3U, 20U, 0U, 0U};
      assert(ioctl(session.slave, TIOCSWINSZ, &size) == 0);
   }
   frame(&session, output, sizeof(output));
   assert(strstr(output, "seven") != NULL && strstr(output, "five") == NULL);
   assert(strstr(output, "\x1b[3;1H") != NULL);
   quiet(&session);
   send_bytes(session.master, wire("gg:q" ENTER));
   frame(&session, output, sizeof(output));
   assert(strstr(output, "one") != NULL && strstr(output, "seven") == NULL);
   assert(snprintf(transcript, sizeof(transcript), "%sGgg:q" ENTER, supported) > 0);
   finish(&session, 0, true, transcript, NULL);
   (void)puts("app: tty resize and viewport following in both directions passed");

   /* Recomputing from clean line zero alone can miss this dirty seventh line. */
   session = start(path, supported, false);
   frame(&session, output, sizeof(output));
   send_bytes(session.master, wire("G0i$\\alpha$ " ESCAPE ":q" ENTER));
   frame(&session, output, sizeof(output));
   assert(strstr(output, "𝛼") != NULL);
   assert(strstr(output, "\x1b[0;33m") != NULL);
   assert(snprintf(transcript, sizeof(transcript),
       "%sG0i$\\alpha$ " ESCAPE ":q" ENTER, supported) > 0);
   finish(&session, 0, true, transcript, NULL);
   content(path, "one\ntwo\nthree\nfour\nfive\nsix\n$\\alpha$ seven",
       CWIKI_LINE_ENDING_LF);
   (void)puts("app: late-line zones, conceal and highlights refresh between events passed");

   session = start(path, supported, false);
   frame(&session, output, sizeof(output));
   send_bytes(session.master, wire("\x1b[999999999999999999999999u:q" ENTER));
   frame(&session, output, sizeof(output));
   assert(snprintf(transcript, sizeof(transcript),
       "%s\x1b[999999999999999999999999u:q" ENTER, supported) > 0);
   finish(&session, 0, true, transcript, NULL);
   (void)puts("app: rejected parser bytes retained exactly in raw recording passed");

   session = start(path, supported, true);
   frame(&session, output, sizeof(output));
   /* Explicit end-update is consumed above; finish requires another complete
    * restoration sequence, so cleanup alone cannot satisfy this assertion. */
   assert(strcmp(strstr(output, CWIKI_TERMINAL_SYNC_BEGIN),
       CWIKI_TERMINAL_SYNC_BEGIN CWIKI_TERMINAL_SYNC_END) == 0);
   finish(&session, 1, true, supported, "Input/output error");
   (void)puts("app: frame-write failure closes synchronized update before cleanup passed");

   session = start(path, "\x1b[?1;2c", false);
   finish(&session, 1, false, "\x1b[?1;2c", "kitty");
   (void)puts("app: capability failure restores termios without mode-reset bytes passed");

   assert(unlink(path) == 0);
   return 0;
}
