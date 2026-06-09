/* `bearer_opaque_token` authenticator — exchange an opaque bearer token for a
 * JWT via a configured `lookup` service, then verify it.
 *
 * Config (one entry PER opaque scheme — PATs, runner tokens, …):
 *   - type: bearer_opaque_token
 *     prefix: "pat_"                                          # which tokens are ours
 *     lookup: personal_access_tokens.personal_access_tokens.lookup
 *   - type: bearer_opaque_token
 *     prefix: "rnr_"
 *     lookup: runner_agent.runner_agent.lookup_token
 *
 * The authenticator does NOT parse the token, know about PAT ids, or mint
 * anything. It passes the whole opaque token to the configured `lookup`; the
 * lookup service returns a JWT (or a payload carrying one), which this
 * authenticator verifies before forwarding. An opaque token that matches our
 * prefix but resolves to no valid JWT FAILS the chain (401). */

#include <picomesh/authenticators/base.h>
#include <picomesh/engine/resolve.h>
#include <picomesh/config/config.h>
#include <picomesh/security/jwt_verifier.h>

#include "../http_util.h"
#include "../jwt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bearer_opaque_state {
    struct picomesh_engine *engine;
    const char *prefix;   /* required: which bearer tokens this entry claims */
    const char *lookup;   /* required: opaque token -> JWT RPC path */
    struct picomesh_jwt_verifier *verifier;
};

static struct picomesh_void_ptr_result bearer_opaque_create(struct picomesh_engine *engine,
                                                            const struct config_node *config)
{
    const char *prefix = config_node_as_string(config_node_get(config, "prefix"), NULL);
    const char *lookup = config_node_as_string(config_node_get(config, "lookup"), NULL);
    if (!prefix || !*prefix)
        return PICOMESH_ERR(picomesh_void_ptr, "bearer_opaque_token: `prefix` is required (e.g. \"pat_\")");
    if (!lookup || !*lookup)
        return PICOMESH_ERR(picomesh_void_ptr, "bearer_opaque_token: `lookup` is required (opaque token -> JWT)");

    struct bearer_opaque_state *state = calloc(1, sizeof(*state));
    if (!state) return PICOMESH_ERR(picomesh_void_ptr, "bearer_opaque_token: out of memory");
    state->engine = engine;
    state->prefix = prefix;
    state->lookup = lookup;
    struct picomesh_void_ptr_result verifier = picomesh_jwt_verifier_create(engine);
    if (PICOMESH_IS_ERR(verifier)) { free(state); return PICOMESH_ERR(picomesh_void_ptr, "bearer_opaque_token: verifier create failed", verifier); }
    state->verifier = verifier.value;
    return PICOMESH_OK(picomesh_void_ptr, state);
}

static struct picomesh_authn_outcome fail(const char *reason)
{
    struct picomesh_authn_outcome outcome = {0};
    outcome.source = "bearer_opaque_token";
    outcome.error = strdup(reason);
    return outcome;
}

static struct picomesh_authn_outcome_result bearer_opaque_authenticate(void *state_ptr,
                                                                const struct picomesh_authn_request *request)
{
    struct bearer_opaque_state *state = state_ptr;
    struct picomesh_authn_outcome outcome = {0};

    char token[512];
    if (!authn_bearer_token(request->headers_raw, request->headers_raw_len, token, sizeof(token)) || !token[0])
        return PICOMESH_OK(picomesh_authn_outcome, outcome); /* no bearer → no match */
    if (strncmp(token, state->prefix, strlen(state->prefix)) != 0)
        return PICOMESH_OK(picomesh_authn_outcome, outcome); /* not our prefix → let another opaque entry try */

    char args[1100];
    if (!authn_build_string_args(args, sizeof(args), token))
        return PICOMESH_OK(picomesh_authn_outcome, fail("opaque token too long"));

    struct picomesh_string_result lookup = picomesh_engine_invoke_json(state->engine, state->lookup, args, NULL);
    /* The lookup RPC breaking is infrastructure failure: propagate the chain
     * (→ 500), don't flatten it into a 401 denial. */
    if (PICOMESH_IS_ERR(lookup))
        return PICOMESH_ERR(picomesh_authn_outcome, "bearer_opaque_token: lookup RPC failed", lookup);

    char *jwt = authn_extract_jwt(lookup.value);
    free(lookup.value);
    if (!jwt) return PICOMESH_OK(picomesh_authn_outcome, fail("opaque token did not resolve to a JWT"));

    struct picomesh_string_result claims = picomesh_jwt_verifier_verify(state->verifier, jwt);
    if (PICOMESH_IS_ERR(claims)) {
        char reason[512];
        picomesh_error_snprint(reason, sizeof(reason), claims.error);
        picomesh_error_print(stderr, "bearer_opaque_token: JWT verification", claims.error);
        picomesh_error_destroy(claims.error);
        free(jwt);
        return PICOMESH_OK(picomesh_authn_outcome, fail(reason));
    }
    free(claims.value);

    outcome.jwt = jwt;
    outcome.source = "bearer_opaque_token";
    return PICOMESH_OK(picomesh_authn_outcome, outcome);
}

static void bearer_opaque_destroy(void *state_ptr)
{
    struct bearer_opaque_state *state = state_ptr;
    if (!state) return;
    picomesh_jwt_verifier_destroy(state->verifier);
    free(state);
}

const struct picomesh_authenticator_ops *picomesh_authenticator_bearer_opaque_token_ops(void)
{
    static const struct picomesh_authenticator_ops ops = {
        .type_name = "bearer_opaque_token",
        .create = bearer_opaque_create,
        .authenticate = bearer_opaque_authenticate,
        .destroy = bearer_opaque_destroy,
    };
    return &ops;
}
