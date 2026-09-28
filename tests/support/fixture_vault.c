#include "fixture_vault.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int remove_tree(const char *path);

static int
has_parent_component(const char *path)
{
   const char *component = path;

   while (*component != '\0') {
      const char *end = strchr(component, '/');
      size_t length = end == NULL ? strlen(component) : (size_t)(end - component);

      if (length == 2U && component[0] == '.' && component[1] == '.') {
         return 1;
      }
      if (end == NULL) {
         break;
      }
      component = end + 1;
   }
   return 0;
}

int
fixture_vault_create(struct fixture_vault *vault)
{
   const char *temporary = getenv("TMPDIR");
   const char suffix[] = "/cwiki-fixture-XXXXXX";
   char *template;
   char *root;
   int descriptor;
   size_t temporary_length;

   if (vault == NULL) {
      errno = EINVAL;
      return -1;
   }
   vault->root = NULL;
   if (temporary == NULL || temporary[0] == '\0') {
      temporary = "/tmp";
   }
   temporary_length = strlen(temporary);
   if (temporary_length > SIZE_MAX - sizeof(suffix)) {
      errno = ENAMETOOLONG;
      return -1;
   }
   template = malloc(temporary_length + sizeof(suffix));
   if (template == NULL) {
      return -1;
   }
   (void)snprintf(template, temporary_length + sizeof(suffix), "%s%s",
       temporary, suffix);
   descriptor = mkstemp(template);
   if (descriptor == -1) {
      free(template);
      return -1;
   }
   if (close(descriptor) != 0 || unlink(template) != 0 ||
       mkdir(template, 0700) != 0) {
      int saved_errno = errno;

      (void)unlink(template);
      free(template);
      errno = saved_errno;
      return -1;
   }
   root = realpath(template, NULL);
   if (root == NULL) {
      int saved_errno = errno;

      (void)rmdir(template);
      free(template);
      errno = saved_errno;
      return -1;
   }
   free(template);
   vault->root = root;
   return 0;
}

int
fixture_vault_destroy(struct fixture_vault *vault)
{
   int result;

   if (vault == NULL || vault->root == NULL) {
      errno = EINVAL;
      return -1;
   }
   result = remove_tree(vault->root);
   free(vault->root);
   vault->root = NULL;
   return result;
}

const char *
fixture_vault_root(const struct fixture_vault *vault)
{
   return vault == NULL ? NULL : vault->root;
}

int
fixture_vault_resolve(const struct fixture_vault *vault,
    const char *relative_path, char **resolved_path)
{
   char *candidate;
   char *resolved;
   size_t root_length;
   size_t relative_length;

   if (vault == NULL || vault->root == NULL || relative_path == NULL ||
       resolved_path == NULL || relative_path[0] == '\0' ||
       relative_path[0] == '/' || has_parent_component(relative_path)) {
      errno = EINVAL;
      return -1;
   }
   *resolved_path = NULL;
   root_length = strlen(vault->root);
   relative_length = strlen(relative_path);
   if (root_length > SIZE_MAX - relative_length - 2U) {
      errno = ENAMETOOLONG;
      return -1;
   }
   candidate = malloc(root_length + relative_length + 2U);
   if (candidate == NULL) {
      return -1;
   }
   (void)snprintf(candidate, root_length + relative_length + 2U, "%s/%s",
       vault->root, relative_path);
   resolved = realpath(candidate, NULL);
   free(candidate);
   if (resolved == NULL) {
      return -1;
   }
   if (strncmp(resolved, vault->root, root_length) != 0 ||
       (resolved[root_length] != '\0' && resolved[root_length] != '/')) {
      free(resolved);
      errno = EPERM;
      return -1;
   }
   *resolved_path = resolved;
   return 0;
}

static int
remove_tree(const char *path)
{
   DIR *directory;
   struct dirent *entry;
   int result = 0;

   directory = opendir(path);
   if (directory == NULL) {
      return -1;
   }
   while ((entry = readdir(directory)) != NULL) {
      char *child;
      struct stat status;
      size_t path_length;
      size_t name_length;

      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
         continue;
      }
      path_length = strlen(path);
      name_length = strlen(entry->d_name);
      if (path_length > SIZE_MAX - name_length - 2U) {
         errno = ENAMETOOLONG;
         result = -1;
         break;
      }
      child = malloc(path_length + name_length + 2U);
      if (child == NULL) {
         result = -1;
         break;
      }
      (void)snprintf(child, path_length + name_length + 2U, "%s/%s", path,
          entry->d_name);
      if (lstat(child, &status) != 0 ||
          (S_ISDIR(status.st_mode) ? remove_tree(child) : unlink(child)) != 0) {
         free(child);
         result = -1;
         break;
      }
      free(child);
   }
   if (closedir(directory) != 0) {
      result = -1;
   }
   if (result == 0 && rmdir(path) != 0) {
      result = -1;
   }
   return result;
}
