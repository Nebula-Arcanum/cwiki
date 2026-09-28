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
