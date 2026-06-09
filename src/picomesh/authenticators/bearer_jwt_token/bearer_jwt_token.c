/* `bearer_jwt_token` authenticator — accept a JWT presented directly as
 * `Authorization: Bearer <jwt>` and verify it.
 *
 * Config:
 *   - type: bearer_jwt_token
 *     reject_prefixes: ["pat_", "rnr_"]   # bearer values with these prefixes
 *                                          # belong to an opaque-token scheme,
 *                                          # so this authenticator ignores them
 *
 * For API clients that legitimately hold a JWT. A bearer value matching a
 * configured reject-prefix is NOT this authenticator's credential shape (it is
 * an opaque token) → no match, the chain tries the opaque authenticator. A
 * non-prefixed bearer that fails verification FAILS the chain (401). */

#include <picomesh/authenticators/base.h>
#include <picomesh/config/config.h>
#include <picomesh/security/jwt_verifier.h>

#include "../http_util.h"

#include <stdlib.h>
#include <string.h>

#define BEARER_JWT_MAX_PREFIXES 8

struct bearer_jwt_state {
    struct picomesh_engine *engine;
    const char *reject_prefixes[BEARER_JWT_MAX_PREFIXES]; /* point into config */
    size_t reject_count;
    struct picomesh_jwt_verifier *verifier;
};

static struct picomesh_void_ptr_result bearer_jwt_create(struct picomesh_engine *engine,
                                                         const struct config_node *config)
{
    struct bearer_jwt_state *state = calloc(1, sizeof(*state));
    if (!state) return PICOMESH_ERR(picomesh_void_ptr, "bearer_jwt_token: out of memory");
    state->engine = engine;

    const struct config_node *rejects = config_node_get(config, "reject_prefixes");
    if (rejects && config_node_kind(rejects) == CONFIG_LIST) {
        size_t count = config_node_size(rejects);
        for (size_t i = 0; i < count && state->reject_count < BEARER_JWT_MAX_PREFIXES; ++i) {
            const char *prefix = config_node_as_string(config_node_at(rejects, i), NULL);
            if (prefix && *prefix) state->reject_prefixes[state->reject_count++] = prefix;
        }
    }

    struct picomesh_void_ptr_result verifier = picomesh_jwt_verifier_create(engine);
    if (PICOMESH_IS_ERR(verifier)) { free(state); return PICOMESH_ERR(picomesh_void_ptr, "bearer_jwt_token: verifier create failed", verifier); }
    state->verifier = verifier.value;
    return PICOMESH_OK(picomesh_void_ptr, state);
}

static struct picomesh_authn_outcome_result bearer_jwt_authenticate(void *state_ptr,
                                                             const struct picomesh_authn_request *request)
{
    struct bearer_jwt_state *state = state_ptr;
    struct picomesh_authn_outcome outcome = {0};

    char token[1100];
    if (!authn_bearer_token(request->headers_raw, request->headers_raw_len, token, sizeof(token)) || !token[0])
        return PICOMESH_OK(picomesh_authn_outcome, outcome); /* no bearer → no match */

    for (size_t i = 0; i < state->reject_count; ++i) {
        size_t prefix_len = strlen(state->reject_prefixes[i]);
        if (strncmp(token, state->reject_prefixes[i], prefix_len) == 0)
            return PICOMESH_OK(picomesh_authn_outcome, outcome); /* belongs to an opaque scheme → not our shape */
    }

    struct picomesh_string_result claims = picomesh_jwt_verifier_verify(state->verifier, token);
    if (PICOMESH_IS_ERR(claims)) {
        /* Verification failure is a denial (401); preserve the reason chain in
         * the outcome and the log rather than flattening it to one line. */
        char reason[512];
        picomesh_error_snprint(reason, sizeof(reason), claims.error);
        picomesh_error_print(stderr, "bearer_jwt_token: JWT verification", claims.error);
        picomesh_error_destroy(claims.error);
        outcome.source = "bearer_jwt_token";
        outcome.error = strdup(reason);
        return PICOMESH_OK(picomesh_authn_outcome, outcome);
    }
    free(claims.value);
    outcome.jwt = strdup(token);
    outcome.source = "bearer_jwt_token";
    return PICOMESH_OK(picomesh_authn_outcome, outcome);
}

static void bearer_jwt_destroy(void *state_ptr)
{
    struct bearer_jwt_state *state = state_ptr;
    if (!state) return;
    picomesh_jwt_verifier_destroy(state->verifier);
    free(state);
}

const struct picomesh_authenticator_ops *picomesh_authenticator_bearer_jwt_token_ops(void)
{
    static const struct picomesh_authenticator_ops ops = {
        .type_name = "bearer_jwt_token",
        .create = bearer_jwt_create,
        .authenticate = bearer_jwt_authenticate,
        .destroy = bearer_jwt_destroy,
    };
    return &ops;
}
