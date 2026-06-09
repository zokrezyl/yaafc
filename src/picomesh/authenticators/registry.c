/* Authenticator registry: build the chain from config, run it. */

#include <picomesh/authenticators/registry.h>
#include <picomesh/config/config.h>
#include <picomesh/core/ytrace.h>

#include "types.h"

#include <stdlib.h>
#include <string.h>

struct chain_entry {
    const struct picomesh_authenticator_ops *ops;
    void *state;
};

struct picomesh_authn_chain {
    struct chain_entry *entries;
    size_t count;
};

/* Resolve a type name to its ops via a function-local registry table (no
 * file-scope data). Adding a type = one line here. */
static const struct picomesh_authenticator_ops *find_ops(const char *type_name)
{
    static const struct picomesh_authenticator_ops *(*const getters[])(void) = {
        picomesh_authenticator_session_cookie_ops,
        picomesh_authenticator_bearer_jwt_token_ops,
        picomesh_authenticator_bearer_opaque_token_ops,
    };
    for (size_t i = 0; i < sizeof(getters) / sizeof(getters[0]); ++i) {
        const struct picomesh_authenticator_ops *ops = getters[i]();
        if (ops && ops->type_name && strcmp(ops->type_name, type_name) == 0) return ops;
    }
    return NULL;
}

struct picomesh_void_ptr_result
picomesh_authn_chain_build(struct picomesh_engine *engine, const struct config_node *list)
{
    struct picomesh_authn_chain *chain = calloc(1, sizeof(*chain));
    if (!chain) return PICOMESH_ERR(picomesh_void_ptr, "authn_chain_build: out of memory");
    if (!list || config_node_kind(list) != CONFIG_LIST)
        return PICOMESH_OK(picomesh_void_ptr, chain); /* empty chain → always anonymous */

    size_t count = config_node_size(list);
    if (count) {
        chain->entries = calloc(count, sizeof(*chain->entries));
        if (!chain->entries) {
            free(chain);
            return PICOMESH_ERR(picomesh_void_ptr, "authn_chain_build: out of memory");
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const struct config_node *entry = config_node_at(list, i);
        if (!entry || config_node_kind(entry) != CONFIG_MAP) {
            picomesh_authn_chain_free(chain);
            return PICOMESH_ERR(picomesh_void_ptr, "authn_chain_build: authenticator entry must be a map");
        }
        const struct config_node *type_node = config_node_get(entry, "type");
        const char *type_name = type_node ? config_node_as_string(type_node, NULL) : NULL;
        if (!type_name) {
            picomesh_authn_chain_free(chain);
            return PICOMESH_ERR(picomesh_void_ptr, "authn_chain_build: authenticator entry missing `type`");
        }
        const struct picomesh_authenticator_ops *ops = find_ops(type_name);
        if (!ops) {
            picomesh_authn_chain_free(chain);
            return PICOMESH_ERR(picomesh_void_ptr, "authn_chain_build: unknown authenticator type");
        }
        struct picomesh_void_ptr_result state = ops->create(engine, entry);
        if (PICOMESH_IS_ERR(state)) {
            picomesh_authn_chain_free(chain);
            return PICOMESH_ERR(picomesh_void_ptr, "authn_chain_build: authenticator create failed", state);
        }
        chain->entries[chain->count].ops = ops;
        chain->entries[chain->count].state = state.value;
        chain->count++;
        ydebug("authenticator loaded: %s", type_name);
    }
    return PICOMESH_OK(picomesh_void_ptr, chain);
}

struct picomesh_authn_outcome_result
picomesh_authn_chain_run(struct picomesh_authn_chain *chain,
                         const struct picomesh_authn_request *request)
{
    struct picomesh_authn_outcome outcome = {0};
    if (!chain) return PICOMESH_OK(picomesh_authn_outcome, outcome);
    for (size_t i = 0; i < chain->count; ++i) {
        struct picomesh_authn_outcome_result one =
            chain->entries[i].ops->authenticate(chain->entries[i].state, request);
        /* An authenticator's infrastructure failure aborts the chain with its
         * full cause chain — never silently downgraded to anonymous. */
        PICOMESH_RETURN_IF_ERR(picomesh_authn_outcome, one, "authn chain: authenticator failed");
        outcome = one.value;
        if (picomesh_authn_outcome_matched(&outcome) || picomesh_authn_outcome_failed(&outcome))
            return PICOMESH_OK(picomesh_authn_outcome, outcome); /* first match wins; a failure short-circuits */
        /* no match → try the next authenticator */
    }
    return PICOMESH_OK(picomesh_authn_outcome, outcome); /* anonymous */
}

void picomesh_authn_outcome_free(struct picomesh_authn_outcome *outcome)
{
    if (!outcome) return;
    free(outcome->jwt);
    free(outcome->error);
    outcome->jwt = NULL;
    outcome->error = NULL;
}

void picomesh_authn_chain_free(struct picomesh_authn_chain *chain)
{
    if (!chain) return;
    for (size_t i = 0; i < chain->count; ++i)
        if (chain->entries[i].ops && chain->entries[i].ops->destroy)
            chain->entries[i].ops->destroy(chain->entries[i].state);
    free(chain->entries);
    free(chain);
}
