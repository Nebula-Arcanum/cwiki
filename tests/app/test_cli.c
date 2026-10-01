#include "capabilities.h"
#include "terminal.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define ENTER "\x1b[13u"
#define ESCAPE "\x1b[27u"
#define OUTPUT_CAPACITY (1024U * 1024U)
#define VAULT_UUID "123e4567-e89b-12d3-a456-426614174000"

static const char capability_reply[] =
    "\x1b[?29u\x1b_Gi=1129797963;OK\x1b\\\x1b[?1;2c";
static const char capability_queries[] = CWIKI_CAPABILITIES_KEYBOARD_QUERY
    CWIKI_CAPABILITIES_GRAPHICS_QUERY CWIKI_CAPABILITIES_DA1_QUERY;

struct result {
   int status;
   char output[OUTPUT_CAPACITY];
   size_t output_length;
   char error[65536];
   size_t error_length;
};

static void
write_all(int fd, const char *bytes, size_t length)
{
   while (length != 0U) {
      ssize_t count = write(fd, bytes, length);

      assert(count > 0);
      bytes += (size_t)count;
      length -= (size_t)count;
   }
}

static void
write_file(const char *path, const char *bytes)
{
   int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

   assert(fd >= 0);
   write_all(fd, bytes, strlen(bytes));
   assert(close(fd) == 0);
}

static void
make_directory(const char *path)
{
   assert(mkdir(path, 0700) == 0);
}

static void
path_join(char *path, size_t capacity, const char *left, const char *right)
{
   int count = snprintf(path, capacity, "%s/%s", left, right);

   assert(count > 0 && (size_t)count < capacity);
}

static size_t
read_to_end(int fd, char *bytes, size_t capacity)
{
   size_t length = 0U;

   while (length + 1U < capacity) {
      ssize_t count = read(fd, bytes + length, capacity - length - 1U);

      if (count > 0) {
         length += (size_t)count;
      } else if (count == 0) {
         break;
      } else if (errno != EINTR) {
         assert(false);
      }
   }
   bytes[length] = '\0';
   return length;
}

static void
wait_success(pid_t child, int expected, int *status)
{
   assert(waitpid(child, status, 0) == child);
   assert(WIFEXITED(*status) && WEXITSTATUS(*status) == expected);
}

static struct result
run_captured(const char *executable, const char *first, const char *note,
    const char *home, const char *xdg, int expected)
{
   int output_pipe[2];
   int error_pipe[2];
   pid_t child;
   struct result result = {0};

   assert(pipe(output_pipe) == 0 && pipe(error_pipe) == 0);
   child = fork();
   assert(child >= 0);
   if (child == 0) {
      int input = open("/dev/null", O_RDONLY);

      assert(input >= 0);
      assert(dup2(input, STDIN_FILENO) == STDIN_FILENO);
      assert(dup2(output_pipe[1], STDOUT_FILENO) == STDOUT_FILENO);
      assert(dup2(error_pipe[1], STDERR_FILENO) == STDERR_FILENO);
      (void)close(input);
      (void)close(output_pipe[0]);
      (void)close(output_pipe[1]);
      (void)close(error_pipe[0]);
      (void)close(error_pipe[1]);
      assert(setenv("HOME", home, 1) == 0);
      assert(setenv("XDG_CONFIG_HOME", xdg, 1) == 0);
      if (note == NULL) {
         execl(executable, executable, first, (char *)NULL);
      } else {
         execl(executable, executable, first, note, (char *)NULL);
      }
      _exit(127);
   }
   assert(close(output_pipe[1]) == 0 && close(error_pipe[1]) == 0);
   result.output_length = read_to_end(output_pipe[0], result.output,
       sizeof(result.output));
   result.error_length = read_to_end(error_pipe[0], result.error,
       sizeof(result.error));
   assert(close(output_pipe[0]) == 0 && close(error_pipe[0]) == 0);
   wait_success(child, expected, &result.status);
   return result;
}

