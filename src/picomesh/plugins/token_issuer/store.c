/* token_issuer — the mesh's JWT issuer (issue #19).
 *
 *   login(method, uid, username, pw_hash) → JSON {access_jwt, refresh_token,
 *                                            uid, username, groups}
 *   refresh(refresh_token)                → JSON {access_jwt, refresh_token, …}
 *   mint(uid, username, groups_csv, ttl)  → access JWT string (PAT path)
 *   count_active                          → number of live refresh tokens
 *
 * Login delegates credential verification to the named authn plugin
 * (`<method>_authn.authenticate`) — authn plugins verify credentials, they do
 * NOT mint framework tokens — then loads the user's groups from `accounts` and
 * mints a short-lived HS256 access JWT plus a long-lived opaque refresh token.
 * The signing secret comes from configured key material
 * (`picomesh_security_jwt_secret`), shared across the trusted mesh.
 *
 * Refresh tokens are opaque bearer secrets persisted in the shared
 * `sharded_storage` service (context `token_issuer`):
 *   count            → live refresh-token count
 *   refresh:<token>  → "<uid>\t<username>"
 *
 * Access JWTs are stateless (verified by signature + expiry); they are not
 * stored. A refresh/login updates the groups claim, so role changes take
 * effect on the next exchange. */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/secret.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>
#include <picomesh/plugin/password_authn/password_authn.h>
#include <picomesh/plugin/accounts/accounts.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#define TI_CTX "token_issuer"

/* No in-memory state — every op delegates to storage / peers. */
struct PICOMESH_CLASS_ANNOTATE("class@token_issuer:token_issuer") token_issuer_token_issuer_data {
    char _unused;
};

struct ti_storage {
    struct ctx c;
    struct object *obj;
};
PICOMESH_RESULT_DECLARE(ti_storage, struct ti_storage);

static struct ti_storage_result ti_open(void)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(ti_storage, "token_issuer: no active engine");
    struct ti_storage h = {.c = picomesh_engine_service_ctx(e, "sharded_storage")};
    struct object_ptr_result o = sharded_storage_db_create(&h.c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(ti_storage, "token_issuer: storage_db_create failed", o);
    h.obj = o.value;
    return PICOMESH_OK(ti_storage, h);
}

/* Atomic counter bump — propagates a backend failure. */
static struct picomesh_int64_result ti_incr(struct ti_storage *h, struct yheaders *hdrs, const char *key, int64_t delta)
{
    struct picomesh_int64_result r = sharded_storage_db_incr(&h->c, h->obj, hdrs, TI_CTX, key, delta);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "token_issuer: counter update failed", r);
    return r;
}

static struct picomesh_int64_result ti_get(struct ti_storage *h, struct yheaders *hdrs, const char *key, int64_t fallback)
{
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, TI_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "token_issuer: storage read failed", r);
    int64_t v = (r.value && r.value[0]) ? strtoll(r.value, NULL, 10) : fallback;
    free(r.value);
    return PICOMESH_OK(picomesh_int64, v);
}

/* Allocate an opaque 128-bit refresh token (hex). Fails closed if secure
 * randomness is unavailable — a refresh token is a bearer secret. */
