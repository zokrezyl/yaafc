/* Gateway authenticator chain + policy authorizer. See authz.h. */

#define _POSIX_C_SOURCE 200809L

#include "authz.h"

#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/yclass/class.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/secret.h>
#include <picomesh/plugin/session/session.h>
#include <picomesh/plugin/accounts/accounts.h>
#include <picomesh/plugin/token_issuer/token_issuer.h>
#include <picomesh/plugin/personal_access_tokens/personal_access_tokens.h>
#include <picomesh/plugin/runner_agent/runner_agent.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------- raw-header helpers (independent of frontend.c) ---------- */

static int header_line_value(const char *raw, size_t raw_len, const char *name,
                             char *out, size_t out_cap)
{
    size_t nlen = strlen(name);
    const char *p = raw;
    const char *end = raw + raw_len;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol) break;
        size_t llen = (size_t)(eol - p);
        if (llen && p[llen - 1] == '\r') llen--;
        if (llen > nlen + 1 && p[nlen] == ':' && strncasecmp(p, name, nlen) == 0) {
            const char *value = p + nlen + 1;
            while (value < p + llen && (*value == ' ' || *value == '\t')) ++value;
            size_t value_len = (size_t)(p + llen - value);
            if (value_len >= out_cap) value_len = out_cap - 1;
            memcpy(out, value, value_len);
            out[value_len] = 0;
            return 1;
        }
        p = eol + 1;
    }
    return 0;
}

static int cookie_value(const char *raw, size_t raw_len, const char *name,
                        char *out, size_t out_cap)
{
    out[0] = 0;
    size_t nlen = strlen(name);
    const char *p = raw;
    const char *end = raw + raw_len;
    while (p < end) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        if (!line_end) line_end = end;
        if (line_end - p > 7 && strncasecmp(p, "Cookie:", 7) == 0) {
            const char *cursor = p + 7;
            while (cursor < line_end && (*cursor == ' ' || *cursor == '\t')) ++cursor;
            while (cursor < line_end) {
                const char *eq = memchr(cursor, '=', (size_t)(line_end - cursor));
                if (!eq) break;
                if ((size_t)(eq - cursor) == nlen && memcmp(cursor, name, nlen) == 0) {
                    const char *vstart = eq + 1;
                    const char *vend = vstart;
                    while (vend < line_end && *vend != ';' && *vend != '\r' && *vend != '\n') ++vend;
                    size_t vlen = (size_t)(vend - vstart);
                    if (vlen >= out_cap) vlen = out_cap - 1;
                    memcpy(out, vstart, vlen);
                    out[vlen] = 0;
                    return 1;
                }
                const char *semi = memchr(cursor, ';', (size_t)(line_end - cursor));
                if (!semi) break;
                cursor = semi + 1;
                while (cursor < line_end && *cursor == ' ') ++cursor;
            }
        }
        if (line_end == end) break;
        p = line_end + 1;
    }
    return 0;
}

/* The opaque session id: picomesh-sid cookie or same-named forwarded header. */
static int extract_sid(const char *raw, size_t raw_len, char *out, size_t out_cap)
{
    if (cookie_value(raw, raw_len, "picomesh-sid", out, out_cap) && out[0]) return 1;
    if (header_line_value(raw, raw_len, "picomesh-sid", out, out_cap) && out[0]) return 1;
    out[0] = 0;
    return 0;
}

/* The Authorization: Bearer <token> value. */
static int extract_bearer(const char *raw, size_t raw_len, char *out, size_t out_cap)
{
    char header[1100];
    if (!header_line_value(raw, raw_len, "authorization", header, sizeof(header))) { out[0] = 0; return 0; }
    const char *value = header;
    if (strncasecmp(value, "Bearer ", 7) == 0) value += 7;
    while (*value == ' ') ++value;
    size_t len = strlen(value);
    if (!len || len >= out_cap) { out[0] = 0; return 0; }
    memcpy(out, value, len + 1);
    return 1;
}

