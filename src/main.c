#include "app.h"
#include "config_files.h"
#include "terminal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
   struct cwiki_app_options options = {
      STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, -1, NULL, NULL, 0U, false
   };
   struct cwiki_config_sources configs = {0};
   struct cwiki_config_files_error config_error = {0};
   enum cwiki_config_files_status config_status;
   char crash_path[] = "/tmp/cwiki-keys-XXXXXX";
   int result;

   if (argc == 2 && strcmp(argv[1], "--reset-terminal") == 0) {
      if (cwiki_terminal_reset(STDOUT_FILENO) == 0) {
         return 0;
      }
      perror("cwiki: reset terminal");
      return 1;
   }
   if ((argc != 2 && argc != 3) ||
       (argc == 3 && strcmp(argv[1], "--inspect-config") != 0) ||
       argv[argc - 1][0] == '\0') {
      (void)fprintf(stderr,
          "usage: cwiki NOTE | cwiki --inspect-config NOTE | "
          "cwiki --reset-terminal\n");
      return 1;
   }
   options.inspect_config = argc == 3;
   config_status = cwiki_config_sources_discover(&configs, argv[argc - 1],
       getenv("HOME"), getenv("XDG_CONFIG_HOME"),
#ifdef __APPLE__
       true,
#else
       false,
#endif
       &config_error);
   if (config_status != CWIKI_CONFIG_FILES_OK) {
      const char *message = config_status == CWIKI_CONFIG_FILES_TOO_LARGE ?
          "configuration exceeds the 1 MiB limit" :
          (config_status == CWIKI_CONFIG_FILES_INVALID_VAULT_ID ?
          "invalid vault UUID" :
          (config_status == CWIKI_CONFIG_FILES_NO_MEMORY ?
          "out of memory" : strerror(config_error.system_errno == 0 ?
          EINVAL : config_error.system_errno)));

      (void)fprintf(stderr, "cwiki: %s: %s\n",
          config_error.path == NULL ? "configuration" : config_error.path,
          message);
      cwiki_config_files_error_free(&config_error);
      return 1;
   }
   options.configs = configs.items;
   options.config_count = configs.count;
   if (options.inspect_config) {
      result = cwiki_app_run(argv[argc - 1], &options);
      cwiki_config_sources_free(&configs);
      return result;
   }
   options.crash_fd = mkstemp(crash_path);
   if (options.crash_fd < 0) {
      perror("cwiki: crash recording");
      cwiki_config_sources_free(&configs);
      return 1;
   }
   result = cwiki_app_run(argv[argc - 1], &options);
   (void)close(options.crash_fd);
   (void)unlink(crash_path);
   cwiki_config_sources_free(&configs);
   return result;
}
