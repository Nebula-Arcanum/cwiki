#include "tex_env.h"

#include "regex.h"
#include "zone.h"

#include <stdlib.h>
#include <string.h>

struct string_list {
   char **items;
   size_t count, cap;
};

struct env_entry {
   char *name;
   const char *provider; /* points into a string_list item or a literal */
};

struct cwiki_tex_env_set {
   struct string_list loaded;
   struct env_entry *envs;
   size_t env_count, env_cap;
   struct string_list owned; /* provider strings the entries point into */
};

struct table {
   const char *package;
   const char *const *names;
};

static const char *const core_names[] = {"document", "itemize", "enumerate",
   "description", "center", "flushleft", "flushright", "quote", "quotation",
   "verse", "verbatim", "tabular", "tabbing", "array", "equation",
   "eqnarray", "displaymath", "math", "minipage", "figure", "table",
   "abstract", "titlepage", "thebibliography", "picture", "list", "trivlist",
   "sloppypar", NULL};
static const char *const amsmath_names[] = {"align", "alignat", "flalign",
   "gather", "multline", "split", "aligned", "alignedat", "gathered", "cases",
   "matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix", "Vmatrix",
   "smallmatrix", "subequations", "subarray", "xalignat", "xxalignat", NULL};
static const char *const tikz_names[] = {"tikzpicture", "scope",
   "tikzfadingfrompicture", NULL};
static const char *const pgf_names[] = {"pgfpicture", "pgfscope",
   "pgfinterruptpicture", "pgfinterruptboundingbox", "pgflowlevelscope", NULL};
static const char *const pgfplots_names[] = {"axis", "semilogxaxis",
   "semilogyaxis", "loglogaxis", "polaraxis", NULL};
static const char *const tikzcd_names[] = {"tikzcd", NULL};
static const char *const mhchem_names[] = {NULL}; /* mhchem defines none */

static const struct table tables[] = {
   {"latex", core_names}, {"amsmath", amsmath_names},
   {"mhchem", mhchem_names}, {"tikz", tikz_names}, {"pgf", pgf_names},
   {"pgfplots", pgfplots_names}, {"tikz-cd", tikzcd_names}};

static const struct {
   const char *package, *implied;
} implied_loads[] = {
   {"pgfplots", "tikz"}, {"tikz-cd", "tikz"}, {"tikz", "pgf"}};

static const char package_pattern[] =
    "(?<!\\\\)\\\\(?:usepackage|RequirePackage)\\s*(?:\\[[^\\]]*\\])?\\s*"
    "\\{([^}]*)\\}";
static const char env_pattern[] =
    "(?<!\\\\)\\\\(?:newenvironment|renewenvironment|NewDocumentEnvironment|"
    "RenewDocumentEnvironment|ProvideDocumentEnvironment|"
    "DeclareDocumentEnvironment)\\*?\\s*\\{([^}]*)\\}";

static char *
dup_n(const char *text, size_t length)
{
   char *copy = malloc(length + 1U);

   if (copy != NULL) {
      memcpy(copy, text, length);
      copy[length] = '\0';
   }
   return copy;
}

static int
list_has(const struct string_list *list, const char *text)
{
   size_t i;

   for (i = 0U; i < list->count; i++) {
      if (strcmp(list->items[i], text) == 0) {
         return 1;
      }
   }
   return 0;
}

/* Takes ownership of text on success. */
static int
list_add(struct string_list *list, char *text)
{
   if (list->count == list->cap) {
      size_t cap = list->cap == 0U ? 16U : list->cap * 2U;
      char **items = realloc(list->items, cap * sizeof *items);

      if (items == NULL) {
         free(text);
         return -1;
      }
      list->items = items;
      list->cap = cap;
   }
   list->items[list->count++] = text;
   return 0;
}

static void
list_free(struct string_list *list)
{
   size_t i;

   for (i = 0U; i < list->count; i++) {
      free(list->items[i]);
   }
   free(list->items);
}

