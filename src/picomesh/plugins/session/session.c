/* session — opaque session-id ↔ stored access JWT (issue #19).
 *
 * Scenario shape:
 *   start(uid, access_jwt, refresh_token) → opaque session id string (sid)
 *   jwt(sid)                              → the stored access JWT ("" if absent)
 *   lookup(sid)                           → uint32 uid (0 if absent) — back-compat
 *   destroy(sid)                          → 1 if removed, 0 if unknown
 *   count_active                          → number of sessions live now
 *
 * The sid is a 128-bit random value (hex): an OPAQUE bearer secret, never
 * derived from anything client-supplied, never logged. The browser only ever
 * holds the sid; the access JWT it maps to never leaves the mesh in the normal
 * cookie flow — the gateway's session_cookie authenticator exchanges the sid
 * for the JWT internally via `jwt(sid)`.
 *
 * All state lives in the shared storage backend. Key layout in the `session`
 * context:
 *
 *   count           → number of live sessions.
 *   uid:<sid>       → uid bound to that session (for listing / back-compat).
 *   jwt:<sid>       → access JWT bound to that session.
 *   refresh:<sid>   → refresh token bound to that session.
 *
 * The plugin process itself carries no in-memory bookkeeping, so a
 * crash + restart still serves the same sessions, and every remote
 * object on this service points at the same data automatically.
 */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>
#include <picomesh/yclass/rpc.h>
#include <string.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>

struct PICOMESH_CLASS_ANNOTATE("class@session:session") session_session_data {
    /* Empty — the plugin holds no per-object state. Codegen still wants
     * a struct annotated with the class accessor; a dummy byte keeps it
     * a complete type so object_alloc has something to size. */
    char _unused;
};

/* Helper: open a storage_sql proxy on the configured `storage` remote.
 * Returns a Result so callers can propagate the failure path cleanly. */
struct storage_handle {
    struct ctx c;
    struct object *obj;
};
PICOMESH_RESULT_DECLARE(session_storage_handle, struct storage_handle);

static struct session_storage_handle_result open_storage(void)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(session_storage_handle, "session: no active engine");
    struct storage_handle h = {.c = picomesh_engine_service_ctx(e, "sharded_storage")};
    /* peer==NULL ⇒ storage is collocated in THIS process; create resolves it
     * as a local in-process object. A non-NULL peer is the remote-mesh path.
     * Both go through sharded_storage_db_create — don't bail on a missing peer. */
    struct object_ptr_result o = sharded_storage_db_create(&h.c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(session_storage_handle, "session: storage_db_create failed", o);
    h.obj = o.value;
    return PICOMESH_OK(session_storage_handle, h);
}

#define SESSION_CTX "session"

static void close_storage(struct storage_handle *h)
{
    /* The storage object is a cached, service-lifetime dependency
     * (rpc_object_acquire owns it); nothing to release per call. */
    (void)h;
}

/* The store holds string values; session state is integer counters and
 * ids, so serialize them as decimal strings on the way in and parse them
 * back on the way out. A failed write is propagated, never swallowed — a
 * session row that was not persisted must not look like success. */
static struct picomesh_void_result kv_set_int(struct storage_handle *h, struct yheaders *hdrs,
                                              const char *key, int64_t value)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%lld", (long long)value);
    struct picomesh_int_result r = sharded_storage_db_set(&h->c, h->obj, hdrs, SESSION_CTX, key, vbuf);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_void, "session: storage write failed", r);
    return PICOMESH_OK_VOID();
}

/* Read an int key. A real backend error is propagated; an absent/empty key
 * (db_get returns "") yields `fallback` — the two are NOT conflated. */
static struct picomesh_int64_result kv_get_int(struct storage_handle *h, struct yheaders *hdrs,
                                               const char *key, int64_t fallback)
{
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, SESSION_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "session: storage read failed", r);
    int64_t v = (r.value && r.value[0]) ? strtoll(r.value, NULL, 10) : fallback;
    free(r.value);
    return PICOMESH_OK(picomesh_int64, v);
}

/* Atomic counter bump — storage serializes the read-add-write so the live
 * session count never loses an update. Propagates a backend failure (the
 * OK value is the count after the add). */
static struct picomesh_int64_result kv_incr(struct storage_handle *h, struct yheaders *hdrs,
                                            const char *key, int64_t delta)
{
    struct picomesh_int64_result r = sharded_storage_db_incr(&h->c, h->obj, hdrs, SESSION_CTX, key, delta);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "session: counter update failed", r);
    return r;
}

