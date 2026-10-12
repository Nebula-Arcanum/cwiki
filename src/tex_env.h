#ifndef CWIKI_TEX_ENV_H
#define CWIKI_TEX_ENV_H

#include <stdbool.h>
#include <stddef.h>

/* One user-declared package -> environments entry (R2.9.5, R1.14.2). */
struct cwiki_tex_env_user_entry {
   const char *package;
   const char *const *names;
   size_t name_count;
};

struct cwiki_tex_env_set;

/*
 * Builds the set of environments in force: LaTeX core always, each shipped
 * or user table once its package is loaded (via `packages`, \usepackage or
 * \RequirePackage in either preamble), and every environment the preambles
 * declare. Returns 0, -1 on allocation failure, or -2 when a regex limit was
 * hit (never a silent no-match, R1.11.4). On failure *set is NULL.
 */
int cwiki_tex_env_build(struct cwiki_tex_env_set **set, const char *vault,
    size_t vault_length, const char *note, size_t note_length,
    const char *const *packages, size_t package_count,
    const struct cwiki_tex_env_user_entry *user, size_t user_count);
void cwiki_tex_env_free(struct cwiki_tex_env_set *set);
bool cwiki_tex_env_available(const struct cwiki_tex_env_set *set,
    const char *name);
/* Providing package ("latex", "amsmath", "preamble", ...) or NULL. */
const char *cwiki_tex_env_provider(const struct cwiki_tex_env_set *set,
    const char *name);
bool cwiki_tex_env_package_loaded(const struct cwiki_tex_env_set *set,
    const char *package);

#endif
