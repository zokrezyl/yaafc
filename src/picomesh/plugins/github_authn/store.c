/* github_authn — GitHub OAuth App bridge.
 *
 * Picoforge's GitHub login follows Woodpecker's forge model: the user
 * authorizes a GitHub OAuth App, the gateway exchanges the one-time code
 * for a GitHub OAuth access token, and the token stays server-side. The
 * browser receives only the ordinary picomesh opaque session cookie.
 *
 *   exchange_code(code, redirect_uri)
 *       -> {"uid":..., "username":"...", "github_id":...}
 *
 * Tokens are persisted in sharded_storage under the `github_authn`
 * context. The HTTPS calls to GitHub go through the vendored static libcurl
 * (OpenSSL TLS) — see build-tools/3rdparty/libcurl. Secrets (the OAuth
 * client_secret and the GitHub access token) are handed to libcurl as a POST
 * body / request header and stay in THIS process's memory: they never appear
 * on a child process's argv (a `ps` listing is world-visible to same-host
 * users) and no shell is ever spawned.
 *
 * The old register_code/resolve methods remain for existing tests and for
 * offline smoke flows. */

#define _POSIX_C_SOURCE 200809L

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/engine/engine.h>
#include <picomesh/config/config.h>
#include <picomesh/core/idkey.h>
#include <picomesh/json/json.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>
#include <picomesh/plugin/accounts/accounts.h>

#include <curl/curl.h>

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GH_MAX_CODES 128

struct gh_code_entry {
    uint32_t code;
    uint32_t user_id;
    int used;
};

struct PICOMESH_CLASS_ANNOTATE("class@github_authn:github_authn") github_authn_github_authn_data {
    uint32_t client_id;
    uint32_t secret_id;     /* opaque secret token id from config substitution */
    struct gh_code_entry codes[GH_MAX_CODES];
    size_t count;
};

static struct github_authn_github_authn_data *gh(struct object *obj)
{
    return (struct github_authn_github_authn_data *)((char *)obj + sizeof(struct object));
}

static struct const_char_ptr_result cfg_string(const char *path, const char *fallback)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_OK(const_char_ptr, fallback);
    struct config_node_ptr_result config_res = config_get(picomesh_engine_config(engine), path);
    PICOMESH_RETURN_IF_ERR(const_char_ptr, config_res, "cfg_string: config read failed");
    const char *out = config_res.value ? config_node_as_string(config_res.value, fallback) : fallback;
    return PICOMESH_OK(const_char_ptr, out && *out ? out : fallback);
}

/* Growable response sink for a libcurl transfer. */
struct gh_http_buf {
    char  *data;
    size_t len;
};

/* CURLOPT_WRITEFUNCTION sink: append the body chunk, NUL-terminating as we go,
 * with a hard ceiling so a hostile/huge response can't exhaust memory. */
static size_t gh_http_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct gh_http_buf *buf = userdata;
    size_t add = size * nmemb;
    if (buf->len + add + 1 < buf->len) return 0;          /* size_t wrap */
    if (buf->len + add > 4u * 1024 * 1024) return 0;      /* 4 MiB ceiling */
    char *grown = realloc(buf->data, buf->len + add + 1);
    if (!grown) return 0;
    buf->data = grown;
    memcpy(buf->data + buf->len, ptr, add);
    buf->len += add;
    buf->data[buf->len] = 0;
    return add;
}

/* Common libcurl options for the GitHub calls (timeouts, no signals so it is
 * safe on the engine's worker threads, no redirects). */
static void gh_http_setup(CURL *curl, struct gh_http_buf *buf)
{
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "picoforge");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, gh_http_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
}

/* 1 if the transfer succeeded with a 2xx status. */
static int gh_http_ok(CURL *curl, CURLcode rc)
{
    if (rc != CURLE_OK) return 0;
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    return status >= 200 && status < 300;
}

/* Exchange the one-time OAuth code for a GitHub access token. The client_secret
 * is set as CURLOPT_POSTFIELDS — it lives only in this process's heap, never on
 * any argv. Returns the token (caller frees) or NULL. */
