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

static struct picomesh_void_result ti_open(struct rel_handle *h, struct yheaders *hdrs, struct object *obj)
{
    struct picomesh_void_result o = rel_open(h, "rstore_token");
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_void, "token_issuer: open relational_storage failed", o);
    return rel_ensure_schema(h, hdrs, &ti(obj)->schema_ensured, TI_DDL);
}

/* Allocate an opaque 128-bit refresh token (hex). Fails closed if secure
 * randomness is unavailable — a refresh token is a bearer secret. */
static int alloc_refresh_token(char *out, size_t cap)
{
    uint8_t raw[16];
    size_t got = 0;
    while (got < sizeof(raw)) {
        ssize_t n = getrandom(raw + got, sizeof(raw) - got, 0);
        if (n < 0) { if (errno == EINTR) continue; return 0; }
        got += (size_t)n;
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
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(picomesh_string, "token_issuer: no engine for groups");
    struct ctx c = picomesh_engine_service_ctx(e, "accounts");
    struct object_ptr_result o = accounts_accounts_create(&c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_string, "token_issuer: accounts_create failed", o);
    struct picomesh_string_result groups = accounts_accounts_groups(&c, o.value, hdrs, uid);
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
    struct yjson_writer *w = yjson_writer_new();
    if (!w) return PICOMESH_ERR(picomesh_json, "token_issuer: writer alloc failed");
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "access_jwt");    yjson_writer_string(w, access_jwt ? access_jwt : "");
    yjson_writer_key(w, "refresh_token"); yjson_writer_string(w, refresh_token ? refresh_token : "");
    yjson_writer_key(w, "uid");           yjson_writer_int(w, (int64_t)uid);
    yjson_writer_key(w, "username");      yjson_writer_string(w, username ? username : "");
    yjson_writer_key(w, "groups");        yjson_writer_string(w, groups_csv ? groups_csv : "");
    yjson_writer_end_object(w);
    size_t len = 0;
    const char *data = yjson_writer_data(w, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(w);
    if (!out) return PICOMESH_ERR(picomesh_json, "token_issuer: token pair encode failed");
    return PICOMESH_OK(picomesh_json, out);
}

/* Mint a fresh opaque refresh token and persist it as a row. */
static struct picomesh_string_result ti_issue_refresh(struct rel_handle *h, struct yheaders *hdrs,
                                                      uint32_t uid, const char *username)
{
    char token[40];
    if (!alloc_refresh_token(token, sizeof(token)))
        return PICOMESH_ERR(picomesh_string, "token_issuer: secure random unavailable");
    h->shard = picomesh_fnv1a32(token); /* lookup cluster: shard by the token we issue */
    struct yjson_writer *aw = yjson_writer_new();
    yjson_writer_begin_array(aw);
    yjson_writer_string(aw, token);
    yjson_writer_int(aw, (int64_t)uid);
    yjson_writer_string(aw, username ? username : "");
    yjson_writer_int(aw, picomesh_yplatform_time_wall_ms() / 1000);
    char *args = rel_args_take(aw);
    int changes = rel_exec_changes(h, hdrs,
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

    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(picomesh_json, "token_issuer_login: no active engine");
    struct ctx pw_ctx = picomesh_engine_service_ctx(e, "password_authn");
    struct object_ptr_result pw_obj = password_authn_password_authn_create(&pw_ctx);
    if (PICOMESH_IS_ERR(pw_obj)) return PICOMESH_ERR(picomesh_json, "token_issuer_login: password_authn unreachable", pw_obj);
    struct picomesh_int_result auth =
        password_authn_password_authn_authenticate(&pw_ctx, pw_obj.value, hdrs, uid, pw_hash);
    if (PICOMESH_IS_ERR(auth)) return PICOMESH_ERR(picomesh_json, "token_issuer_login: authenticate failed", auth);
    if (auth.value != 1) return PICOMESH_ERR(picomesh_json, "token_issuer_login: invalid credentials");

    struct picomesh_string_result groups = ti_load_groups(hdrs, uid);
    if (PICOMESH_IS_ERR(groups)) return PICOMESH_ERR(picomesh_json, "token_issuer_login: load groups failed", groups);

    int64_t ttl = picomesh_security_access_ttl(e);
    struct picomesh_string_result access = ti_mint_access(uid, username, groups.value, ttl);
    if (PICOMESH_IS_ERR(access)) { free(groups.value); return PICOMESH_ERR(picomesh_json, "token_issuer_login: mint access failed", access); }

    struct rel_handle h;
    struct picomesh_void_result oh = ti_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) { free(groups.value); free(access.value); return PICOMESH_ERR(picomesh_json, "token_issuer_login: storage open failed", oh); }
    struct picomesh_string_result refresh = ti_issue_refresh(&h, hdrs, uid, username);
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
    struct rel_handle h;
    struct picomesh_void_result oh = ti_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: storage open failed", oh);
    h.shard = picomesh_fnv1a32(refresh_token); /* route to the presented token's shard */

    /* Resolve the presented refresh token to its (uid, username). */
    char *args = rel_args1s(refresh_token);
    struct picomesh_json_result row = rel_query(&h, hdrs, "SELECT uid,username FROM refresh_tokens WHERE token=?", args);
    if (PICOMESH_IS_ERR(row)) { free(args); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: read failed", row); }
    uint32_t uid = 0;
    char username[64] = {0};
    struct yjson_doc *doc = yjson_parse(row.value ? row.value : "[]", row.value ? strlen(row.value) : 2);
    int found = 0;
    if (doc) {
        const struct yjson_value *arr = yjson_doc_root(doc);
        if (arr && yjson_array_size(arr) > 0) {
            const struct yjson_value *r0 = yjson_array_at(arr, 0);
            uid = (uint32_t)yjson_as_int(yjson_object_get(r0, "uid"), 0);
            const char *u = yjson_as_string(yjson_object_get(r0, "username"), "");
            snprintf(username, sizeof(username), "%s", u ? u : "");
            found = 1;
        }
        yjson_doc_free(doc);
    }
    free(row.value);
    if (!found) { free(args); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: unknown refresh token"); }

    /* Rotate: delete the presented token (gated on actually removing it). */
    int deleted = rel_exec_changes(&h, hdrs, "DELETE FROM refresh_tokens WHERE token=?", args);
    free(args);
    if (deleted < 1) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: token already rotated");

    struct picomesh_string_result groups = ti_load_groups(hdrs, uid);
    if (PICOMESH_IS_ERR(groups)) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: load groups failed", groups);
    int64_t ttl = picomesh_security_access_ttl(picomesh_active_engine());
    struct picomesh_string_result access = ti_mint_access(uid, username, groups.value, ttl);
    if (PICOMESH_IS_ERR(access)) { free(groups.value); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: mint access failed", access); }
    struct picomesh_string_result fresh = ti_issue_refresh(&h, hdrs, uid, username);
    if (PICOMESH_IS_ERR(fresh)) { free(groups.value); free(access.value); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: issue refresh failed", fresh); }

    struct picomesh_json_result out = ti_token_pair_json(access.value, fresh.value, uid, username, groups.value);
    free(groups.value); free(access.value); free(fresh.value);
    return out;
}

/* Low-level mint: a signed access JWT for an already-resolved identity (the
 * gateway's bearer-opaque/runner path). Minting stays in the issuer. */
PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_mint")
struct picomesh_string_result token_issuer_token_issuer_mint_impl(struct ctx *ctx, struct object *obj,
                                                                  struct yheaders *hdrs,
                                                                  uint32_t uid, const char *username,
                                                                  const char *groups_csv, int64_t ttl_seconds)
{
    (void)ctx; (void)obj; (void)hdrs;
    if (ttl_seconds <= 0) ttl_seconds = picomesh_security_access_ttl(picomesh_active_engine());
    return ti_mint_access(uid, username, groups_csv ? groups_csv : "", ttl_seconds);
}

PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_count_active")
struct picomesh_size_result token_issuer_token_issuer_count_active_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = ti_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_size, "token_issuer_count: storage open failed", oh);
    int64_t n = rel_query_int_all(&h, hdrs, "SELECT COUNT(*) AS n FROM refresh_tokens", "[]", "n");
    return PICOMESH_OK(picomesh_size, (size_t)(n < 0 ? 0 : n));
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
    /* No cross-shard pagination yet (needs a global merge after fan-out); return
     * all shards' rows shard-grouped. Use count_active() + client-side paging. */
    (void)offset; (void)limit;
    struct rel_handle h;
    struct picomesh_void_result oh = ti_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "token_issuer_list: storage open failed", oh);
    return rel_query_all(&h, hdrs, "SELECT uid,username,created_at FROM refresh_tokens ORDER BY created_at", "[]");
}

PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_list_all")
struct picomesh_json_result token_issuer_token_issuer_list_all_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = ti_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "token_issuer_list_all: storage open failed", oh);
    return rel_query_all(&h, hdrs, "SELECT uid,username,created_at FROM refresh_tokens ORDER BY created_at", "[]");
}

#include "store.gen.c"