static void
test_reset(const char *executable, const char *home, const char *xdg)
{
   struct result result = run_captured(executable, "--reset-terminal", NULL,
       home, xdg, 0);

   assert(result.output_length == sizeof(CWIKI_TERMINAL_RESTORE) - 1U);
   assert(memcmp(result.output, CWIKI_TERMINAL_RESTORE,
       sizeof(CWIKI_TERMINAL_RESTORE) - 1U) == 0);
   assert(result.error_length == 0U);
}

static void
test_discovered_config(const char *executable, const char *home,
    const char *xdg, const char *note, const char *global_config,
    const char *vault_config, const char *local_config)
{
   struct result result;
   char expected[1024];
   char *canonical_vault_config;

   write_file(global_config, "save-policy: manual\n");
   write_file(vault_config, "display:\n  wrap: false\n");
   write_file(local_config, "modules:\n  snippets: false\n");
   write_file(note, "---\nsubject: [calculus]\n---\nFixture\n");
   canonical_vault_config = realpath(vault_config, NULL);
   assert(canonical_vault_config != NULL);
   result = run_captured(executable, "--inspect-config", note, home, xdg, 0);
   assert(result.error_length == 0U);
   assert(strstr(result.output, capability_queries) == NULL);
   assert(strstr(result.output, "\x1b[") == NULL);
   assert(snprintf(expected, sizeof(expected),
       "setting save-policy=manual source=%s:1:14", global_config) > 0);
   assert(strstr(result.output, expected) != NULL);
   assert(snprintf(expected, sizeof(expected),
       "setting display.wrap=false source=%s:2:9", canonical_vault_config) > 0);
   assert(strstr(result.output, expected) != NULL);
   assert(snprintf(expected, sizeof(expected),
       "setting modules.snippets=false source=%s:2:13", local_config) > 0);
   assert(strstr(result.output, expected) != NULL);
   assert(snprintf(expected, sizeof(expected),
       "subject value=calculus source=%s:2:11", note) > 0);
   assert(strstr(result.output, expected) != NULL);
   free(canonical_vault_config);

   write_file(global_config, "not-a-setting: true\n");
   result = run_captured(executable, note, NULL, home, xdg, 1);
   assert(result.output_length == 0U);
   assert(strstr(result.error, global_config) != NULL);
   assert(strstr(result.error, "unknown configuration key") != NULL);
   assert(strstr(result.error, "terminal") == NULL);
   write_file(global_config, "save-policy: manual\n");
}

static bool
termios_equal(const struct termios *left, const struct termios *right)
{
   tcflag_t ignored = 0U;

#ifdef PENDIN
   ignored |= PENDIN;
#endif
#ifdef FLUSHO
   ignored |= FLUSHO;
#endif
#if defined(__APPLE__) && !defined(PENDIN)
   ignored |= (tcflag_t)0x20000000U;
#endif
   return left->c_iflag == right->c_iflag &&
       left->c_oflag == right->c_oflag &&
       left->c_cflag == right->c_cflag &&
       (left->c_lflag & (tcflag_t)~ignored) ==
       (right->c_lflag & (tcflag_t)~ignored) &&
       memcmp(left->c_cc, right->c_cc, sizeof(left->c_cc)) == 0;
}

static void
receive_until(int fd, const char *needle, char *bytes, size_t capacity,
    size_t *length)
{
   while (strstr(bytes, needle) == NULL) {
      struct pollfd pending = {fd, POLLIN, 0};
      ssize_t count;

      assert(poll(&pending, 1U, 5000) == 1);
      assert(*length + 1U < capacity);
      count = read(fd, bytes + *length, capacity - *length - 1U);
      assert(count > 0);
      *length += (size_t)count;
      bytes[*length] = '\0';
   }
}

static void
send_text(int fd, const char *text)
{
   char wire[4096];
   size_t used = 0U;

   while (*text != '\0') {
      if ((unsigned char)*text == 27U) {
         do {
            assert(used + 1U < sizeof(wire));
            wire[used++] = *text;
         } while (*text++ != 'u');
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
         } else if (value == '#') {
            key = '3';
            modifiers = 2U;
         }
         count = snprintf(wire + used, sizeof(wire) - used,
             "\x1b[%u;%u;%uu", key, modifiers, value);
         assert(count > 0 && (size_t)count < sizeof(wire) - used);
         used += (size_t)count;
      }
   }
   write_all(fd, wire, used);
}

