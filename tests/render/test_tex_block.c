/*
 * Tests for src/tex_block.h (delivery d-20261012-251ff8, plan steps 4-5):
 * AC1-AC8, AC10-AC15, AC26, AC29-AC31. Line numbers in checks are 1-based note lines; the
 * API reports 0-based indices.
 */
#include "tex_block.h"

#include "config.h"
#include "fixture_vault.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

static struct cwiki_zone_engine *
builtin_engine(void)
{
   struct cwiki_zone_engine *engine = NULL;
   size_t count;
   uint64_t top_level;
   const struct cwiki_zone_region *regions =
       cwiki_zone_builtin_regions(&count, &top_level);

   check(cwiki_zone_engine_init(&engine, regions, count, top_level) == 0,
       "builtin zone engine compiles");
   return engine;
}

static void
load(struct cwiki_buffer *note, const char *text, size_t length)
{
   check(cwiki_buffer_init(note) == 0, "buffer init");
   check(cwiki_buffer_load(note, text, length) == 0, "buffer load");
}

static void
extract_text(struct cwiki_zone_engine *engine, const char *text,
    size_t body_first, struct cwiki_buffer *note,
    struct cwiki_tex_extraction *out)
{
   load(note, text, strlen(text));
   check(cwiki_tex_extract(engine, note, body_first, NULL, out) == 0,
       "extraction succeeds");
}

static bool
block_is(const struct cwiki_buffer *note, const struct cwiki_tex_extraction *x,
    size_t index, enum cwiki_tex_block_kind kind, size_t first, size_t last,
    const char *bytes)
{
   char *got = NULL;
   size_t length = 0U;
   bool same;

   if (index >= x->block_count) {
      return false;
   }
   if (cwiki_tex_block_bytes(note, &x->blocks[index], &got, &length) != 0) {
      return false;
   }
   same = x->blocks[index].kind == kind &&
       x->blocks[index].first_line + 1U == first &&
       x->blocks[index].last_line + 1U == last && length == strlen(bytes) &&
       memcmp(got, bytes, length) == 0;
   free(got);
   return same;
}

static void
test_math_and_chemistry(void)
{
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer note;
   struct cwiki_tex_extraction x;

   extract_text(engine,
       "Say $a+b$ and \\(c\\) here.\n"
       "$$x$$\n"
       "\\[\ny\n\\]\n"
       "$$\nz\n$$\n"
       "Water \\ce{H2O} and $\\ce{CO2} \\text{hi} \\label{q}$ done\n",
       0U, &note, &x);
   check(x.block_count == 7U, "AC1-AC3 seven blocks found");
   check(block_is(&note, &x, 0U, CWIKI_TEX_BLOCK_INLINE, 1U, 1U, "$a+b$"),
       "AC1 dollar inline exact bytes");
   check(block_is(&note, &x, 1U, CWIKI_TEX_BLOCK_INLINE, 1U, 1U, "\\(c\\)"),
       "AC1 paren inline exact bytes");
   check(block_is(&note, &x, 2U, CWIKI_TEX_BLOCK_DISPLAY, 2U, 2U, "$$x$$"),
       "AC2 one-line display dollars");
   check(block_is(&note, &x, 3U, CWIKI_TEX_BLOCK_DISPLAY, 3U, 5U,
       "\\[\ny\n\\]"), "AC2 multi-line display brackets");
   check(block_is(&note, &x, 4U, CWIKI_TEX_BLOCK_DISPLAY, 6U, 8U,
       "$$\nz\n$$"), "AC2 multi-line display dollars");
   check(block_is(&note, &x, 5U, CWIKI_TEX_BLOCK_CHEMISTRY, 9U, 9U,
       "\\ce{H2O}"), "AC3 chemistry in prose is a block");
   check(block_is(&note, &x, 6U, CWIKI_TEX_BLOCK_INLINE, 9U, 9U,
       "$\\ce{CO2} \\text{hi} \\label{q}$"),
       "AC3 ce/text/label inside math are not separate blocks");
   check(x.report_count == 0U, "clean blocks produce no reports");
   check(x.blocks[0].compilable && x.blocks[6].compilable,
       "terminated trusted blocks are compilable");
   cwiki_tex_extraction_free(&x);
   cwiki_buffer_free(&note);
   cwiki_zone_engine_free(engine);
}

