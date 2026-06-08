/* token_issuer — the mesh's JWT issuer (issue #19).
 *
 *   login(method, uid, username, pw_hash) → JSON {access_jwt, refresh_token, …}
 *   refresh(refresh_token)                → JSON {access_jwt, refresh_token, …}
 *   mint(uid, username, groups_csv, ttl)  → access JWT string (PAT/runner path)
 *   count_active                          → number of live refresh tokens
 *
 * Login delegates credential verification to `<method>_authn`, loads groups
 * from `accounts`, and mints a short-lived HS256 access JWT plus a long-lived
 * opaque refresh token. Refresh tokens are ROWS in the `refresh_tokens` table
 * in `relational_storage` (one row per token, real columns) — not prefixed KV
 * keys. The token_issuer plugin OWNS this table and creates it itself.
 *
 *   refresh_tokens(token PK, uid, username, created_at)
 *
 * Access JWTs are stateless (verified by signature + expiry); they are not
 * stored. A refresh/login refreshes the groups claim, so role changes take
 * effect on the next exchange. */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yplatform/time.h>
#include <picomesh/ycore/idkey.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/secret.h>
#include <picomesh/plugin/relational_storage/relational_sql.h>
#include <picomesh/plugin/password_authn/password_authn.h>
#include <picomesh/plugin/accounts/accounts.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#define TI_DDL \
    "CREATE TABLE IF NOT EXISTS refresh_tokens(" \
    "token TEXT PRIMARY KEY, uid INTEGER NOT NULL, username TEXT, " \
    "created_at INTEGER NOT NULL DEFAULT 0)"

struct PICOMESH_CLASS_ANNOTATE("class@token_issuer:token_issuer") token_issuer_token_issuer_data {
    int schema_ensured; /* per-worker: set once this worker has created the table */
};

static struct token_issuer_token_issuer_data *ti(struct object *obj)
{
    return (struct token_issuer_token_issuer_data *)((char *)obj + sizeof(struct object));
}

static struct picomesh_void_result ti_open(struct rel_handle *rel_handle, struct yheaders *hdrs, struct object *obj)
{
    struct picomesh_void_result open_res = rel_open(rel_handle, "rstore_token", "token");
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_void, "token_issuer: open relational_storage failed", open_res);
    return rel_ensure_schema(rel_handle, hdrs, &ti(obj)->schema_ensured, TI_DDL);
}

/* Allocate an opaque 128-bit refresh token (hex). Fails closed if secure
 * randomness is unavailable — a refresh token is a bearer secret. */
static int alloc_refresh_token(char *out, size_t cap)
{
    uint8_t raw[16];
    size_t got = 0;
    while (got < sizeof(raw)) {
        ssize_t read_len = getrandom(raw + got, sizeof(raw) - got, 0);
        if (read_len < 0) { if (errno == EINTR) continue; return 0; }
        got += (size_t)read_len;
    }
    static const char hex[] = "0123456789abcdef";
    size_t k = 0;
    for (size_t i = 0; i < sizeof(raw) && k + 2 < cap; ++i) {
        out[k++] = hex[raw[i] >> 4];
        out[k++] = hex[raw[i] & 0x0f];
    }
    out[k < cap ? k : cap - 1] = 0;
    return 1;
}

static struct picomesh_string_result ti_load_groups(struct yheaders *hdrs, uint32_t uid)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(picomesh_string, "token_issuer: no engine for groups");
    struct ctx accounts_ctx = picomesh_engine_service_ctx(engine, "accounts");
    struct object_ptr_result create_res = accounts_accounts_create(&accounts_ctx);
    if (PICOMESH_IS_ERR(create_res)) return PICOMESH_ERR(picomesh_string, "token_issuer: accounts_create failed", create_res);
    struct picomesh_string_result groups = accounts_accounts_groups(&accounts_ctx, create_res.value, hdrs, uid);
    if (PICOMESH_IS_ERR(groups)) return PICOMESH_ERR(picomesh_string, "token_issuer: load groups failed", groups);
    if (!groups.value) {
        char *empty = strdup("");
        return empty ? PICOMESH_OK(picomesh_string, empty) : PICOMESH_ERR(picomesh_string, "token_issuer: out of memory");
    }
    return groups;
}

