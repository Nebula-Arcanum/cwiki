#ifndef CWIKI_SNIPPET_CATALOG_H
#define CWIKI_SNIPPET_CATALOG_H

#include "snippet.h"

struct cwiki_snippet_catalog_entry {
   const char *name;
   struct cwiki_snippet_spec spec;
};

struct cwiki_snippet_catalog_override {
   const char *name;
   /* NULL disables this entry; otherwise replaces its entire specification. */
   const struct cwiki_snippet_spec *replacement;
};

/* Immutable defaults, in stable order. Names, not array indices, are identity.
 * VimTeX's 68 ordinary backtick mappings preserve exact command bytes (no
 * extra spaces or braces), with $0 added for the snippet engine. Source:
 * https://github.com/lervag/vimtex/blob/16a5609d17a436db7f48046d91747fd6a9d75c74/autoload/vimtex/options.vim#L189-L270
 * The six # leader getchar() style mappings and leader-escape mapping are not
 * included: reading the next arbitrary key is not the engine's suffix-trigger
 * model. No partial emulation or scripting is installed.
 *
 * Math definitions gate on innermost inline/display math. Auto-subscript is
 * ten named entries because regex dispatch requires a literal final character.
 * Postfix accents intentionally cover single ASCII letters, not TeX commands
 * or arbitrary balanced expressions. Shells and mnemonic chemistry/TikZ
 * templates require explicit expansion at a word boundary. The chem/unit
 * shells require math: M1 recognizes \ce{} only inside math. Chemistry-internal
 * templates are restricted to \ce{} and avoid nested brace groups, whose
 * closing braces end M1's chemistry zone early. \pu{} has no dedicated zone
 * in M1, so its contents retain the enclosing math context.
 */
const struct cwiki_snippet_catalog_entry *cwiki_snippet_catalog_entries(
    size_t *count);

/* Append defaults to an existing registry, applying overrides BEFORE adding
 * them. Call once per registry; rebuild it to change overrides. Unknown names,
 * duplicate overrides and missing inputs are rejected before any addition.
 * Replacement specifications use the existing engine's validation/ownership:
 * strings are copied, and callers may release them after this call.
 * An engine error may leave earlier entries installed; discard/rebuild that
 * registry on failure (there is no engine batch rollback/removal API).
 * Existing registry entries and subject selection are preserved.
 */
enum cwiki_snippet_status cwiki_snippet_catalog_install(
    struct cwiki_snippet_registry *registry,
    const struct cwiki_snippet_catalog_override *overrides,
    size_t override_count);

#endif