static void
test_environments(void)
{
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer note;
   struct cwiki_tex_extraction x;

   extract_text(engine,
       "\\begin{tikzpicture}[scale=1.5]\n  \\draw (0,0);\n\\end{tikzpicture}\n"
       "\\begin{align}\n a &= $b$ \\\\\n \\begin{cases} 1 \\end{cases}\n"
       "\\end{align}\n",
       0U, &note, &x);
   check(x.block_count == 2U, "AC4 two outermost environments");
   check(block_is(&note, &x, 0U, CWIKI_TEX_BLOCK_ENVIRONMENT, 1U, 3U,
       "\\begin{tikzpicture}[scale=1.5]\n  \\draw (0,0);\n"
       "\\end{tikzpicture}") && x.blocks[0].env != NULL &&
       strcmp(x.blocks[0].env, "tikzpicture") == 0,
       "AC4 tikzpicture bytes include options and name");
   check(block_is(&note, &x, 1U, CWIKI_TEX_BLOCK_ENVIRONMENT, 4U, 7U,
       "\\begin{align}\n a &= $b$ \\\\\n \\begin{cases} 1 \\end{cases}\n"
       "\\end{align}") && x.blocks[1].env != NULL &&
       strcmp(x.blocks[1].env, "align") == 0,
       "AC3/AC4 nested math and environment stay inside align");
   check(x.report_count == 0U, "environments produce no reports");
   cwiki_tex_extraction_free(&x);
   cwiki_buffer_free(&note);
   cwiki_zone_engine_free(engine);
}

static void
test_frontmatter_offset(void)
{
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer note;
   struct cwiki_tex_extraction x;

   extract_text(engine,
       "---\npreamble: |\n  $ \\begin{x}\n---\nText $q$ end\n", 4U, &note, &x);
   check(x.block_count == 1U, "AC7 nothing yielded from frontmatter");
   check(block_is(&note, &x, 0U, CWIKI_TEX_BLOCK_INLINE, 5U, 5U, "$q$"),
       "AC7 body block keeps its note line number");
   check(x.report_count == 0U, "AC7 stray frontmatter delimiters not reported");
   cwiki_tex_extraction_free(&x);
   cwiki_buffer_free(&note);
   extract_text(engine, "---\n$$\n---\n", 5U, &note, &x);
   check(x.block_count == 0U, "body start past the end yields nothing");
   cwiki_tex_extraction_free(&x);
   cwiki_buffer_free(&note);
   cwiki_zone_engine_free(engine);
}

static void
test_unterminated(void)
{
   static const char *const cases[] = {
      "a\n$$\nx\n", "a\n\\[\nx\n", "a\n\\begin{foo}\nx\n", "a\n$ x\ny\n",
      "a\n\\( x\ny\n", "a\n\\ce{H2\nO\n"
   };
   size_t i;

   for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
      struct cwiki_zone_engine *engine = builtin_engine();
      struct cwiki_buffer note;
      struct cwiki_tex_extraction x;

      extract_text(engine, cases[i], 0U, &note, &x);
      check(x.block_count == 1U && !x.blocks[0].compilable &&
          x.report_count == 1U &&
          x.reports[0].reason == CWIKI_TEX_REPORT_UNTERMINATED &&
          x.reports[0].line == 1U,
          "AC31 unterminated block reported at its start line");
      if (x.block_count == 1U) {
         check(x.blocks[0].last_line == note.line_count - 1U,
             "unterminated block runs to the end of the note");
      }
      cwiki_tex_extraction_free(&x);
      cwiki_buffer_free(&note);
      cwiki_zone_engine_free(engine);
   }
}

