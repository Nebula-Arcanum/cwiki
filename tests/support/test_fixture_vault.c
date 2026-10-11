#include "fixture_vault.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void
check(int condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static void
write_file(const char *path, const char *text)
{
   FILE *file = fopen(path, "wb");

   check(file != NULL, "create source file");
   if (file != NULL) {
      check(fputs(text, file) >= 0, "write source file");
      check(fclose(file) == 0, "close source file");
   }
}

static int
read_file(const char *path, char *buffer, size_t size)
{
   FILE *file = fopen(path, "rb");
   size_t n;

   if (file == NULL) {
      return -1;
   }
   n = fread(buffer, 1, size - 1, file);
   buffer[n] = '\0';
   (void)fclose(file);
   return 0;
}

/* AC8: copy-in copies a nested tree, including dot-directories, byte for byte. */
static void
test_copy_in(void)
{
   struct fixture_vault src;
   struct fixture_vault dst;
   char path[4096];
   char got[256];

   check(fixture_vault_create(&src) == 0, "AC8 create source root");
   check(fixture_vault_create(&dst) == 0, "AC8 create destination root");
   (void)snprintf(path, sizeof(path), "%s/.cwiki", fixture_vault_root(&src));
   check(mkdir(path, 0700) == 0, "AC8 create source dot-directory");
   (void)snprintf(path, sizeof(path), "%s/.cwiki/preamble.tex",
       fixture_vault_root(&src));
   write_file(path, "\\usepackage{amsmath}\n");
   (void)snprintf(path, sizeof(path), "%s/sub", fixture_vault_root(&src));
   check(mkdir(path, 0700) == 0, "AC8 create source subdirectory");
   (void)snprintf(path, sizeof(path), "%s/sub/note.md", fixture_vault_root(&src));
   write_file(path, "$x$\n");

   check(fixture_vault_copy_in(&dst, fixture_vault_root(&src)) == 0,
       "AC8 copy_in succeeds for a regular tree");
   (void)snprintf(path, sizeof(path), "%s/.cwiki/preamble.tex",
       fixture_vault_root(&dst));
   check(read_file(path, got, sizeof(got)) == 0 &&
       strcmp(got, "\\usepackage{amsmath}\n") == 0,
       "AC8 dot-directory file copied with identical bytes");
   (void)snprintf(path, sizeof(path), "%s/sub/note.md", fixture_vault_root(&dst));
   check(read_file(path, got, sizeof(got)) == 0 && strcmp(got, "$x$\n") == 0,
       "AC8 nested note copied with identical bytes");
   (void)snprintf(path, sizeof(path), "%s/sub/note.md", fixture_vault_root(&src));
   check(read_file(path, got, sizeof(got)) == 0 && strcmp(got, "$x$\n") == 0,
       "AC8 source tree unchanged after copy");

   /* A symlink anywhere in the source is rejected with EINVAL. */
   (void)snprintf(path, sizeof(path), "%s/sub/link", fixture_vault_root(&src));
   check(symlink("/etc/hosts", path) == 0, "AC8 create source symlink");
   errno = 0;
   check(fixture_vault_copy_in(&dst, fixture_vault_root(&src)) == -1 &&
       errno == EINVAL, "AC8 copy_in rejects a symlink with EINVAL");

   check(fixture_vault_destroy(&src) == 0, "AC8 destroy source root");
   check(fixture_vault_destroy(&dst) == 0, "AC8 destroy destination root");
}

int
main(void)
{
   struct fixture_vault vault;
   char nested[4096];
   char note[4096];
   char link[4096];
   char outside[] = "/tmp/cwiki-outside-XXXXXX";
   char *resolved = NULL;
   int outside_fd;

   test_copy_in();
   check(fixture_vault_create(&vault) == 0, "create disposable fixture root");
   if (fixture_vault_root(&vault) == NULL) {
      return 1;
   }
   (void)snprintf(nested, sizeof(nested), "%s/course", fixture_vault_root(&vault));
   (void)snprintf(note, sizeof(note), "%s/course/week-01.md",
       fixture_vault_root(&vault));
   check(mkdir(nested, 0700) == 0, "create allowed nested directory");
   {
      FILE *file = fopen(note, "wb");

      check(file != NULL, "create allowed nested note");
      if (file != NULL) {
         check(fputs("fixture\n", file) >= 0, "write allowed nested note");
         check(fclose(file) == 0, "close allowed nested note");
      }
   }
   check(fixture_vault_resolve(&vault, "course/week-01.md", &resolved) == 0,
       "resolve allowed nested path");
   check(resolved != NULL && strcmp(resolved, note) == 0,
       "nested path resolves to independently expected bytes");
   free(resolved);
   resolved = NULL;

   check(fixture_vault_resolve(&vault, note, &resolved) == -1 && errno == EINVAL,
       "reject absolute path");
   check(fixture_vault_resolve(&vault, "../outside", &resolved) == -1 &&
       errno == EINVAL, "reject leading parent traversal");
   check(fixture_vault_resolve(&vault, "course/../../outside", &resolved) == -1 &&
       errno == EINVAL, "reject nested parent traversal");

   outside_fd = mkstemp(outside);
   check(outside_fd >= 0, "create external symlink target");
   if (outside_fd >= 0) {
      check(close(outside_fd) == 0, "close external symlink target");
      (void)snprintf(link, sizeof(link), "%s/escape", fixture_vault_root(&vault));
      check(symlink(outside, link) == 0, "create escaping symlink");
      check(fixture_vault_resolve(&vault, "escape", &resolved) == -1 &&
          errno == EPERM, "reject symlink escape");
      check(unlink(outside) == 0, "remove external symlink target");
   }

   check(fixture_vault_destroy(&vault) == 0, "destroy fixture tree");
   if (failures != 0) {
      return 1;
   }
   (void)puts("fixture vault support: ok");
   return 0;
}
