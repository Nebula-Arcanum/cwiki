#include "snippet_catalog.h"
#include "undo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define Z(kind) (1U << (unsigned)(kind))
#define PROSE Z(CWIKI_ZONE_PROSE)
#define MATH (Z(CWIKI_ZONE_MATH_INLINE) | Z(CWIKI_ZONE_MATH_DISPLAY))
#define CHEM Z(CWIKI_ZONE_CHEMISTRY)
#define TIKZ Z(CWIKI_ZONE_TIKZ)

static size_t checks;

static void
check(bool condition, const char *message)
{
   checks++;
   if (!condition) {
      (void)fprintf(stderr, "FAIL: %s\n", message);
      exit(EXIT_FAILURE);
   }
}

/* Independently transcribed from the pinned VimTeX table, not catalog macros.
 * RHS has neither spaces nor braces, even for sqrt and boldsymbol. */
static const struct { const char *trigger; const char *command; } symbols[] = {
   {"`0", "emptyset"}, {"`2", "sqrt"}, {"`6", "partial"}, {"`8", "infty"},
   {"`=", "equiv"}, {"`\\", "setminus"}, {"`.", "cdot"}, {"`*", "times"},
   {"`<", "langle"}, {"`>", "rangle"}, {"`H", "hbar"}, {"`+", "dagger"},
   {"`[", "subseteq"}, {"`]", "supseteq"}, {"`(", "subset"}, {"`)", "supset"},
   {"`A", "forall"}, {"`B", "boldsymbol"}, {"`E", "exists"}, {"`N", "nabla"},
   {"`jj", "downarrow"}, {"`jJ", "Downarrow"}, {"`jk", "uparrow"}, {"`jK", "Uparrow"},
   {"`jh", "leftarrow"}, {"`jH", "Leftarrow"}, {"`jl", "rightarrow"}, {"`jL", "Rightarrow"},
   {"`a", "alpha"}, {"`b", "beta"}, {"`c", "chi"}, {"`d", "delta"},
   {"`e", "epsilon"}, {"`f", "phi"}, {"`g", "gamma"}, {"`h", "eta"},
   {"`i", "iota"}, {"`k", "kappa"}, {"`l", "lambda"}, {"`m", "mu"},
   {"`n", "nu"}, {"`p", "pi"}, {"`q", "theta"}, {"`r", "rho"},
   {"`s", "sigma"}, {"`t", "tau"}, {"`y", "psi"}, {"`u", "upsilon"},
   {"`w", "omega"}, {"`z", "zeta"}, {"`x", "xi"}, {"`D", "Delta"},
   {"`F", "Phi"}, {"`G", "Gamma"}, {"`L", "Lambda"}, {"`P", "Pi"},
   {"`Q", "Theta"}, {"`S", "Sigma"}, {"`U", "Upsilon"}, {"`W", "Omega"},
   {"`X", "Xi"}, {"`Y", "Psi"}, {"`ve", "varepsilon"}, {"`vf", "varphi"},
   {"`vk", "varkappa"}, {"`vp", "varpi"}, {"`vq", "vartheta"}, {"`vr", "varrho"}
};

