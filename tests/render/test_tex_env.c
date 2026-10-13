/*
 * Acceptance tests for src/tex_env.h (delivery d-20261012-251ff8, step S1):
 * AC9, AC12, AC32, AC33. The API below is derived from the plan; it is the
 * contract the implementation must provide.
 */
#include "tex_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;

static void
check(bool condition, const char *message)
{
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      failures++;
   }
}

/* Builds a set from NUL-terminated inputs; NULL means empty. */
static struct cwiki_tex_env_set *
build(const char *vault, const char *note, const char *const *packages,
    size_t package_count, const struct cwiki_tex_env_user_entry *user,
    size_t user_count)
{
   struct cwiki_tex_env_set *set = NULL;
   int status;

   status = cwiki_tex_env_build(&set, vault, vault ? strlen(vault) : 0U,
       note, note ? strlen(note) : 0U, packages, package_count, user,
       user_count);
   check(status == 0 && set != NULL, "availability set builds");
   return set;
}

static bool
avail(const struct cwiki_tex_env_set *set, const char *name)
{
   return set != NULL && cwiki_tex_env_available(set, name);
}

static bool
provided_by(const struct cwiki_tex_env_set *set, const char *name,
    const char *package)
{
   const char *provider;

   if (set == NULL) {
      return false;
   }
   provider = cwiki_tex_env_provider(set, name);
   return provider != NULL && strcmp(provider, package) == 0;
}

static void
test_ac9_core_always_others_gated(void)
{
   struct cwiki_tex_env_set *set = build(NULL, NULL, NULL, 0U, NULL, 0U);

   check(avail(set, "itemize"), "AC9 core environment in force with nothing loaded");
   check(provided_by(set, "itemize", "latex"), "AC9 core environment keyed to latex");
   check(!avail(set, "align"), "AC9 amsmath table inactive until loaded");
   check(!avail(set, "tikzpicture"), "AC9 tikz table inactive until loaded");
   check(!avail(set, "pgfpicture"), "AC9 pgf table inactive until loaded");
   check(!avail(set, "axis"), "AC9 pgfplots table inactive until loaded");
   check(!avail(set, "tikzcd"), "AC9 tikz-cd table inactive until loaded");
   check(!avail(set, "nosuchenv"), "AC9 unknown name unavailable");
   cwiki_tex_env_free(set);
}

static void
test_ac9_tables_by_package(void)
{
   const char *const list[] = {"amsmath", "pgfplots", "tikz-cd", "mhchem"};
   struct cwiki_tex_env_set *set = build(NULL, NULL, list, 4U, NULL, 0U);

   check(provided_by(set, "align", "amsmath"), "AC9 align keyed to amsmath");
   check(provided_by(set, "axis", "pgfplots"), "AC9 axis keyed to pgfplots");
   check(provided_by(set, "tikzcd", "tikz-cd"), "AC9 tikzcd keyed to tikz-cd");
   check(provided_by(set, "tikzpicture", "tikz"), "AC9 tikzpicture keyed to tikz");
   check(provided_by(set, "pgfpicture", "pgf"), "AC9 pgfpicture keyed to pgf");
   check(cwiki_tex_env_package_loaded(set, "mhchem"), "AC9 mhchem counts as loaded");
   cwiki_tex_env_free(set);

   {
      const char *const only[] = {"amsmath"};

      set = build(NULL, NULL, only, 1U, NULL, 0U);
      check(avail(set, "align") && avail(set, "gather"), "AC9 amsmath table active");
      check(!avail(set, "tikzpicture"), "AC9 loading amsmath does not enable tikz");
      cwiki_tex_env_free(set);
   }
}

static void
test_ac32_usepackage_forms(void)
{
   struct cwiki_tex_env_set *set;

   set = build("\\usepackage[leqno]{amsmath,tikz}\n", NULL, NULL, 0U, NULL, 0U);
   check(avail(set, "align"), "AC32 \\usepackage[opt]{a,b} loads first package");
   check(avail(set, "tikzpicture"), "AC32 \\usepackage[opt]{a,b} loads second package");
   cwiki_tex_env_free(set);

   set = build(NULL, "\\RequirePackage{amsmath}\n", NULL, 0U, NULL, 0U);
   check(avail(set, "align"), "AC32 \\RequirePackage in note preamble loads package");
   cwiki_tex_env_free(set);

   set = build("\\usepackage{ amsmath }\n", NULL, NULL, 0U, NULL, 0U);
   check(set != NULL, "AC32 padded package list still builds");
   cwiki_tex_env_free(set);
}