/* Mint a signed access JWT for (uid, username, groups) with the given TTL. */
static struct picomesh_string_result ti_mint_access(uint32_t uid, const char *username,
                                                    const char *groups_csv, int64_t ttl_seconds)
{
    struct picomesh_string_result sec = picomesh_security_jwt_secret(picomesh_active_engine());
    if (PICOMESH_IS_ERR(sec)) return PICOMESH_ERR(picomesh_string, "token_issuer: signing secret unavailable", sec);
    int64_t now = picomesh_security_now();
    char *claims = picomesh_jwt_build_claims("picomesh", uid, username, groups_csv, now, now + ttl_seconds);
    if (!claims) { free(sec.value); return PICOMESH_ERR(picomesh_string, "token_issuer: build claims failed"); }
    char *jwt = picomesh_jwt_encode(claims, sec.value);
    free(claims);
    free(sec.value);
    if (!jwt) return PICOMESH_ERR(picomesh_string, "token_issuer: jwt encode failed");
    return PICOMESH_OK(picomesh_string, jwt);
}

static struct picomesh_json_result ti_token_pair_json(const char *access_jwt, const char *refresh_token,
                                                      uint32_t uid, const char *username, const char *groups_csv)
{
    struct yjson_writer *writer = yjson_writer_new();
    if (!writer) return PICOMESH_ERR(picomesh_json, "token_issuer: writer alloc failed");
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "access_jwt");    yjson_writer_string(writer, access_jwt ? access_jwt : "");
    yjson_writer_key(writer, "refresh_token"); yjson_writer_string(writer, refresh_token ? refresh_token : "");
    yjson_writer_key(writer, "uid");           yjson_writer_int(writer, (int64_t)uid);
    yjson_writer_key(writer, "username");      yjson_writer_string(writer, username ? username : "");
    yjson_writer_key(writer, "groups");        yjson_writer_string(writer, groups_csv ? groups_csv : "");
    yjson_writer_end_object(writer);
    size_t len = 0;
    const char *data = yjson_writer_data(writer, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_json, "token_issuer: token pair encode failed");
    return PICOMESH_OK(picomesh_json, out);
}

/* Mint a fresh opaque refresh token and persist it as a row. */
static struct picomesh_string_result ti_issue_refresh(struct rel_handle *rel_handle, struct yheaders *hdrs,
                                                      uint32_t uid, const char *username)
{
    char token[40];
    if (!alloc_refresh_token(token, sizeof(token)))
        return PICOMESH_ERR(picomesh_string, "token_issuer: secure random unavailable");
    rel_handle->shard = picomesh_fnv1a32(token); /* lookup cluster: shard by the token we issue */
    struct yjson_writer *args_writer = yjson_writer_new();
    yjson_writer_begin_array(args_writer);
    yjson_writer_string(args_writer, token);
    yjson_writer_int(args_writer, (int64_t)uid);
    yjson_writer_string(args_writer, username ? username : "");
    yjson_writer_int(args_writer, picomesh_yplatform_time_wall_ms() / 1000);
    char *args = rel_args_take(args_writer);
    int changes = rel_exec_changes(rel_handle, hdrs,
        "INSERT INTO refresh_tokens(token,uid,username,created_at) VALUES(?,?,?,?)", args);
    free(args);
    if (changes < 1) return PICOMESH_ERR(picomesh_string, "token_issuer: persist refresh failed");
    char *out = strdup(token);
    return out ? PICOMESH_OK(picomesh_string, out) : PICOMESH_ERR(picomesh_string, "token_issuer: out of memory");
}

PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_login")
struct picomesh_json_result token_issuer_token_issuer_login_impl(struct ctx *ctx, struct object *obj,
                                                                 struct yheaders *hdrs,
                                                                 const char *method, uint32_t uid,
                                                                 const char *username, int64_t pw_hash)
{
    (void)ctx;
    if (!method || strcmp(method, "password") != 0)
        return PICOMESH_ERR(picomesh_json, "token_issuer_login: unsupported auth method");
    if (!username) username = "";

    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(picomesh_json, "token_issuer_login: no active engine");
    struct ctx pw_ctx = picomesh_engine_service_ctx(engine, "password_authn");
    struct object_ptr_result pw_obj = password_authn_password_authn_create(&pw_ctx);
    if (PICOMESH_IS_ERR(pw_obj)) return PICOMESH_ERR(picomesh_json, "token_issuer_login: password_authn unreachable", pw_obj);
    struct picomesh_int_result auth =
        password_authn_password_authn_authenticate(&pw_ctx, pw_obj.value, hdrs, uid, pw_hash);
    if (PICOMESH_IS_ERR(auth)) return PICOMESH_ERR(picomesh_json, "token_issuer_login: authenticate failed", auth);
    if (auth.value != 1) return PICOMESH_ERR(picomesh_json, "token_issuer_login: invalid credentials");

    struct picomesh_string_result groups = ti_load_groups(hdrs, uid);
    if (PICOMESH_IS_ERR(groups)) return PICOMESH_ERR(picomesh_json, "token_issuer_login: load groups failed", groups);

    int64_t ttl = picomesh_security_access_ttl(engine);
    struct picomesh_string_result access = ti_mint_access(uid, username, groups.value, ttl);
    if (PICOMESH_IS_ERR(access)) { free(groups.value); return PICOMESH_ERR(picomesh_json, "token_issuer_login: mint access failed", access); }

    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = ti_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) { free(groups.value); free(access.value); return PICOMESH_ERR(picomesh_json, "token_issuer_login: storage open failed", open_res); }
    struct picomesh_string_result refresh = ti_issue_refresh(&rel_handle, hdrs, uid, username);
    if (PICOMESH_IS_ERR(refresh)) { free(groups.value); free(access.value); return PICOMESH_ERR(picomesh_json, "token_issuer_login: issue refresh failed", refresh); }

    struct picomesh_json_result out = ti_token_pair_json(access.value, refresh.value, uid, username, groups.value);
    yinfo("token_issuer: login uid=%u method=%s", uid, method);
    free(groups.value); free(access.value); free(refresh.value);
    return out;
}

PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_refresh")
struct picomesh_json_result token_issuer_token_issuer_refresh_impl(struct ctx *ctx, struct object *obj,
                                                                   struct yheaders *hdrs,
                                                                   const char *refresh_token)
{
    (void)ctx;
    if (!refresh_token || !*refresh_token)
        return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: missing refresh token");
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = ti_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: storage open failed", open_res);
    rel_handle.shard = picomesh_fnv1a32(refresh_token); /* route to the presented token's shard */

    /* Resolve the presented refresh token to its (uid, username). */
    char *args = rel_args1s(refresh_token);
    struct picomesh_json_result row = rel_query(&rel_handle, hdrs, "SELECT uid,username FROM refresh_tokens WHERE token=?", args);
    if (PICOMESH_IS_ERR(row)) { free(args); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: read failed", row); }
    uint32_t uid = 0;
    char username[64] = {0};
    struct yjson_doc *doc = yjson_parse(row.value ? row.value : "[]", row.value ? strlen(row.value) : 2);
    int found = 0;
    if (doc) {
        const struct yjson_value *arr = yjson_doc_root(doc);
        if (arr && yjson_array_size(arr) > 0) {
            const struct yjson_value *first_row = yjson_array_at(arr, 0);
            uid = (uint32_t)yjson_as_int(yjson_object_get(first_row, "uid"), 0);
            const char *username_val = yjson_as_string(yjson_object_get(first_row, "username"), "");
            snprintf(username, sizeof(username), "%s", username_val ? username_val : "");
            found = 1;
        }
        yjson_doc_free(doc);
    }
    free(row.value);
    if (!found) { free(args); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: unknown refresh token"); }

    /* Rotate: delete the presented token (gated on actually removing it). */
    int deleted = rel_exec_changes(&rel_handle, hdrs, "DELETE FROM refresh_tokens WHERE token=?", args);
    free(args);
    if (deleted < 1) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: token already rotated");

    struct picomesh_string_result groups = ti_load_groups(hdrs, uid);
    if (PICOMESH_IS_ERR(groups)) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: load groups failed", groups);
    int64_t ttl = picomesh_security_access_ttl(picomesh_active_engine());
    struct picomesh_string_result access = ti_mint_access(uid, username, groups.value, ttl);
    if (PICOMESH_IS_ERR(access)) { free(groups.value); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: mint access failed", access); }
    struct picomesh_string_result fresh = ti_issue_refresh(&rel_handle, hdrs, uid, username);
    if (PICOMESH_IS_ERR(fresh)) { free(groups.value); free(access.value); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: issue refresh failed", fresh); }

    struct picomesh_json_result out = ti_token_pair_json(access.value, fresh.value, uid, username, groups.value);
    free(groups.value); free(access.value); free(fresh.value);
    return out;
}

/* 1 if `groups_csv` contains a privilege-granting membership that the low-level
 * mint must never forge: the reserved `system:internal` capability, or any
 * "<account>:<role>" whose role is on the authorization LADDER (guest..owner).
 * Runner tokens (e.g. "site:runner,runner:<id>") carry no ladder role, so they
 * pass; an attempt to mint "system:internal", "site:owner", or "acme:owner"
 * does not. */
static int ti_groups_privileged(const char *groups_csv)
{
    if (!groups_csv) return 0;
    if (picomesh_groups_contains(groups_csv, PICOMESH_GROUP_SYSTEM)) return 1;
    const char *cursor = groups_csv;
    while (*cursor) {
        const char *comma = strchr(cursor, ',');
        size_t span = comma ? (size_t)(comma - cursor) : strlen(cursor);
        const char *colon = memchr(cursor, ':', span);
        if (colon) {
            char role[32];
            size_t role_len = span - (size_t)(colon - cursor) - 1;
            if (role_len < sizeof(role)) {
                memcpy(role, colon + 1, role_len);
                role[role_len] = 0;
                if (picomesh_role_rank(role) >= 0) return 1; /* a ladder-role membership */
            }
        }
        if (!comma) break;
        cursor = comma + 1;
    }
    return 0;
}

/* Low-level mint: a signed access JWT for an already-resolved identity. Its ONLY
 * legitimate caller is the runner-token exchange (runner_agent.exchange), which
 * mints non-privileged runner tokens. Because the RBAC model trusts signed JWT
 * groups, this endpoint must never forge a privilege-granting claim — otherwise
 * any caller that reaches it (it is default-denied on the public gateway, but
 * fronted unauthenticated by the loopback operator bridge) could mint itself a
 * `system:internal` capability or a `site:owner`/namespace-owner membership and
 * bypass authorization entirely. So it REJECTS any ladder-role or system
 * membership in `groups_csv`. (issue #30) */
PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_mint")
struct picomesh_string_result token_issuer_token_issuer_mint_impl(struct ctx *ctx, struct object *obj,
                                                                  struct yheaders *hdrs,
                                                                  uint32_t uid, const char *username,
                                                                  const char *groups_csv, int64_t ttl_seconds)
{
    (void)ctx; (void)obj;
    int internal = 0;
    const char *jwt = hdrs ? yheaders_get(hdrs, "jwt") : NULL;
    if (jwt && *jwt) {
        struct picomesh_string_result secret = picomesh_security_jwt_secret(picomesh_active_engine());
        if (PICOMESH_IS_OK(secret)) {
            struct picomesh_authctx caller;
            struct picomesh_void_result verify_res = picomesh_authctx_from_jwt(jwt, secret.value, &caller);
            if (PICOMESH_IS_OK(verify_res) && caller.authenticated &&
                picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM))
                internal = 1;
            else if (PICOMESH_IS_ERR(verify_res)) picomesh_error_destroy(verify_res.error);
            free(secret.value);
        } else {
            picomesh_error_destroy(secret.error);
        }
    }
    if (ti_groups_privileged(groups_csv) && !internal)
        return PICOMESH_ERR(picomesh_string, "token_issuer_mint: refusing to mint a privilege-granting token");
    if (ttl_seconds <= 0) ttl_seconds = picomesh_security_access_ttl(picomesh_active_engine());
    return ti_mint_access(uid, username, groups_csv ? groups_csv : "", ttl_seconds);
}

PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_count_active")
struct picomesh_size_result token_issuer_token_issuer_count_active_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = ti_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_size, "token_issuer_count: storage open failed", open_res);
    struct picomesh_int64_result count_res = rel_query_int_all(&rel_handle, hdrs, "SELECT COUNT(*) AS n FROM refresh_tokens", "[]", "n");
    if (PICOMESH_IS_ERR(count_res)) return PICOMESH_ERR(picomesh_size, "token_issuer_count: aggregate failed", count_res);
    return PICOMESH_OK(picomesh_size, (size_t)(count_res.value < 0 ? 0 : count_res.value));
}

/* List live refresh tokens as `[{"uid":…,"username":…,"created_at":…}, …]` —
 * non-secret metadata only. The refresh token is an opaque bearer secret:
 * returning it here would let any caller with list access harvest live
 * credentials, so it is never selected. One-time token material is handed out
 * only at issuance (login/refresh). */
PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_list")
struct picomesh_json_result token_issuer_token_issuer_list_impl(struct ctx *ctx, struct object *obj,
                                                         struct yheaders *hdrs,
                                                         int64_t offset, int64_t limit)
{
    (void)ctx;
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = ti_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "token_issuer_list: storage open failed", open_res);
    /* Globally ordered + paginated across shards by created_at. */
    return rel_query_page(&rel_handle, hdrs, "SELECT uid,username,created_at FROM refresh_tokens", "[]", "created_at", 0, offset, limit);
}

PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_list_all")
struct picomesh_json_result token_issuer_token_issuer_list_all_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = ti_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "token_issuer_list_all: storage open failed", open_res);
    /* Unbounded but GLOBALLY ordered by created_at (limit<=0). */
    return rel_query_page(&rel_handle, hdrs, "SELECT uid,username,created_at FROM refresh_tokens", "[]", "created_at", 0, 0, 0);
}

#include "store.gen.c"
