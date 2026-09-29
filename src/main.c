#include "app.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
   struct cwiki_app_options options = {
      STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, -1, NULL, NULL, 0U
   };
   char crash_path[] = "/tmp/cwiki-keys-XXXXXX";
   int result;

   if (argc == 2 && strcmp(argv[1], "--reset-terminal") == 0) {
      if (cwiki_terminal_reset(STDOUT_FILENO) == 0) {
         return 0;
      }
      perror("cwiki: reset terminal");
      return 1;
   }
   if (argc != 2 || argv[1][0] == '\0') {
      (void)fprintf(stderr, "usage: cwiki NOTE | cwiki --reset-terminal\n");
      return 1;
   }
   options.crash_fd = mkstemp(crash_path);
   if (options.crash_fd < 0) {
      perror("cwiki: crash recording");
      return 1;
   }
   result = cwiki_app_run(argv[1], &options);
   (void)close(options.crash_fd);
   (void)unlink(crash_path);
   return result;
}