static struct picomesh_string_result github_token_exchange(const char *code, const char *redirect_uri)
{
    struct const_char_ptr_result client_res = cfg_string("github_authn.client_id", getenv("PICOFORGE_GITHUB_CLIENT"));
    PICOMESH_RETURN_IF_ERR(picomesh_string, client_res, "github_token_exchange: client_id config");
    struct const_char_ptr_result secret_res = cfg_string("github_authn.client_secret", getenv("PICOFORGE_GITHUB_SECRET"));
    PICOMESH_RETURN_IF_ERR(picomesh_string, secret_res, "github_token_exchange: client_secret config");
    struct const_char_ptr_result url_res = cfg_string("github_authn.oauth_url", "https://github.com/login/oauth/access_token");
    PICOMESH_RETURN_IF_ERR(picomesh_string, url_res, "github_token_exchange: oauth_url config");
    const char *client = client_res.value, *secret = secret_res.value, *url = url_res.value;
    if (!client || !*client || !secret || !*secret) return PICOMESH_OK(picomesh_string, NULL);

    CURL *curl = curl_easy_init();
    if (!curl) return PICOMESH_OK(picomesh_string, NULL);

    char *enc_client   = curl_easy_escape(curl, client, 0);
    char *enc_secret   = curl_easy_escape(curl, secret, 0);
    char *enc_code     = curl_easy_escape(curl, code, 0);
    char *enc_redirect = curl_easy_escape(curl, redirect_uri, 0);
    struct curl_slist *headers = NULL;
    struct gh_http_buf buf = {0};
    char *body = NULL, *token = NULL;

    if (!enc_client || !enc_secret || !enc_code || !enc_redirect) goto done;
    size_t need = strlen(enc_client) + strlen(enc_secret) +
                  strlen(enc_code) + strlen(enc_redirect) + 64;
    body = malloc(need);
    if (!body) goto done;
    snprintf(body, need, "client_id=%s&client_secret=%s&code=%s&redirect_uri=%s",
             enc_client, enc_secret, enc_code, enc_redirect);

    headers = curl_slist_append(NULL, "Accept: application/json");
    if (!headers) goto done;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    gh_http_setup(curl, &buf);

    CURLcode rc = curl_easy_perform(curl);
    if (gh_http_ok(curl, rc) && buf.data) {
        struct json_doc *doc = json_parse(buf.data, strlen(buf.data));
        if (doc) {
            const char *tok = json_as_string(
                json_object_get(json_doc_root(doc), "access_token"), NULL);
            if (tok && *tok) token = strdup(tok);
            json_doc_free(doc);
        }
    }
done:
    free(body);
    free(buf.data);
    curl_slist_free_all(headers);
    curl_free(enc_client);
    curl_free(enc_secret);
    curl_free(enc_code);
    curl_free(enc_redirect);
    curl_easy_cleanup(curl);
    return PICOMESH_OK(picomesh_string, token);
}

/* Fetch the authenticated user's GitHub profile JSON. The access token is set
 * as an Authorization header — in-process only, never on any argv. Returns the
 * response body (caller frees) or NULL. */
static struct picomesh_string_result github_user_json(const char *access_token)
{
    struct const_char_ptr_result api_res = cfg_string("github_authn.api_url", "https://api.github.com");
    PICOMESH_RETURN_IF_ERR(picomesh_string, api_res, "github_user_json: api_url config");
    const char *api = api_res.value;
    if (!access_token || !*access_token) return PICOMESH_OK(picomesh_string, NULL);
    char api_user[768];
    int un = snprintf(api_user, sizeof(api_user), "%s/user", api ? api : "");
    if (un <= 0 || (size_t)un >= sizeof(api_user)) return PICOMESH_OK(picomesh_string, NULL);

    char auth[2200];
    int an = snprintf(auth, sizeof(auth), "Authorization: Bearer %s", access_token);
    if (an <= 0 || (size_t)an >= sizeof(auth)) return PICOMESH_OK(picomesh_string, NULL);

    CURL *curl = curl_easy_init();
    if (!curl) return PICOMESH_OK(picomesh_string, NULL);
    struct curl_slist *headers = NULL;
    struct gh_http_buf buf = {0};
    char *out = NULL;

    headers = curl_slist_append(NULL, "Accept: application/vnd.github+json");
    struct curl_slist *with_auth = headers ? curl_slist_append(headers, auth) : NULL;
    if (!with_auth) goto done;
    headers = with_auth;

