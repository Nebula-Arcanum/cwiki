#include "snippet_catalog.h"

#include <string.h>

#define AUTO (CWIKI_SNIPPET_TRIGGER_AUTO | CWIKI_SNIPPET_TRIGGER_EXPLICIT)
#define EXPLICIT (CWIKI_SNIPPET_TRIGGER_EXPLICIT | CWIKI_SNIPPET_TRIGGER_WORD_BOUNDARY)
#define BODY(zone, text) {zone, text, sizeof(text) - 1U}
#define ENTRY(name, kind, trigger, zone, flags, count, ...) \
   {name, {kind, trigger, sizeof(trigger) - 1U, zone, \
       (const struct cwiki_snippet_body_spec[]){__VA_ARGS__}, count, \
       NULL, 0U, flags, 0, CWIKI_SNIPPET_DEFAULT_MATCH_LIMIT, \
       CWIKI_SNIPPET_DEFAULT_DEPTH_LIMIT}}
#define MATH(name, kind, trigger, body, flags) \
   ENTRY(name, kind, trigger, CWIKI_ZONE_MATH_INLINE, flags, 2U, \
       BODY(CWIKI_ZONE_MATH_INLINE, body), BODY(CWIKI_ZONE_MATH_DISPLAY, body))
#define SYMBOL(key, command) \
   MATH("vimtex." command, CWIKI_SNIPPET_LITERAL, "`" key, "\\" command "$0", AUTO)
#define SINGLE(name, trigger, body, zone) \
   ENTRY(name, CWIKI_SNIPPET_LITERAL, trigger, zone, EXPLICIT, 1U, BODY(zone, body))
#define SUBSCRIPT(digit) \
   MATH("math.subscript-" digit, CWIKI_SNIPPET_REGEX, \
       "(?<![A-Za-z\\\\])([A-Za-z])" digit, "${capture:1}_{" digit "}$0", AUTO)
#define SHELL(name, trigger, body) \
   ENTRY(name, CWIKI_SNIPPET_LITERAL, trigger, CWIKI_ZONE_PROSE, EXPLICIT, 3U, \
       BODY(CWIKI_ZONE_PROSE, body), BODY(CWIKI_ZONE_MATH_INLINE, body), \
       BODY(CWIKI_ZONE_MATH_DISPLAY, body))

