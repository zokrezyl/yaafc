/* `session_cookie` authenticator — resolve an opaque session id (cookie or a
 * same-named forwarded header) to a JWT via a configured `lookup` RPC, then
 * verify it.
 *
 * Config:
 *   - type: session_cookie
 *     cookie: picomesh-sid                       # cookie name
 *     header: picomesh-sid                       # optional alt header
 *     lookup: session.session.session_jwt        # RPC: sid -> JWT (required)
 *
 * The authenticator knows nothing about how sessions are stored — it calls the
 * configured lookup path and treats the result as a JWT (or a payload carrying
 * one). The sid is a bearer secret; an sid that is present but resolves to no
 * valid JWT FAILS the chain (401), it does not fall through. */

#include <picomesh/authenticators/base.h>
#include <picomesh/engine/resolve.h>
#include <picomesh/config/config.h>
#include <picomesh/security/jwt_verifier.h>
#include <picomesh/security/jwt.h>      /* picomesh_security_now */
#include <uthash.h>                       /* the project's standard hash map */

#include "../http_util.h"
#include "../jwt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-worker sid -> JWT cache. WITHOUT it the gateway re-runs the `lookup` RPC
 * (a relational SELECT on the session store) on EVERY request — the dominant
 * per-request cost on the read hot path. The cache returns a recently-resolved
 * JWT for up to `cache_ttl` seconds with no round-trip and no re-verify (the
 * JWT was verified when cached). The fast path still enforces the JWT's own
 * `exp`, so a token cached just before expiry is not served past it. It is
 * per-worker (the authn chain is built once per worker) and
 * touched only by that worker's cooperative coroutines — map ops never yield,
 * and we never hold an entry pointer across the lookup RPC — so no locking.
 * Bounded staleness: a logout/refresh is not observed until the entry ages out,
 * so keep `cache_ttl` small. `cache_max` is a FIFO memory backstop. Set
 * `cache_ttl_seconds: 0` to disable.
 *
 * uthash (string-keyed by sid) is used rather than a hand-rolled table: it is
 * the project's standard map (class.c / engine.c / rpc.c), and chaining means a
 * hash collision keeps both entries instead of evicting one — no hit-rate
 * thrash between two sids that happen to collide. */
#define SESSION_CACHE_MAX_DEFAULT 8192

struct sid_cache_entry {
    char    sid[64];     /* key (an sid is 32 hex chars; 64 is ample) */
    char   *jwt;         /* malloc'd resolved JWT */
    int64_t inserted;    /* unix seconds (picomesh_security_now) */
    int64_t jwt_exp;     /* the cached JWT's own `exp` (unix seconds) */
    UT_hash_handle hh;
};

struct session_cookie_state {
    struct picomesh_engine *engine;
    const char *cookie;            /* cookie name (points into config) */
    const char *header;            /* alt header name */
    const char *lookup;            /* sid -> JWT RPC path */
    struct picomesh_jwt_verifier *verifier;
    int64_t cache_ttl;             /* seconds; <= 0 disables the cache */
    size_t  cache_max;             /* entry cap; oldest evicted (FIFO) over it */
    struct sid_cache_entry *cache; /* uthash root, keyed by sid; NULL when empty */
};

static struct picomesh_void_ptr_result session_cookie_create(struct picomesh_engine *engine,
                                                             const struct config_node *config)
{
    const char *cookie = config_node_as_string(config_node_get(config, "cookie"), "picomesh-sid");
    const char *header = config_node_as_string(config_node_get(config, "header"), cookie);
    const char *lookup = config_node_as_string(config_node_get(config, "lookup"), NULL);
    if (!lookup || !*lookup)
        return PICOMESH_ERR(picomesh_void_ptr,
                            "session_cookie: `lookup` is required (e.g. session.session.session_jwt)");

    struct session_cookie_state *state = calloc(1, sizeof(*state));
    if (!state) return PICOMESH_ERR(picomesh_void_ptr, "session_cookie: out of memory");
    state->engine = engine;
    state->cookie = cookie;
    state->header = header;
    state->lookup = lookup;
    state->cache_ttl = (int64_t)config_node_as_int(config_node_get(config, "cache_ttl_seconds"), 5);
    state->cache_max = (size_t)config_node_as_int(config_node_get(config, "cache_max"), SESSION_CACHE_MAX_DEFAULT);
    state->cache = NULL;   /* uthash root; grows lazily on first insert */
    struct picomesh_void_ptr_result verifier = picomesh_jwt_verifier_create(engine);
    if (PICOMESH_IS_ERR(verifier)) { free(state); return PICOMESH_ERR(picomesh_void_ptr, "session_cookie: verifier create failed", verifier); }
    state->verifier = verifier.value;
    return PICOMESH_OK(picomesh_void_ptr, state);
}

static struct picomesh_authn_outcome fail(const char *reason)
{
    struct picomesh_authn_outcome outcome = {0};
    outcome.source = "session_cookie";
    outcome.error = strdup(reason);
    return outcome;
}

