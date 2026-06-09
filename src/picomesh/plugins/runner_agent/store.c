/* runner_agent — registry of external CI runner agents (docs/runner-agent.md).
 *
 * A runner agent is a process outside the mesh that authenticates to the
 * gateway with an opaque `rnr_<secret>` bearer token, registers, then
 * poll-leases pipeline jobs from git_pipeline. This plugin owns the runner
 * IDENTITY side: token mint/lookup/revoke and the runner record (name, labels,
 * version, host, status, last_seen). git_pipeline owns job-state correctness.
 *
 *   create_token(name, labels)  → JSON {runner_id, token}  (admin; token shown
 *                                  once — only its hash is stored)
 *   lookup_token(token)         → runner_id (0 if absent/revoked/disabled)
 *                                  — the gateway's runner authenticator path
 *   revoke_token(runner_id)     → 1 ok / 0 unknown
 *   register(runner_id, name, labels, version, host) → runner_id (status→online)
 *   heartbeat(runner_id, status)→ 1 (last_seen bumped)
 *   get(runner_id)              → JSON record
 *   list / list_all / count_active
 *
 * State lives in the shared `sharded_storage` service (context `runner_agent`):
 *   next_id              → monotonic runner-id counter
 *   count                → live (non-disabled) runner count
 *   runner:<id>          → JSON {name,labels,version,host,status,last_seen,tok_hash}
 *   tok:<sha256hex>      → "<runner_id>"   (token at rest is only its hash)
 *
 * The raw token is a bearer secret: it is returned exactly once at creation and
 * never stored, logged, or recoverable — only its SHA-256 hash is persisted. */

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/yheaders.h>
#include <picomesh/engine/engine.h>
#include <picomesh/json/json.h>
#include <picomesh/security/sha256.h>
#include <picomesh/security/jwt.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>
#include <picomesh/plugin/token_issuer/token_issuer.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#define RA_CTX "runner_agent"

/* No in-memory state — every op delegates to storage. */
struct PICOMESH_CLASS_ANNOTATE("class@runner_agent:runner_agent") runner_agent_runner_agent_data {
    char _unused;
};

struct ra_storage {
    struct ctx c;
    struct object *obj;
};
PICOMESH_RESULT_DECLARE(ra_storage, struct ra_storage);

/* Parsed runner record (the JSON stored at runner:<id>). */
struct ra_record {
    char name[128];
    char labels[256];
    char version[64];
    char host[160];
    char status[32];
    int64_t last_seen;
    char tok_hash[65]; /* 64 hex + nul */
};

static struct ra_storage_result ra_open(void)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(ra_storage, "runner_agent: no active engine");
    struct ra_storage storage = {.c = picomesh_engine_service_ctx(engine, "sharded_storage")};
    struct object_ptr_result create_res = sharded_storage_db_create(&storage.c);
    if (PICOMESH_IS_ERR(create_res)) return PICOMESH_ERR(ra_storage, "runner_agent: storage_db_create failed", create_res);
    storage.obj = create_res.value;
    return PICOMESH_OK(ra_storage, storage);
}

/* Atomic counter / id bump — OK value is the value after the add. */
static struct picomesh_int64_result ra_incr(struct ra_storage *storage, struct yheaders *hdrs, const char *key, int64_t delta)
{
    struct picomesh_int64_result incr_res = sharded_storage_db_incr(&storage->c, storage->obj, hdrs, RA_CTX, key, delta);
    if (PICOMESH_IS_ERR(incr_res)) return PICOMESH_ERR(picomesh_int64, "runner_agent: counter update failed", incr_res);
    return incr_res;
}

static struct picomesh_int64_result ra_get_int(struct ra_storage *storage, struct yheaders *hdrs, const char *key, int64_t fallback)
{
    struct picomesh_string_result get_res = sharded_storage_db_get(&storage->c, storage->obj, hdrs, RA_CTX, key);
    if (PICOMESH_IS_ERR(get_res)) return PICOMESH_ERR(picomesh_int64, "runner_agent: storage read failed", get_res);
    int64_t value = (get_res.value && get_res.value[0]) ? strtoll(get_res.value, NULL, 10) : fallback;
    free(get_res.value);
    return PICOMESH_OK(picomesh_int64, value);
}

