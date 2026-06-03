/* authz — the gateway's per-request authenticator chain + policy authorizer
 * (issue #19, docs/security.md).
 *
 * A client-facing frontend turns an accepted credential into a verified JWT
 * (or anonymous), then a policy authorizer decides whether that identity may
 * invoke the requested endpoint with these arguments — before any backend
 * service is touched. Both are config-driven, so adding a backend method does
 * not expose it by default (absent from policy → denied).
 *
 * This pipeline runs ONLY on a node that has `security.authenticators`
 * configured (the picoforge gateway). A plain transport bridge has no security
 * block and is therefore never gated here. */

#ifndef PICOMESH_FRONTENDS_YHTTP_AUTHZ_H
#define PICOMESH_FRONTENDS_YHTTP_AUTHZ_H

#include <stddef.h>

#include <picomesh/ysecurity/jwt.h>

struct picomesh_engine;
struct yjson_value;

/* 1 if this node is configured as an auth boundary (has
 * `security.authenticators`). */
int picomesh_security_configured(struct picomesh_engine *engine);

/* Outcome of running the authenticator chain. */
struct picomesh_authn_outcome {
    int http_status;       /* 0 ok (possibly anonymous); 401 invalid credential;
                            * 500 security misconfigured (no signing secret) */
    char *jwt;             /* owned verified JWT, or NULL when anonymous */
    struct picomesh_authctx claims; /* verified identity (authenticated=0 if anonymous) */
};

/* Run the configured authenticator chain over the raw request headers. The
 * first authenticator whose credential shape is present wins; a present-but-
 * invalid credential fails closed (401) and does NOT fall through to a weaker
 * scheme. No credential → anonymous. */
struct picomesh_authn_outcome picomesh_gateway_authenticate(struct picomesh_engine *engine,
                                                            const char *headers_raw,
                                                            size_t headers_raw_len);
void picomesh_authn_outcome_free(struct picomesh_authn_outcome *outcome);

/* Authorize a fully-qualified `endpoint` ("service.class.method") for the
 * verified `claims`, using the request `args` where a rule needs them.
 * Returns 0 to allow, 401 if authentication is required but absent, or 403 if
 * the endpoint is absent from policy (default deny) or the role is
 * insufficient. */
int picomesh_gateway_authorize(struct picomesh_engine *engine, const char *endpoint,
                               const struct yjson_value *args,
                               const struct picomesh_authctx *claims);

/* 1 if `endpoint` is a credential-exchange method (session→JWT, refresh, PAT
 * lookup, login, mint) that must never be reachable via public /_rpc — these
 * are authenticator-internal exchanges, not open data endpoints. */
int picomesh_is_credential_exchange(const char *endpoint);

#endif /* PICOMESH_FRONTENDS_YHTTP_AUTHZ_H */
