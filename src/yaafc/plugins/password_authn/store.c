/* password_authn — password verification backed by storage_sql.
 *
 *   register(user_id, pw_hash)      → 1 created, 0 already-exists
 *   authenticate(user_id, pw_hash)  → 1 ok, 0 mismatch / unknown
 *   change_password(user_id, hash)  → 1 ok, 0 unknown
 *   count_registered                → size
 *
 * Passwords are compared by hash bit-for-bit. The wire format carries
 * int64; the frontend converts the user's password string to an int64
 * via a deterministic hash before calling.
 *
 * Storage layout:
 *   password_authn:hash:<uid> → int64 hash
 *   password_authn:count      → number of registered users
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

struct [[clang::annotate("class@password_authn:store")]] password_authn_store_data {
    char _unused;
};

struct pw_storage_handle {
    struct ctx c;
    struct object *obj;
};
YAAFC_RESULT_DECLARE(pw_storage_handle, struct pw_storage_handle);

static struct pw_storage_handle_result open_storage(void)
{
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return YAAFC_ERR(pw_storage_handle, "password_authn: no active engine");
    struct rpc_session *st = yaafc_engine_remote(e, "storage");
    if (!st) return YAAFC_ERR(pw_storage_handle, "password_authn: no 'storage' remote");
    struct pw_storage_handle h = {.c = {.session = st}};
    struct object_ptr_result o = storage_sql_create(&h.c);
    if (YAAFC_IS_ERR(o)) return YAAFC_ERR(pw_storage_handle, "password_authn: storage_sql_create failed", o);
    h.obj = o.value;
    return YAAFC_OK(pw_storage_handle, h);
}

static void close_storage(struct pw_storage_handle *h)
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

static int64_t kv_get_or(struct pw_storage_handle *h, const char *key, int64_t fallback)
{
    struct yaafc_int64_result r = storage_sql_get(&h->c, h->obj, key);
    if (YAAFC_IS_ERR(r)) { yaafc_error_destroy(r.error); return fallback; }
    return r.value;
}

static int kv_exists(struct pw_storage_handle *h, const char *key)
{
    struct yaafc_int_result r = storage_sql_exists(&h->c, h->obj, key);
    int present = YAAFC_IS_OK(r) && r.value;
    if (YAAFC_IS_ERR(r)) yaafc_error_destroy(r.error);
    return present;
}

[[clang::annotate("override@password_authn:store:store_register")]]
struct yaafc_int_result password_authn_store_register_impl(struct ctx *ctx, struct object *obj,
                                                           uint32_t user_id, int64_t hash)
{
    (void)ctx; (void)obj;
    struct pw_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "password_authn_register: open_storage failed", sr);
    struct pw_storage_handle h = sr.value;

    char k[64];
    snprintf(k, sizeof(k), "password_authn:hash:%u", user_id);
    if (kv_exists(&h, k)) { close_storage(&h); return YAAFC_OK(yaafc_int, 0); }
    storage_sql_set(&h.c, h.obj, k, hash);
    int64_t count = kv_get_or(&h, "password_authn:count", 0) + 1;
    storage_sql_set(&h.c, h.obj, "password_authn:count", count);
    close_storage(&h);
    yinfo("password_authn: registered uid=%u", user_id);
    return YAAFC_OK(yaafc_int, 1);
}

[[clang::annotate("override@password_authn:store:store_authenticate")]]
struct yaafc_int_result password_authn_store_authenticate_impl(struct ctx *ctx,
                                                               struct object *obj,
                                                               uint32_t user_id, int64_t hash)
{
    (void)ctx; (void)obj;
    struct pw_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "password_authn_authenticate: open_storage failed", sr);
    struct pw_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "password_authn:hash:%u", user_id);
    if (!kv_exists(&h, k)) { close_storage(&h); return YAAFC_OK(yaafc_int, 0); }
    int64_t stored = kv_get_or(&h, k, 0);
    close_storage(&h);
    return YAAFC_OK(yaafc_int, stored == hash ? 1 : 0);
}

[[clang::annotate("override@password_authn:store:store_change_password")]]
struct yaafc_int_result password_authn_store_change_password_impl(struct ctx *ctx,
                                                                  struct object *obj,
                                                                  uint32_t user_id, int64_t hash)
{
    (void)ctx; (void)obj;
    struct pw_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "password_authn_change_password: open_storage failed", sr);
    struct pw_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "password_authn:hash:%u", user_id);
    if (!kv_exists(&h, k)) { close_storage(&h); return YAAFC_OK(yaafc_int, 0); }
    storage_sql_set(&h.c, h.obj, k, hash);
    close_storage(&h);
    return YAAFC_OK(yaafc_int, 1);
}

[[clang::annotate("override@password_authn:store:store_count_registered")]]
struct yaafc_size_result password_authn_store_count_registered_impl(struct ctx *ctx,
                                                                    struct object *obj)
{
    (void)ctx; (void)obj;
    struct pw_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_size, "password_authn_count: open_storage failed", sr);
    struct pw_storage_handle h = sr.value;
    int64_t c = kv_get_or(&h, "password_authn:count", 0);
    close_storage(&h);
    return YAAFC_OK(yaafc_size, (size_t)(c < 0 ? 0 : c));
}

#include "store.gen.c"