static int
load_package(struct cwiki_tex_env_set *set, const char *text, size_t length)
{
   char *copy;

   while (length > 0U && (*text == ' ' || *text == '\t' || *text == '\n')) {
      text++;
      length--;
   }
   while (length > 0U && (text[length - 1U] == ' ' ||
       text[length - 1U] == '\t' || text[length - 1U] == '\n')) {
      length--;
   }
   if (length == 0U) {
      return 0;
   }
   copy = dup_n(text, length);
   if (copy == NULL) {
      return -1;
   }
   if (list_has(&set->loaded, copy)) {
      free(copy);
      return 0;
   }
   return list_add(&set->loaded, copy);
}

static int
add_env(struct cwiki_tex_env_set *set, const char *name, size_t length,
    const char *provider)
{
   size_t i;
   char *copy;

   for (i = 0U; i < set->env_count; i++) {
      if (strlen(set->envs[i].name) == length &&
          memcmp(set->envs[i].name, name, length) == 0) {
         return 0; /* first provider wins */
      }
   }
   if (set->env_count == set->env_cap) {
      size_t cap = set->env_cap == 0U ? 64U : set->env_cap * 2U;
      struct env_entry *envs = realloc(set->envs, cap * sizeof *envs);

      if (envs == NULL) {
         return -1;
      }
      set->envs = envs;
      set->env_cap = cap;
   }
   copy = dup_n(name, length);
   if (copy == NULL) {
      return -1;
   }
   set->envs[set->env_count].name = copy;
   set->envs[set->env_count].provider = provider;
   set->env_count++;
   return 0;
}

/* Copy of text with each unescaped %-comment replaced by nothing. */
static char *
strip_comments(const char *text, size_t length, size_t *out_length)
{
   char *out = malloc(length + 1U);
   size_t i, n = 0U, slashes = 0U;
   bool comment = false;

   if (out == NULL) {
      return NULL;
   }
   for (i = 0U; i < length; i++) {
      char c = text[i];

      if (c == '\n') {
         comment = false;
         slashes = 0U;
      } else if (!comment && c == '%' && slashes % 2U == 0U) {
         comment = true;
      }
      if (!comment) {
         out[n++] = c;
      }
      slashes = c == '\\' ? slashes + 1U : 0U;
   }
   out[n] = '\0';
   *out_length = n;
   return out;
}

/* kind 0: package loads, 1: environment declarations. */
static int
scan(struct cwiki_tex_env_set *set, const char *text, size_t length, int kind)
{
   struct cwiki_regex_compile_error error;
   struct cwiki_regex *regex;
   size_t clean_length, offset = 0U;
   char *clean;
   int status = 0;

   if (text == NULL || length == 0U) {
      return 0;
   }
   clean = strip_comments(text, length, &clean_length);
   if (clean == NULL) {
      return -1;
   }
   if (cwiki_regex_compile(&regex, kind == 0 ? package_pattern : env_pattern,
       kind == 0 ? sizeof package_pattern - 1U : sizeof env_pattern - 1U, 0U,
       CWIKI_ZONE_DEFAULT_MATCH_LIMIT, CWIKI_ZONE_DEFAULT_DEPTH_LIMIT,
       &error) != CWIKI_REGEX_COMPILE_OK) {
      free(clean);
      return -2;
   }
   while (offset <= clean_length) {
      struct cwiki_regex_result r = cwiki_regex_execute(regex, clean,
          clean_length, offset, false);
      const char *name;
      size_t start, end, pos;

      if (r.status == CWIKI_REGEX_NO_MATCH) {
         break;
      }
      if (r.status != CWIKI_REGEX_MATCH || r.capture_count < 2U) {
         status = -2; /* limit or engine error: never a silent no-match */
         break;
      }
      name = clean + r.captures[1].start;
      end = r.captures[1].end;
      offset = r.captures[0].end > offset ? r.captures[0].end : offset + 1U;
      if (kind == 0) {
         for (start = r.captures[1].start, pos = start; pos <= end; pos++) {
            if (pos == end || clean[pos] == ',') {
               if (load_package(set, clean + start, pos - start) != 0) {
                  status = -1;
                  break;
               }
               start = pos + 1U;
            }
         }
      } else if (add_env(set, name, end - r.captures[1].start,
          "preamble") != 0) {
         status = -1;
      }
      if (status != 0) {
         break;
      }
   }
   cwiki_regex_free(regex);
   free(clean);
   return status;
}