/* A JWT has exactly two '.' separators; an opaque token has none. */
static int looks_like_jwt(const char *token)
{
    int dots = 0;
    for (const char *p = token; *p; ++p) if (*p == '.') ++dots;
    return dots == 2;
}

/* ---------- config helpers ---------- */

static const struct yconfig_node *security_node(struct picomesh_engine *engine, const char *key)
{
    const struct yconfig *config = picomesh_engine_config(engine);
    if (!config) return NULL;
    char path[64];
    snprintf(path, sizeof(path), "security.%s", key);
    struct yconfig_node_ptr_result r = yconfig_get(config, path);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return NULL; }
    return r.value;
}

struct map_find { const char *key; const struct yconfig_node *match; };
static int map_find_cb(const char *key, const struct yconfig_node *value, void *ud)
{
    struct map_find *find = ud;
    if (strcmp(key, find->key) == 0) { find->match = value; return 1; }
    return 0;
}

static const struct yconfig_node *map_child(const struct yconfig_node *map, const char *key)
{
    if (!map || yconfig_node_kind(map) != YCONFIG_MAP) return NULL;
    struct map_find find = {.key = key, .match = NULL};
    yconfig_node_for_each(map, map_find_cb, &find);
    return find.match;
}

int picomesh_security_configured(struct picomesh_engine *engine)
{
    const struct yconfig_node *authenticators = security_node(engine, "authenticators");
    return authenticators && yconfig_node_kind(authenticators) == YCONFIG_LIST;
}

/* ---------- backend credential-exchange calls ---------- */

/* sid → stored access JWT (owned, possibly empty). */
static struct picomesh_string_result call_session_jwt(struct picomesh_engine *engine, const char *sid)
{
    struct ctx c = picomesh_engine_service_ctx(engine, "session");
    struct object_ptr_result o = session_session_create(&c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_string, "authz: session_create failed", o);
    return session_session_jwt(&c, o.value, NULL, sid);
}

/* opaque PAT (numeric surrogate in the current PAT model) → uid (0 if unknown). */
static uint32_t call_pat_lookup(struct picomesh_engine *engine, const char *opaque)
{
    /* The current personal_access_tokens plugin keys on a uint32 surrogate;
     * a real opaque-string PAT store is a future plugin change. */
    char *end = NULL;
    unsigned long pat_id = strtoul(opaque, &end, 10);
    if (!end || *end != 0 || pat_id == 0) return 0;
    struct ctx c = picomesh_engine_service_ctx(engine, "personal_access_tokens");
    struct object_ptr_result o = personal_access_tokens_personal_access_tokens_create(&c);
    if (PICOMESH_IS_ERR(o)) { picomesh_error_destroy(o.error); return 0; }
    struct picomesh_uint32_result r =
        personal_access_tokens_personal_access_tokens_lookup(&c, o.value, NULL, (uint32_t)pat_id);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return 0; }
    return r.value;
}

/* opaque rnr_ runner token → runner_id (0 if unknown / revoked / disabled).
 * The runner registry stores only the token's hash; this is the gateway's
 * external→internal exchange for a runner-agent bearer credential. */
static uint32_t call_runner_lookup(struct picomesh_engine *engine, const char *opaque)
{
    struct ctx c = picomesh_engine_service_ctx(engine, "runner_agent");
    struct object_ptr_result o = runner_agent_runner_agent_create(&c);
    if (PICOMESH_IS_ERR(o)) { picomesh_error_destroy(o.error); return 0; }
    struct picomesh_uint32_result r = runner_agent_runner_agent_lookup_token(&c, o.value, NULL, opaque);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return 0; }
    return r.value;
}