static size_t
list_crash_files(char names[][64], size_t capacity)
{
   DIR *directory = opendir("/tmp");
   struct dirent *entry;
   size_t count = 0U;

   assert(directory != NULL);
   while ((entry = readdir(directory)) != NULL) {
      if (strncmp(entry->d_name, "cwiki-keys-", 11U) == 0) {
         assert(count < capacity && strlen(entry->d_name) < sizeof(names[0]));
         (void)strcpy(names[count++], entry->d_name);
      }
   }
   assert(closedir(directory) == 0);
   return count;
}

static bool
name_present(char names[][64], size_t count, const char *name)
{
   size_t i;

   for (i = 0U; i < count; i++) {
      if (strcmp(names[i], name) == 0) {
         return true;
      }
   }
   return false;
}

static void
test_interactive(const char *executable, const char *home, const char *xdg,
    const char *note)
{
   char before[128][64];
   char during[128][64];
   char after[128][64];
   char created[2][64];
   char output[OUTPUT_CAPACITY] = {0};
   char saved[64];
   struct winsize window = {8U, 60U, 0U, 0U};
   struct termios original;
   struct termios restored;
   size_t before_count = list_crash_files(before, 128U);
   size_t during_count;
   size_t after_count;
   size_t created_count = 0U;
   size_t output_length = 0U;
   int master = posix_openpt(O_RDWR | O_NOCTTY);
   int slave;
   int status;
   char *slave_name;
   pid_t child;

   write_file(note, "");
   assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
   slave_name = ptsname(master);
   assert(slave_name != NULL);
   slave = open(slave_name, O_RDWR | O_NOCTTY);
   assert(slave >= 0);
   assert(ioctl(slave, TIOCSWINSZ, &window) == 0);
   assert(tcgetattr(slave, &original) == 0);
   child = fork();
   assert(child >= 0);
   if (child == 0) {
      assert(dup2(slave, STDIN_FILENO) == STDIN_FILENO);
      assert(dup2(slave, STDOUT_FILENO) == STDOUT_FILENO);
      assert(dup2(slave, STDERR_FILENO) == STDERR_FILENO);
      (void)close(master);
      (void)close(slave);
      assert(setenv("HOME", home, 1) == 0);
      assert(setenv("XDG_CONFIG_HOME", xdg, 1) == 0);
#ifdef __APPLE__
      assert(setenv("MallocNanoZone", "0", 1) == 0);
#endif
      execl(executable, executable, note, (char *)NULL);
      _exit(127);
   }
   receive_until(master, CWIKI_CAPABILITIES_DA1_QUERY, output,
       sizeof(output), &output_length);
   if (output_length != sizeof(capability_queries) - 1U ||
       memcmp(output, capability_queries, sizeof(capability_queries) - 1U) !=
       0) {
      size_t i;

      (void)fprintf(stderr, "unexpected startup bytes (%zu):", output_length);
      for (i = 0U; i < output_length; i++) {
         (void)fprintf(stderr, " %02x", (unsigned char)output[i]);
      }
      (void)fputc('\n', stderr);
   }
   assert(output_length == sizeof(capability_queries) - 1U);
   assert(memcmp(output, capability_queries, output_length) == 0);
   during_count = list_crash_files(during, 128U);
   for (size_t i = 0U; i < during_count; i++) {
      if (!name_present(before, before_count, during[i])) {
         assert(created_count < 2U);
         (void)strcpy(created[created_count++], during[i]);
      }
   }
   assert(created_count == 2U);
   assert((strstr(created[0], ".txt") == NULL) !=
       (strstr(created[1], ".txt") == NULL));
   write_all(master, capability_reply, sizeof(capability_reply) - 1U);
   receive_until(master, CWIKI_TERMINAL_SYNC_END, output,
       sizeof(output), &output_length);
   send_text(master, "i# CLI" ENTER "body" ESCAPE ":wq" ENTER);
   receive_until(master, CWIKI_TERMINAL_RESTORE, output,
       sizeof(output), &output_length);
   wait_success(child, 0, &status);
   assert(output_length >= sizeof(CWIKI_TERMINAL_RESTORE) - 1U);
   assert(memcmp(output + output_length - (sizeof(CWIKI_TERMINAL_RESTORE) - 1U),
       CWIKI_TERMINAL_RESTORE, sizeof(CWIKI_TERMINAL_RESTORE) - 1U) == 0);
   assert(tcgetattr(slave, &restored) == 0);
   assert(termios_equal(&original, &restored));
   assert(close(master) == 0 && close(slave) == 0);

   {
      int fd = open(note, O_RDONLY);
      ssize_t count;

      assert(fd >= 0);
      count = read(fd, saved, sizeof(saved));
      assert(count == 10);
      assert(memcmp(saved, "# CLI\nbody", 10U) == 0);
      assert(close(fd) == 0);
   }
   after_count = list_crash_files(after, 128U);
   assert(!name_present(after, after_count, created[0]));
   assert(!name_present(after, after_count, created[1]));
}

