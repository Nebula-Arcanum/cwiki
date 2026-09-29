#ifndef CWIKI_APP_H
#define CWIKI_APP_H

#include "config_files.h"

#include <stddef.h>
#include <sys/types.h>

struct cwiki_key_record;

struct cwiki_app_options {
   int input_fd;
   int output_fd;
   int error_fd;
   /* Caller-owned, preopened outside the vault; -1 disables crash persistence. */
   int crash_fd;
   /* Optional caller-owned initialized recorder, also useful for replay tests. */
   struct cwiki_key_record *record;
   /* Ordered declarative sources, parsed before terminal startup. */
   const struct cwiki_config_source *configs;
   size_t config_count;
};

/* Returns 0 on requested exit, 1 on failure; diagnostics follow restoration. */
int cwiki_app_run(const char *path, const struct cwiki_app_options *options);

#ifdef CWIKI_APP_TESTING
void cwiki_app_test_set_write(ssize_t (*write_fn)(int, const void *, size_t));
#endif

#endif