/* uid → group CSV (owned, possibly empty). */
static char *call_load_groups(struct picomesh_engine *engine, uint32_t uid)
{
    struct ctx c = picomesh_engine_service_ctx(engine, "accounts");
    struct object_ptr_result o = accounts_accounts_create(&c);
    if (PICOMESH_IS_ERR(o)) { picomesh_error_destroy(o.error); return strdup(""); }
    struct picomesh_string_result r = accounts_accounts_groups(&c, o.value, NULL, uid);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return strdup(""); }
    return r.value ? r.value : strdup("");
}

/* Mint a signed access JWT for (uid, groups) via the issuer (owned). */
static struct picomesh_string_result call_mint(struct picomesh_engine *engine, uint32_t uid,
                                               const char *username, const char *groups_csv)
{
    struct ctx c = picomesh_engine_service_ctx(engine, "token_issuer");
    struct object_ptr_result o = token_issuer_token_issuer_create(&c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_string, "authz: token_issuer_create failed", o);
    return token_issuer_token_issuer_mint(&c, o.value, NULL, uid, username, groups_csv, 0);
}

/* ---------- authenticator chain ---------- */

void picomesh_authn_outcome_free(struct picomesh_authn_outcome *outcome)
{
    if (!outcome) return;
    free(outcome->jwt);
    outcome->jwt = NULL;
}