static struct picomesh_authn_outcome_result session_cookie_authenticate(void *state_ptr,
                                                                 const struct picomesh_authn_request *request)
{
    struct session_cookie_state *state = state_ptr;
    struct picomesh_authn_outcome outcome = {0};

    char sid[160];
    if (!authn_cookie_value(request->headers_raw, request->headers_raw_len, state->cookie, sid, sizeof(sid)) || !sid[0]) {
        if (!authn_header_value(request->headers_raw, request->headers_raw_len, state->header, sid, sizeof(sid)) || !sid[0])
            return PICOMESH_OK(picomesh_authn_outcome, outcome); /* no cookie/header → no match, try next */
    }

    /* Cache fast path: a recently-resolved JWT for this sid → no RPC, no verify.
     * HASH_FIND + strdup are non-yielding, so they can't interleave with another
     * coroutine's mutation on this (single-threaded) worker. */
    int64_t now = picomesh_security_now();
    if (state->cache_ttl > 0) {
        struct sid_cache_entry *e = NULL;
        HASH_FIND_STR(state->cache, sid, e);
        if (e) {
            /* Honor BOTH the insertion TTL and the JWT's own expiry: a token
             * cached just before `exp` must not stay valid for the rest of the
             * TTL window. An expired cached JWT is dropped; the slow path then
             * re-resolves and fails closed if the session's JWT is truly expired. */
            if ((now - e->inserted) < state->cache_ttl && e->jwt_exp > now) {
                char *hit = strdup(e->jwt);
                if (hit) { outcome.jwt = hit; outcome.source = "session_cookie"; return PICOMESH_OK(picomesh_authn_outcome, outcome); }
                /* strdup OOM → fall through to the slow path */
            } else {
                /* stale TTL or expired JWT → drop it; the slow path re-resolves */
                HASH_DEL(state->cache, e); free(e->jwt); free(e);
            }
        }
    }

    char args[256];
    if (!authn_build_string_args(args, sizeof(args), sid))
        return PICOMESH_OK(picomesh_authn_outcome, fail("session id too long"));
    struct picomesh_string_result lookup = picomesh_engine_invoke_json(state->engine, state->lookup, args, NULL);
    /* The lookup RPC breaking is infrastructure failure, not an auth denial:
     * propagate the cause chain so it surfaces as a 500, not a silent 401. */
    if (PICOMESH_IS_ERR(lookup))
        return PICOMESH_ERR(picomesh_authn_outcome, "session_cookie: lookup RPC failed", lookup);

    char *jwt = authn_extract_jwt(lookup.value);
    free(lookup.value);
    if (!jwt) return PICOMESH_OK(picomesh_authn_outcome, fail("unknown or expired session"));

    struct picomesh_string_result claims = picomesh_jwt_verifier_verify(state->verifier, jwt);
    if (PICOMESH_IS_ERR(claims)) {
        /* A JWT that fails verification is a denial (401), but the reason chain
         * is useful — render it into the failure outcome and the log. */
        char reason[512];
        picomesh_error_snprint(reason, sizeof(reason), claims.error);
        picomesh_error_print(stderr, "session_cookie: JWT verification", claims.error);
        picomesh_error_destroy(claims.error);
        free(jwt);
        return PICOMESH_OK(picomesh_authn_outcome, fail(reason));
    }
    int64_t jwt_exp = authn_claims_exp(claims.value);
    free(claims.value);

    /* Populate the cache for subsequent requests on this worker. Re-find rather
     * than reuse any pre-RPC pointer: a concurrent coroutine may have inserted
     * this sid while we were parked on the lookup RPC. */
    if (state->cache_ttl > 0 && strlen(sid) < sizeof(((struct sid_cache_entry *)0)->sid)) {
        struct sid_cache_entry *entry = NULL;
        HASH_FIND_STR(state->cache, sid, entry);
        if (entry) {
            char *jwt_copy = strdup(jwt);
            if (jwt_copy) { free(entry->jwt); entry->jwt = jwt_copy; entry->inserted = now; entry->jwt_exp = jwt_exp; }
        } else {
            entry = calloc(1, sizeof(*entry));
            char *jwt_copy = entry ? strdup(jwt) : NULL;
            if (entry && jwt_copy) {
                snprintf(entry->sid, sizeof(entry->sid), "%s", sid);
                entry->jwt = jwt_copy; entry->inserted = now; entry->jwt_exp = jwt_exp;
                HASH_ADD_STR(state->cache, sid, entry);
                /* FIFO memory backstop: over the cap, evict the oldest (head). */
                if (state->cache_max && HASH_COUNT(state->cache) > state->cache_max) {
                    struct sid_cache_entry *oldest = state->cache;
                    HASH_DEL(state->cache, oldest); free(oldest->jwt); free(oldest);
                }
            } else { free(jwt_copy); free(entry); }
        }
    }

    outcome.jwt = jwt;
    outcome.source = "session_cookie";
    return PICOMESH_OK(picomesh_authn_outcome, outcome);
}

static void session_cookie_destroy(void *state_ptr)
{
    struct session_cookie_state *state = state_ptr;
    if (!state) return;
    struct sid_cache_entry *entry, *tmp;
    HASH_ITER(hh, state->cache, entry, tmp) {
        HASH_DEL(state->cache, entry); free(entry->jwt); free(entry);
    }
    picomesh_jwt_verifier_destroy(state->verifier);
    free(state);
}

const struct picomesh_authenticator_ops *picomesh_authenticator_session_cookie_ops(void)
{
    static const struct picomesh_authenticator_ops ops = {
        .type_name = "session_cookie",
        .create = session_cookie_create,
        .authenticate = session_cookie_authenticate,
        .destroy = session_cookie_destroy,
    };
    return &ops;
}