/* Allocate a fresh session token: 128 bits of randomness, lowercase-hex
 * encoded (32 chars + NUL). Opaque and unguessable, and a 2^128 space makes
 * collisions impossible in practice.
 *
 * This replaces the old sequential `next_sid` counter, whose non-atomic
 * `get → +1 → set` over two storage RPCs (each a coroutine yield) raced under
 * concurrent logins: two users were handed the SAME id, the later `uid:<id>`
 * write clobbered the earlier, and the token then resolved to the WRONG user —
 * which made owner-checked `git_repo.git_repo.put_file` fail under load. A random
 * token shares no counter, so there is nothing to race on.
 *
 * FAIL CLOSED: a session token is a bearer secret. If the kernel cannot
 * give us secure random bytes we refuse to mint one — never fall back to a
 * predictable clock/address-seeded PRNG. Returns 1 on success, 0 if secure
 * randomness was unavailable. */
static int alloc_token(char *out, size_t cap)
{
    uint8_t raw[16];
    size_t got = 0;
    while (got < sizeof(raw)) {
        ssize_t n = getrandom(raw + got, sizeof(raw) - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;  /* interrupted by signal → retry */
            return 0;                       /* no secure entropy → fail closed */
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

PICOMESH_CLASS_ANNOTATE("override@session:session:session_start")
struct picomesh_string_result session_session_start_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                    uint32_t user_id, const char *access_jwt,
                                                    const char *refresh_token)
{
    (void)ctx; (void)obj;
    if (!access_jwt) access_jwt = "";
    if (!refresh_token) refresh_token = "";
    struct session_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "session_start: open_storage failed", sr);
    struct storage_handle h = sr.value;

    char tok[40];
    if (!alloc_token(tok, sizeof(tok)))
        return PICOMESH_ERR(picomesh_string, "session_start: secure random unavailable");

    /* Every row here is mandatory: if any write fails, the session is only
     * partially persisted, so we must NOT hand back a valid-looking sid.
     * Propagate the first failure instead. */
    char k[64];
    snprintf(k, sizeof(k), "uid:%s", tok);
    struct picomesh_void_result w1 = kv_set_int(&h, hdrs, k, (int64_t)user_id);
    if (PICOMESH_IS_ERR(w1)) { close_storage(&h); return PICOMESH_ERR(picomesh_string, "session_start: persist uid failed", w1); }
    snprintf(k, sizeof(k), "jwt:%s", tok);
    struct picomesh_int_result wj = sharded_storage_db_set(&h.c, h.obj, hdrs, SESSION_CTX, k, access_jwt);
    if (PICOMESH_IS_ERR(wj)) { close_storage(&h); return PICOMESH_ERR(picomesh_string, "session_start: persist jwt failed", wj); }
    snprintf(k, sizeof(k), "refresh:%s", tok);
    struct picomesh_int_result wr = sharded_storage_db_set(&h.c, h.obj, hdrs, SESSION_CTX, k, refresh_token);
    if (PICOMESH_IS_ERR(wr)) { close_storage(&h); return PICOMESH_ERR(picomesh_string, "session_start: persist refresh failed", wr); }
    struct picomesh_int64_result wc = kv_incr(&h, hdrs, "count", 1);
    if (PICOMESH_IS_ERR(wc)) { close_storage(&h); return PICOMESH_ERR(picomesh_string, "session_start: bump count failed", wc); }

    close_storage(&h);
    /* Never log the sid or the JWT — both are bearer secrets. */
    yinfo("session: started user=%u", user_id);
    char *out = strdup(tok);
    if (!out) return PICOMESH_ERR(picomesh_string, "session_start: out of memory");
    return PICOMESH_OK(picomesh_string, out);
}

/* Exchange an opaque sid for the stored access JWT. This is an
 * authenticator-internal credential exchange (the gateway's session_cookie
 * authenticator calls it); it must not be reachable as a public /_rpc call. */
PICOMESH_CLASS_ANNOTATE("override@session:session:session_jwt")
struct picomesh_string_result session_session_jwt_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                       const char *sid)
{
    (void)ctx; (void)obj;
    if (!sid || !*sid) {
        char *empty = strdup("");
        return empty ? PICOMESH_OK(picomesh_string, empty) : PICOMESH_ERR(picomesh_string, "session_jwt: out of memory");
    }
    struct session_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "session_jwt: open_storage failed", sr);
    struct storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "jwt:%s", sid);
    struct picomesh_string_result r = sharded_storage_db_get(&h.c, h.obj, hdrs, SESSION_CTX, k);
    close_storage(&h);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_string, "session_jwt: read failed", r);
    if (!r.value) {
        char *empty = strdup("");
        return empty ? PICOMESH_OK(picomesh_string, empty) : PICOMESH_ERR(picomesh_string, "session_jwt: out of memory");
    }
    return PICOMESH_OK(picomesh_string, r.value);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_lookup")