    curl_easy_setopt(curl, CURLOPT_URL, api_user);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    gh_http_setup(curl, &buf);

    CURLcode rc = curl_easy_perform(curl);
    if (gh_http_ok(curl, rc) && buf.data) {
        out = buf.data;
        buf.data = NULL;
    }
done:
    free(buf.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return PICOMESH_OK(picomesh_string, out);
}

static void normalize_github_login(const char *login, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = login ? login : ""; *p && o + 1 < cap && o < 32; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[o++] = c;
    }
    out[o] = 0;
}

/* Persist the KV records that bind a GitHub identity to `uid`.
 *
 * No multi-key transaction exists, so the writes are ordered for crash-safety:
 * the reverse index `uid:<uid>:github_id` — the row the caller's collision guard
 * reads — is claimed FIRST via put_if_absent (an atomic winner-election). This
 * closes the partial-failure window that a plain ordered set sequence had: if a
 * prior attempt wrote the forward `github:<id>:*` rows but died before the
 * reverse index, a DIFFERENT GitHub identity hashing to the same uid would have
 * read an absent reverse index and silently adopted the account. Now:
 *   - first writer wins the reverse index and owns the uid;
 *   - the same identity retrying sees its own value and proceeds idempotently;
 *   - a different identity is rejected here, not just at the caller's read guard.
 * The forward rows are then (re)written unconditionally — safe because the uid
 * is already pinned to this github_id, so a retry merely refreshes them. */
static struct picomesh_void_result store_mapping(struct yheaders *hdrs, uint32_t uid,
                                                 int64_t github_id, const char *username,
                                                 const char *access_token)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine || !access_token || !*access_token || github_id <= 0)
        return PICOMESH_ERR(picomesh_void, "store_mapping: bad args or no active engine");
    struct ctx storage_ctx = picomesh_engine_service_ctx(engine, "sharded_storage");
    struct object_ptr_result db_handle_res = sharded_storage_db_create(&storage_ctx);
    if (PICOMESH_IS_ERR(db_handle_res))
        return PICOMESH_ERR(picomesh_void, "store_mapping: storage handle create failed",
                            db_handle_res);
    struct object *db_handle = db_handle_res.value;

    struct picomesh_void_result result = PICOMESH_OK_VOID();
    char key[96], value[512];
    struct picomesh_int_result set_res;

    /* Authoritative claim of the reverse index. */
    snprintf(key, sizeof(key), "uid:%u:github_id", uid);
    snprintf(value, sizeof(value), "%lld", (long long)github_id);
    struct picomesh_int_result claim_res =
        sharded_storage_db_put_if_absent(&storage_ctx, db_handle, hdrs, "github_authn", key, value);
    if (PICOMESH_IS_ERR(claim_res)) {
        result = PICOMESH_ERR(picomesh_void, "store_mapping: reverse-index claim failed", claim_res);
        goto done;
    }
    if (claim_res.value == 0) {
        /* Already claimed — accept only if it is THIS identity (idempotent
         * retry); a mismatch is a collision and must not overwrite the binding. */
        struct picomesh_string_result cur =
            sharded_storage_db_get(&storage_ctx, db_handle, hdrs, "github_authn", key);
        if (PICOMESH_IS_ERR(cur)) {
            result = PICOMESH_ERR(picomesh_void, "store_mapping: reverse-index read-back failed", cur);
            goto done;
        }
        int64_t bound = (cur.value && *cur.value) ? strtoll(cur.value, NULL, 10) : 0;
        free(cur.value);
        if (bound != github_id) {
            result = PICOMESH_ERR(picomesh_void,
                                  "store_mapping: uid already bound to a different GitHub identity");
            goto done;
        }
    }

    snprintf(key, sizeof(key), "github:%lld:token", (long long)github_id);
    set_res = sharded_storage_db_set(&storage_ctx, db_handle, hdrs, "github_authn", key,
                                     access_token);
    if (PICOMESH_IS_ERR(set_res)) {
        result = PICOMESH_ERR(picomesh_void, "store_mapping: github token write failed", set_res);
        goto done;
    }
    snprintf(key, sizeof(key), "github:%lld:uid", (long long)github_id);
    snprintf(value, sizeof(value), "%u", uid);
    set_res = sharded_storage_db_set(&storage_ctx, db_handle, hdrs, "github_authn", key, value);
    if (PICOMESH_IS_ERR(set_res)) {
        result = PICOMESH_ERR(picomesh_void, "store_mapping: github_id->uid write failed", set_res);
        goto done;
    }
    snprintf(key, sizeof(key), "uid:%u:username", uid);
    set_res = sharded_storage_db_set(&storage_ctx, db_handle, hdrs, "github_authn", key,
                                     username ? username : "");
    if (PICOMESH_IS_ERR(set_res)) {
        result = PICOMESH_ERR(picomesh_void, "store_mapping: uid->username write failed", set_res);
        goto done;
    }