/* SHA-256 hex of a NUL-terminated string into a 65-byte buffer. */
static void ra_sha256_hex(const char *s, char out[65])
{
    uint8_t digest[PICOMESH_SHA256_DIGEST_LEN];
    picomesh_sha256(s, strlen(s), digest);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < PICOMESH_SHA256_DIGEST_LEN; ++i) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 0x0f];
    }
    out[64] = 0;
}

/* Allocate an opaque `rnr_<32hex>` bearer token. Fails closed if secure
 * randomness is unavailable — the token is a bearer secret. */
static int ra_alloc_token(char *out, size_t cap)
{
    if (cap < 4 + 32 + 1) return 0;
    uint8_t raw[16];
    size_t got = 0;
    while (got < sizeof(raw)) {
        ssize_t read_len = getrandom(raw + got, sizeof(raw) - got, 0);
        if (read_len < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        got += (size_t)read_len;
    }
    static const char hex[] = "0123456789abcdef";
    memcpy(out, "rnr_", 4);
    size_t k = 4;
    for (size_t i = 0; i < sizeof(raw); ++i) {
        out[k++] = hex[raw[i] >> 4];
        out[k++] = hex[raw[i] & 0x0f];
    }
    out[k] = 0;
    return 1;
}

/* Serialize a runner record to owned JSON. */
static char *ra_record_to_json(const struct ra_record *rec)
{
    struct json_writer *writer = json_writer_new();
    if (!writer) return NULL;
    json_writer_begin_object(writer);
    json_writer_key(writer, "name");      json_writer_string(writer, rec->name);
    json_writer_key(writer, "labels");    json_writer_string(writer, rec->labels);
    json_writer_key(writer, "version");   json_writer_string(writer, rec->version);
    json_writer_key(writer, "host");      json_writer_string(writer, rec->host);
    json_writer_key(writer, "status");    json_writer_string(writer, rec->status);
    json_writer_key(writer, "last_seen"); json_writer_int(writer, rec->last_seen);
    json_writer_key(writer, "tok_hash");  json_writer_string(writer, rec->tok_hash);
    json_writer_end_object(writer);
    size_t len = 0;
    const char *data = json_writer_data(writer, &len);
    char *out = data ? strdup(data) : NULL;
    json_writer_free(writer);
    return out;
}

static void ra_copy_field(char *dst, size_t cap, const struct json_value *obj, const char *key)
{
    const char *value = json_as_string(json_object_get(obj, key), "");
    snprintf(dst, cap, "%s", value ? value : "");
}

/* Read runner:<id> into `rec`. OK value: 1 = present, 0 = absent. */
static struct picomesh_int_result ra_record_load(struct ra_storage *storage, struct yheaders *hdrs,
                                                 uint32_t runner_id, struct ra_record *rec)
{
    char key[48];
    snprintf(key, sizeof(key), "runner:%u", runner_id);
    struct picomesh_string_result get_res = sharded_storage_db_get(&storage->c, storage->obj, hdrs, RA_CTX, key);
    if (PICOMESH_IS_ERR(get_res)) return PICOMESH_ERR(picomesh_int, "runner_agent: record read failed", get_res);
    if (!get_res.value || !get_res.value[0]) { free(get_res.value); return PICOMESH_OK(picomesh_int, 0); }
    struct json_doc *doc = json_parse(get_res.value, strlen(get_res.value));
    free(get_res.value);
    if (!doc) return PICOMESH_ERR(picomesh_int, "runner_agent: record parse failed");
    const struct json_value *obj = json_doc_root(doc);
    memset(rec, 0, sizeof(*rec));
    ra_copy_field(rec->name, sizeof(rec->name), obj, "name");
    ra_copy_field(rec->labels, sizeof(rec->labels), obj, "labels");
    ra_copy_field(rec->version, sizeof(rec->version), obj, "version");
    ra_copy_field(rec->host, sizeof(rec->host), obj, "host");
    ra_copy_field(rec->status, sizeof(rec->status), obj, "status");
    ra_copy_field(rec->tok_hash, sizeof(rec->tok_hash), obj, "tok_hash");
    rec->last_seen = json_as_int(json_object_get(obj, "last_seen"), 0);
    json_doc_free(doc);
    return PICOMESH_OK(picomesh_int, 1);
}

/* Persist a runner record at runner:<id>. */
static struct picomesh_void_result ra_record_store(struct ra_storage *storage, struct yheaders *hdrs,
                                                   uint32_t runner_id, const struct ra_record *rec)
{
    char *json = ra_record_to_json(rec);
    if (!json) return PICOMESH_ERR(picomesh_void, "runner_agent: record encode failed");
    char key[48];
    snprintf(key, sizeof(key), "runner:%u", runner_id);
    struct picomesh_int_result set_res = sharded_storage_db_set(&storage->c, storage->obj, hdrs, RA_CTX, key, json);
    free(json);
    if (PICOMESH_IS_ERR(set_res)) return PICOMESH_ERR(picomesh_void, "runner_agent: record write failed", set_res);
    return PICOMESH_OK_VOID();
}

/* The caller's authenticated runner identity, as resolved by the gateway into
 * yheaders["uid"]. A runner JWT carries sub=runner_id, so uid IS the runner_id.
 * 0 means "no gateway context" (internal/test call) — allowed. */
static int ra_caller_is(struct yheaders *hdrs, uint32_t runner_id)
{
    uint32_t caller = hdrs ? yheaders_get_u32(hdrs, "uid", 0) : 0;
    return caller == 0 || caller == runner_id;
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_create_token")
struct picomesh_json_result runner_agent_runner_agent_create_token_impl(struct ctx *ctx, struct object *obj,
                                                                        struct yheaders *hdrs,
                                                                        const char *name, const char *labels)
{
    (void)ctx; (void)obj;
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: storage open failed", open_res);
    struct ra_storage storage = open_res.value;

    char token[40];
    if (!ra_alloc_token(token, sizeof(token)))
        return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: secure random unavailable");

    struct picomesh_int64_result id_res = ra_incr(&storage, hdrs, "next_id", 1);
    if (PICOMESH_IS_ERR(id_res)) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: allocate id failed", id_res);
    uint32_t runner_id = (uint32_t)id_res.value;

    struct ra_record rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.name, sizeof(rec.name), "%s", name ? name : "");
    snprintf(rec.labels, sizeof(rec.labels), "%s", labels ? labels : "");
    snprintf(rec.status, sizeof(rec.status), "offline");
    rec.last_seen = picomesh_security_now();
    ra_sha256_hex(token, rec.tok_hash);

    struct picomesh_void_result store_res = ra_record_store(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(store_res)) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: persist record failed", store_res);

    /* Build the response BEFORE writing the token→id mapping (the write that
     * makes the token usable). The storage layer has no multi-key transaction,
     * so order to avoid the one harmful partial state: a usable credential the
     * client never receives. If encoding fails here, no mapping exists yet, so
     * no usable token is stranded — only a tokenless record (benign). */
    struct json_writer *writer = json_writer_new();
    if (!writer) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: writer alloc failed");
    json_writer_begin_object(writer);
    json_writer_key(writer, "runner_id"); json_writer_int(writer, (int64_t)runner_id);
    json_writer_key(writer, "token");     json_writer_string(writer, token);
    json_writer_end_object(writer);
    size_t len = 0;
    const char *data = json_writer_data(writer, &len);
    char *out = data ? strdup(data) : NULL;
    json_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: encode failed");

    char tok_key[80];
    snprintf(tok_key, sizeof(tok_key), "tok:%s", rec.tok_hash);
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", runner_id);
    struct picomesh_int_result tok_set_res = sharded_storage_db_set(&storage.c, storage.obj, hdrs, RA_CTX, tok_key, id_str);
    if (PICOMESH_IS_ERR(tok_set_res)) { free(out); return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: persist token failed", tok_set_res); }

    /* The active-count is a soft metric, not a security control. The credential
     * is already created and the client must receive it, so a count-bump failure
     * is logged (count may drift low) rather than failing the whole creation. */
    struct picomesh_int64_result count_res = ra_incr(&storage, hdrs, "count", 1);
    if (PICOMESH_IS_ERR(count_res)) {
        picomesh_error_print(stderr, "runner_agent_create_token: active-count bump (count may drift low)", count_res.error);
        picomesh_error_destroy(count_res.error);
    }

    /* The raw token is never logged — only the runner id. */
    yinfo("runner_agent: created runner=%u name=%s", runner_id, rec.name);
    return PICOMESH_OK(picomesh_json, out);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_lookup_token")
struct picomesh_uint32_result runner_agent_runner_agent_lookup_token_impl(struct ctx *ctx, struct object *obj,
                                                                          struct yheaders *hdrs,
                                                                          const char *token)
{
    (void)ctx; (void)obj;
    if (!token || !*token) return PICOMESH_OK(picomesh_uint32, 0);
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_lookup_token: storage open failed", open_res);
    struct ra_storage storage = open_res.value;

    char hash[65];
    ra_sha256_hex(token, hash);
    char tok_key[80];
    snprintf(tok_key, sizeof(tok_key), "tok:%s", hash);
    struct picomesh_string_result get_res = sharded_storage_db_get(&storage.c, storage.obj, hdrs, RA_CTX, tok_key);
    if (PICOMESH_IS_ERR(get_res)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_lookup_token: read failed", get_res);
    if (!get_res.value || !get_res.value[0]) { free(get_res.value); return PICOMESH_OK(picomesh_uint32, 0); }
    uint32_t runner_id = (uint32_t)strtoul(get_res.value, NULL, 10);
    free(get_res.value);
    if (runner_id == 0) return PICOMESH_OK(picomesh_uint32, 0);

    /* A disabled (revoked) runner must not authenticate even if a stale token
     * mapping lingers. */
    struct ra_record rec;
    struct picomesh_int_result load_res = ra_record_load(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(load_res)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_lookup_token: record load failed", load_res);
    if (load_res.value == 0) return PICOMESH_OK(picomesh_uint32, 0);
    if (strcmp(rec.status, "disabled") == 0) return PICOMESH_OK(picomesh_uint32, 0);
    return PICOMESH_OK(picomesh_uint32, runner_id);
}

/* Exchange an opaque `rnr_` token for a runner access JWT. This is the
 * credential-exchange the gateway's generic `bearer_opaque_token` authenticator
 * calls (it passes the token, gets back a JWT, and verifies it — it never mints
 * or learns runner internals). The JWT's sub is the runner_id and its groups
 * are "site:runner,runner:<id>" so the policy can gate the runner-only methods.
 * Minting stays with the token issuer; this service just composes the
 * exchange. */
PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_exchange")
struct picomesh_string_result runner_agent_runner_agent_exchange_impl(struct ctx *ctx, struct object *obj,
                                                                      struct yheaders *hdrs, const char *token)
{
    struct picomesh_uint32_result lookup_res = runner_agent_runner_agent_lookup_token_impl(ctx, obj, hdrs, token);
    if (PICOMESH_IS_ERR(lookup_res)) return PICOMESH_ERR(picomesh_string, "runner_agent_exchange: lookup failed", lookup_res);
    uint32_t runner_id = lookup_res.value;
    if (runner_id == 0) return PICOMESH_ERR(picomesh_string, "runner_agent_exchange: unknown or revoked runner token");

    char username[40], groups[64];
    snprintf(username, sizeof(username), "runner-%u", runner_id);
    snprintf(groups, sizeof(groups), "site:runner,runner:%u", runner_id);

    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(picomesh_string, "runner_agent_exchange: no active engine");
    struct ctx ti_ctx = picomesh_engine_service_ctx(engine, "token_issuer");
    struct object_ptr_result ti_obj = token_issuer_token_issuer_create(&ti_ctx);
    if (PICOMESH_IS_ERR(ti_obj)) return PICOMESH_ERR(picomesh_string, "runner_agent_exchange: token_issuer unreachable", ti_obj);
    struct picomesh_string_result jwt =
        token_issuer_token_issuer_mint(&ti_ctx, ti_obj.value, hdrs, runner_id, username, groups, 0);
    if (PICOMESH_IS_ERR(jwt)) return PICOMESH_ERR(picomesh_string, "runner_agent_exchange: mint failed", jwt);
    return jwt;
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_revoke_token")
struct picomesh_int_result runner_agent_runner_agent_revoke_token_impl(struct ctx *ctx, struct object *obj,
                                                                       struct yheaders *hdrs,
                                                                       uint32_t runner_id)
{
    (void)ctx; (void)obj;
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: storage open failed", open_res);
    struct ra_storage storage = open_res.value;

    struct ra_record rec;
    struct picomesh_int_result load_res = ra_record_load(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(load_res)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: record load failed", load_res);
    if (load_res.value == 0) return PICOMESH_OK(picomesh_int, 0);
    if (strcmp(rec.status, "disabled") == 0) return PICOMESH_OK(picomesh_int, 0);

    if (rec.tok_hash[0]) {
        char tok_key[80];
        snprintf(tok_key, sizeof(tok_key), "tok:%s", rec.tok_hash);
        struct picomesh_int_result del_res = sharded_storage_db_del(&storage.c, storage.obj, hdrs, RA_CTX, tok_key);
        if (PICOMESH_IS_ERR(del_res)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: token delete failed", del_res);
    }
    snprintf(rec.status, sizeof(rec.status), "disabled");
    struct picomesh_void_result store_res = ra_record_store(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(store_res)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: record write failed", store_res);

    /* Token mapping is deleted and the record is now `disabled`: the revoke has
     * already taken effect (the token can no longer authenticate). The active-
     * count is a soft metric. If the decrement fails, do NOT return an error —
     * the caller would retry, hit the `disabled` short-circuit above, and never
     * reconcile the count, leaving it permanently high. Log the drift and report
     * success instead. */
    struct picomesh_int64_result count_res = ra_incr(&storage, hdrs, "count", -1);
    if (PICOMESH_IS_ERR(count_res)) {
        picomesh_error_print(stderr, "runner_agent_revoke_token: active-count decrement (count may drift high)", count_res.error);
        picomesh_error_destroy(count_res.error);
    }
    yinfo("runner_agent: revoked runner=%u", runner_id);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_register")
struct picomesh_uint32_result runner_agent_runner_agent_register_impl(struct ctx *ctx, struct object *obj,
                                                                      struct yheaders *hdrs,
                                                                      uint32_t runner_id, const char *name,
                                                                      const char *labels, const char *version,
                                                                      const char *host)
{
    (void)ctx; (void)obj;
    if (!ra_caller_is(hdrs, runner_id))
        return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: caller is not this runner");
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: storage open failed", open_res);
    struct ra_storage storage = open_res.value;

    struct ra_record rec;
    struct picomesh_int_result load_res = ra_record_load(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(load_res)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: record load failed", load_res);
    if (load_res.value == 0) return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: unknown runner");
    if (strcmp(rec.status, "disabled") == 0)
        return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: runner is disabled");

    if (name && *name) snprintf(rec.name, sizeof(rec.name), "%s", name);
    if (labels) snprintf(rec.labels, sizeof(rec.labels), "%s", labels);
    snprintf(rec.version, sizeof(rec.version), "%s", version ? version : "");
    snprintf(rec.host, sizeof(rec.host), "%s", host ? host : "");
    snprintf(rec.status, sizeof(rec.status), "online");
    rec.last_seen = picomesh_security_now();

    struct picomesh_void_result store_res = ra_record_store(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(store_res)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: record write failed", store_res);
    yinfo("runner_agent: register runner=%u labels=%s", runner_id, rec.labels);
    return PICOMESH_OK(picomesh_uint32, runner_id);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_heartbeat")
struct picomesh_int_result runner_agent_runner_agent_heartbeat_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs,
                                                                    uint32_t runner_id, const char *status)
{
    (void)ctx; (void)obj;
    if (!ra_caller_is(hdrs, runner_id))
        return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: caller is not this runner");
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: storage open failed", open_res);
    struct ra_storage storage = open_res.value;

    struct ra_record rec;
    struct picomesh_int_result load_res = ra_record_load(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(load_res)) return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: record load failed", load_res);
    if (load_res.value == 0) return PICOMESH_OK(picomesh_int, 0);
    if (strcmp(rec.status, "disabled") == 0)
        return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: runner is disabled");

    snprintf(rec.status, sizeof(rec.status), "%s", (status && *status) ? status : "online");
    rec.last_seen = picomesh_security_now();
    struct picomesh_void_result store_res = ra_record_store(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(store_res)) return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: record write failed", store_res);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_get")
struct picomesh_json_result runner_agent_runner_agent_get_impl(struct ctx *ctx, struct object *obj,
                                                               struct yheaders *hdrs, uint32_t runner_id)
{
    (void)ctx; (void)obj;
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "runner_agent_get: storage open failed", open_res);
    struct ra_storage storage = open_res.value;
    struct ra_record rec;
    struct picomesh_int_result load_res = ra_record_load(&storage, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(load_res)) return PICOMESH_ERR(picomesh_json, "runner_agent_get: record load failed", load_res);
    if (load_res.value == 0) { char *empty = strdup("{}"); return empty ? PICOMESH_OK(picomesh_json, empty)
                                                                  : PICOMESH_ERR(picomesh_json, "runner_agent_get: oom"); }
    /* Echo the record but never the token hash — that is at-rest secret material. */
    struct json_writer *writer = json_writer_new();
    if (!writer) return PICOMESH_ERR(picomesh_json, "runner_agent_get: writer alloc failed");
    json_writer_begin_object(writer);
    json_writer_key(writer, "runner_id"); json_writer_int(writer, (int64_t)runner_id);
    json_writer_key(writer, "name");      json_writer_string(writer, rec.name);
    json_writer_key(writer, "labels");    json_writer_string(writer, rec.labels);
    json_writer_key(writer, "version");   json_writer_string(writer, rec.version);
    json_writer_key(writer, "host");      json_writer_string(writer, rec.host);
    json_writer_key(writer, "status");    json_writer_string(writer, rec.status);
    json_writer_key(writer, "last_seen"); json_writer_int(writer, rec.last_seen);
    json_writer_end_object(writer);
    size_t len = 0;
    const char *data = json_writer_data(writer, &len);
    char *out = data ? strdup(data) : NULL;
    json_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_json, "runner_agent_get: encode failed");
    return PICOMESH_OK(picomesh_json, out);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_list")
struct picomesh_json_result runner_agent_runner_agent_list_impl(struct ctx *ctx, struct object *obj,
                                                                struct yheaders *hdrs,
                                                                int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "runner_agent_list: storage open failed", open_res);
    struct ra_storage storage = open_res.value;
    return sharded_storage_db_list(&storage.c, storage.obj, hdrs, RA_CTX, "runner:", offset, limit);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_list_all")
struct picomesh_json_result runner_agent_runner_agent_list_all_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "runner_agent_list_all: storage open failed", open_res);
    struct ra_storage storage = open_res.value;
    return sharded_storage_db_list_all(&storage.c, storage.obj, hdrs, RA_CTX, "runner:");
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_count_active")
struct picomesh_size_result runner_agent_runner_agent_count_active_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct ra_storage_result open_res = ra_open();
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_size, "runner_agent_count_active: storage open failed", open_res);
    struct ra_storage storage = open_res.value;
    struct picomesh_int64_result count_res = ra_get_int(&storage, hdrs, "count", 0);
    if (PICOMESH_IS_ERR(count_res)) return PICOMESH_ERR(picomesh_size, "runner_agent_count_active: read failed", count_res);
    return PICOMESH_OK(picomesh_size, (size_t)(count_res.value < 0 ? 0 : count_res.value));
}

#include "store.gen.c"
