/* password_authn — password verification backed by the storage service.
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
 * Storage layout in the `password_authn` context:
 *   hash:<uid>  → int64 hash
 *   count       → number of registered users
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

struct YAAFC_CLASS_ANNOTATE("class@password_authn:store") password_authn_store_data {
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
    struct pw_storage_handle h = {.c = yaafc_engine_service_ctx(e, "sharded_storage")};
    if (!h.c.peer)
        return YAAFC_ERR(pw_storage_handle, "password_authn: no 'storage' remote");
    struct object_ptr_result o = sharded_storage_db_create(&h.c);
    if (YAAFC_IS_ERR(o)) return YAAFC_ERR(pw_storage_handle, "password_authn: storage_db_create failed", o);
    h.obj = o.value;
    return YAAFC_OK(pw_storage_handle, h);
}

#define PW_CTX "password_authn"

static void close_storage(struct pw_storage_handle *h)
{
    /* The storage object is a cached, service-lifetime dependency
     * (rpc_object_acquire owns it); nothing to release per call. */
    (void)h;
}

/* The store holds string values; the password hash and the registered
 * count are integers, so serialize as decimal strings and parse back. */
static void kv_set_int(struct pw_storage_handle *h, struct yheaders *hdrs,
                       const char *key, int64_t value)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%lld", (long long)value);
    sharded_storage_db_set(&h->c, h->obj, hdrs, PW_CTX, key, vbuf);
}

static int64_t kv_get_or(struct pw_storage_handle *h, struct yheaders *hdrs, const char *key, int64_t fallback)
{
    struct yaafc_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, PW_CTX, key);
    if (YAAFC_IS_ERR(r)) { yaafc_error_destroy(r.error); return fallback; }
    int64_t v = r.value ? strtoll(r.value, NULL, 10) : fallback;
    free(r.value);
    return v;
}

static int kv_exists(struct pw_storage_handle *h, struct yheaders *hdrs, const char *key)
{
    struct yaafc_int_result r = sharded_storage_db_exists(&h->c, h->obj, hdrs, PW_CTX, key);
    int present = YAAFC_IS_OK(r) && r.value;
    if (YAAFC_IS_ERR(r)) yaafc_error_destroy(r.error);
    return present;
}

YAAFC_CLASS_ANNOTATE("override@password_authn:store:store_register")
struct yaafc_int_result password_authn_store_register_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                           uint32_t user_id, int64_t hash)
{
    (void)ctx; (void)obj;
    struct pw_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "password_authn_register: open_storage failed", sr);
    struct pw_storage_handle h = sr.value;

    char k[64];
    snprintf(k, sizeof(k), "hash:%u", user_id);
    if (kv_exists(&h, hdrs, k)) { close_storage(&h); return YAAFC_OK(yaafc_int, 0); }
    kv_set_int(&h, hdrs, k, hash);
    int64_t count = kv_get_or(&h, hdrs, "count", 0) + 1;
    kv_set_int(&h, hdrs, "count", count);
    close_storage(&h);
    yinfo("password_authn: registered uid=%u", user_id);
    return YAAFC_OK(yaafc_int, 1);
}

YAAFC_CLASS_ANNOTATE("override@password_authn:store:store_authenticate")
struct yaafc_int_result password_authn_store_authenticate_impl(struct ctx *ctx,
                                                               struct object *obj,
                                                               struct yheaders *hdrs,
                                                               uint32_t user_id, int64_t hash)
{
    (void)ctx; (void)obj;
    struct pw_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "password_authn_authenticate: open_storage failed", sr);
    struct pw_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "hash:%u", user_id);
    if (!kv_exists(&h, hdrs, k)) { close_storage(&h); return YAAFC_OK(yaafc_int, 0); }
    int64_t stored = kv_get_or(&h, hdrs, k, 0);
    close_storage(&h);
    return YAAFC_OK(yaafc_int, stored == hash ? 1 : 0);
}

YAAFC_CLASS_ANNOTATE("override@password_authn:store:store_change_password")
struct yaafc_int_result password_authn_store_change_password_impl(struct ctx *ctx,
                                                                  struct object *obj,
                                                                  struct yheaders *hdrs,
                                                                  uint32_t user_id, int64_t hash)
{
    (void)ctx; (void)obj;
    struct pw_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "password_authn_change_password: open_storage failed", sr);
    struct pw_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "hash:%u", user_id);
    if (!kv_exists(&h, hdrs, k)) { close_storage(&h); return YAAFC_OK(yaafc_int, 0); }
    kv_set_int(&h, hdrs, k, hash);
    close_storage(&h);
    return YAAFC_OK(yaafc_int, 1);
}

YAAFC_CLASS_ANNOTATE("override@password_authn:store:store_count_registered")
struct yaafc_size_result password_authn_store_count_registered_impl(struct ctx *ctx,
                                                                    struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct pw_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_size, "password_authn_count: open_storage failed", sr);
    struct pw_storage_handle h = sr.value;
    int64_t c = kv_get_or(&h, hdrs, "count", 0);
    close_storage(&h);
    return YAAFC_OK(yaafc_size, (size_t)(c < 0 ? 0 : c));
}

#include "store.gen.c"