static void
test_untrusted(void)
{
   /* A pathological custom region inside display math exhausts a tiny match
    * limit on line 2, degrading that line (as tests/zone/test_zone.c does). */
   static const struct cwiki_zone_region regions[] = {
      {CWIKI_ZONE_MATH_DISPLAY, "display", "\\$\\$", "\\$\\$", NULL,
          2U, 0U, 0U, 0U, false},
      {CWIKI_ZONE_CUSTOM, "match-bound", "(*NO_AUTO_POSSESS)^(a+)+$", ">",
          NULL, 0U, 0U, 0U, 0U, false}
   };
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_buffer note;
   struct cwiki_tex_extraction x;

   check(cwiki_zone_engine_init_with_limits(&engine, regions, 2U, 1U, 50U,
       1000U) == 0, "limited zone engine compiles");
   if (engine == NULL) {
      return;
   }
   extract_text(engine,
       "$$\n"
       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaX\n"
       "$$\n", 0U, &note, &x);
   check(x.block_count == 1U && !x.blocks[0].compilable &&
       x.report_count == 1U &&
       x.reports[0].reason == CWIKI_TEX_REPORT_UNTRUSTED &&
       x.reports[0].line == 0U,
       "AC31 block touching a degraded line is untrusted and reported");
   cwiki_tex_extraction_free(&x);
   cwiki_buffer_free(&note);
   cwiki_zone_engine_free(engine);
}

static void
test_untrusted_single_line(void)
{
   /* A block that opens and closes on one degraded line leaves no spans and
    * an empty end stack; it must still be reported, never silently dropped. */
   static const struct cwiki_zone_region regions[] = {
      {CWIKI_ZONE_MATH_DISPLAY, "display", "\\$\\$", "\\$\\$", NULL,
          2U, 0U, 0U, 0U, false},
      {CWIKI_ZONE_CUSTOM, "match-bound", "(*NO_AUTO_POSSESS)^(a+)+$", ">",
          NULL, 0U, 0U, 0U, 0U, false}
   };
   static const char *const clean[] = {
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaX\n",
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaX \\$5\n"
   };
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_buffer note;
   struct cwiki_tex_extraction x;
   size_t i;

   check(cwiki_zone_engine_init_with_limits(&engine, regions, 2U, 3U, 50U,
       1000U) == 0, "limited top-level zone engine compiles");
   if (engine == NULL) {
      return;
   }
   extract_text(engine,
       "ok\n"
       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaX $$y$$\n",
       0U, &note, &x);
   check(x.block_count == 0U && x.report_count == 1U &&
       x.reports[0].reason == CWIKI_TEX_REPORT_UNTRUSTED &&
       x.reports[0].line == 1U,
       "AC31 block wholly inside a degraded line is reported");
   cwiki_tex_extraction_free(&x);
   cwiki_buffer_free(&note);
   for (i = 0U; i < sizeof(clean) / sizeof(clean[0]); i++) {
      extract_text(engine, clean[i], 0U, &note, &x);
      check(x.block_count == 0U && x.report_count == 0U,
          "degraded line without a block start is not reported");
      cwiki_tex_extraction_free(&x);
      cwiki_buffer_free(&note);
   }
   cwiki_zone_engine_free(engine);
}

/* Copies the committed fixture into a disposable root (AC26). */
static char *
read_from_fixture(const struct fixture_vault *vault, const char *name,
    struct cwiki_buffer *note)
{
   char *path = NULL;
   FILE *file;
   char *bytes = NULL;
   long size;

   if (fixture_vault_resolve(vault, name, &path) != 0) {
      check(false, "fixture note resolves in disposable root");
      return NULL;
   }
   file = fopen(path, "rb");
   free(path);
   if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
       (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
      check(false, "fixture note opens");
      if (file != NULL) {
         (void)fclose(file);
      }
      return NULL;
   }
   bytes = malloc((size_t)size + 1U);
   if (bytes != NULL && fread(bytes, 1U, (size_t)size, file) == (size_t)size) {
      load(note, bytes, (size_t)size);
   } else {
      check(false, "fixture note reads");
   }
   (void)fclose(file);
   return bytes;
}

static struct cwiki_tex_env_set *
build_env(const char *vault, const char *note, const char *const *packages,
    size_t package_count, const struct cwiki_tex_env_user_entry *user,
    size_t user_count)
{
   struct cwiki_tex_env_set *env = NULL;

   check(cwiki_tex_env_build(&env, vault, vault == NULL ? 0U : strlen(vault),
       note, note == NULL ? 0U : strlen(note), packages, package_count, user,
       user_count) == 0, "env builds");
   return env;
}