static int
add_table(struct cwiki_tex_env_set *set, const char *package,
    const char *const *names)
{
   for (; *names != NULL; names++) {
      if (add_env(set, *names, strlen(*names), package) != 0) {
         return -1;
      }
   }
   return 0;
}

int
cwiki_tex_env_build(struct cwiki_tex_env_set **out, const char *vault,
    size_t vault_length, const char *note, size_t note_length,
    const char *const *packages, size_t package_count,
    const struct cwiki_tex_env_user_entry *user, size_t user_count)
{
   struct cwiki_tex_env_set *set = calloc(1U, sizeof *set);
   size_t i, j;
   int status = -1;

   *out = NULL;
   if (set == NULL) {
      return -1;
   }
   for (i = 0U; i < package_count; i++) {
      if (load_package(set, packages[i], strlen(packages[i])) != 0) {
         goto fail;
      }
   }
   if ((status = scan(set, vault, vault_length, 0)) != 0 ||
       (status = scan(set, note, note_length, 0)) != 0) {
      goto fail;
   }
   status = -1;
   for (i = 0U; i < sizeof implied_loads / sizeof implied_loads[0]; i++) {
      /* Table order lists implied sources before their targets' own implied
       * loads, so one pass reaches the fixed point (pgfplots->tikz->pgf). */
      if (list_has(&set->loaded, implied_loads[i].package) &&
          load_package(set, implied_loads[i].implied,
          strlen(implied_loads[i].implied)) != 0) {
         goto fail;
      }
   }
   for (i = 0U; i < sizeof tables / sizeof tables[0]; i++) {
      if ((strcmp(tables[i].package, "latex") == 0 ||
          list_has(&set->loaded, tables[i].package)) &&
          add_table(set, tables[i].package, tables[i].names) != 0) {
         goto fail;
      }
   }
   for (i = 0U; i < user_count; i++) {
      char *provider;

      if (!list_has(&set->loaded, user[i].package)) {
         continue;
      }
      provider = dup_n(user[i].package, strlen(user[i].package));
      if (provider == NULL || list_add(&set->owned, provider) != 0) {
         goto fail;
      }
      for (j = 0U; j < user[i].name_count; j++) {
         if (add_env(set, user[i].names[j], strlen(user[i].names[j]),
             provider) != 0) {
            goto fail;
         }
      }
   }
   if ((status = scan(set, vault, vault_length, 1)) != 0 ||
       (status = scan(set, note, note_length, 1)) != 0) {
      goto fail;
   }
   *out = set;
   return 0;
fail:
   cwiki_tex_env_free(set);
   return status;
}

void
cwiki_tex_env_free(struct cwiki_tex_env_set *set)
{
   size_t i;

   if (set == NULL) {
      return;
   }
   for (i = 0U; i < set->env_count; i++) {
      free(set->envs[i].name);
   }
   free(set->envs);
   list_free(&set->loaded);
   list_free(&set->owned);
   free(set);
}

const char *
cwiki_tex_env_provider(const struct cwiki_tex_env_set *set, const char *name)
{
   size_t i, length = strlen(name);

   for (i = 0U; i < set->env_count; i++) {
      if (strcmp(set->envs[i].name, name) == 0) {
         return set->envs[i].provider;
      }
   }
   /* amsmath-style starred forms share their base environment. */
   if (length > 1U && name[length - 1U] == '*') {
      for (i = 0U; i < set->env_count; i++) {
         if (strlen(set->envs[i].name) == length - 1U &&
             memcmp(set->envs[i].name, name, length - 1U) == 0) {
            return set->envs[i].provider;
         }
      }
   }
   return NULL;
}

bool
cwiki_tex_env_available(const struct cwiki_tex_env_set *set, const char *name)
{
   return cwiki_tex_env_provider(set, name) != NULL;
}

bool
cwiki_tex_env_package_loaded(const struct cwiki_tex_env_set *set,
    const char *package)
{
   return list_has(&set->loaded, package) != 0;
}