done:
    object_release_in_ctx(&storage_ctx, db_handle);
    return result;
}

/* The uid currently bound to this GitHub id (the forward mapping written by
 * store_mapping): >0 if this GitHub identity already has an account, 0 if not,
 * error on storage failure (callers fail closed). The GitHub id is the STABLE
 * anchor for the identity, so this is the first thing exchange_code consults —
 * it makes repeated sign-ins resolve to the same account regardless of how the
 * uid was assigned. */
static struct picomesh_int64_result gh_lookup_uid_by_github_id(struct yheaders *hdrs, int64_t github_id)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(picomesh_int64, "gh_lookup_uid_by_github_id: no active engine");
    struct ctx service_ctx = picomesh_engine_service_ctx(engine, "sharded_storage");
    struct object_ptr_result object_res = sharded_storage_db_create(&service_ctx);
    PICOMESH_RETURN_IF_ERR(picomesh_int64, object_res, "gh_lookup_uid_by_github_id: storage create failed");
    char key[96];
    snprintf(key, sizeof(key), "github:%lld:uid", (long long)github_id);
    struct picomesh_string_result get_res = sharded_storage_db_get(&service_ctx, object_res.value, hdrs, "github_authn", key);
    object_release_in_ctx(&service_ctx, object_res.value);
    PICOMESH_RETURN_IF_ERR(picomesh_int64, get_res, "gh_lookup_uid_by_github_id: storage get failed");
    int64_t uid = (get_res.value && *get_res.value) ? strtoll(get_res.value, NULL, 10) : 0;
    free(get_res.value);
    return PICOMESH_OK(picomesh_int64, uid < 0 ? 0 : uid);
}

/* Resolve a username to its assigned uid via the accounts service (0 if no such
 * confirmed account). github_authn lists accounts as a remote; the internal
 * backend-to-backend call carries no caller auth. */
static struct picomesh_int64_result gh_uid_for_username(const char *username)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(picomesh_int64, "gh_uid_for_username: no active engine");
    struct ctx service_ctx = picomesh_engine_service_ctx(engine, "accounts");
    struct object_ptr_result object_res = accounts_accounts_create(&service_ctx);
    PICOMESH_RETURN_IF_ERR(picomesh_int64, object_res, "gh_uid_for_username: accounts create failed");
    struct picomesh_int64_result uid_res = accounts_accounts_uid_for_username(&service_ctx, object_res.value, NULL, username);
    object_release_in_ctx(&service_ctx, object_res.value);
    return uid_res;
}

/* Allocate a fresh, never-reused uid via the accounts service. */
static struct picomesh_int64_result gh_allocate_uid(void)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(picomesh_int64, "gh_allocate_uid: no active engine");
    struct ctx service_ctx = picomesh_engine_service_ctx(engine, "accounts");
    struct object_ptr_result object_res = accounts_accounts_create(&service_ctx);
    PICOMESH_RETURN_IF_ERR(picomesh_int64, object_res, "gh_allocate_uid: accounts create failed");
    struct picomesh_int64_result uid_res = accounts_accounts_allocate_uid(&service_ctx, object_res.value, NULL);
    object_release_in_ctx(&service_ctx, object_res.value);
    return uid_res;
}