static void
test_fixtures(void)
{
   struct cwiki_zone_engine *engine = builtin_engine();
   struct fixture_vault vault;
   struct cwiki_buffer note;
   struct cwiki_tex_extraction x;
   char *bytes;

   check(fixture_vault_create(&vault) == 0, "create disposable root");
   check(fixture_vault_copy_in(&vault, "tests/fixtures/vaults/m2-render") == 0,
       "copy render fixture into root");

   bytes = read_from_fixture(&vault, "class-note.md", &note);
   {
      struct cwiki_tex_env_set *gate =
          build_env("\\usepackage{amsmath}\n\\usepackage{tikz}\n", NULL,
          NULL, 0U, NULL, 0U);

      check(cwiki_tex_extract(engine, &note, 0U, gate, &x) == 0,
          "AC8 extract");
      cwiki_tex_env_free(gate);
   }
   check(x.block_count == 6U && x.report_count == 0U,
       "AC8 six blocks and no reports");
   check(block_is(&note, &x, 0U, CWIKI_TEX_BLOCK_INLINE, 3U, 3U, "$E = mc^2$"),
       "AC8 inline math line 3");
   check(block_is(&note, &x, 1U, CWIKI_TEX_BLOCK_DISPLAY, 5U, 5U,
       "$$\\int_0^1 x^2 \\, dx = \\frac{1}{3}$$"), "AC8 display math line 5");
   check(block_is(&note, &x, 2U, CWIKI_TEX_BLOCK_CHEMISTRY, 7U, 7U,
       "\\ce{2H2 + O2 -> 2H2O}"), "AC8 chemistry line 7");
   check(x.block_count == 6U && x.blocks[3].kind == CWIKI_TEX_BLOCK_ENVIRONMENT
       && x.blocks[3].first_line == 8U && x.blocks[3].last_line == 10U &&
       strcmp(x.blocks[3].env, "tikzpicture") == 0, "AC8 tikzpicture 9-11");
   check(x.block_count == 6U && x.blocks[4].kind == CWIKI_TEX_BLOCK_ENVIRONMENT
       && x.blocks[4].first_line == 12U && x.blocks[4].last_line == 18U &&
       strcmp(x.blocks[4].env, "align") == 0, "AC8 align 13-19");
   check(block_is(&note, &x, 5U, CWIKI_TEX_BLOCK_DISPLAY, 23U, 23U,
       "$$\\undefinedfixturemacro{x}$$"), "AC8 failing display line 23");
   if (x.block_count == 6U) {
      size_t i;

      for (i = 0U; i < 6U; i++) {
         check(x.blocks[i].compilable, "AC8 every class-note block compilable");
      }
   }
   cwiki_tex_extraction_free(&x);
   cwiki_buffer_free(&note);
   free(bytes);

   bytes = read_from_fixture(&vault, "fence-note.md", &note);
   check(cwiki_tex_extract(engine, &note, 0U, NULL, &x) == 0, "fence note extract");
   check(x.block_count == 1U && x.report_count == 0U,
       "AC5/AC6 fences, comments, SynIgnore and \\$ yield no blocks");
   check(block_is(&note, &x, 0U, CWIKI_TEX_BLOCK_INLINE, 23U, 23U, "$r$"),
       "AC5/AC6 only the real inline block survives");
   cwiki_tex_extraction_free(&x);
   cwiki_buffer_free(&note);
   free(bytes);

   bytes = read_from_fixture(&vault, "unknown-env-note.md", &note);
   {
      static const char *const packages[] = {"amsmath"};
      struct cwiki_tex_env_set *env = NULL;

      check(cwiki_tex_env_build(&env, NULL, 0U, NULL, 0U, packages, 1U, NULL,
          0U) == 0, "env for unknown-env note");
      check(cwiki_tex_extract(engine, &note, 0U, env, &x) == 0,
          "unknown-env note extract");
      check(x.block_count == 6U && x.blocks[0].compilable &&
          !x.blocks[1].compilable && !x.blocks[2].compilable &&
          !x.blocks[3].compilable && !x.blocks[4].compilable &&
          !x.blocks[5].compilable,
          "AC14/AC30 unknown names withhold exactly their blocks");
      check(x.report_count == 3U &&
          x.reports[0].reason == CWIKI_TEX_REPORT_UNKNOWN_ENV &&
          strcmp(x.reports[0].name, "zzz") == 0 && x.reports[0].line == 4U &&
          x.reports[1].reason == CWIKI_TEX_REPORT_UNKNOWN_ENV &&
          strcmp(x.reports[1].name, "bar") == 0 && x.reports[1].line == 13U &&
          x.reports[2].reason == CWIKI_TEX_REPORT_UNTERMINATED &&
          strcmp(x.reports[2].name, "align") == 0,
          "AC14 zzz and bar reported once each; align unterminated");
      cwiki_tex_extraction_free(&x);
      cwiki_tex_env_free(env);
   }
   cwiki_buffer_free(&note);
   free(bytes);

   check(fixture_vault_destroy(&vault) == 0, "destroy disposable root");
   cwiki_zone_engine_free(engine);
}