static int alloc_refresh_token(char *out, size_t cap)
{
    uint8_t raw[16];
    size_t got = 0;
    while (got < sizeof(raw)) {
        ssize_t n = getrandom(raw + got, sizeof(raw) - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
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

/* Load this process's JWT signing secret (caller frees on success). */
static struct picomesh_string_result ti_secret(void)
{
    return picomesh_security_jwt_secret(picomesh_active_engine());
}

/* Fetch a user's group memberships from the accounts service. Returns an
 * owned CSV string (possibly empty); never NULL on success. */
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
        if (!empty) return PICOMESH_ERR(picomesh_string, "token_issuer: out of memory");
        return PICOMESH_OK(picomesh_string, empty);
    }
    return groups;
}

/* Mint a signed access JWT for (uid, username, groups) with the given TTL.
 * Returns an owned token string. */
static struct picomesh_string_result ti_mint_access(uint32_t uid, const char *username,
                                                    const char *groups_csv, int64_t ttl_seconds)
{
    struct picomesh_string_result sec = ti_secret();
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

/* Build the {access_jwt, refresh_token, uid, username, groups} JSON returned
 * by login/refresh. Consumes none of its inputs. */
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

/* Mint a fresh opaque refresh token, persist it, and bump the live count. */
static struct picomesh_string_result ti_issue_refresh(struct ti_storage *h, struct yheaders *hdrs,
                                                      uint32_t uid, const char *username)
{
    char token[40];
    if (!alloc_refresh_token(token, sizeof(token)))
        return PICOMESH_ERR(picomesh_string, "token_issuer: secure random unavailable");
    char key[64], value[128];
    snprintf(key, sizeof(key), "refresh:%s", token);
    snprintf(value, sizeof(value), "%u\t%s", uid, username ? username : "");
    struct picomesh_int_result w = sharded_storage_db_set(&h->c, h->obj, hdrs, TI_CTX, key, value);
    if (PICOMESH_IS_ERR(w)) return PICOMESH_ERR(picomesh_string, "token_issuer: persist refresh failed", w);
    struct picomesh_int64_result cc = ti_incr(h, hdrs, "count", 1);
    if (PICOMESH_IS_ERR(cc)) return PICOMESH_ERR(picomesh_string, "token_issuer: bump count failed", cc);
    char *out = strdup(token);
    if (!out) return PICOMESH_ERR(picomesh_string, "token_issuer: out of memory");
    return PICOMESH_OK(picomesh_string, out);
}

PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_login")
struct picomesh_json_result token_issuer_token_issuer_login_impl(struct ctx *ctx, struct object *obj,
                                                                 struct yheaders *hdrs,
                                                                 const char *method, uint32_t uid,
                                                                 const char *username, int64_t pw_hash)
{
    (void)ctx; (void)obj;
    if (!method || strcmp(method, "password") != 0)
        return PICOMESH_ERR(picomesh_json, "token_issuer_login: unsupported auth method");
    if (!username) username = "";

    /* Delegate credential verification to the password authn plugin. */
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

    struct ti_storage_result sr = ti_open();
    if (PICOMESH_IS_ERR(sr)) { free(groups.value); free(access.value); return PICOMESH_ERR(picomesh_json, "token_issuer_login: storage open failed", sr); }
    struct ti_storage h = sr.value;
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
    (void)ctx; (void)obj;
    if (!refresh_token || !*refresh_token)
        return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: missing refresh token");
    struct ti_storage_result sr = ti_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: storage open failed", sr);
    struct ti_storage h = sr.value;

    char key[64];
    snprintf(key, sizeof(key), "refresh:%s", refresh_token);
    struct picomesh_string_result row = sharded_storage_db_get(&h.c, h.obj, hdrs, TI_CTX, key);
    if (PICOMESH_IS_ERR(row)) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: read failed", row);
    if (!row.value || !row.value[0]) { free(row.value); return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: unknown refresh token"); }

    uint32_t uid = 0;
    char username[64] = {0};
    char *tab = strchr(row.value, '\t');
    if (tab) { *tab = 0; uid = (uint32_t)strtoul(row.value, NULL, 10); snprintf(username, sizeof(username), "%s", tab + 1); }
    free(row.value);

    /* Rotate: delete the presented token (gated on actually removing it), then
     * issue a fresh one. */
    struct picomesh_int_result del = sharded_storage_db_del(&h.c, h.obj, hdrs, TI_CTX, key);
    if (PICOMESH_IS_ERR(del)) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: delete failed", del);
    if (del.value == 0) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: token already rotated");
    struct picomesh_int64_result dc = ti_incr(&h, hdrs, "count", -1);
    if (PICOMESH_IS_ERR(dc)) return PICOMESH_ERR(picomesh_json, "token_issuer_refresh: count update failed", dc);

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

/* Low-level mint: a signed access JWT for an already-resolved identity. Used
 * by the gateway's bearer-opaque (PAT) authenticator after it resolves the
 * token to a uid — minting stays in the issuer, not the authn path. */
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
    (void)ctx; (void)obj;
    struct ti_storage_result sr = ti_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "token_issuer_count: storage open failed", sr);
    struct ti_storage h = sr.value;
    struct picomesh_int64_result cr = ti_get(&h, hdrs, "count", 0);
    if (PICOMESH_IS_ERR(cr)) return PICOMESH_ERR(picomesh_size, "token_issuer_count: read failed", cr);
    return PICOMESH_OK(picomesh_size, (size_t)(cr.value < 0 ? 0 : cr.value));
}

/* List ALL live refresh tokens as a JSON array (gh#15). */
PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_list")
struct picomesh_json_result token_issuer_token_issuer_list_impl(struct ctx *ctx, struct object *obj,
                                                         struct yheaders *hdrs,
                                                         int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    struct ti_storage_result sr = ti_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "token_issuer_list: storage open failed", sr);
    struct ti_storage h = sr.value;
    return sharded_storage_db_list(&h.c, h.obj, hdrs, TI_CTX, "refresh:", offset, limit);
}

PICOMESH_CLASS_ANNOTATE("override@token_issuer:token_issuer:token_issuer_list_all")
struct picomesh_json_result token_issuer_token_issuer_list_all_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct ti_storage_result sr = ti_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "token_issuer_list_all: storage open failed", sr);
    struct ti_storage h = sr.value;
    return sharded_storage_db_list_all(&h.c, h.obj, hdrs, TI_CTX, "refresh:");
}

#include "store.gen.c"
