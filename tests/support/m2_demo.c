/* M2 demo driver: copies a fixture vault into a disposable root, runs the
 * demo script on it, then removes the root. Usage: m2_demo FIXTURE_DIR SCRIPT */
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fixture_vault.h"

int
main(int argc, char **argv)
{
   struct fixture_vault vault;
   int status = 1;
   pid_t pid;
   int wstatus;

   if (argc != 3) {
      fprintf(stderr, "usage: %s FIXTURE_DIR SCRIPT\n", argv[0]);
      return 1;
   }
   if (fixture_vault_create(&vault) != 0) {
      perror("fixture_vault_create");
      return 1;
   }
   if (fixture_vault_copy_in(&vault, argv[1]) != 0) {
      perror("fixture_vault_copy_in");
      (void)fixture_vault_destroy(&vault);
      return 1;
   }
   printf("fixture vault: %s\n", fixture_vault_root(&vault));
   (void)fflush(stdout);
   pid = fork();
   if (pid == 0) {
      execl("/bin/sh", "sh", argv[2], fixture_vault_root(&vault), (char *)NULL);
      _exit(127);
   }
   if (pid < 0) {
      perror("fork");
   } else if (waitpid(pid, &wstatus, 0) < 0) {
      perror("waitpid");
   } else if (WIFEXITED(wstatus)) {
      status = WEXITSTATUS(wstatus);
   }
   if (access(fixture_vault_root(&vault), F_OK) != 0) {
      fprintf(stderr, "vault root vanished before destroy\n");
      status = 1;
   }
   {
      char *root = vault.root;
      char saved[4096];
      (void)snprintf(saved, sizeof saved, "%s", root);
      if (fixture_vault_destroy(&vault) != 0) {
         perror("fixture_vault_destroy");
         return 1;
      }
      if (access(saved, F_OK) == 0) {
         fprintf(stderr, "vault root still exists: %s\n", saved);
         return 1;
      }
   }
   return status;
}
