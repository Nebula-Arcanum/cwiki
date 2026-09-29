#include "config_files.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
make_directory(const char *path)
{
   assert(mkdir(path, 0700) == 0);
}

static void
write_file(const char *path, const char *bytes)
{
   size_t length = strlen(bytes);
   int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

   assert(fd >= 0);
   while (length != 0U) {
      ssize_t count = write(fd, bytes, length);

      assert(count > 0);
      bytes += (size_t)count;
      length -= (size_t)count;
   }
   assert(close(fd) == 0);
}

static void
path(char *output, size_t capacity, const char *left, const char *right)
{
   int length = snprintf(output, capacity, "%s/%s", left, right);

   assert(length > 0 && (size_t)length < capacity);
}

static void
expect_source(const struct cwiki_config_source *source,
    enum cwiki_config_scope scope, const char *suffix, const char *bytes)
{
   size_t path_length = strlen(source->path);
   size_t suffix_length = strlen(suffix);

   assert(source->scope == scope && path_length >= suffix_length);
   assert(strcmp(source->path + path_length - suffix_length, suffix) == 0);
   assert(source->length == strlen(bytes));
   assert(memcmp(source->bytes, bytes, source->length) == 0);
}

static size_t
entry_count(const char *directory)
{
   DIR *entries = opendir(directory);
   struct dirent *entry;
   size_t count = 0U;

   assert(entries != NULL);
   while ((entry = readdir(entries)) != NULL) {
      if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
         count++;
      }
   }
   assert(closedir(entries) == 0);
   return count;
}

int
main(void)
{
   static const char uuid[] = "123e4567-e89b-42d3-a456-426614174000";
   char root[] = "/tmp/cwiki-config-files-XXXXXX";
   char home[512];
   char xdg[512];
   char cwiki[512];
   char vaults[512];
   char local[512];
   char vault[512];
   char marker[512];
   char nested[512];
   char note[512];
   char global[512];
   char synced[512];
   char id[512];
   char library[512];
   char application_support[512];
   char mac_cwiki[512];
   char mac_global[512];
   struct cwiki_config_sources sources = {0};
   struct cwiki_config_files_error error = {0};
   int root_fd = mkstemp(root);

   assert(root_fd >= 0 && close(root_fd) == 0 && unlink(root) == 0);
   make_directory(root);
   path(home, sizeof(home), root, "home");
   path(xdg, sizeof(xdg), root, "xdg");
   path(vault, sizeof(vault), root, "vault");
   make_directory(home);
   make_directory(xdg);
   make_directory(vault);
   path(cwiki, sizeof(cwiki), xdg, "cwiki");
   path(vaults, sizeof(vaults), cwiki, "vaults");
   make_directory(cwiki);
   make_directory(vaults);
   path(global, sizeof(global), cwiki, "config.yaml");
   write_file(global, "keymaps: {}\n");
   path(marker, sizeof(marker), vault, ".cwiki");
   make_directory(marker);
   path(synced, sizeof(synced), marker, "config.yaml");
   write_file(synced, "clue-groups: {}\n");
   path(id, sizeof(id), marker, "vault-id");
   write_file(id, "123e4567-e89b-42d3-a456-426614174000\n");
   path(local, sizeof(local), vaults,
       "123e4567-e89b-42d3-a456-426614174000.yaml");
   write_file(local, "{}\n");
   path(nested, sizeof(nested), vault, "nested");
   make_directory(nested);
   path(note, sizeof(note), nested, "note.md");

   assert(cwiki_config_sources_discover(&sources, note, home, xdg, false,
       &error) == CWIKI_CONFIG_FILES_OK);
   assert(sources.count == 3U);
   expect_source(&sources.items[0], CWIKI_CONFIG_GLOBAL,
       "/xdg/cwiki/config.yaml", "keymaps: {}\n");
   expect_source(&sources.items[1], CWIKI_CONFIG_VAULT,
       "/vault/.cwiki/config.yaml", "clue-groups: {}\n");
   expect_source(&sources.items[2], CWIKI_CONFIG_MACHINE_LOCAL,
       "/xdg/cwiki/vaults/123e4567-e89b-42d3-a456-426614174000.yaml",
       "{}\n");
   assert(entry_count(marker) == 2U);
   cwiki_config_sources_free(&sources);

   assert(unlink(local) == 0 && unlink(id) == 0);
   assert(cwiki_config_sources_discover(&sources, note, home, xdg, false,
       &error) == CWIKI_CONFIG_FILES_OK && sources.count == 2U);
   assert(entry_count(marker) == 1U);
   cwiki_config_sources_free(&sources);

   write_file(id, "123E4567-e89b-42d3-a456-426614174000\n");
   assert(cwiki_config_sources_discover(&sources, note, home, xdg, false,
       &error) == CWIKI_CONFIG_FILES_INVALID_VAULT_ID);
   assert(error.path != NULL && strstr(error.path,
       "/vault/.cwiki/vault-id") != NULL && error.system_errno == EINVAL);
   cwiki_config_files_error_free(&error);

   assert(unlink(id) == 0 && unlink(synced) == 0 && rmdir(marker) == 0);
   assert(cwiki_config_sources_discover(&sources, note, home, "relative", false,
       &error) == CWIKI_CONFIG_FILES_OK && sources.count == 0U);
   assert(entry_count(home) == 0U);
   cwiki_config_sources_free(&sources);

   path(library, sizeof(library), home, "Library");
   path(application_support, sizeof(application_support), library,
       "Application Support");
   path(mac_cwiki, sizeof(mac_cwiki), application_support, "cwiki");
   path(mac_global, sizeof(mac_global), mac_cwiki, "config.yaml");
   make_directory(library);
   make_directory(application_support);
   make_directory(mac_cwiki);
   write_file(mac_global, "{}\n");
   assert(cwiki_config_sources_discover(&sources, note, home, xdg, true,
       &error) == CWIKI_CONFIG_FILES_OK && sources.count == 1U);
   expect_source(&sources.items[0], CWIKI_CONFIG_GLOBAL,
       "/Library/Application Support/cwiki/config.yaml", "{}\n");
   cwiki_config_sources_free(&sources);
   assert(unlink(mac_global) == 0 && rmdir(mac_cwiki) == 0 &&
       rmdir(application_support) == 0 && rmdir(library) == 0);

   {
      int oversized = open(global, O_WRONLY);

      assert(oversized >= 0 && ftruncate(oversized, 1024 * 1024 + 1) == 0 &&
          close(oversized) == 0);
   }
   assert(cwiki_config_sources_discover(&sources, note, home, xdg, false,
       &error) == CWIKI_CONFIG_FILES_TOO_LARGE);
   assert(error.path != NULL && strstr(error.path, "/xdg/cwiki/config.yaml") !=
       NULL && error.system_errno == EFBIG);
   cwiki_config_files_error_free(&error);

   (void)printf("config files: ok (%s)\n", uuid);
   assert(unlink(global) == 0);
   assert(rmdir(nested) == 0 && rmdir(vault) == 0 && rmdir(vaults) == 0 &&
       rmdir(cwiki) == 0 && rmdir(xdg) == 0 && rmdir(home) == 0 &&
       rmdir(root) == 0);
   return EXIT_SUCCESS;
}
