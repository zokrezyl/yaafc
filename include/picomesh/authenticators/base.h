/* Authenticator framework — base types (issue: pluggable authn/authz).
 *
 * An authenticator inspects an inbound request and tries to resolve it to a
 * verified JWT. It does NOT decide allow/deny — that is the authorizer's job.
 * Authenticators are a pluggable framework category, like frontends: each
 * concrete type registers its ops and is selected by `type:` in config. They
 * run in-process in the frontend pipeline (the hot path) and reach real mesh
 * services only for credential exchange / key material (e.g. a configured
 * `lookup` RPC).
 *
 * Outcome contract (mirrors yaapp):
 *   - no match  : this authenticator's credential shape isn't present → the
 *                 chain tries the next one.
 *   - match     : a credential was found and resolved to a VALID JWT → first
 *                 match wins.
 *   - failure   : a credential of this shape was present but INVALID → the
 *                 chain stops with 401 (no downgrade to a weaker scheme). */

#ifndef PICOMESH_AUTHENTICATORS_BASE_H
#define PICOMESH_AUTHENTICATORS_BASE_H

#include <stddef.h>

#include <picomesh/core/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct picomesh_engine;
struct config_node;

/* Transport-agnostic view of an inbound request. Authenticators read cookies
 * and headers out of the raw HTTP header block (the yhttp frontend supplies
 * it; a yrpc/msgpack frontend would adapt its own metadata to the same view).
 * `endpoint` is for log lines only, never for the authn decision. */
struct picomesh_authn_request {
    struct picomesh_engine *engine;
    const char *headers_raw;
    size_t headers_raw_len;
    const char *endpoint;
};

/* Result of one authenticator (and, after run, of the whole chain). `jwt` and
 * `error` are owned (malloc'd); `source` is the matching type's static name. */
struct picomesh_authn_outcome {
    char *jwt;          /* verified JWT to forward, or NULL when no match */
    const char *source; /* type_name of the matching authenticator (static) */
    char *error;        /* failure reason when a credential matched but was invalid */
};

static inline int picomesh_authn_outcome_matched(const struct picomesh_authn_outcome *outcome)
{
    return outcome && outcome->jwt != NULL;
}
static inline int picomesh_authn_outcome_failed(const struct picomesh_authn_outcome *outcome)
{
    return outcome && outcome->error != NULL;
}

/* OK carries the outcome (match / denial / no-match — all normal authn data);
 * ERR carries an infrastructure failure (e.g. a credential-exchange RPC broke)
 * with its full cause chain, distinct from an auth denial. */
PICOMESH_RESULT_DECLARE(picomesh_authn_outcome, struct picomesh_authn_outcome);

/* The ops a concrete authenticator type implements. */
struct picomesh_authenticator_ops {
    const char *type_name;
    /* Parse `config` (the yaml entry under `authenticators:`) into instance
     * state. OK value is the owned state pointer (freed via destroy). */
    struct picomesh_void_ptr_result (*create)(struct picomesh_engine *engine,
                                              const struct config_node *config);
    /* Inspect the request; fill an outcome per the contract above. ERR is
     * reserved for infrastructure failures (the cause chain propagates up to
     * a 500); an auth denial is a normal OK outcome carrying `.error`. */
    struct picomesh_authn_outcome_result (*authenticate)(void *state,
                                                  const struct picomesh_authn_request *request);
    void (*destroy)(void *state);
};

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_AUTHENTICATORS_BASE_H */
