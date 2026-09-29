#ifndef CWIKI_CONFIG_FILES_H
#define CWIKI_CONFIG_FILES_H

#include <stdbool.h>
#include <stddef.h>

#define CWIKI_CONFIG_SOURCE_MAX 3U

enum cwiki_config_scope {
   CWIKI_CONFIG_GLOBAL,
   CWIKI_CONFIG_VAULT,
   CWIKI_CONFIG_MACHINE_LOCAL
};

struct cwiki_config_source {
   enum cwiki_config_scope scope;
   char *path;
   unsigned char *bytes;
   size_t length;
};

struct cwiki_config_sources {
   struct cwiki_config_source items[CWIKI_CONFIG_SOURCE_MAX];
   size_t count;
};

enum cwiki_config_files_status {
   CWIKI_CONFIG_FILES_OK,
   CWIKI_CONFIG_FILES_INVALID,
   CWIKI_CONFIG_FILES_NO_MEMORY,
   CWIKI_CONFIG_FILES_SYSTEM,
   CWIKI_CONFIG_FILES_TOO_LARGE,
   CWIKI_CONFIG_FILES_INVALID_VAULT_ID
};

struct cwiki_config_files_error {
   char *path;
   int system_errno;
};

/*
 * home and xdg_config_home are explicit for deterministic startup tests.
 * xdg_config_home is used only when absolute. macos selects the platform path.
 */
enum cwiki_config_files_status cwiki_config_sources_discover(
    struct cwiki_config_sources *sources, const char *note_path,
    const char *home, const char *xdg_config_home, bool macos,
    struct cwiki_config_files_error *error);
void cwiki_config_sources_free(struct cwiki_config_sources *sources);
void cwiki_config_files_error_free(struct cwiki_config_files_error *error);

#endif
