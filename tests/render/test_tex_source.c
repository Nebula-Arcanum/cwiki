/*
 * Tests for src/tex_source.h (delivery d-20261012-251ff8, plan step 6):
 * AC16, AC17, AC20-AC23, AC28, AC34, AC35.
 */
#include "tex_source.h"

#include "fixture_vault.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static bool
has_line(const struct cwiki_tex_source *s, const char *text,
    enum cwiki_tex_origin origin, size_t source_line, size_t *at)
{
   size_t i;
   size_t pos = 0U;
   size_t len = strlen(text);

   for (i = 0U; i < s->line_count; i++) {
      const char *nl = strchr(s->text + pos, '\n');
      size_t n = (size_t)(nl - (s->text + pos));

      if (n == len && memcmp(s->text + pos, text, len) == 0 &&
          s->lines[i].origin == origin &&
          (origin == CWIKI_TEX_ORIGIN_TEMPLATE ||
          s->lines[i].source_line == source_line)) {
         if (at != NULL) {
            *at = i;
         }
         return true;
      }
      pos += n + 1U;
   }
   return false;
}

static bool
same_lines(const struct cwiki_tex_source *a, const struct cwiki_tex_source *b)
{
   size_t i;

   for (i = 0U; i < a->line_count; i++) {
      if (a->lines[i].origin != b->lines[i].origin ||
          a->lines[i].source_line != b->lines[i].source_line) {
         return false;
      }
   }
   return true;
}

static void
test_fixture(void)
{
   struct fixture_vault vault;
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_buffer note;
   struct cwiki_config_note_tex tex;
   struct cwiki_config_error error;
   struct cwiki_tex_extraction x;
   struct cwiki_tex_source a, b;
   char *pre = NULL;
   size_t pre_len = 0U;
   char message[256];
   char *path;
   FILE *f;
   char *bytes = malloc(4096U);
   size_t got;
   size_t count;
   uint64_t top;
   const struct cwiki_zone_region *regions;
   size_t chosen[2] = {5U, 1U};
   size_t at;
   size_t i;

   check(fixture_vault_create(&vault) == 0, "create root");
   check(fixture_vault_copy_in(&vault, "tests/fixtures/vaults/m2-render") == 0,
       "copy fixture");
   check(fixture_vault_resolve(&vault, "class-note.md", &path) == 0, "resolve");
   f = fopen(path, "rb");
   check(f != NULL && bytes != NULL, "open note");
   got = fread(bytes, 1U, 4096U, f);
   (void)fclose(f);
   free(path);
   check(cwiki_buffer_init(&note) == 0 &&
       cwiki_buffer_load(&note, bytes, got) == 0, "load note");
   memset(&tex, 0, sizeof(tex));
   check(cwiki_config_parse_note_tex(&note, &tex, &error) == CWIKI_CONFIG_OK,
       "AC19 inputs parse");
   regions = cwiki_zone_builtin_regions(&count, &top);
   check(cwiki_zone_engine_init(&engine, regions, count, top) == 0, "engine");
   check(cwiki_tex_extract(engine, &note, tex.body_first_line, NULL, &x) == 0,
       "extract");
   check(x.block_count == 6U, "six blocks");

   check(cwiki_tex_vault_preamble_read(fixture_vault_root(&vault), &pre,
       &pre_len, message, sizeof(message)) == 0 && pre != NULL,
       "AC17 vault preamble read");
   check(cwiki_tex_generate(&note, &tex, pre, pre_len, &x, chosen, 2U, &a) == 0,
       "generate subset");
   check(cwiki_tex_generate(&note, &tex, pre, pre_len, &x, chosen, 2U, &b) == 0,
       "generate again");
   check(a.length == b.length && memcmp(a.text, b.text, a.length) == 0 &&
       a.line_count == b.line_count &&
       same_lines(&a, &b),
       "AC23 byte-identical twice");
   check(a.page_count == 2U && a.pages[0] == 5U && a.pages[1] == 1U,
       "AC20 page map in caller order");
   check(has_line(&a, "\\usepackage{amsmath}", CWIKI_TEX_ORIGIN_VAULT_PREAMBLE,
       0U, NULL) && has_line(&a, "\\usepackage{tikz}",
       CWIKI_TEX_ORIGIN_VAULT_PREAMBLE, 2U, NULL),
       "AC17/AC22 preamble lines verbatim and tagged");
   check(has_line(&a, "$$\\undefinedfixturemacro{x}$$", CWIKI_TEX_ORIGIN_BLOCK,
       22U, &at), "AC21/AC22 failing block maps to note line 23");
   check(has_line(&a, "$$\\int_0^1 x^2 \\, dx = \\frac{1}{3}$$",
       CWIKI_TEX_ORIGIN_BLOCK, 4U, &i) && i > at,
       "AC20 order: display 23 then display 5");
   for (i = 0U; i < a.line_count; i++) {
      if (a.lines[i].origin != CWIKI_TEX_ORIGIN_BLOCK) {
         continue;
      }
      check(a.lines[i].source_line == 22U || a.lines[i].source_line == 4U,
          "AC22 only chosen note lines are mapped");
   }
   check(strncmp(a.text, "\\documentclass[varwidth,multi=cwikiblock]"
       "{standalone}\n", 54U) == 0 &&
       strstr(a.text, "\\begin{cwikiblock}\n$$") != NULL,
       "AC21 blocks inside the varwidth box");
   cwiki_tex_source_free(&a);
   cwiki_tex_source_free(&b);

   x.blocks[0].compilable = false;
   chosen[0] = 0U;
   check(cwiki_tex_generate(&note, &tex, pre, pre_len, &x, chosen, 1U, &a) != 0,
       "non-compilable block refused");
   chosen[0] = 99U;
   check(cwiki_tex_generate(&note, &tex, pre, pre_len, &x, chosen, 1U, &a) != 0,
       "bad index refused");

   cwiki_tex_extraction_free(&x);
   cwiki_zone_engine_free(engine);
   cwiki_buffer_free(&note);
   cwiki_config_note_tex_free(&tex);
   free(pre);
   free(bytes);
   (void)fixture_vault_destroy(&vault);
}

