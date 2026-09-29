#include "config_files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_LIMIT (1024U * 1024U)

static char *
copy_string(const char *value)
{
   size_t length;
   char *copy;

   if (value == NULL) {
      return NULL;
   }
   length = strlen(value);
   if (length == SIZE_MAX) {
      return NULL;
   }
   copy = malloc(length + 1U);
   if (copy != NULL) {
      (void)memcpy(copy, value, length + 1U);
   }
   return copy;
}

static char *
join_path(const char *left, const char *right)
{
   size_t left_length = strlen(left);
   size_t right_length = strlen(right);
   bool slash = left_length != 0U && left[left_length - 1U] != '/';
   size_t length;
   char *path;

   if (left_length > SIZE_MAX - right_length - 2U) {
      return NULL;
   }
   length = left_length + (slash ? 1U : 0U) + right_length;
   path = malloc(length + 1U);
   if (path == NULL) {
      return NULL;
   }
   (void)memcpy(path, left, left_length);
   if (slash) {
      path[left_length++] = '/';
   }
   (void)memcpy(path + left_length, right, right_length + 1U);
   return path;
}

static void
set_error(struct cwiki_config_files_error *error, const char *path,
    int system_errno)
{
   if (error == NULL) {
      return;
   }
   free(error->path);
   error->path = copy_string(path);
   error->system_errno = system_errno;
}

void
cwiki_config_sources_free(struct cwiki_config_sources *sources)
{
   size_t i;

   if (sources == NULL) {
      return;
   }
   for (i = 0U; i < sources->count; i++) {
      free(sources->items[i].path);
      free(sources->items[i].bytes);
   }
   *sources = (struct cwiki_config_sources){0};
}

void
cwiki_config_files_error_free(struct cwiki_config_files_error *error)
{
   if (error != NULL) {
      free(error->path);
      *error = (struct cwiki_config_files_error){0};
   }
}

static enum cwiki_config_files_status
read_optional(struct cwiki_config_sources *sources, const char *path,
    enum cwiki_config_scope scope, struct cwiki_config_files_error *error)
{
   unsigned char *bytes;
   size_t length = 0U;
   int fd = open(path, O_RDONLY);

   if (fd < 0) {
      if (errno == ENOENT) {
         return CWIKI_CONFIG_FILES_OK;
      }
      set_error(error, path, errno);
      return CWIKI_CONFIG_FILES_SYSTEM;
   }
   bytes = malloc(CONFIG_LIMIT + 1U);
   if (bytes == NULL) {
      (void)close(fd);
      return CWIKI_CONFIG_FILES_NO_MEMORY;
   }
   while (length <= CONFIG_LIMIT) {
      ssize_t count = read(fd, bytes + length, CONFIG_LIMIT + 1U - length);

      if (count > 0) {
         length += (size_t)count;
      } else if (count == 0) {
         break;
      } else if (errno != EINTR) {
         int saved = errno;

         free(bytes);
         (void)close(fd);
         set_error(error, path, saved);
         return CWIKI_CONFIG_FILES_SYSTEM;
      }
   }
   if (close(fd) != 0) {
      int saved = errno;

      free(bytes);
      set_error(error, path, saved);
      return CWIKI_CONFIG_FILES_SYSTEM;
   }
   if (length > CONFIG_LIMIT) {
      free(bytes);
      set_error(error, path, EFBIG);
      return CWIKI_CONFIG_FILES_TOO_LARGE;
   }
   if (sources->count >= CWIKI_CONFIG_SOURCE_MAX) {
      free(bytes);
      return CWIKI_CONFIG_FILES_INVALID;
   }
   sources->items[sources->count].scope = scope;
   sources->items[sources->count].path = copy_string(path);
   sources->items[sources->count].bytes = bytes;
   sources->items[sources->count].length = length;
   if (sources->items[sources->count].path == NULL) {
      free(bytes);
      sources->items[sources->count].bytes = NULL;
      return CWIKI_CONFIG_FILES_NO_MEMORY;
   }
   sources->count++;
   return CWIKI_CONFIG_FILES_OK;
}

static char *
note_directory(const char *note_path)
{
   char *copy = copy_string(note_path);
   char *slash;

   if (copy == NULL) {
      return NULL;
   }
   slash = strrchr(copy, '/');
   if (slash == NULL) {
      free(copy);
      return copy_string(".");
   }
   if (slash == copy) {
      slash[1] = '\0';
   } else {
      *slash = '\0';
   }
   return copy;
}

static char *
find_vault(const char *note_path, struct cwiki_config_files_error *error,
    enum cwiki_config_files_status *status)
{
   char *directory = note_directory(note_path);
   char *current;

   if (directory == NULL) {
      *status = CWIKI_CONFIG_FILES_NO_MEMORY;
      return NULL;
   }
   current = realpath(directory, NULL);
   if (current == NULL) {
      set_error(error, directory, errno);
      free(directory);
      *status = CWIKI_CONFIG_FILES_SYSTEM;
      return NULL;
   }
   free(directory);
   for (;;) {
      char *marker = join_path(current, ".cwiki");
      struct stat metadata;
      int found;

      if (marker == NULL) {
         free(current);
         *status = CWIKI_CONFIG_FILES_NO_MEMORY;
         return NULL;
      }
      found = stat(marker, &metadata);
      if (found == 0 && S_ISDIR(metadata.st_mode)) {
         free(marker);
         return current;
      }
      if (found != 0 && errno != ENOENT && errno != ENOTDIR) {
         int saved = errno;

         set_error(error, marker, saved);
         free(marker);
         free(current);
         *status = CWIKI_CONFIG_FILES_SYSTEM;
         return NULL;
      }
      free(marker);
      if (strcmp(current, "/") == 0) {
         free(current);
         return NULL;
      }
      {
         char *slash = strrchr(current, '/');

         if (slash == current) {
            current[1] = '\0';
         } else {
            *slash = '\0';
         }
      }
   }
}