int
main(int argc, char **argv)
{
   char root[] = "/tmp/cwiki-cli-test-XXXXXX";
   char home[512];
   char xdg[512];
   char config_base[512];
   char vaults[512];
   char vault[512];
   char marker[512];
   char note[512];
   char global_config[512];
   char vault_config[512];
   char vault_id[512];
   char local_name[64];
   char local_config[512];
   int root_fd;

   assert(argc == 2);
   root_fd = mkstemp(root);
   assert(root_fd >= 0 && close(root_fd) == 0 && unlink(root) == 0);
   make_directory(root);
   path_join(home, sizeof(home), root, "home");
   path_join(xdg, sizeof(xdg), root, "xdg");
   path_join(vault, sizeof(vault), root, "vault");
   make_directory(home);
   make_directory(xdg);
   make_directory(vault);
   path_join(marker, sizeof(marker), vault, ".cwiki");
   make_directory(marker);
   path_join(note, sizeof(note), vault, "note.md");
   path_join(vault_config, sizeof(vault_config), marker, "config.yaml");
   path_join(vault_id, sizeof(vault_id), marker, "vault-id");
   write_file(vault_id, VAULT_UUID "\n");

#ifdef __APPLE__
   path_join(config_base, sizeof(config_base), home, "Library");
   make_directory(config_base);
   path_join(config_base, sizeof(config_base), home,
       "Library/Application Support");
   make_directory(config_base);
   path_join(config_base, sizeof(config_base), home,
       "Library/Application Support/cwiki");
#else
   path_join(config_base, sizeof(config_base), xdg, "cwiki");
#endif
   make_directory(config_base);
   path_join(global_config, sizeof(global_config), config_base, "config.yaml");
   path_join(vaults, sizeof(vaults), config_base, "vaults");
   make_directory(vaults);
   assert(snprintf(local_name, sizeof(local_name), "%s.yaml", VAULT_UUID) > 0);
   path_join(local_config, sizeof(local_config), vaults, local_name);

   test_reset(argv[1], home, xdg);
   test_discovered_config(argv[1], home, xdg, note, global_config,
       vault_config, local_config);
   test_interactive(argv[1], home, xdg, note);

   assert(unlink(note) == 0);
   assert(unlink(local_config) == 0);
   assert(unlink(vault_id) == 0);
   assert(unlink(vault_config) == 0);
   assert(unlink(global_config) == 0);
   assert(rmdir(vaults) == 0 && rmdir(config_base) == 0);
#ifdef __APPLE__
   path_join(config_base, sizeof(config_base), home, "Library/Application Support");
   assert(rmdir(config_base) == 0);
   path_join(config_base, sizeof(config_base), home, "Library");
   assert(rmdir(config_base) == 0);
#endif
   assert(rmdir(marker) == 0 && rmdir(vault) == 0);
   assert(rmdir(xdg) == 0 && rmdir(home) == 0 && rmdir(root) == 0);
   (void)puts("cli: production reset, config discovery, PTY edit and cleanup passed");
   return 0;
}