static const struct expected {
   const char *name, *trigger, *body, *sample;
   unsigned zones;
   bool automatic, regex;
} structural[] = {
   {"math.inline", "mk", "$$$1$$$0", "mk", PROSE, false, false},
   {"math.display", "dm", "\\[\n$1\n\\]$0", "dm", PROSE, false, false},
   {"math.fraction", "//", "\\frac{$1}{$2}$0", "//", MATH, true, false},
   {"math.square", "sr", "^2$0", "sr", MATH, true, false},
   {"math.cube", "cb", "^3$0", "cb", MATH, true, false},
   {"math.subscript-0", "(?<![A-Za-z\\\\])([A-Za-z])0", "${capture:1}_{0}$0", "x0", MATH, true, true},
   {"math.subscript-1", "(?<![A-Za-z\\\\])([A-Za-z])1", "${capture:1}_{1}$0", "x1", MATH, true, true},
   {"math.subscript-2", "(?<![A-Za-z\\\\])([A-Za-z])2", "${capture:1}_{2}$0", "x2", MATH, true, true},
   {"math.subscript-3", "(?<![A-Za-z\\\\])([A-Za-z])3", "${capture:1}_{3}$0", "x3", MATH, true, true},
   {"math.subscript-4", "(?<![A-Za-z\\\\])([A-Za-z])4", "${capture:1}_{4}$0", "x4", MATH, true, true},
   {"math.subscript-5", "(?<![A-Za-z\\\\])([A-Za-z])5", "${capture:1}_{5}$0", "x5", MATH, true, true},
   {"math.subscript-6", "(?<![A-Za-z\\\\])([A-Za-z])6", "${capture:1}_{6}$0", "x6", MATH, true, true},
   {"math.subscript-7", "(?<![A-Za-z\\\\])([A-Za-z])7", "${capture:1}_{7}$0", "x7", MATH, true, true},
   {"math.subscript-8", "(?<![A-Za-z\\\\])([A-Za-z])8", "${capture:1}_{8}$0", "x8", MATH, true, true},
   {"math.subscript-9", "(?<![A-Za-z\\\\])([A-Za-z])9", "${capture:1}_{9}$0", "x9", MATH, true, true},
   {"math.bar", "(?<![A-Za-z\\\\])([A-Za-z])bar", "\\overline{${capture:1}}$0", "zbar", MATH, true, true},
   {"math.hat", "(?<![A-Za-z\\\\])([A-Za-z])hat", "\\hat{${capture:1}}$0", "phat", MATH, true, true},
   {"math.limit", "lim", "\\lim_{${1:n} \\to ${2:\\infty}} $0", "lim", MATH, false, false},
   {"math.sum", "sum", "\\sum_{${1:i}=${2:1}}^{${3:n}} $0", "sum", MATH, false, false},
   {"math.integral", "int", "\\int_{${1:a}}^{${2:b}} $3\\,d${4:x}$0", "int", MATH, false, false},
   {"math.matrix", "mat", "\\begin{pmatrix}\n$1 & $2 \\\\\n$3 & $4\n\\end{pmatrix}$0", "mat", MATH, false, false},
   {"math.environment", "env", "\\begin{${1:align}}\n$2\n\\end{$1}$0", "env", PROSE | MATH, false, false},
   {"chem.ce", "chem", "\\ce{$1}$0", "chem", MATH, false, false},
   {"chem.pu", "unit", "\\pu{${1:1} ${2:mol}}$0", "unit", MATH, false, false},
   {"chem.reaction", "reaction", "${1:A} -> ${2:B}$0", "reaction", CHEM, false, false},
   {"chem.equilibrium", "equilibrium", "${1:A} <=> ${2:B}$0", "equilibrium", CHEM, false, false},
   {"chem.aqueous", "aqueous", "${1:Na+}(aq)$0", "aqueous", CHEM, false, false},
   {"tikz.environment", "tikz", "\\begin{tikzpicture}\n$1\n\\end{tikzpicture}$0", "tikz", PROSE, false, false},
   {"tikz.draw", "draw", "\\draw (${1:0,0}) -- (${2:1,1});$0", "draw", TIKZ, false, false},
   {"tikz.node", "node", "\\node (${1:name}) at (${2:0,0}) {$3};$0", "node", TIKZ, false, false},
   {"tikz.coordinate", "coord", "\\coordinate (${1:name}) at (${2:0,0});$0", "coord", TIKZ, false, false}
};

static struct cwiki_zone_engine *zones;

static struct cwiki_position
load(struct cwiki_buffer *buffer, const char *text)
{
   size_t scanned;
   struct cwiki_position end;
   check(cwiki_buffer_init(buffer) == 0 &&
       cwiki_buffer_load(buffer, text, strlen(text)) == 0 &&
       cwiki_zone_recompute(zones, buffer, 0U, &scanned) == 0, "load fixture");
   end.line = buffer->line_count - 1U;
   end.byte = buffer->lines[end.line].length;
   return end;
}