struct picomesh_authn_outcome picomesh_gateway_authenticate(struct picomesh_engine *engine,
                                                            const char *headers_raw,
                                                            size_t headers_raw_len)
{
    struct picomesh_authn_outcome outcome;
    memset(&outcome, 0, sizeof(outcome));

    struct picomesh_string_result secret_r = picomesh_security_jwt_secret(engine);
    if (PICOMESH_IS_ERR(secret_r)) {
        picomesh_error_destroy(secret_r.error);
        outcome.http_status = 500;
        return outcome;
    }
    char *secret = secret_r.value;

    const struct yconfig_node *authenticators = security_node(engine, "authenticators");
    size_t count = authenticators ? yconfig_node_size(authenticators) : 0;

    for (size_t i = 0; i < count; ++i) {
        const char *name = yconfig_node_as_string(yconfig_node_at(authenticators, i), NULL);
        if (!name) continue;

        if (strcmp(name, "session_cookie") == 0) {
            char sid[128];
            if (!extract_sid(headers_raw, headers_raw_len, sid, sizeof(sid))) continue;
            /* Credential present: it must resolve to a verifiable JWT or fail. */
            struct picomesh_string_result jwt_r = call_session_jwt(engine, sid);
            if (PICOMESH_IS_ERR(jwt_r) || !jwt_r.value || !jwt_r.value[0]) {
                if (PICOMESH_IS_OK(jwt_r)) free(jwt_r.value); else picomesh_error_destroy(jwt_r.error);
                outcome.http_status = 401; free(secret); return outcome;
            }
            struct picomesh_void_result a = picomesh_authctx_from_jwt(jwt_r.value, secret, &outcome.claims);
            if (PICOMESH_IS_ERR(a) || !outcome.claims.authenticated) {
                if (PICOMESH_IS_ERR(a)) picomesh_error_destroy(a.error);
                free(jwt_r.value); outcome.http_status = 401; free(secret); return outcome;
            }
            outcome.jwt = jwt_r.value;
            free(secret);
            return outcome;
        }

        if (strcmp(name, "bearer_jwt_token") == 0) {
            char token[1100];
            if (!extract_bearer(headers_raw, headers_raw_len, token, sizeof(token))) continue;
            if (!looks_like_jwt(token)) continue;
            struct picomesh_void_result a = picomesh_authctx_from_jwt(token, secret, &outcome.claims);
            if (PICOMESH_IS_ERR(a) || !outcome.claims.authenticated) {
                if (PICOMESH_IS_ERR(a)) picomesh_error_destroy(a.error);
                outcome.http_status = 401; free(secret); return outcome;
            }
            outcome.jwt = strdup(token);
            free(secret);
            return outcome;
        }

        if (strcmp(name, "bearer_opaque_token") == 0) {
            char token[256];
            if (!extract_bearer(headers_raw, headers_raw_len, token, sizeof(token))) continue;
            /* A raw JWT presented as a bearer token is rejected on this public
             * gateway — opaque tokens only (clients never hold a mesh JWT). */
            if (looks_like_jwt(token)) { outcome.http_status = 401; free(secret); return outcome; }
            /* Runner-agent token: resolve the rnr_ secret to a runner_id and
             * mint a JWT carrying the runner identity (sub=runner_id) and the
             * runner groups, so policy can gate the runner-only methods. */
            if (strncmp(token, "rnr_", 4) == 0) {
                uint32_t runner_id = call_runner_lookup(engine, token);
                if (runner_id == 0) { outcome.http_status = 401; free(secret); return outcome; }
                char username[32];
                snprintf(username, sizeof(username), "runner-%u", runner_id);
                char groups[64];
                snprintf(groups, sizeof(groups), "site:runner,runner:%u", runner_id);
                struct picomesh_string_result jwt_r = call_mint(engine, runner_id, username, groups);
                if (PICOMESH_IS_ERR(jwt_r) || !jwt_r.value) {
                    if (PICOMESH_IS_ERR(jwt_r)) picomesh_error_destroy(jwt_r.error);
                    outcome.http_status = 401; free(secret); return outcome;
                }
                struct picomesh_void_result a = picomesh_authctx_from_jwt(jwt_r.value, secret, &outcome.claims);
                if (PICOMESH_IS_ERR(a) || !outcome.claims.authenticated) {
                    if (PICOMESH_IS_ERR(a)) picomesh_error_destroy(a.error);
                    free(jwt_r.value); outcome.http_status = 401; free(secret); return outcome;
                }
                outcome.jwt = jwt_r.value;
                free(secret);
                return outcome;
            }
            uint32_t uid = call_pat_lookup(engine, token);
            if (uid == 0) { outcome.http_status = 401; free(secret); return outcome; }
            char *groups = call_load_groups(engine, uid);
            struct picomesh_string_result jwt_r = call_mint(engine, uid, "", groups ? groups : "");
            free(groups);
            if (PICOMESH_IS_ERR(jwt_r) || !jwt_r.value) {
                if (PICOMESH_IS_ERR(jwt_r)) picomesh_error_destroy(jwt_r.error);
                outcome.http_status = 401; free(secret); return outcome;
            }
            struct picomesh_void_result a = picomesh_authctx_from_jwt(jwt_r.value, secret, &outcome.claims);
            if (PICOMESH_IS_ERR(a) || !outcome.claims.authenticated) {
                if (PICOMESH_IS_ERR(a)) picomesh_error_destroy(a.error);
                free(jwt_r.value); outcome.http_status = 401; free(secret); return outcome;
            }
            outcome.jwt = jwt_r.value;
            free(secret);
            return outcome;
        }
    }

    /* No credential matched → anonymous. */
    free(secret);
    outcome.http_status = 0;
    outcome.claims.authenticated = 0;
    return outcome;
}

/* ---------- policy authorizer ---------- */

/* 1 if the comma-separated groups list contains the exact group token
 * (e.g. "site:runner"). Distinct from the role-ladder check — a capability
 * group like "runner" is membership, not a rank. */
static int groups_contains(const char *groups_csv, const char *group)
{
    if (!groups_csv || !group || !*group) return 0;
    size_t glen = strlen(group);
    const char *cursor = groups_csv;
    while (*cursor) {
        const char *comma = strchr(cursor, ',');
        size_t span = comma ? (size_t)(comma - cursor) : strlen(cursor);
        if (span == glen && memcmp(cursor, group, glen) == 0) return 1;
        if (!comma) break;
        cursor = comma + 1;
    }
    return 0;
}