PICOMESH_CLASS_ANNOTATE("override@github_authn:github_authn:github_authn_exchange_code")
struct picomesh_json_result github_authn_github_authn_exchange_code_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs, const char *code, const char *redirect_uri)
{
    (void)ctx; (void)obj;
    if (!code || !*code || !redirect_uri || !*redirect_uri)
        return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: missing code or redirect_uri");
    struct picomesh_string_result token_res = github_token_exchange(code, redirect_uri);
    PICOMESH_RETURN_IF_ERR(picomesh_json, token_res, "github_authn_exchange_code: token exchange failed");
    char *token = token_res.value;
    if (!token) return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: token exchange failed");
    struct picomesh_string_result user_res = github_user_json(token);
    if (PICOMESH_IS_ERR(user_res)) { free(token); return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: /user request failed", user_res); }
    char *user_json = user_res.value;
    if (!user_json) { free(token); return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: /user request failed"); }
    struct json_doc *doc = json_parse(user_json, strlen(user_json));
    free(user_json);
    if (!doc) { free(token); return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: bad /user JSON"); }
    const struct json_value *root = json_doc_root(doc);
    int64_t github_id = json_as_int(json_object_get(root, "id"), 0);
    const char *login = json_as_string(json_object_get(root, "login"), NULL);
    char username[64];
    normalize_github_login(login, username, sizeof(username));
    json_doc_free(doc);
    if (github_id <= 0 || !username[0]) { free(token); return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: missing GitHub id/login"); }
    /* Resolve the uid for this GitHub identity (issue #29: assigned, never-reused
     * uids, not fnv1a32(username)). The GitHub id is the STABLE anchor:
     *   1) if it is already bound to a uid, reuse it — repeated sign-ins and
     *      retries resolve to the same account;
     *   2) otherwise, if a (e.g. password) account already owns this username,
     *      REFUSE — GitHub sign-in must never adopt an account that is not this
     *      GitHub identity;
     *   3) a brand-new GitHub identity gets a freshly allocated uid.
     * Any storage/lookup/allocation error fails closed. */
    uint32_t uid = 0;
    struct picomesh_int64_result bound_uid_res = gh_lookup_uid_by_github_id(hdrs, github_id);
    if (PICOMESH_IS_ERR(bound_uid_res)) {
        free(token);
        return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: identity lookup failed", bound_uid_res);
    }
    if (bound_uid_res.value > 0) {
        uid = (uint32_t)bound_uid_res.value;
    } else {
        struct picomesh_int64_result named_res = gh_uid_for_username(username);
        if (PICOMESH_IS_ERR(named_res)) {
            free(token);
            return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: account lookup failed", named_res);
        }
        if (named_res.value > 0) {
            ywarn("github_authn: refusing to bind GitHub id=%lld — username '%s' already held by a non-GitHub account (uid=%lld)",
                  (long long)github_id, username, (long long)named_res.value);
            free(token);
            return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: an account with this name already exists");
        }
        struct picomesh_int64_result alloc_res = gh_allocate_uid();
        if (PICOMESH_IS_ERR(alloc_res)) {
            free(token);
            return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: uid allocation failed", alloc_res);
        }
        if (alloc_res.value <= 0) {
            free(token);
            return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: uid allocation failed");
        }
        uid = (uint32_t)alloc_res.value;
    }
    struct picomesh_void_result map_res = store_mapping(hdrs, uid, github_id, username, token);
    free(token);
    if (PICOMESH_IS_ERR(map_res))
        return PICOMESH_ERR(picomesh_json,
                            "github_authn_exchange_code: persisting GitHub identity mapping failed",
                            map_res);
    struct json_writer *w = json_writer_new();
    if (!w) return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: writer alloc failed");
    json_writer_begin_object(w);
    json_writer_key(w, "uid");       json_writer_int(w, (int64_t)uid);
    json_writer_key(w, "username");  json_writer_string(w, username);
    json_writer_key(w, "github_id"); json_writer_int(w, github_id);
    json_writer_end_object(w);
    size_t len = 0;
    const char *data = json_writer_data(w, &len);
    char *out = data ? strdup(data) : NULL;
    json_writer_free(w);
    if (!out) return PICOMESH_ERR(picomesh_json, "github_authn_exchange_code: strdup failed");
    yinfo("github_authn: exchanged GitHub OAuth code for user=%s id=%lld", username, (long long)github_id);
    return PICOMESH_OK(picomesh_json, out);
}

PICOMESH_CLASS_ANNOTATE("override@github_authn:github_authn:github_authn_set_credentials")
struct picomesh_int_result github_authn_github_authn_set_credentials_impl(struct ctx *ctx,
                                                                struct object *obj,
                                                                struct yheaders *hdrs,
                                                                uint32_t client_id,
                                                                uint32_t secret_id)
{
    (void)ctx;
    struct github_authn_github_authn_data *d = gh(obj);
    d->client_id = client_id;
    d->secret_id = secret_id;
    yinfo("github_authn: credentials set (client_id=%u)", client_id);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@github_authn:github_authn:github_authn_register_code")
struct picomesh_int_result github_authn_github_authn_register_code_impl(struct ctx *ctx,
                                                              struct object *obj,
                                                              struct yheaders *hdrs,
                                                              uint32_t code, uint32_t user_id)
{
    (void)ctx;
    struct github_authn_github_authn_data *d = gh(obj);
    for (size_t i = 0; i < GH_MAX_CODES; ++i) {
        if (!d->codes[i].used) {
            d->codes[i].code = code;
            d->codes[i].user_id = user_id;
            d->codes[i].used = 1;
            d->count++;
            return PICOMESH_OK(picomesh_int, 1);
        }
    }
    return PICOMESH_ERR(picomesh_int, "github_authn_register_code: table full");
}

PICOMESH_CLASS_ANNOTATE("override@github_authn:github_authn:github_authn_resolve")
struct picomesh_uint32_result github_authn_github_authn_resolve_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                           uint32_t code)
{
    (void)ctx;
    struct github_authn_github_authn_data *d = gh(obj);
    for (size_t i = 0; i < GH_MAX_CODES; ++i) {
        if (d->codes[i].used && d->codes[i].code == code) {
            return PICOMESH_OK(picomesh_uint32, d->codes[i].user_id);
        }
    }
    return PICOMESH_OK(picomesh_uint32, 0);
}

PICOMESH_CLASS_ANNOTATE("override@github_authn:github_authn:github_authn_count_codes")
struct picomesh_size_result github_authn_github_authn_count_codes_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    return PICOMESH_OK(picomesh_size, gh(obj)->count);
}

/* List ALL OAuth codes this service manages, as a JSON array
 * `[{"code":…,"user_id":…}, …]` (gh#15) — every code, not a count. State is
 * the in-memory code table. */
/* Build the code list as JSON, honoring offset/limit (< 0 == all). */
static struct picomesh_json_result github_authn_list_window(struct object *obj, int64_t offset, int64_t limit)
{
    struct github_authn_github_authn_data *d = gh(obj);
    struct json_writer *w = json_writer_new();
    if (!w) return PICOMESH_ERR(picomesh_json, "github_authn_list: writer alloc failed");
    json_writer_begin_array(w);
    int64_t skip = offset > 0 ? offset : 0, emitted = 0;
    for (size_t i = 0; i < GH_MAX_CODES && (limit < 0 || emitted < limit); ++i) {
        if (!d->codes[i].used) continue;
        if (skip > 0) { --skip; continue; }
        json_writer_begin_object(w);
        json_writer_key(w, "code");    json_writer_int(w, (int64_t)d->codes[i].code);
        json_writer_key(w, "user_id"); json_writer_int(w, (int64_t)d->codes[i].user_id);
        json_writer_end_object(w);
        ++emitted;
    }
    json_writer_end_array(w);
    size_t len = 0;
    const char *data = json_writer_data(w, &len);
    char *out = strdup(data ? data : "[]");
    json_writer_free(w);
    if (!out) return PICOMESH_ERR(picomesh_json, "github_authn_list: strdup failed");
    return PICOMESH_OK(picomesh_json, out);
}

/* List ALL OAuth codes as a JSON array, paginated (gh#15). */
PICOMESH_CLASS_ANNOTATE("override@github_authn:github_authn:github_authn_list")
struct picomesh_json_result github_authn_github_authn_list_impl(struct ctx *ctx, struct object *obj,
                                                         struct yheaders *hdrs,
                                                         int64_t offset, int64_t limit)
{
    (void)ctx; (void)hdrs;
    if (limit <= 0) limit = 100;
    return github_authn_list_window(obj, offset, limit);
}

/* Unbounded variant — every code. */
PICOMESH_CLASS_ANNOTATE("override@github_authn:github_authn:github_authn_list_all")
struct picomesh_json_result github_authn_github_authn_list_all_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx; (void)hdrs;
    return github_authn_list_window(obj, 0, -1);
}

#include "store.gen.c"