static void
test_inputs(void)
{
   struct cwiki_buffer note;
   struct cwiki_config_note_tex tex;
   struct cwiki_config_error error;
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_tex_extraction x;
   struct cwiki_tex_source s;
   size_t count;
   uint64_t top;
   const struct cwiki_zone_region *regions = cwiki_zone_builtin_regions(&count,
       &top);
   const char *text = "---\npackages: [tikz-cd, siunitx]\npreamble: |\n"
       "  \\newcommand{\\R}{\\mathbb{R}}\n  \\def\\x{1}\n---\nA $x$ and \\ce{H2}\n";
   size_t chosen[2] = {0U, 1U};
   size_t a = 0U, b = 0U, c = 0U, d = 0U, e = 0U;

   check(cwiki_zone_engine_init(&engine, regions, count, top) == 0, "engine");
   check(cwiki_buffer_init(&note) == 0 &&
       cwiki_buffer_load(&note, text, strlen(text)) == 0, "load");
   memset(&tex, 0, sizeof(tex));
   check(cwiki_config_parse_note_tex(&note, &tex, &error) == CWIKI_CONFIG_OK,
       "parse");
   check(cwiki_tex_extract(engine, &note, tex.body_first_line, NULL, &x) == 0 &&
       x.block_count == 2U, "two blocks");
   check(cwiki_tex_generate(&note, &tex, "\\usepackage{amsmath}\n", 20U, &x,
       chosen, 2U, &s) == 0, "generate");
   check(has_line(&s, "\\usepackage{amsmath}", CWIKI_TEX_ORIGIN_VAULT_PREAMBLE,
       0U, &a) && has_line(&s, "\\usepackage{tikz-cd}",
       CWIKI_TEX_ORIGIN_PACKAGES, 0U, &b) && has_line(&s,
       "\\usepackage{siunitx}", CWIKI_TEX_ORIGIN_PACKAGES, 1U, &c) &&
       has_line(&s, "\\newcommand{\\R}{\\mathbb{R}}",
       CWIKI_TEX_ORIGIN_NOTE_PREAMBLE, 0U, &d) && has_line(&s,
       "\\def\\x{1}", CWIKI_TEX_ORIGIN_NOTE_PREAMBLE, 1U, &e),
       "AC16 packages and preamble lines");
   check(a < b && b < c && c < d && d < e, "AC35 vault, packages, preamble order");
   check(has_line(&s, "$x$", CWIKI_TEX_ORIGIN_BLOCK, 6U, NULL) &&
       has_line(&s, "\\ce{H2}", CWIKI_TEX_ORIGIN_BLOCK, 6U, NULL),
       "AC21/AC22 inline and chemistry on note line 7 (frontmatter offset)");
   check(strstr(s.text, "\\[") == NULL && strstr(s.text, "\\begin{equation") == NULL,
       "AC21 no display construct added around inline blocks");
   cwiki_tex_source_free(&s);
   check(cwiki_tex_generate(&note, &tex, NULL, 0U, &x, chosen, 2U, &s) == 0 &&
       !has_line(&s, "\\usepackage{amsmath}", CWIKI_TEX_ORIGIN_VAULT_PREAMBLE,
       0U, NULL), "AC34 no vault preamble contributes nothing");
   cwiki_tex_source_free(&s);
   cwiki_tex_extraction_free(&x);
   cwiki_zone_engine_free(engine);
   cwiki_buffer_free(&note);
   cwiki_config_note_tex_free(&tex);

   /* AC28: engine selection does not change the generated source. */
   check(cwiki_buffer_init(&note) == 0 &&
       cwiki_buffer_load(&note, "---\nengine: pdflatex\n---\n$x$\n", 28U) == 0 &&
       cwiki_config_parse_note_tex(&note, &tex, &error) == CWIKI_CONFIG_OK &&
       tex.engine == CWIKI_TEX_ENGINE_PDFLATEX, "AC28 pdflatex parsed");
   cwiki_config_note_tex_free(&tex);
   cwiki_buffer_free(&note);
   check(cwiki_buffer_init(&note) == 0 &&
       cwiki_buffer_load(&note, "---\nengine: nope\n---\n", 21U) == 0 &&
       cwiki_config_parse_note_tex(&note, &tex, &error) !=
       CWIKI_CONFIG_OK, "invalid frontmatter reports an error (no source)");
   cwiki_config_note_tex_free(&tex);
   cwiki_buffer_free(&note);
}