static const struct cwiki_snippet_catalog_entry entries[] = {
   SYMBOL("0", "emptyset"), SYMBOL("2", "sqrt"),
   SYMBOL("6", "partial"), SYMBOL("8", "infty"),
   SYMBOL("=", "equiv"), SYMBOL("\\", "setminus"),
   SYMBOL(".", "cdot"), SYMBOL("*", "times"),
   SYMBOL("<", "langle"), SYMBOL(">", "rangle"),
   SYMBOL("H", "hbar"), SYMBOL("+", "dagger"),
   SYMBOL("[", "subseteq"), SYMBOL("]", "supseteq"),
   SYMBOL("(", "subset"), SYMBOL(")", "supset"),
   SYMBOL("A", "forall"), SYMBOL("B", "boldsymbol"),
   SYMBOL("E", "exists"), SYMBOL("N", "nabla"),
   SYMBOL("jj", "downarrow"), SYMBOL("jJ", "Downarrow"),
   SYMBOL("jk", "uparrow"), SYMBOL("jK", "Uparrow"),
   SYMBOL("jh", "leftarrow"), SYMBOL("jH", "Leftarrow"),
   SYMBOL("jl", "rightarrow"), SYMBOL("jL", "Rightarrow"),
   SYMBOL("a", "alpha"), SYMBOL("b", "beta"), SYMBOL("c", "chi"),
   SYMBOL("d", "delta"), SYMBOL("e", "epsilon"), SYMBOL("f", "phi"),
   SYMBOL("g", "gamma"), SYMBOL("h", "eta"), SYMBOL("i", "iota"),
   SYMBOL("k", "kappa"), SYMBOL("l", "lambda"), SYMBOL("m", "mu"),
   SYMBOL("n", "nu"), SYMBOL("p", "pi"), SYMBOL("q", "theta"),
   SYMBOL("r", "rho"), SYMBOL("s", "sigma"), SYMBOL("t", "tau"),
   SYMBOL("y", "psi"), SYMBOL("u", "upsilon"), SYMBOL("w", "omega"),
   SYMBOL("z", "zeta"), SYMBOL("x", "xi"), SYMBOL("D", "Delta"),
   SYMBOL("F", "Phi"), SYMBOL("G", "Gamma"), SYMBOL("L", "Lambda"),
   SYMBOL("P", "Pi"), SYMBOL("Q", "Theta"), SYMBOL("S", "Sigma"),
   SYMBOL("U", "Upsilon"), SYMBOL("W", "Omega"), SYMBOL("X", "Xi"),
   SYMBOL("Y", "Psi"), SYMBOL("ve", "varepsilon"),
   SYMBOL("vf", "varphi"), SYMBOL("vk", "varkappa"),
   SYMBOL("vp", "varpi"), SYMBOL("vq", "vartheta"), SYMBOL("vr", "varrho"),

   SINGLE("math.inline", "mk", "$$$1$$$0", CWIKI_ZONE_PROSE),
   SINGLE("math.display", "dm", "\\[\n$1\n\\]$0", CWIKI_ZONE_PROSE),
   MATH("math.fraction", CWIKI_SNIPPET_LITERAL, "//", "\\frac{$1}{$2}$0", AUTO),
   MATH("math.square", CWIKI_SNIPPET_LITERAL, "sr", "^2$0", AUTO),
   MATH("math.cube", CWIKI_SNIPPET_LITERAL, "cb", "^3$0", AUTO),
   SUBSCRIPT("0"), SUBSCRIPT("1"), SUBSCRIPT("2"), SUBSCRIPT("3"),
   SUBSCRIPT("4"), SUBSCRIPT("5"), SUBSCRIPT("6"), SUBSCRIPT("7"),
   SUBSCRIPT("8"), SUBSCRIPT("9"),
   MATH("math.bar", CWIKI_SNIPPET_REGEX, "(?<![A-Za-z\\\\])([A-Za-z])bar",
       "\\overline{${capture:1}}$0", AUTO),
   MATH("math.hat", CWIKI_SNIPPET_REGEX, "(?<![A-Za-z\\\\])([A-Za-z])hat",
       "\\hat{${capture:1}}$0", AUTO),
   MATH("math.limit", CWIKI_SNIPPET_LITERAL, "lim",
       "\\lim_{${1:n} \\to ${2:\\infty}} $0", EXPLICIT),
   MATH("math.sum", CWIKI_SNIPPET_LITERAL, "sum",
       "\\sum_{${1:i}=${2:1}}^{${3:n}} $0", EXPLICIT),
   MATH("math.integral", CWIKI_SNIPPET_LITERAL, "int",
       "\\int_{${1:a}}^{${2:b}} $3\\,d${4:x}$0", EXPLICIT),
   MATH("math.matrix", CWIKI_SNIPPET_LITERAL, "mat",
       "\\begin{pmatrix}\n$1 & $2 \\\\\n$3 & $4\n\\end{pmatrix}$0", EXPLICIT),
   SHELL("math.environment", "env", "\\begin{${1:align}}\n$2\n\\end{$1}$0"),

   MATH("chem.ce", CWIKI_SNIPPET_LITERAL, "chem", "\\ce{$1}$0", EXPLICIT),
   MATH("chem.pu", CWIKI_SNIPPET_LITERAL, "unit", "\\pu{${1:1} ${2:mol}}$0", EXPLICIT),
   SINGLE("chem.reaction", "reaction", "${1:A} -> ${2:B}$0", CWIKI_ZONE_CHEMISTRY),
   SINGLE("chem.equilibrium", "equilibrium", "${1:A} <=> ${2:B}$0", CWIKI_ZONE_CHEMISTRY),
   SINGLE("chem.aqueous", "aqueous", "${1:Na+}(aq)$0", CWIKI_ZONE_CHEMISTRY),

   SINGLE("tikz.environment", "tikz", "\\begin{tikzpicture}\n$1\n\\end{tikzpicture}$0", CWIKI_ZONE_PROSE),
   SINGLE("tikz.draw", "draw", "\\draw (${1:0,0}) -- (${2:1,1});$0", CWIKI_ZONE_TIKZ),
   SINGLE("tikz.node", "node", "\\node (${1:name}) at (${2:0,0}) {$3};$0", CWIKI_ZONE_TIKZ),
   SINGLE("tikz.coordinate", "coord", "\\coordinate (${1:name}) at (${2:0,0});$0", CWIKI_ZONE_TIKZ)
};

const struct cwiki_snippet_catalog_entry *
cwiki_snippet_catalog_entries(size_t *count)
{
   if (count != NULL) *count = sizeof(entries) / sizeof(entries[0]);
   return entries;
}

enum cwiki_snippet_status
cwiki_snippet_catalog_install(struct cwiki_snippet_registry *registry,
    const struct cwiki_snippet_catalog_override *overrides, size_t override_count)
{
   size_t i, j, count = sizeof(entries) / sizeof(entries[0]);
   if (registry == NULL || (overrides == NULL && override_count != 0U))
      return CWIKI_SNIPPET_INVALID;
   for (i = 0U; i < override_count; i++) {
      if (overrides[i].name == NULL) return CWIKI_SNIPPET_INVALID;
      for (j = 0U; j < count; j++)
         if (strcmp(overrides[i].name, entries[j].name) == 0) break;
      if (j == count) return CWIKI_SNIPPET_INVALID;
      for (j = 0U; j < i; j++)
         if (strcmp(overrides[i].name, overrides[j].name) == 0)
            return CWIKI_SNIPPET_INVALID;
   }
   for (i = 0U; i < count; i++) {
      const struct cwiki_snippet_spec *spec = &entries[i].spec;
      enum cwiki_snippet_status status;
      for (j = 0U; j < override_count; j++) {
         if (strcmp(entries[i].name, overrides[j].name) == 0) {
            spec = overrides[j].replacement;
            break;
         }
      }
      if (spec == NULL) continue;
      status = cwiki_snippet_registry_add(registry, spec);
      if (status != CWIKI_SNIPPET_OK) return status;
   }
   return CWIKI_SNIPPET_OK;
}