static void
expect_buffer(const struct cwiki_buffer *buffer, const char *expected)
{
   char *bytes = NULL;
   size_t length = 0U;
   check(cwiki_buffer_encode(buffer, &bytes, &length) == 0 &&
       length == strlen(expected) && memcmp(bytes, expected, length) == 0, expected);
   free(bytes);
}

static void
expect_match(struct cwiki_snippet_registry *registry, const char *text,
    bool expected)
{
   struct cwiki_buffer buffer;
   struct cwiki_position cursor = load(&buffer, text);
   struct cwiki_snippet_match match = {0};
   check(cwiki_snippet_match(registry, zones, &buffer, cursor,
       CWIKI_SNIPPET_EXPLICIT, 0U, &match) ==
       (expected ? CWIKI_SNIPPET_OK : CWIKI_SNIPPET_NO_MATCH), text);
   cwiki_snippet_match_free(&match);
   cwiki_buffer_free(&buffer);
}

static void
expect_expansion(struct cwiki_snippet_registry *registry, const char *text,
    const char *expected)
{
   struct cwiki_buffer buffer;
   struct cwiki_position cursor = load(&buffer, text);
   struct cwiki_snippet_match match = {0};
   struct cwiki_snippet_engine *engine = NULL;
   struct cwiki_undo undo;
   check(cwiki_undo_init(&undo, &buffer) == 0 &&
       cwiki_snippet_engine_init(&engine, &buffer, &undo) == CWIKI_SNIPPET_OK &&
       cwiki_snippet_match(registry, zones, &buffer, cursor,
       CWIKI_SNIPPET_EXPLICIT, 0U, &match) == CWIKI_SNIPPET_OK &&
       cwiki_snippet_expand(engine, &match, NULL, 0U, 1U, &cursor) == CWIKI_SNIPPET_OK,
       text);
   expect_buffer(&buffer, expected);
   check(cwiki_undo_to_parent(&undo) == 0, "expansion is undoable");
   cwiki_snippet_expansion_undone(engine);
   expect_buffer(&buffer, text);
   cwiki_snippet_match_free(&match);
   cwiki_snippet_engine_free(engine);
   cwiki_undo_free(&undo);
   cwiki_buffer_free(&buffer);
}

static const struct cwiki_snippet_catalog_entry *
find(const char *name)
{
   size_t i, count;
   const struct cwiki_snippet_catalog_entry *entries = cwiki_snippet_catalog_entries(&count);
   for (i = 0U; i < count; i++)
      if (strcmp(entries[i].name, name) == 0) return &entries[i];
   check(false, name);
   return NULL;
}