static bool
valid_uuid(const unsigned char *bytes, size_t length)
{
   size_t i;

   if (length == 37U && bytes[36] == '\n') {
      length--;
   }
   if (length != 36U) {
      return false;
   }
   for (i = 0U; i < length; i++) {
      if (i == 8U || i == 13U || i == 18U || i == 23U) {
         if (bytes[i] != '-') {
            return false;
         }
      } else if (!((bytes[i] >= '0' && bytes[i] <= '9') ||
          (bytes[i] >= 'a' && bytes[i] <= 'f'))) {
         return false;
      }
   }
   return bytes[14] >= '1' && bytes[14] <= '5' &&
       (bytes[19] == '8' || bytes[19] == '9' || bytes[19] == 'a' ||
       bytes[19] == 'b');
}

static char *
config_base(const char *home, const char *xdg_config_home, bool macos)
{
   char *base;

   if (macos) {
      return home == NULL ? NULL :
          join_path(home, "Library/Application Support/cwiki");
   }
   if (xdg_config_home != NULL && xdg_config_home[0] == '/') {
      return join_path(xdg_config_home, "cwiki");
   }
   if (home == NULL) {
      return NULL;
   }
   base = join_path(home, ".config");
   if (base != NULL) {
      char *cwiki = join_path(base, "cwiki");

      free(base);
      base = cwiki;
   }
   return base;
}

static enum cwiki_config_files_status
add_path(struct cwiki_config_sources *sources, const char *directory,
    const char *name, enum cwiki_config_scope scope,
    struct cwiki_config_files_error *error)
{
   char *path;
   enum cwiki_config_files_status status;

   if (directory == NULL) {
      return CWIKI_CONFIG_FILES_OK;
   }
   path = join_path(directory, name);
   if (path == NULL) {
      return CWIKI_CONFIG_FILES_NO_MEMORY;
   }
   status = read_optional(sources, path, scope, error);
   free(path);
   return status;
}

enum cwiki_config_files_status
cwiki_config_sources_discover(struct cwiki_config_sources *sources,
    const char *note_path, const char *home, const char *xdg_config_home,
    bool macos, struct cwiki_config_files_error *error)
{
   enum cwiki_config_files_status status = CWIKI_CONFIG_FILES_OK;
   struct cwiki_config_sources created = {0};
   char *base = NULL;
   char *vault = NULL;
   char *vault_config = NULL;
   char *id_path = NULL;
   struct cwiki_config_sources id = {0};

   if (sources == NULL || note_path == NULL || note_path[0] == '\0') {
      return CWIKI_CONFIG_FILES_INVALID;
   }
   *sources = (struct cwiki_config_sources){0};
   if (error != NULL) {
      *error = (struct cwiki_config_files_error){0};
   }
   base = config_base(home, xdg_config_home, macos);
   if (base == NULL && ((macos && home != NULL) || (!macos &&
       (home != NULL ||
       (xdg_config_home != NULL && xdg_config_home[0] == '/'))))) {
      status = CWIKI_CONFIG_FILES_NO_MEMORY;
      goto done;
   }
   status = add_path(&created, base, "config.yaml", CWIKI_CONFIG_GLOBAL, error);
   if (status != CWIKI_CONFIG_FILES_OK) {
      goto done;
   }
   vault = find_vault(note_path, error, &status);
   if (status != CWIKI_CONFIG_FILES_OK || vault == NULL) {
      goto done;
   }
   vault_config = join_path(vault, ".cwiki");
   if (vault_config == NULL) {
      status = CWIKI_CONFIG_FILES_NO_MEMORY;
      goto done;
   }
   status = add_path(&created, vault_config, "config.yaml", CWIKI_CONFIG_VAULT,
       error);
   if (status != CWIKI_CONFIG_FILES_OK) {
      goto done;
   }
   id_path = join_path(vault_config, "vault-id");
   if (id_path == NULL) {
      status = CWIKI_CONFIG_FILES_NO_MEMORY;
      goto done;
   }
   status = read_optional(&id, id_path, CWIKI_CONFIG_VAULT, error);
   if (status != CWIKI_CONFIG_FILES_OK || id.count == 0U) {
      goto done;
   }
   if (!valid_uuid(id.items[0].bytes, id.items[0].length)) {
      set_error(error, id_path, EINVAL);
      status = CWIKI_CONFIG_FILES_INVALID_VAULT_ID;
      goto done;
   }
   if (base != NULL) {
      char *vaults = join_path(base, "vaults");
      char name[42];

      if (vaults == NULL) {
         status = CWIKI_CONFIG_FILES_NO_MEMORY;
         goto done;
      }
      (void)memcpy(name, id.items[0].bytes, 36U);
      (void)memcpy(name + 36U, ".yaml", 6U);
      status = add_path(&created, vaults, name, CWIKI_CONFIG_MACHINE_LOCAL,
          error);
      free(vaults);
   }
done:
   free(base);
   free(vault);
   free(vault_config);
   free(id_path);
   cwiki_config_sources_free(&id);
   if (status == CWIKI_CONFIG_FILES_OK) {
      *sources = created;
   } else {
      cwiki_config_sources_free(&created);
   }
   return status;
}
