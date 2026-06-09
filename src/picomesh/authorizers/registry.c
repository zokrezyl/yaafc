/* Authorizer registry: build the one authorizer from config, run it. */

#include <picomesh/authorizers/registry.h>
#include <picomesh/config/config.h>
#include <picomesh/core/ytrace.h>

#include "types.h"

#include <stdlib.h>
#include <string.h>

struct picomesh_authorizer {
    const struct picomesh_authorizer_ops *ops;
    void *state;
};

static const struct picomesh_authorizer_ops *find_ops(const char *type_name)
{
    static const struct picomesh_authorizer_ops *(*const getters[])(void) = {
        picomesh_authorizer_policy_ops,
        picomesh_authorizer_none_ops,
    };
    for (size_t i = 0; i < sizeof(getters) / sizeof(getters[0]); ++i) {
        const struct picomesh_authorizer_ops *ops = getters[i]();
        if (ops && ops->type_name && strcmp(ops->type_name, type_name) == 0) return ops;
    }
    return NULL;
}

struct picomesh_void_ptr_result
picomesh_authorizer_build(struct picomesh_engine *engine, const struct config_node *config)
{
    if (!config || config_node_kind(config) != CONFIG_MAP)
        return PICOMESH_ERR(picomesh_void_ptr, "authorizer_build: `authorizer:` must be a map with a `type`");
    const char *type_name = config_node_as_string(config_node_get(config, "type"), NULL);
    if (!type_name)
        return PICOMESH_ERR(picomesh_void_ptr, "authorizer_build: `authorizer.type` is required");
    const struct picomesh_authorizer_ops *ops = find_ops(type_name);
    if (!ops)
        return PICOMESH_ERR(picomesh_void_ptr, "authorizer_build: unknown authorizer type");

    struct picomesh_authorizer *authorizer = calloc(1, sizeof(*authorizer));
    if (!authorizer) return PICOMESH_ERR(picomesh_void_ptr, "authorizer_build: out of memory");
    authorizer->ops = ops;
    struct picomesh_void_ptr_result state = ops->create(engine, config);
    if (PICOMESH_IS_ERR(state)) {
        free(authorizer);
        return PICOMESH_ERR(picomesh_void_ptr, "authorizer_build: create failed", state);
    }
    authorizer->state = state.value;
    ydebug("authorizer loaded: %s", type_name);
    return PICOMESH_OK(picomesh_void_ptr, authorizer);
}

struct picomesh_authz_decision_result
picomesh_authorizer_decide(struct picomesh_authorizer *authorizer, const char *endpoint,
                           const struct json_value *args, const char *jwt)
{
    if (!authorizer) {
        struct picomesh_authz_decision deny = {.allowed = 0, .status = 403};
        snprintf(deny.reason, sizeof(deny.reason), "no authorizer configured");
        return PICOMESH_OK(picomesh_authz_decision, deny);
    }
    return authorizer->ops->authorize(authorizer->state, endpoint, args, jwt);
}

void picomesh_authorizer_free(struct picomesh_authorizer *authorizer)
{
    if (!authorizer) return;
    if (authorizer->ops && authorizer->ops->destroy)
        authorizer->ops->destroy(authorizer->state);
    free(authorizer);
}
