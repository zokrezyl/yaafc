/* session — session-id ↔ user/token mapping.
 *
 * Scenario shape:
 *   start(user_id, provider_id)  → uint32 sid (mints, stores)
 *   lookup(sid)                  → uint32 user_id (0 if absent / expired)
 *   destroy(sid)                 → 1 if removed, 0 if unknown
 *   count_active                 → number of sessions live now
 *
 * All state lives in the storage backend reached via the engine's
 * `storage` remote. Key layout in the `session` context:
 *
 *   next_sid     → monotonic counter for sid allocation.
 *   count        → number of live sessions.
 *   uid:<sid>    → user_id bound to that sid.
 *   prov:<sid>   → provider_id bound to that sid.
 *
 * The plugin process itself carries no in-memory bookkeeping, so a
 * crash + restart still serves the same sessions, and every remote
 * object on this service points at the same data automatically.
 */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yengine/engine.h>
#include <yaafc/plugin/sharded_storage/sharded_storage.h>
#include <yaafc/yclass/rpc.h>
#include <string.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct YAAFC_CLASS_ANNOTATE("class@session:store") session_store_data {
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
YAAFC_RESULT_DECLARE(session_storage_handle, struct storage_handle);

static struct session_storage_handle_result open_storage(void)
{
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return YAAFC_ERR(session_storage_handle, "session: no active engine");
    struct storage_handle h = {.c = yaafc_engine_service_ctx(e, "sharded_storage")};
    if (!h.c.peer)
        return YAAFC_ERR(session_storage_handle, "session: no 'storage' remote");
    struct object_ptr_result o = sharded_storage_db_create(&h.c);
    if (YAAFC_IS_ERR(o)) return YAAFC_ERR(session_storage_handle, "session: storage_db_create failed", o);
    h.obj = o.value;
    return YAAFC_OK(session_storage_handle, h);
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
 * back on the way out. */
static void kv_set_int(struct storage_handle *h, struct yheaders *hdrs,
                       const char *key, int64_t value)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%lld", (long long)value);
    sharded_storage_db_set(&h->c, h->obj, hdrs, SESSION_CTX, key, vbuf);
}

/* Read an int key from storage, treating any error as "missing → 0". */
static int64_t kv_get_or_zero(struct storage_handle *h, struct yheaders *hdrs, const char *key)
{
    struct yaafc_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, SESSION_CTX, key);
    if (YAAFC_IS_ERR(r)) { yaafc_error_destroy(r.error); return 0; }
    int64_t v = r.value ? strtoll(r.value, NULL, 10) : 0;
    free(r.value);
    return v;
}

YAAFC_CLASS_ANNOTATE("override@session:store:store_start")
struct yaafc_uint32_result session_store_start_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                    uint32_t user_id, uint32_t provider_id)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_uint32, "session_start: open_storage failed", sr);
    struct storage_handle h = sr.value;

    uint32_t sid = (uint32_t)kv_get_or_zero(&h, hdrs, "next_sid") + 1;
    kv_set_int(&h, hdrs, "next_sid", (int64_t)sid);

    char k[64];
    snprintf(k, sizeof(k), "uid:%u", sid);
    kv_set_int(&h, hdrs, k, (int64_t)user_id);
    snprintf(k, sizeof(k), "prov:%u", sid);
    kv_set_int(&h, hdrs, k, (int64_t)provider_id);

    int64_t count = kv_get_or_zero(&h, hdrs, "count") + 1;
    kv_set_int(&h, hdrs, "count", count);

    close_storage(&h);
    yinfo("session: sid=%u user=%u provider=%u", sid, user_id, provider_id);
    return YAAFC_OK(yaafc_uint32, sid);
}

YAAFC_CLASS_ANNOTATE("override@session:store:store_lookup")
struct yaafc_uint32_result session_store_lookup_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                     uint32_t sid)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_uint32, "session_lookup: open_storage failed", sr);
    struct storage_handle h = sr.value;

    char k[64];
    snprintf(k, sizeof(k), "uid:%u", sid);
    int64_t uid = kv_get_or_zero(&h, hdrs, k);
    close_storage(&h);
    return YAAFC_OK(yaafc_uint32, (uint32_t)uid);
}

YAAFC_CLASS_ANNOTATE("override@session:store:store_destroy")
struct yaafc_int_result session_store_destroy_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   uint32_t sid)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "session_destroy: open_storage failed", sr);
    struct storage_handle h = sr.value;

    char k[64];
    snprintf(k, sizeof(k), "uid:%u", sid);
    struct yaafc_int_result ex = sharded_storage_db_exists(&h.c, h.obj, hdrs, SESSION_CTX, k);
    int present = YAAFC_IS_OK(ex) && ex.value;
    if (YAAFC_IS_ERR(ex)) yaafc_error_destroy(ex.error);
    if (!present) { close_storage(&h); return YAAFC_OK(yaafc_int, 0); }

    sharded_storage_db_del(&h.c, h.obj, hdrs, SESSION_CTX, k);
    snprintf(k, sizeof(k), "prov:%u", sid);
    sharded_storage_db_del(&h.c, h.obj, hdrs, SESSION_CTX, k);

    int64_t count = kv_get_or_zero(&h, hdrs, "count");
    if (count > 0) kv_set_int(&h, hdrs, "count", count - 1);

    close_storage(&h);
    return YAAFC_OK(yaafc_int, 1);
}

YAAFC_CLASS_ANNOTATE("override@session:store:store_count_active")
struct yaafc_size_result session_store_count_active_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_size, "session_count: open_storage failed", sr);
    struct storage_handle h = sr.value;
    int64_t count = kv_get_or_zero(&h, hdrs, "count");
    close_storage(&h);
    return YAAFC_OK(yaafc_size, (size_t)(count < 0 ? 0 : count));
}

#include "session.gen.c"
