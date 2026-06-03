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

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/ysecurity/sha256.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>

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
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(ra_storage, "runner_agent: no active engine");
    struct ra_storage h = {.c = picomesh_engine_service_ctx(e, "sharded_storage")};
    struct object_ptr_result o = sharded_storage_db_create(&h.c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(ra_storage, "runner_agent: storage_db_create failed", o);
    h.obj = o.value;
    return PICOMESH_OK(ra_storage, h);
}

/* Atomic counter / id bump — OK value is the value after the add. */
static struct picomesh_int64_result ra_incr(struct ra_storage *h, struct yheaders *hdrs, const char *key, int64_t delta)
{
    struct picomesh_int64_result r = sharded_storage_db_incr(&h->c, h->obj, hdrs, RA_CTX, key, delta);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "runner_agent: counter update failed", r);
    return r;
}

static struct picomesh_int64_result ra_get_int(struct ra_storage *h, struct yheaders *hdrs, const char *key, int64_t fallback)
{
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, RA_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "runner_agent: storage read failed", r);
    int64_t v = (r.value && r.value[0]) ? strtoll(r.value, NULL, 10) : fallback;
    free(r.value);
    return PICOMESH_OK(picomesh_int64, v);
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
        ssize_t n = getrandom(raw + got, sizeof(raw) - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        got += (size_t)n;
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
    struct yjson_writer *w = yjson_writer_new();
    if (!w) return NULL;
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "name");      yjson_writer_string(w, rec->name);
    yjson_writer_key(w, "labels");    yjson_writer_string(w, rec->labels);
    yjson_writer_key(w, "version");   yjson_writer_string(w, rec->version);
    yjson_writer_key(w, "host");      yjson_writer_string(w, rec->host);
    yjson_writer_key(w, "status");    yjson_writer_string(w, rec->status);
    yjson_writer_key(w, "last_seen"); yjson_writer_int(w, rec->last_seen);
    yjson_writer_key(w, "tok_hash");  yjson_writer_string(w, rec->tok_hash);
    yjson_writer_end_object(w);
    size_t len = 0;
    const char *data = yjson_writer_data(w, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(w);
    return out;
}

static void ra_copy_field(char *dst, size_t cap, const struct yjson_value *obj, const char *key)
{
    const char *value = yjson_as_string(yjson_object_get(obj, key), "");
    snprintf(dst, cap, "%s", value ? value : "");
}

/* Read runner:<id> into `rec`. OK value: 1 = present, 0 = absent. */
static struct picomesh_int_result ra_record_load(struct ra_storage *h, struct yheaders *hdrs,
                                                 uint32_t runner_id, struct ra_record *rec)
{
    char key[48];
    snprintf(key, sizeof(key), "runner:%u", runner_id);
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, RA_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int, "runner_agent: record read failed", r);
    if (!r.value || !r.value[0]) { free(r.value); return PICOMESH_OK(picomesh_int, 0); }
    struct yjson_doc *doc = yjson_parse(r.value, strlen(r.value));
    free(r.value);
    if (!doc) return PICOMESH_ERR(picomesh_int, "runner_agent: record parse failed");
    const struct yjson_value *obj = yjson_doc_root(doc);
    memset(rec, 0, sizeof(*rec));
    ra_copy_field(rec->name, sizeof(rec->name), obj, "name");
    ra_copy_field(rec->labels, sizeof(rec->labels), obj, "labels");
    ra_copy_field(rec->version, sizeof(rec->version), obj, "version");
    ra_copy_field(rec->host, sizeof(rec->host), obj, "host");
    ra_copy_field(rec->status, sizeof(rec->status), obj, "status");
    ra_copy_field(rec->tok_hash, sizeof(rec->tok_hash), obj, "tok_hash");
    rec->last_seen = yjson_as_int(yjson_object_get(obj, "last_seen"), 0);
    yjson_doc_free(doc);
    return PICOMESH_OK(picomesh_int, 1);
}