static void
test_vault_preamble(void)
{
   struct fixture_vault vault;
   char message[256];
   char *bytes = NULL;
   size_t length = 0U;
   char *path;
   FILE *f;
   size_t i;
   char block[1024];

   check(fixture_vault_create(&vault) == 0, "create root");
   check(cwiki_tex_vault_preamble_read(fixture_vault_root(&vault), &bytes,
       &length, message, sizeof(message)) == 0 && bytes == NULL,
       "AC34 missing preamble is not an error");

   {
      char dir[4096];

      (void)snprintf(dir, sizeof(dir), "%s/.cwiki", fixture_vault_root(&vault));
      check(mkdir(dir, 0700) == 0, "mkdir .cwiki");
   }
   check(fixture_vault_resolve(&vault, ".cwiki", &path) == 0, "resolve dir");
   {
      char file[4096];

      (void)snprintf(file, sizeof(file), "%s/preamble.tex", path);
      free(path);
      memset(block, '%', sizeof(block));
      f = fopen(file, "wb");
      check(f != NULL, "create preamble");
      for (i = 0U; i < 1024U; i++) {
         (void)fwrite(block, 1U, sizeof(block), f);
      }
      (void)fputc('%', f);
      (void)fclose(f);
      check(cwiki_tex_vault_preamble_read(fixture_vault_root(&vault), &bytes,
          &length, message, sizeof(message)) != 0 && bytes == NULL &&
          strstr(message, "preamble.tex") != NULL,
          "AC34 one byte over 1 MiB is unreadable, path named");
      f = fopen(file, "wb");
      for (i = 0U; i < 1024U; i++) {
         (void)fwrite(block, 1U, sizeof(block), f);
      }
      (void)fclose(f);
      check(cwiki_tex_vault_preamble_read(fixture_vault_root(&vault), &bytes,
          &length, message, sizeof(message)) == 0 && length == 1048576U,
          "exactly 1 MiB is accepted");
      free(bytes);
      bytes = NULL;
      if (geteuid() == 0) {
         (void)fprintf(stderr, "skip: unreadable-file case needs non-root\n");
      } else {
         check(chmod(file, 0) == 0, "chmod 000");
         check(cwiki_tex_vault_preamble_read(fixture_vault_root(&vault),
             &bytes, &length, message, sizeof(message)) != 0 && bytes == NULL &&
             strstr(message, "preamble.tex") != NULL,
             "AC34 unreadable preamble reported with path");
         (void)chmod(file, 0600);
      }
   }
   (void)fixture_vault_destroy(&vault);
}

