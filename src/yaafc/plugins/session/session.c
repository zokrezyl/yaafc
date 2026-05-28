/* session — session-id ↔ user/token mapping.
 *
 * Scenario shape:
 *   start(user_id, provider_id)  → uint32 sid (mints, stores)
 *   lookup(sid)                  → uint32 user_id (0 if absent / expired)
 *   destroy(sid)                 → 1 if removed, 0 if unknown
 *   count_active                 → number of sessions live now
 *
 * All state lives in the storage_sql backend reached via the engine's
 * `storage` remote. Key layout in the shared kv table:
 *
 *   session:next_sid     → monotonic counter for sid allocation.
 *   session:count        → number of live sessions.
 *   session:uid:<sid>    → user_id bound to that sid.
 *   session:prov:<sid>   → provider_id bound to that sid.
 *
 * The plugin process itself carries no in-memory bookkeeping, so a
 * crash + restart still serves the same sessions, and every remote
 * object on this service points at the same data automatically.
 */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yengine/engine.h>
#include <yaafc/plugin/storage/storage.h>
#include <yaafc/yclass/rpc.h>
#include <string.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct [[clang::annotate("class@session:store")]] session_store_data {
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
    struct rpc_session *st = yaafc_engine_remote(e, "storage");
    if (!st) return YAAFC_ERR(session_storage_handle, "session: no 'storage' remote");
    struct storage_handle h = {.c = {.session = st}};
    struct object_ptr_result o = storage_sql_create(&h.c);
    if (YAAFC_IS_ERR(o)) return YAAFC_ERR(session_storage_handle, "session: storage_sql_create failed", o);
    h.obj = o.value;
    return YAAFC_OK(session_storage_handle, h);
}

static void close_storage(struct storage_handle *h)
{
    if (!h || !h->obj) return;
    if (h->c.session) {
        uint64_t _h;
        memcpy(&_h, (char *)h->obj + sizeof(struct object), sizeof(_h));
        uint8_t _r;
        rpc_call(h->c.session, RPC_OP_DESTROY, 0, &_h, sizeof(_h), &_r, 1);
    }
    free(h->obj);
    h->obj = NULL;
}

/* Read an int64 key from storage, treating any error as "missing → 0". */
static int64_t kv_get_or_zero(struct storage_handle *h, const char *key)
{
    struct yaafc_int64_result r = storage_sql_get(&h->c, h->obj, key);
    if (YAAFC_IS_ERR(r)) { yaafc_error_destroy(r.error); return 0; }
    return r.value;
}

[[clang::annotate("override@session:store:store_start")]]
struct yaafc_uint32_result session_store_start_impl(struct ctx *ctx, struct object *obj,
                                                    uint32_t user_id, uint32_t provider_id)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_uint32, "session_start: open_storage failed", sr);
    struct storage_handle h = sr.value;

    uint32_t sid = (uint32_t)kv_get_or_zero(&h, "session:next_sid") + 1;
    storage_sql_set(&h.c, h.obj, "session:next_sid", (int64_t)sid);

    char k[64];
    snprintf(k, sizeof(k), "session:uid:%u", sid);
    storage_sql_set(&h.c, h.obj, k, (int64_t)user_id);
    snprintf(k, sizeof(k), "session:prov:%u", sid);
    storage_sql_set(&h.c, h.obj, k, (int64_t)provider_id);

    int64_t count = kv_get_or_zero(&h, "session:count") + 1;
    storage_sql_set(&h.c, h.obj, "session:count", count);

    close_storage(&h);
    yinfo("session: sid=%u user=%u provider=%u", sid, user_id, provider_id);
    return YAAFC_OK(yaafc_uint32, sid);
}

[[clang::annotate("override@session:store:store_lookup")]]
struct yaafc_uint32_result session_store_lookup_impl(struct ctx *ctx, struct object *obj,
                                                     uint32_t sid)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_uint32, "session_lookup: open_storage failed", sr);
    struct storage_handle h = sr.value;

    char k[64];
    snprintf(k, sizeof(k), "session:uid:%u", sid);
    int64_t uid = kv_get_or_zero(&h, k);
    close_storage(&h);
    return YAAFC_OK(yaafc_uint32, (uint32_t)uid);
}

[[clang::annotate("override@session:store:store_destroy")]]
struct yaafc_int_result session_store_destroy_impl(struct ctx *ctx, struct object *obj,
                                                   uint32_t sid)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "session_destroy: open_storage failed", sr);
    struct storage_handle h = sr.value;

    char k[64];
    snprintf(k, sizeof(k), "session:uid:%u", sid);
    struct yaafc_int_result ex = storage_sql_exists(&h.c, h.obj, k);
    int present = YAAFC_IS_OK(ex) && ex.value;
    if (YAAFC_IS_ERR(ex)) yaafc_error_destroy(ex.error);
    if (!present) { close_storage(&h); return YAAFC_OK(yaafc_int, 0); }

    storage_sql_del(&h.c, h.obj, k);
    snprintf(k, sizeof(k), "session:prov:%u", sid);
    storage_sql_del(&h.c, h.obj, k);

    int64_t count = kv_get_or_zero(&h, "session:count");
    if (count > 0) storage_sql_set(&h.c, h.obj, "session:count", count - 1);

    close_storage(&h);
    return YAAFC_OK(yaafc_int, 1);
}

[[clang::annotate("override@session:store:store_count_active")]]
struct yaafc_size_result session_store_count_active_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx; (void)obj;
    struct session_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_size, "session_count: open_storage failed", sr);
    struct storage_handle h = sr.value;
    int64_t count = kv_get_or_zero(&h, "session:count");
    close_storage(&h);
    return YAAFC_OK(yaafc_size, (size_t)(count < 0 ? 0 : count));
}

#include "session.gen.c"