static void
test_ac32_implied_loads(void)
{
   const char *const pp[] = {"pgfplots"};
   const char *const cd[] = {"tikz-cd"};
   const char *const tz[] = {"tikz"};
   struct cwiki_tex_env_set *set;

   set = build(NULL, NULL, pp, 1U, NULL, 0U);
   check(avail(set, "tikzpicture") && avail(set, "pgfpicture"),
       "AC32 pgfplots alone brings TikZ and PGF tables");
   cwiki_tex_env_free(set);

   set = build(NULL, NULL, cd, 1U, NULL, 0U);
   check(avail(set, "tikzpicture") && avail(set, "pgfpicture") &&
       avail(set, "tikzcd"), "AC32 tikz-cd alone brings TikZ and PGF tables");
   check(!avail(set, "axis"), "AC32 tikz-cd does not bring pgfplots");
   cwiki_tex_env_free(set);

   set = build(NULL, NULL, tz, 1U, NULL, 0U);
   check(avail(set, "pgfpicture"), "AC32 tikz implies pgf");
   cwiki_tex_env_free(set);
}

static void
test_ac32_comments_do_not_load(void)
{
   struct cwiki_tex_env_set *set;

   set = build("% \\usepackage{amsmath}\n", NULL, NULL, 0U, NULL, 0U);
   check(!avail(set, "align"), "AC32 commented-out \\usepackage loads nothing");
   cwiki_tex_env_free(set);

   set = build("50\\% \\usepackage{amsmath}\n", NULL, NULL, 0U, NULL, 0U);
   check(avail(set, "align"), "AC32 escaped percent is not a comment");
   cwiki_tex_env_free(set);

   set = build(NULL, "% \\newenvironment{foo}{}{}\n", NULL, 0U, NULL, 0U);
   check(!avail(set, "foo"), "AC32 commented-out declaration registers nothing");
   cwiki_tex_env_free(set);
}

static void
test_ac12_ac32_declaration_commands(void)
{
   static const struct {
      const char *source;
      const char *name;
   } cases[] = {
      {"\\newenvironment{fooa}{}{}\n", "fooa"},
      {"\\renewenvironment{foob}{}{}\n", "foob"},
      {"\\NewDocumentEnvironment{fooc}{}{}{}\n", "fooc"},
      {"\\RenewDocumentEnvironment{food}{}{}{}\n", "food"},
      {"\\ProvideDocumentEnvironment{fooe}{}{}{}\n", "fooe"},
      {"\\DeclareDocumentEnvironment{foof}{}{}{}\n", "foof"},
   };
   size_t i;

   for (i = 0U; i < sizeof cases / sizeof cases[0]; i++) {
      struct cwiki_tex_env_set *set;
      char message[96];

      set = build(cases[i].source, NULL, NULL, 0U, NULL, 0U);
      (void)snprintf(message, sizeof message,
          "AC12/AC32 vault preamble declaration makes %s available",
          cases[i].name);
      check(avail(set, cases[i].name), message);
      cwiki_tex_env_free(set);

      set = build(NULL, cases[i].source, NULL, 0U, NULL, 0U);
      (void)snprintf(message, sizeof message,
          "AC12/AC32 note preamble declaration makes %s available",
          cases[i].name);
      check(avail(set, cases[i].name), message);
      cwiki_tex_env_free(set);
   }
}

static void
test_ac12_declaration_scope(void)
{
   struct cwiki_tex_env_set *set;

   set = build("\\newenvironment{foo}{\\begin{center}}{\\end{center}}\n"
       "\\newcommand{\\bar}{x}\n", NULL, NULL, 0U, NULL, 0U);
   check(avail(set, "foo"), "AC12 \\newenvironment with body registers its name");
   check(!avail(set, "bar") && !avail(set, "center_"),
       "AC12 non-environment declarations register nothing");
   cwiki_tex_env_free(set);
}