/* Resolve the account slug a role rule is scoped to. Supports "site" (the
 * deployment-wide account) and "arg:<n>" (the nth positional /_rpc arg taken
 * as an account slug). Returns 1 on success. */
static int resolve_account(const char *account_from, const struct yjson_value *args,
                           char *out, size_t out_cap)
{
    if (!account_from || !*account_from) return 0;
    if (strcmp(account_from, "site") == 0) { snprintf(out, out_cap, "site"); return 1; }
    if (strncmp(account_from, "arg:", 4) == 0) {
        long index = strtol(account_from + 4, NULL, 10);
        if (index < 0 || !args || !yjson_is_array(args)) return 0;
        const struct yjson_value *element = yjson_array_at(args, (size_t)index);
        const char *slug = yjson_as_string(element, NULL);
        if (!slug || !*slug) return 0;
        snprintf(out, out_cap, "%s", slug);
        return 1;
    }
    return 0;
}

int picomesh_gateway_authorize(struct picomesh_engine *engine, const char *endpoint,
                               const struct yjson_value *args,
                               const struct picomesh_authctx *claims)
{
    const struct yconfig_node *policy = security_node(engine, "policy");
    if (!policy || yconfig_node_kind(policy) != YCONFIG_MAP) return 403; /* no policy → deny */

    const struct yconfig_node *rule = map_child(policy, endpoint);
    if (!rule) return 403; /* absent from policy → default deny */

    enum yconfig_kind kind = yconfig_node_kind(rule);
    if (kind == YCONFIG_STRING) {
        const char *value = yconfig_node_as_string(rule, "");
        if (strcmp(value, "anonymous") == 0) return 0;
        if (!claims->authenticated) return 401;       /* must authenticate */
        if (strcmp(value, "authenticated") == 0) return 0;
        return 403;                                    /* unknown scalar rule */
    }

    if (kind == YCONFIG_MAP) {
        if (!claims->authenticated) return 401;
        /* Capability gate: the verified JWT groups must contain an exact group
         * (e.g. "site:runner" for runner-agent methods). */
        const struct yconfig_node *group_node = map_child(rule, "required_group");
        if (group_node) {
            const char *required_group = yconfig_node_as_string(group_node, NULL);
            if (!required_group) return 403;
            return groups_contains(claims->groups_csv, required_group) ? 0 : 403;
        }
        const struct yconfig_node *role_node = map_child(rule, "required_role");
        const struct yconfig_node *account_node = map_child(rule, "account_from");
        const char *required_role = role_node ? yconfig_node_as_string(role_node, NULL) : NULL;
        const char *account_from = account_node ? yconfig_node_as_string(account_node, "site") : "site";
        if (!required_role) return 403;
        int need = picomesh_role_rank(required_role);
        if (need < 0) return 403;

        char account[128];
        if (!resolve_account(account_from, args, account, sizeof(account))) return 403;
        int have = picomesh_groups_max_role(claims->groups_csv, account);
        /* Explicit site-level bypass: a site owner/maintainer may act on any
         * per-account resource. */
        if (have < need && strcmp(account, "site") != 0) {
            int site = picomesh_groups_max_role(claims->groups_csv, "site");
            if (site > have) have = site;
        }
        return have >= need ? 0 : 403;
    }

    return 403;
}

/* ---------- credential-exchange guard ---------- */

int picomesh_is_credential_exchange(const char *endpoint)
{
    static const char *const exchanges[] = {
        "session.session.session_jwt",
        "session.session.lookup",
        "token_issuer.token_issuer.login",
        "token_issuer.token_issuer.refresh",
        "token_issuer.token_issuer.mint",
        "personal_access_tokens.personal_access_tokens.lookup",
        "runner_agent.runner_agent.lookup_token",
        NULL,
    };
    for (size_t i = 0; exchanges[i]; ++i)
        if (strcmp(endpoint, exchanges[i]) == 0) return 1;
    return 0;
}