struct picomesh_uint32_result session_session_lookup_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                     const char *token)
{
    (void)ctx; (void)obj;
    if (!token || !*token) return PICOMESH_OK(picomesh_uint32, 0);
    struct session_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_uint32, "session_lookup: open_storage failed", sr);
    struct storage_handle h = sr.value;

    char k[64];
    snprintf(k, sizeof(k), "uid:%s", token);
    struct picomesh_int64_result uidr = kv_get_int(&h, hdrs, k, 0);
    close_storage(&h);
    if (PICOMESH_IS_ERR(uidr)) return PICOMESH_ERR(picomesh_uint32, "session_lookup: read failed", uidr);
    return PICOMESH_OK(picomesh_uint32, (uint32_t)uidr.value);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_destroy")
struct picomesh_int_result session_session_destroy_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   const char *token)
{
    (void)ctx; (void)obj;
    if (!token || !*token) return PICOMESH_OK(picomesh_int, 0);
    struct session_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "session_destroy: open_storage failed", sr);
    struct storage_handle h = sr.value;

    /* db_del is the atomic point: MDBX serializes the delete on the shard,
     * so exactly ONE of two concurrent destroys of the same token sees
     * removed==1. Gate the count decrement on that → idempotent, no
     * double-decrement (the old exists-then-decrement raced). */
    char k[64];
    snprintf(k, sizeof(k), "uid:%s", token);
    struct picomesh_int_result del = sharded_storage_db_del(&h.c, h.obj, hdrs, SESSION_CTX, k);
    if (PICOMESH_IS_ERR(del)) { close_storage(&h); return PICOMESH_ERR(picomesh_int, "session_destroy: delete failed", del); }
    if (del.value == 0) { close_storage(&h); return PICOMESH_OK(picomesh_int, 0); }  /* unknown / already gone */

    snprintf(k, sizeof(k), "jwt:%s", token);
    struct picomesh_int_result jdel = sharded_storage_db_del(&h.c, h.obj, hdrs, SESSION_CTX, k);
    if (PICOMESH_IS_ERR(jdel)) { close_storage(&h); return PICOMESH_ERR(picomesh_int, "session_destroy: delete jwt failed", jdel); }
    snprintf(k, sizeof(k), "refresh:%s", token);
    struct picomesh_int_result rdel = sharded_storage_db_del(&h.c, h.obj, hdrs, SESSION_CTX, k);
    if (PICOMESH_IS_ERR(rdel)) { close_storage(&h); return PICOMESH_ERR(picomesh_int, "session_destroy: delete refresh failed", rdel); }
    struct picomesh_int64_result dc = kv_incr(&h, hdrs, "count", -1);
    if (PICOMESH_IS_ERR(dc)) { close_storage(&h); return PICOMESH_ERR(picomesh_int, "session_destroy: count update failed", dc); }

    close_storage(&h);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_count_active")
struct picomesh_size_result session_session_count_active_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "session_count: open_storage failed", sr);
    struct storage_handle h = sr.value;
    struct picomesh_int64_result cr = kv_get_int(&h, hdrs, "count", 0);
    close_storage(&h);
    if (PICOMESH_IS_ERR(cr)) return PICOMESH_ERR(picomesh_size, "session_count: read failed", cr);
    return PICOMESH_OK(picomesh_size, (size_t)(cr.value < 0 ? 0 : cr.value));
}

/* List ALL session entries as a JSON array (gh#15) — every object, not a
 * count of active. Delegates to the namespace scan. */
PICOMESH_CLASS_ANNOTATE("override@session:session:session_list")
struct picomesh_json_result session_session_list_impl(struct ctx *ctx, struct object *obj,
                                                    struct yheaders *hdrs,
                                                    int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "session_list: storage open failed", sr);
    struct storage_handle h = sr.value;
    return sharded_storage_db_list(&h.c, h.obj, hdrs, SESSION_CTX, "uid:", offset, limit);
}

/* Unbounded variant — every session. Use with care on large deployments. */
PICOMESH_CLASS_ANNOTATE("override@session:session:session_list_all")
struct picomesh_json_result session_session_list_all_impl(struct ctx *ctx, struct object *obj,
                                                          struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "session_list_all: storage open failed", sr);
    struct storage_handle h = sr.value;
    return sharded_storage_db_list_all(&h.c, h.obj, hdrs, SESSION_CTX, "uid:");
}

#include "session.gen.c"