static void
verify_entry(const struct expected *expected)
{
   static const struct { const char *prefix; enum cwiki_zone_kind zone; } contexts[] = {
      {"", CWIKI_ZONE_PROSE}, {"$", CWIKI_ZONE_MATH_INLINE},
      {"\\[", CWIKI_ZONE_MATH_DISPLAY}, {"$$", CWIKI_ZONE_MATH_DISPLAY},
      {"$\\ce{", CWIKI_ZONE_CHEMISTRY},
      {"\\begin{tikzpicture}", CWIKI_ZONE_TIKZ}, {"```tex\n", CWIKI_ZONE_CODE},
      {"$% ", CWIKI_ZONE_COMMENT_PERCENT}, {"%% ", CWIKI_ZONE_COMMENT_NOTE},
      {"<!-- ", CWIKI_ZONE_COMMENT_HTML}, {"$\\text{", CWIKI_ZONE_TEXT},
      {"$\\intertext{", CWIKI_ZONE_TEXT}, {"$\\ref{", CWIKI_ZONE_REFERENCE},
      {"$\\label{", CWIKI_ZONE_REFERENCE}, {"$\\eqref{", CWIKI_ZONE_REFERENCE},
      {"\\begin{proof}", CWIKI_ZONE_LATEX},
      {"$\\ce{% ", CWIKI_ZONE_COMMENT_PERCENT},
      {"\\begin{tikzpicture}% ", CWIKI_ZONE_COMMENT_PERCENT},
      {"$\\text{$", CWIKI_ZONE_MATH_INLINE}
   };
   const struct cwiki_snippet_spec *spec = &find(expected->name)->spec;
   struct cwiki_snippet_registry *registry = NULL;
   size_t i;
   unsigned mask = 0U;
   check(strcmp(spec->trigger, expected->trigger) == 0 &&
       spec->trigger_length == strlen(expected->trigger), expected->name);
   check(spec->kind == (expected->regex ? CWIKI_SNIPPET_REGEX : CWIKI_SNIPPET_LITERAL) &&
       spec->match_limit != 0U && spec->depth_limit != 0U &&
       spec->subject == NULL && spec->subject_length == 0U, "kind/layer/limits");
   check(spec->flags == (expected->automatic ?
       CWIKI_SNIPPET_TRIGGER_AUTO | CWIKI_SNIPPET_TRIGGER_EXPLICIT :
       CWIKI_SNIPPET_TRIGGER_EXPLICIT | CWIKI_SNIPPET_TRIGGER_WORD_BOUNDARY), "flags");
   for (i = 0U; i < spec->body_count; i++) {
      check((mask & Z(spec->bodies[i].zone)) == 0U, "no duplicate zones");
      mask |= Z(spec->bodies[i].zone);
      check(strcmp(spec->bodies[i].body, expected->body) == 0 &&
          spec->bodies[i].body_length == strlen(expected->body), expected->name);
   }
   check(mask == expected->zones && (mask & Z(spec->required_zone)) != 0U, "exact zones");
   check(cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK &&
       cwiki_snippet_registry_add(registry, spec) == CWIKI_SNIPPET_OK, "install isolated entry");
   for (i = 0U; i < COUNT(contexts); i++) {
      char text[256];
      struct cwiki_buffer buffer;
      struct cwiki_position cursor;
      struct cwiki_zone zone;
      struct cwiki_snippet_match match = {0};
      bool allowed = (expected->zones & Z(contexts[i].zone)) != 0U;
      (void)snprintf(text, sizeof(text), "%s%s", contexts[i].prefix, expected->sample);
      cursor = load(&buffer, text);
      check(cwiki_zone_at(zones, &buffer, cursor.line, cursor.byte, &zone) == 0 &&
          zone.kind == contexts[i].zone, text);
      check(cwiki_snippet_match(registry, zones, &buffer, cursor,
          CWIKI_SNIPPET_EXPLICIT, 0U, &match) ==
          (allowed ? CWIKI_SNIPPET_OK : CWIKI_SNIPPET_NO_MATCH), text);
      cwiki_snippet_match_free(&match);
      check(cwiki_snippet_match(registry, zones, &buffer, cursor,
          CWIKI_SNIPPET_AUTO, 0U, &match) ==
          (allowed && expected->automatic ? CWIKI_SNIPPET_OK : CWIKI_SNIPPET_NO_MATCH), text);
      cwiki_snippet_match_free(&match);
      check(cwiki_snippet_match(registry, zones, &buffer, cursor,
          CWIKI_SNIPPET_AUTO, CWIKI_SNIPPET_INPUT_PASTE, &match) == CWIKI_SNIPPET_NO_MATCH,
          "paste never expands");
      cwiki_snippet_match_free(&match);
      cwiki_buffer_free(&buffer);
   }
   cwiki_snippet_registry_free(registry);
   {
      struct cwiki_snippet_catalog_override disabled = {expected->name, NULL};
      char text[256];
      registry = NULL;
      check(cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK &&
          cwiki_snippet_catalog_install(registry, &disabled, 1U) == CWIKI_SNIPPET_OK,
          "every stable name can be disabled independently");
      for (i = 0U; i < COUNT(contexts); i++) {
         if ((expected->zones & Z(contexts[i].zone)) == 0U) continue;
         (void)snprintf(text, sizeof(text), "%s%s", contexts[i].prefix, expected->sample);
         expect_match(registry, text, false);
      }
      cwiki_snippet_registry_free(registry);
   }
}