static void
test_gating(void)
{
   static const char *const loaded[] = {"extras"};
   static const char *const names[] = {"widget"};
   static const struct cwiki_tex_env_user_entry user[] = {{"extras", names, 1U}};
   static const char align[] = "\\begin{align}\na\n\\end{align}\n";
   struct cwiki_zone_engine *engine = builtin_engine();
   struct cwiki_buffer note;
   struct cwiki_tex_extraction x;
   struct cwiki_tex_env_set *env;

   /* AC10 */
   env = build_env("\\usepackage{amsmath}\n", NULL, NULL, 0U, NULL, 0U);
   extract_text(engine, align, 0U, &note, &x);
   cwiki_tex_extraction_free(&x);
   check(cwiki_tex_extract(engine, &note, 0U, env, &x) == 0 &&
       x.block_count == 1U && x.blocks[0].compilable && x.report_count == 0U,
       "AC10 align compilable with amsmath loaded");
   cwiki_tex_extraction_free(&x);
   cwiki_tex_env_free(env);
   env = build_env("", NULL, NULL, 0U, NULL, 0U);
   check(cwiki_tex_extract(engine, &note, 0U, env, &x) == 0 &&
       x.block_count == 1U && !x.blocks[0].compilable &&
       x.report_count == 1U &&
       x.reports[0].reason == CWIKI_TEX_REPORT_UNKNOWN_ENV &&
       strcmp(x.reports[0].name, "align") == 0 && x.reports[0].line == 0U,
       "AC10 align withheld and named without amsmath");
   cwiki_tex_extraction_free(&x);
   cwiki_tex_env_free(env);
   cwiki_buffer_free(&note);

   /* AC11, AC12: frontmatter note */
   {
      struct fixture_vault vault;
      struct cwiki_config_note_tex tex;
      struct cwiki_config_error error;
      char *bytes;

      check(fixture_vault_create(&vault) == 0, "create disposable root");
      check(fixture_vault_copy_in(&vault, "tests/fixtures/vaults/m2-render") ==
          0, "copy render fixture into root");
      bytes = read_from_fixture(&vault, "frontmatter-note.md", &note);
      memset(&error, 0, sizeof(error));
      check(cwiki_config_parse_note_tex(&note, &tex, &error) ==
          CWIKI_CONFIG_OK, "frontmatter note parses");
      env = build_env("", tex.preamble, (const char *const *)tex.packages,
          tex.package_count, NULL, 0U);
      check(cwiki_tex_extract(engine, &note, tex.body_first_line, env, &x) ==
          0 && x.block_count == 2U && x.blocks[0].compilable &&
          x.blocks[1].compilable && x.report_count == 0U,
          "AC11/AC12 tikzcd via packages and foo via note preamble");
      cwiki_tex_extraction_free(&x);
      cwiki_tex_env_free(env);
      env = build_env("", tex.preamble, NULL, 0U, NULL, 0U);
      check(cwiki_tex_extract(engine, &note, tex.body_first_line, env, &x) ==
          0 && x.block_count == 2U && !x.blocks[0].compilable &&
          x.blocks[1].compilable && x.report_count == 1U &&
          strcmp(x.reports[0].name, "tikzcd") == 0,
          "AC11 tikzcd withheld without the package");
      cwiki_tex_extraction_free(&x);
      cwiki_tex_env_free(env);
      env = build_env("\\newenvironment{foo}{}{}\n", NULL, (const char *const *)
          tex.packages, tex.package_count, NULL, 0U);
      check(cwiki_tex_extract(engine, &note, tex.body_first_line, env, &x) ==
          0 && x.block_count == 2U && x.blocks[1].compilable &&
          x.report_count == 0U, "AC12 foo via vault preamble");
      cwiki_tex_extraction_free(&x);
      cwiki_tex_env_free(env);
      cwiki_config_note_tex_free(&tex);
      cwiki_buffer_free(&note);
      free(bytes);

      /* AC15, AC29, AC33 directives */
      bytes = read_from_fixture(&vault, "directive-note.md", &note);
      env = build_env("\\usepackage{tikz}\n", NULL, NULL, 0U, NULL, 0U);
      check(cwiki_tex_extract(engine, &note, 0U, env, &x) == 0 &&
          x.block_count == 4U, "directive note extracts four blocks");
      if (x.block_count == 4U) {
         check(x.blocks[0].suppressed && !x.blocks[0].compilable &&
             x.blocks[1].suppressed && !x.blocks[1].compilable &&
             x.blocks[2].suppressed && !x.blocks[2].compilable,
             "AC15/AC29 norender suppresses environment, $$ and \\[ blocks");
         check(!x.blocks[3].suppressed && x.blocks[3].compilable,
             "AC29 unknown directive leaves the block compilable");
      }
      check(x.report_count == 1U &&
          x.reports[0].reason == CWIKI_TEX_REPORT_UNKNOWN_DIRECTIVE &&
          strcmp(x.reports[0].name, "frobnicate") == 0 &&
          x.reports[0].line == 18U,
          "AC15/AC29 only the unknown directive word is reported");
      cwiki_tex_extraction_free(&x);
      cwiki_tex_env_free(env);
      cwiki_buffer_free(&note);
      free(bytes);
      check(fixture_vault_destroy(&vault) == 0, "destroy disposable root");
   }

   /* AC13 user table */
   extract_text(engine, "\\begin{widget}\nw\n\\end{widget}\n", 0U, &note, &x);
   cwiki_tex_extraction_free(&x);
   env = build_env("", NULL, loaded, 1U, user, 1U);
   check(cwiki_tex_extract(engine, &note, 0U, env, &x) == 0 &&
       x.block_count == 1U && x.blocks[0].compilable && x.report_count == 0U,
       "AC13 user entry compilable with its package loaded");
   cwiki_tex_extraction_free(&x);
   cwiki_tex_env_free(env);
   env = build_env("", NULL, NULL, 0U, user, 1U);
   check(cwiki_tex_extract(engine, &note, 0U, env, &x) == 0 &&
       x.block_count == 1U && !x.blocks[0].compilable &&
       x.report_count == 1U, "AC13 user entry inactive without its package");
   cwiki_tex_extraction_free(&x);
   cwiki_tex_env_free(env);
   cwiki_buffer_free(&note);

   /* AC30 nested unknown in align and in $$ */
   env = build_env("\\usepackage{amsmath}\n", NULL, NULL, 0U, NULL, 0U);
   extract_text(engine,
       "\\begin{align}\n\\begin{bar}\n\\end{bar}\n\\end{align}\n"
       "$$\n\\begin{bar}\nx\n\\end{bar}\n$$\n", 0U, &note, &x);
   cwiki_tex_extraction_free(&x);
   check(cwiki_tex_extract(engine, &note, 0U, env, &x) == 0 &&
       x.block_count == 2U && !x.blocks[0].compilable &&
       !x.blocks[1].compilable && x.report_count == 1U &&
       strcmp(x.reports[0].name, "bar") == 0 && x.reports[0].line == 1U,
       "AC30 nested unknown environment withholds align and $$ blocks");
   cwiki_tex_extraction_free(&x);
   cwiki_tex_env_free(env);
   cwiki_buffer_free(&note);
   cwiki_zone_engine_free(engine);
}

int
main(void)
{
   test_math_and_chemistry();
   test_environments();
   test_frontmatter_offset();
   test_unterminated();
   test_untrusted();
   test_untrusted_single_line();
   test_fixtures();
   test_gating();
   if (failures != 0) {
      (void)fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   return 0;
}
