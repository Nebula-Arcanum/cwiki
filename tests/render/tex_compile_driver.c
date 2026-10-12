/* Test-only driver (AC24): copies a fixture vault into a disposable root,
 * extracts and generates every block of class-note.md, writes class-note.tex
 * and class-note.lines ("genline origin noteline", 1-based) into the root,
 * runs SCRIPT ROOT, then removes the root. Usage: driver FIXTURE_DIR SCRIPT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "buffer.h"
#include "config.h"
#include "fixture_vault.h"
#include "tex_block.h"
#include "tex_env.h"
#include "tex_source.h"
#include "zone.h"

static const char *const origin_name[] = {
   "template", "vault-preamble", "packages", "note-preamble", "block"
};

static int
write_files(const char *root)
{
   struct cwiki_buffer note;
   struct cwiki_config_note_tex tex;
   struct cwiki_config_error error;
   struct cwiki_zone_engine *engine = NULL;
   const struct cwiki_zone_region *regions;
   struct cwiki_tex_extraction x;
   struct cwiki_tex_env_set *env = NULL;
   struct cwiki_tex_source src;
   char path[4096], message[256], *bytes, *pre = NULL;
   size_t count, pre_len = 0U, got, i, *chosen = NULL, n = 0U;
   uint64_t top;
   FILE *f;
   int rc = 1;

   bytes = malloc(1U << 16);
   (void)snprintf(path, sizeof path, "%s/class-note.md", root);
   f = fopen(path, "rb");
   if (bytes == NULL || f == NULL) {
      return 1;
   }
   got = fread(bytes, 1U, 1U << 16, f);
   (void)fclose(f);
   memset(&tex, 0, sizeof tex);
   if (cwiki_buffer_init(&note) != 0 ||
       cwiki_buffer_load(&note, bytes, got) != 0 ||
       cwiki_config_parse_note_tex(&note, &tex, &error) != CWIKI_CONFIG_OK ||
       cwiki_tex_vault_preamble_read(root, &pre, &pre_len, message,
           sizeof message) != 0) {
      return 1;
   }
   regions = cwiki_zone_builtin_regions(&count, &top);
   if (cwiki_zone_engine_init(&engine, regions, count, top) != 0 ||
       cwiki_tex_env_build(&env, pre, pre_len, tex.preamble,
           tex.preamble_length, (const char *const *)tex.packages,
           tex.package_count, NULL, 0U) != 0 ||
       cwiki_tex_extract(engine, &note, tex.body_first_line, env, &x) != 0) {
      return 1;
   }
   chosen = malloc((x.block_count + 1U) * sizeof *chosen);
   for (i = 0U; chosen != NULL && i < x.block_count; i++) {
      if (x.blocks[i].compilable) {
         chosen[n++] = i;
      }
   }
   if (chosen != NULL &&
       cwiki_tex_generate(&note, &tex, pre, pre_len, &x, chosen, n, &src) == 0) {
      (void)snprintf(path, sizeof path, "%s/class-note.tex", root);
      f = fopen(path, "wb");
      if (f != NULL) {
         rc = fwrite(src.text, 1U, src.length, f) == src.length ? 0 : 1;
         rc |= fclose(f) != 0;
      }
      (void)snprintf(path, sizeof path, "%s/class-note.lines", root);
      f = fopen(path, "wb");
      if (f != NULL && rc == 0) {
         for (i = 0U; i < src.line_count; i++) {
            (void)fprintf(f, "%zu %s %zu\n", i + 1U,
                origin_name[src.lines[i].origin], src.lines[i].source_line + 1U);
         }
         rc |= fclose(f) != 0;
      }
      cwiki_tex_source_free(&src);
   }
   free(chosen);
   cwiki_tex_extraction_free(&x);
   cwiki_tex_env_free(env);
   cwiki_zone_engine_free(engine);
   cwiki_config_note_tex_free(&tex);
   cwiki_buffer_free(&note);
   free(pre);
   free(bytes);
   return rc;
}

int
main(int argc, char **argv)
{
   struct fixture_vault vault;
   int status = 1, wstatus;
   pid_t pid;

   if (argc != 3) {
      fprintf(stderr, "usage: %s FIXTURE_DIR SCRIPT\n", argv[0]);
      return 1;
   }
   if (fixture_vault_create(&vault) != 0) {
      perror("fixture_vault_create");
      return 1;
   }
   if (fixture_vault_copy_in(&vault, argv[1]) == 0 &&
       write_files(fixture_vault_root(&vault)) == 0) {
      (void)fflush(stdout);
      pid = fork();
      if (pid == 0) {
         execl("/bin/sh", "sh", argv[2], fixture_vault_root(&vault),
             (char *)NULL);
         _exit(127);
      }
      if (pid > 0 && waitpid(pid, &wstatus, 0) >= 0 && WIFEXITED(wstatus)) {
         status = WEXITSTATUS(wstatus);
      }
   } else {
      fprintf(stderr, "driver setup failed\n");
   }
   if (fixture_vault_destroy(&vault) != 0) {
      perror("fixture_vault_destroy");
      return 1;
   }
   return status;
}
