#ifndef CWIKI_TESTS_SUPPORT_FIXTURE_VAULT_H
#define CWIKI_TESTS_SUPPORT_FIXTURE_VAULT_H

struct fixture_vault {
   char *root;
};

int fixture_vault_create(struct fixture_vault *vault);
int fixture_vault_destroy(struct fixture_vault *vault);
const char *fixture_vault_root(const struct fixture_vault *vault);
/* Resolves an existing path. The caller owns *resolved_path on success. */
int fixture_vault_resolve(const struct fixture_vault *vault,
    const char *relative_path, char **resolved_path);

#endif