static void
test_tables(struct cwiki_snippet_registry *registry)
{
   size_t i, j, count;
   const struct cwiki_snippet_catalog_entry *entries = cwiki_snippet_catalog_entries(&count);
   check(count == 99U && COUNT(symbols) == 68U && COUNT(structural) == 31U, "exact catalog counts");
   check(cwiki_snippet_catalog_entries(NULL) == entries, "optional count output");
   for (i = 0U; i < count; i++)
      for (j = 0U; j < i; j++) check(strcmp(entries[i].name, entries[j].name) != 0, "unique names");
   for (i = 0U; i < COUNT(symbols); i++) {
      char name[64], body[64], input[64], output[64];
      struct expected expected;
      (void)snprintf(name, sizeof(name), "vimtex.%s", symbols[i].command);
      (void)snprintf(body, sizeof(body), "\\%s$0", symbols[i].command);
      expected = (struct expected){name, symbols[i].trigger, body, symbols[i].trigger, MATH, true, false};
      verify_entry(&expected);
      (void)snprintf(input, sizeof(input), "$%s", symbols[i].trigger);
      (void)snprintf(output, sizeof(output), "$\\%s", symbols[i].command);
      expect_expansion(registry, input, output);
      (void)snprintf(input, sizeof(input), "\\[%s", symbols[i].trigger);
      (void)snprintf(output, sizeof(output), "\\[\\%s", symbols[i].command);
      expect_expansion(registry, input, output);
   }
   for (i = 0U; i < COUNT(structural); i++) verify_entry(&structural[i]);
}

static void
test_structural_expansions(struct cwiki_snippet_registry *registry)
{
   size_t i;
   expect_expansion(registry, "mk", "$$");
   expect_expansion(registry, "dm", "\\[\n\n\\]");
   expect_expansion(registry, "$//", "$\\frac{}{}");
   expect_expansion(registry, "$xsr", "$x^2");
   expect_expansion(registry, "$ycb", "$y^3");
   for (i = 0U; i < 10U; i++) {
      char input[32], expected[32];
      (void)snprintf(input, sizeof(input), "$Q%zu", i);
      (void)snprintf(expected, sizeof(expected), "$Q_{%zu}", i);
      expect_expansion(registry, input, expected);
   }
   expect_expansion(registry, "$zbar", "$\\overline{z}");
   expect_expansion(registry, "$phat", "$\\hat{p}");
   expect_expansion(registry, "$lim", "$\\lim_{n \\to \\infty} ");
   expect_expansion(registry, "$sum", "$\\sum_{i=1}^{n} ");
   expect_expansion(registry, "$int", "$\\int_{a}^{b} \\,dx");
   expect_expansion(registry, "$mat", "$\\begin{pmatrix}\n &  \\\\\n & \n\\end{pmatrix}");
   expect_expansion(registry, "env", "\\begin{align}\n\n\\end{align}");
   expect_expansion(registry, "$chem", "$\\ce{}");
   expect_expansion(registry, "$unit", "$\\pu{1 mol}");
   expect_expansion(registry, "$\\ce{reaction", "$\\ce{A -> B");
   expect_expansion(registry, "$\\ce{equilibrium", "$\\ce{A <=> B");
   expect_expansion(registry, "$\\ce{aqueous", "$\\ce{Na+(aq)");
   expect_expansion(registry, "tikz", "\\begin{tikzpicture}\n\n\\end{tikzpicture}");
   expect_expansion(registry, "\\begin{tikzpicture}draw", "\\begin{tikzpicture}\\draw (0,0) -- (1,1);");
   expect_expansion(registry, "\\begin{tikzpicture}node", "\\begin{tikzpicture}\\node (name) at (0,0) {};");
   expect_expansion(registry, "\\begin{tikzpicture}coord", "\\begin{tikzpicture}\\coordinate (name) at (0,0);");
   expect_match(registry, "bookmark", false);
   expect_match(registry, "$sublim", false);
   expect_match(registry, "$\\alpha1", false);
   expect_match(registry, "$\\x1", false);
   expect_match(registry, "$alphabetabar", false);
   expect_match(registry, "$\\phat", false);
   expect_match(registry, "$`j", false);
   expect_match(registry, "$`v", false);
   expect_match(registry, "$#bx", false);
}