/* Persist a runner record at runner:<id>. */
static struct picomesh_void_result ra_record_store(struct ra_storage *h, struct yheaders *hdrs,
                                                   uint32_t runner_id, const struct ra_record *rec)
{
    char *json = ra_record_to_json(rec);
    if (!json) return PICOMESH_ERR(picomesh_void, "runner_agent: record encode failed");
    char key[48];
    snprintf(key, sizeof(key), "runner:%u", runner_id);
    struct picomesh_int_result r = sharded_storage_db_set(&h->c, h->obj, hdrs, RA_CTX, key, json);
    free(json);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_void, "runner_agent: record write failed", r);
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
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: storage open failed", sr);
    struct ra_storage h = sr.value;

    char token[40];
    if (!ra_alloc_token(token, sizeof(token)))
        return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: secure random unavailable");

    struct picomesh_int64_result idr = ra_incr(&h, hdrs, "next_id", 1);
    if (PICOMESH_IS_ERR(idr)) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: allocate id failed", idr);
    uint32_t runner_id = (uint32_t)idr.value;

    struct ra_record rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.name, sizeof(rec.name), "%s", name ? name : "");
    snprintf(rec.labels, sizeof(rec.labels), "%s", labels ? labels : "");
    snprintf(rec.status, sizeof(rec.status), "offline");
    rec.last_seen = picomesh_security_now();
    ra_sha256_hex(token, rec.tok_hash);

    struct picomesh_void_result ws = ra_record_store(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(ws)) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: persist record failed", ws);

    char tok_key[80];
    snprintf(tok_key, sizeof(tok_key), "tok:%s", rec.tok_hash);
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", runner_id);
    struct picomesh_int_result ts = sharded_storage_db_set(&h.c, h.obj, hdrs, RA_CTX, tok_key, id_str);
    if (PICOMESH_IS_ERR(ts)) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: persist token failed", ts);

    struct picomesh_int64_result cc = ra_incr(&h, hdrs, "count", 1);
    if (PICOMESH_IS_ERR(cc)) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: bump count failed", cc);

    struct yjson_writer *w = yjson_writer_new();
    if (!w) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: writer alloc failed");
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "runner_id"); yjson_writer_int(w, (int64_t)runner_id);
    yjson_writer_key(w, "token");     yjson_writer_string(w, token);
    yjson_writer_end_object(w);
    size_t len = 0;
    const char *data = yjson_writer_data(w, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(w);
    if (!out) return PICOMESH_ERR(picomesh_json, "runner_agent_create_token: encode failed");
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
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_lookup_token: storage open failed", sr);
    struct ra_storage h = sr.value;

    char hash[65];
    ra_sha256_hex(token, hash);
    char tok_key[80];
    snprintf(tok_key, sizeof(tok_key), "tok:%s", hash);
    struct picomesh_string_result r = sharded_storage_db_get(&h.c, h.obj, hdrs, RA_CTX, tok_key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_lookup_token: read failed", r);
    if (!r.value || !r.value[0]) { free(r.value); return PICOMESH_OK(picomesh_uint32, 0); }
    uint32_t runner_id = (uint32_t)strtoul(r.value, NULL, 10);
    free(r.value);
    if (runner_id == 0) return PICOMESH_OK(picomesh_uint32, 0);

    /* A disabled (revoked) runner must not authenticate even if a stale token
     * mapping lingers. */
    struct ra_record rec;
    struct picomesh_int_result lr = ra_record_load(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_lookup_token: record load failed", lr);
    if (lr.value == 0) return PICOMESH_OK(picomesh_uint32, 0);
    if (strcmp(rec.status, "disabled") == 0) return PICOMESH_OK(picomesh_uint32, 0);
    return PICOMESH_OK(picomesh_uint32, runner_id);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_revoke_token")
struct picomesh_int_result runner_agent_runner_agent_revoke_token_impl(struct ctx *ctx, struct object *obj,
                                                                       struct yheaders *hdrs,
                                                                       uint32_t runner_id)
{
    (void)ctx; (void)obj;
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: storage open failed", sr);
    struct ra_storage h = sr.value;

    struct ra_record rec;
    struct picomesh_int_result lr = ra_record_load(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: record load failed", lr);
    if (lr.value == 0) return PICOMESH_OK(picomesh_int, 0);
    if (strcmp(rec.status, "disabled") == 0) return PICOMESH_OK(picomesh_int, 0);

    if (rec.tok_hash[0]) {
        char tok_key[80];
        snprintf(tok_key, sizeof(tok_key), "tok:%s", rec.tok_hash);
        struct picomesh_int_result del = sharded_storage_db_del(&h.c, h.obj, hdrs, RA_CTX, tok_key);
        if (PICOMESH_IS_ERR(del)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: token delete failed", del);
    }
    snprintf(rec.status, sizeof(rec.status), "disabled");
    struct picomesh_void_result ws = ra_record_store(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(ws)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: record write failed", ws);
    struct picomesh_int64_result cc = ra_incr(&h, hdrs, "count", -1);
    if (PICOMESH_IS_ERR(cc)) return PICOMESH_ERR(picomesh_int, "runner_agent_revoke_token: count update failed", cc);
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
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: storage open failed", sr);
    struct ra_storage h = sr.value;

    struct ra_record rec;
    struct picomesh_int_result lr = ra_record_load(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: record load failed", lr);
    if (lr.value == 0) return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: unknown runner");
    if (strcmp(rec.status, "disabled") == 0)
        return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: runner is disabled");

    if (name && *name) snprintf(rec.name, sizeof(rec.name), "%s", name);
    if (labels) snprintf(rec.labels, sizeof(rec.labels), "%s", labels);
    snprintf(rec.version, sizeof(rec.version), "%s", version ? version : "");
    snprintf(rec.host, sizeof(rec.host), "%s", host ? host : "");
    snprintf(rec.status, sizeof(rec.status), "online");
    rec.last_seen = picomesh_security_now();

    struct picomesh_void_result ws = ra_record_store(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(ws)) return PICOMESH_ERR(picomesh_uint32, "runner_agent_register: record write failed", ws);
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
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: storage open failed", sr);
    struct ra_storage h = sr.value;

    struct ra_record rec;
    struct picomesh_int_result lr = ra_record_load(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: record load failed", lr);
    if (lr.value == 0) return PICOMESH_OK(picomesh_int, 0);
    if (strcmp(rec.status, "disabled") == 0)
        return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: runner is disabled");

    snprintf(rec.status, sizeof(rec.status), "%s", (status && *status) ? status : "online");
    rec.last_seen = picomesh_security_now();
    struct picomesh_void_result ws = ra_record_store(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(ws)) return PICOMESH_ERR(picomesh_int, "runner_agent_heartbeat: record write failed", ws);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_get")
struct picomesh_json_result runner_agent_runner_agent_get_impl(struct ctx *ctx, struct object *obj,
                                                               struct yheaders *hdrs, uint32_t runner_id)
{
    (void)ctx; (void)obj;
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "runner_agent_get: storage open failed", sr);
    struct ra_storage h = sr.value;
    struct ra_record rec;
    struct picomesh_int_result lr = ra_record_load(&h, hdrs, runner_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_json, "runner_agent_get: record load failed", lr);
    if (lr.value == 0) { char *empty = strdup("{}"); return empty ? PICOMESH_OK(picomesh_json, empty)
                                                                  : PICOMESH_ERR(picomesh_json, "runner_agent_get: oom"); }
    /* Echo the record but never the token hash — that is at-rest secret material. */
    struct yjson_writer *w = yjson_writer_new();
    if (!w) return PICOMESH_ERR(picomesh_json, "runner_agent_get: writer alloc failed");
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "runner_id"); yjson_writer_int(w, (int64_t)runner_id);
    yjson_writer_key(w, "name");      yjson_writer_string(w, rec.name);
    yjson_writer_key(w, "labels");    yjson_writer_string(w, rec.labels);
    yjson_writer_key(w, "version");   yjson_writer_string(w, rec.version);
    yjson_writer_key(w, "host");      yjson_writer_string(w, rec.host);
    yjson_writer_key(w, "status");    yjson_writer_string(w, rec.status);
    yjson_writer_key(w, "last_seen"); yjson_writer_int(w, rec.last_seen);
    yjson_writer_end_object(w);
    size_t len = 0;
    const char *data = yjson_writer_data(w, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(w);
    if (!out) return PICOMESH_ERR(picomesh_json, "runner_agent_get: encode failed");
    return PICOMESH_OK(picomesh_json, out);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_list")
struct picomesh_json_result runner_agent_runner_agent_list_impl(struct ctx *ctx, struct object *obj,
                                                                struct yheaders *hdrs,
                                                                int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "runner_agent_list: storage open failed", sr);
    struct ra_storage h = sr.value;
    return sharded_storage_db_list(&h.c, h.obj, hdrs, RA_CTX, "runner:", offset, limit);
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_list_all")
struct picomesh_json_result runner_agent_runner_agent_list_all_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "runner_agent_list_all: storage open failed", sr);
    struct ra_storage h = sr.value;
    return sharded_storage_db_list_all(&h.c, h.obj, hdrs, RA_CTX, "runner:");
}

PICOMESH_CLASS_ANNOTATE("override@runner_agent:runner_agent:runner_agent_count_active")
struct picomesh_size_result runner_agent_runner_agent_count_active_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct ra_storage_result sr = ra_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "runner_agent_count_active: storage open failed", sr);
    struct ra_storage h = sr.value;
    struct picomesh_int64_result cr = ra_get_int(&h, hdrs, "count", 0);
    if (PICOMESH_IS_ERR(cr)) return PICOMESH_ERR(picomesh_size, "runner_agent_count_active: read failed", cr);
    return PICOMESH_OK(picomesh_size, (size_t)(cr.value < 0 ? 0 : cr.value));
}

#include "store.gen.c"