static void
test_refuses_withheld_blocks(void)
{
   /* AC14/AC15: bytes of non-compilable blocks never reach generated TeX. */
   struct cwiki_buffer note;
   struct cwiki_config_note_tex tex;
   struct cwiki_config_error error;
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_tex_extraction x;
   struct cwiki_tex_source s;
   struct cwiki_tex_env_set *env = NULL;
   size_t count;
   uint64_t top;
   const struct cwiki_zone_region *regions = cwiki_zone_builtin_regions(&count,
       &top);
   const char *text = "$ok$\n\n\\begin{zzzunknown}\nUNKNOWNBYTES\n"
       "\\end{zzzunknown}\n\n$$\n% cwiki: norender\nSUPPRESSEDBYTES\n$$\n\n"
       "$$\nUNTERMINATEDBYTES\n";
   size_t chosen[1];
   size_t i;

   check(cwiki_zone_engine_init(&engine, regions, count, top) == 0, "engine");
   check(cwiki_buffer_init(&note) == 0 &&
       cwiki_buffer_load(&note, text, strlen(text)) == 0, "load");
   memset(&tex, 0, sizeof(tex));
   check(cwiki_config_parse_note_tex(&note, &tex, &error) == CWIKI_CONFIG_OK,
       "parse");
   check(cwiki_tex_env_build(&env, NULL, 0U, NULL, 0U, NULL, 0U, NULL, 0U) == 0,
       "env");
   check(cwiki_tex_extract(engine, &note, tex.body_first_line, env, &x) == 0 &&
       x.block_count == 4U, "four blocks");
   check(x.blocks[0].compilable && !x.blocks[1].compilable &&
       !x.blocks[2].compilable && !x.blocks[3].compilable,
       "only the first block is compilable");
   for (i = 1U; i < x.block_count; i++) {
      chosen[0] = i;
      memset(&s, 0, sizeof(s));
      check(cwiki_tex_generate(&note, &tex, NULL, 0U, &x, chosen, 1U, &s) != 0,
          "AC14/AC15 non-compilable block refused");
      cwiki_tex_source_free(&s);
   }
   chosen[0] = 0U;
   check(cwiki_tex_generate(&note, &tex, NULL, 0U, &x, chosen, 1U, &s) == 0 &&
       strstr(s.text, "UNKNOWNBYTES") == NULL &&
       strstr(s.text, "SUPPRESSEDBYTES") == NULL &&
       strstr(s.text, "UNTERMINATEDBYTES") == NULL,
       "AC14/AC15 withheld bytes absent from generated TeX");
   cwiki_tex_source_free(&s);
   cwiki_tex_extraction_free(&x);
   cwiki_tex_env_free(env);
   cwiki_zone_engine_free(engine);
   cwiki_buffer_free(&note);
   cwiki_config_note_tex_free(&tex);
}

int
main(void)
{
   test_fixture();
   test_inputs();
   test_refuses_withheld_blocks();
   test_vault_preamble();
   if (failures != 0) {
      (void)fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   return 0;
}