static void
test_ac33_user_table(void)
{
   static const char *const names[] = {"circuitikz", "bodeplot"};
   const struct cwiki_tex_env_user_entry entry = {"mypkg", names, 2U};
   const char *const loaded[] = {"mypkg"};
   struct cwiki_tex_env_set *set;

   set = build(NULL, NULL, NULL, 0U, &entry, 1U);
   check(!avail(set, "circuitikz") && !avail(set, "bodeplot"),
       "AC33 user entry inactive when its package is not loaded");
   cwiki_tex_env_free(set);

   set = build(NULL, NULL, loaded, 1U, &entry, 1U);
   check(avail(set, "circuitikz") && avail(set, "bodeplot"),
       "AC33 user entry active when loaded via packages:");
   check(provided_by(set, "circuitikz", "mypkg"),
       "AC33 user entry keyed to its package");
   cwiki_tex_env_free(set);

   set = build("\\usepackage{mypkg}\n", NULL, NULL, 0U, &entry, 1U);
   check(avail(set, "circuitikz"), "AC33 user entry active when loaded via preamble");
   cwiki_tex_env_free(set);

   set = build(NULL, NULL, loaded, 1U, NULL, 0U);
   check(!avail(set, "circuitikz"), "AC13 no entry means the name stays unknown");
   cwiki_tex_env_free(set);
}

static void
test_starred_forms_explicit(void)
{
   static const char *const amsmath[] = {"amsmath"};
   struct cwiki_tex_env_set *set = build(
       "\\newenvironment{foo}{}{}\n\\newenvironment{bar*}{}{}\n", NULL,
       amsmath, 1U, NULL, 0U);

   check(avail(set, "align*") && provided_by(set, "align*", "amsmath"),
       "starred amsmath form is listed explicitly");
   check(avail(set, "equation*"), "amsmath provides equation*");
   check(avail(set, "figure*") && provided_by(set, "figure*", "latex"),
       "starred core form is listed explicitly");
   check(!avail(set, "itemize*"), "itemize has no starred form");
   check(!avail(set, "foo*"), "declaring foo does not declare foo*");
   check(avail(set, "bar*") && !avail(set, "bar"),
       "a declared starred name stands alone");
   cwiki_tex_env_free(set);
   set = build(NULL, NULL, NULL, 0U, NULL, 0U);
   check(!avail(set, "align*") && !avail(set, "equation*"),
       "amsmath starred forms need amsmath");
   cwiki_tex_env_free(set);
}

/* R2.8.6 allows a 1 MiB preamble over Git: the scan must stay linear. A
 * per-match UTF re-check of the rest of the subject takes minutes here. */
static void
test_large_preamble_is_linear(void)
{
   static const char unit[] =
       "\\usepackage{amsmath}\n\\newenvironment{foo}{}{}\ntext \xc3\xa9\n";
   size_t unit_length = sizeof unit - 1U;
   size_t count = (1024U * 1024U) / unit_length;
   char *text = malloc(count * unit_length + 1U);
   struct cwiki_tex_env_set *set = NULL;
   clock_t start;
   double seconds;
   size_t i;

   check(text != NULL, "large preamble allocates");
   if (text == NULL) {
      return;
   }
   for (i = 0U; i < count; i++) {
      memcpy(text + i * unit_length, unit, unit_length);
   }
   text[count * unit_length] = '\0';
   start = clock();
   check(cwiki_tex_env_build(&set, text, count * unit_length, text,
       count * unit_length, NULL, 0U, NULL, 0U) == 0, "1 MiB preamble builds");
   seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
   check(seconds < 10.0, "1 MiB preamble scan stays linear (under 10 s CPU)");
   check(avail(set, "align") && avail(set, "foo"),
       "1 MiB preamble scan finds its loads and declarations");
   cwiki_tex_env_free(set);
   free(text);
}

static void
test_invalid_utf8_reported(void)
{
   static const char bad[] = "\\usepackage{amsmath}\n\xff\xfe\n";
   struct cwiki_tex_env_set *set = NULL;

   check(cwiki_tex_env_build(&set, bad, sizeof bad - 1U, NULL, 0U, NULL, 0U,
       NULL, 0U) == CWIKI_TEX_ENV_INVALID_UTF8 && set == NULL,
       "invalid UTF-8 in the vault preamble is reported distinctly");
   check(cwiki_tex_env_build(&set, NULL, 0U, bad, sizeof bad - 1U, NULL, 0U,
       NULL, 0U) == CWIKI_TEX_ENV_INVALID_UTF8 && set == NULL,
       "invalid UTF-8 in the note preamble is reported distinctly");
}

int
main(void)
{
   test_large_preamble_is_linear();
   test_invalid_utf8_reported();
   test_ac9_core_always_others_gated();
   test_ac9_tables_by_package();
   test_ac32_usepackage_forms();
   test_ac32_implied_loads();
   test_ac32_comments_do_not_load();
   test_ac12_ac32_declaration_commands();
   test_ac12_declaration_scope();
   test_ac33_user_table();
   test_starred_forms_explicit();
   if (failures != 0) {
      (void)fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   return 0;
}