static void
expand_at(struct cwiki_snippet_registry *registry, struct cwiki_buffer *buffer,
    struct cwiki_snippet_engine *engine, struct cwiki_position *cursor)
{
   size_t scanned;
   struct cwiki_snippet_match match = {0};
   check(cwiki_zone_recompute(zones, buffer, 0U, &scanned) == 0 &&
       cwiki_snippet_match(registry, zones, buffer, *cursor, CWIKI_SNIPPET_EXPLICIT,
       0U, &match) == CWIKI_SNIPPET_OK &&
       cwiki_snippet_expand(engine, &match, NULL, 0U, 1U, cursor) == CWIKI_SNIPPET_OK,
       "expand live session");
   cwiki_snippet_match_free(&match);
}

static void
replace_stop(struct cwiki_snippet_engine *engine, const char *value,
    struct cwiki_position *cursor)
{
   struct cwiki_position start, end;
   check(cwiki_snippet_current_stop(engine, &start, &end) == 0 &&
       cwiki_snippet_edit(engine, start, end, value, strlen(value), 2U, cursor) == CWIKI_SNIPPET_OK,
       "replace current tab stop");
}

static void
test_sessions(struct cwiki_snippet_registry *registry)
{
   struct cwiki_buffer buffer;
   struct cwiki_position cursor = load(&buffer, "$//");
   struct cwiki_snippet_engine *engine = NULL;
   struct cwiki_undo undo;
   check(cwiki_undo_init(&undo, &buffer) == 0 &&
       cwiki_snippet_engine_init(&engine, &buffer, &undo) == CWIKI_SNIPPET_OK, "session init");
   expand_at(registry, &buffer, engine, &cursor);
   replace_stop(engine, "//", &cursor);
   expand_at(registry, &buffer, engine, &cursor);
   check(cwiki_snippet_session_depth(engine) == 2U, "nested fraction session");
   replace_stop(engine, "α+7", &cursor);
   check(cwiki_snippet_next_stop(engine, &cursor) == 0, "nested denominator");
   replace_stop(engine, "b", &cursor);
   expect_buffer(&buffer, "$\\frac{\\frac{α+7}{b}}{}");
   check(cwiki_snippet_next_stop(engine, &cursor) == 0 &&
       cwiki_snippet_next_stop(engine, &cursor) == 1 &&
       cwiki_snippet_session_depth(engine) == 1U &&
       cwiki_snippet_next_stop(engine, &cursor) == 0, "return to outer denominator");
   replace_stop(engine, "c", &cursor);
   expect_buffer(&buffer, "$\\frac{\\frac{α+7}{b}}{c}");
   cwiki_snippet_engine_free(engine); cwiki_undo_free(&undo); cwiki_buffer_free(&buffer);

   cursor = load(&buffer, "env"); engine = NULL;
   check(cwiki_undo_init(&undo, &buffer) == 0 &&
       cwiki_snippet_engine_init(&engine, &buffer, &undo) == CWIKI_SNIPPET_OK, "mirror init");
   expand_at(registry, &buffer, engine, &cursor);
   replace_stop(engine, "gather*", &cursor);
   expect_buffer(&buffer, "\\begin{gather*}\n\n\\end{gather*}");
   check(cwiki_undo_to_parent(&undo) == 0, "mirror edit is one undo");
   expect_buffer(&buffer, "\\begin{align}\n\n\\end{align}");
   cwiki_snippet_engine_free(engine); cwiki_undo_free(&undo); cwiki_buffer_free(&buffer);
}

static void
test_overrides(void)
{
   struct cwiki_snippet_registry *registry = NULL;
   struct cwiki_snippet_spec replacement = find("math.square")->spec;
   const struct cwiki_snippet_body_spec body = {CWIKI_ZONE_PROSE, "custom$0", 8U};
   struct cwiki_snippet_catalog_override overrides[] = {
      {"vimtex.alpha", NULL}, {"math.square", &replacement},
      {"math.subscript-7", NULL}, {"chem.reaction", NULL}, {"tikz.draw", NULL}
   };
   const struct cwiki_snippet_catalog_override unknown = {"missing", NULL};
   const struct cwiki_snippet_catalog_override missing = {NULL, NULL};
   const struct cwiki_snippet_catalog_override duplicate[] = {
      {"math.square", NULL}, {"math.square", &replacement}
   };
   replacement.trigger = "squared"; replacement.trigger_length = 7U;
   replacement.required_zone = CWIKI_ZONE_PROSE;
   replacement.bodies = &body; replacement.body_count = 1U;
   check(cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK, "override registry");
   check(cwiki_snippet_catalog_install(NULL, NULL, 0U) == CWIKI_SNIPPET_INVALID &&
       cwiki_snippet_catalog_install(registry, NULL, 1U) == CWIKI_SNIPPET_INVALID &&
       cwiki_snippet_catalog_install(registry, &unknown, 1U) == CWIKI_SNIPPET_INVALID &&
       cwiki_snippet_catalog_install(registry, &missing, 1U) == CWIKI_SNIPPET_INVALID &&
       cwiki_snippet_catalog_install(registry, duplicate, COUNT(duplicate)) == CWIKI_SNIPPET_INVALID,
       "reject malformed override metadata");
   expect_match(registry, "$`0", false);
   {
      struct cwiki_snippet_spec existing = replacement;
      existing.trigger = "existing"; existing.trigger_length = 8U;
      existing.subject = "physics"; existing.subject_length = 7U;
      check(cwiki_snippet_registry_add(registry, &existing) == CWIKI_SNIPPET_OK &&
          cwiki_snippet_registry_set_subject(registry, "physics", 7U) == CWIKI_SNIPPET_OK,
          "existing subject-scoped definition");
   }
   check(cwiki_snippet_catalog_install(registry, overrides, COUNT(overrides)) == CWIKI_SNIPPET_OK,
       "install with name-based disable/replace");
   expect_expansion(registry, "existing", "custom");
   expect_expansion(registry, "squared", "custom");
   expect_match(registry, "$sr", false);
   expect_match(registry, "$`a", false);
   expect_match(registry, "$x7", false);
   expect_match(registry, "$\\ce{reaction", false);
   expect_match(registry, "\\begin{tikzpicture}draw", false);
   expect_expansion(registry, "$`b", "$\\beta");
   expect_expansion(registry, "$x8", "$x_{8}");
   cwiki_snippet_registry_free(registry);
   check(cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK, "invalid replacement registry");
   replacement.trigger_length = 0U;
   check(cwiki_snippet_catalog_install(registry, overrides, COUNT(overrides)) == CWIKI_SNIPPET_INVALID,
       "engine replacement validation propagates");
   cwiki_snippet_registry_free(registry);
}

int
main(void)
{
   struct cwiki_snippet_registry *registry = NULL;
   size_t count;
   uint64_t top;
   const struct cwiki_zone_region *regions = cwiki_zone_builtin_regions(&count, &top);
   check(cwiki_zone_engine_init(&zones, regions, count, top) == 0 &&
       cwiki_snippet_registry_init(&registry) == CWIKI_SNIPPET_OK &&
       cwiki_snippet_catalog_install(registry, NULL, 0U) == CWIKI_SNIPPET_OK, "catalog init");
   test_tables(registry);
   test_structural_expansions(registry);
   test_sessions(registry);
   test_overrides();
   cwiki_snippet_registry_free(registry);
   cwiki_zone_engine_free(zones);
   (void)printf("snippet catalog: %zu checks passed (68 symbols, 22 structural, 5 mhchem, 4 TikZ)\n", checks);
   return EXIT_SUCCESS;
}
