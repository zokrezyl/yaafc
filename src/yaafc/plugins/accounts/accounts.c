/* accounts plugin — user registry backed by the storage service.
 *
 * Methods (uid is uint32 on the wire today; usernames are mapped to
 * uids client-side by the frontend, mirroring yaapp's accounts plugin
 * but without the string-key round-trip):
 *
 *   accounts_register(uid)        1 if newly created, 0 if already there
 *   accounts_exists(uid)          1 if present, 0 otherwise
 *   accounts_set_balance(uid, n)  set balance (errors if uid unknown)
 *   accounts_balance(uid)         current balance (errors if uid unknown)
 *   accounts_count()              live row count
 *
 * Storage layout in the `accounts` context:
 *
 *   user:<uid>      → 1 (registered marker)
 *   balance:<uid>   → int64 balance
 *   count           → number of registered users
 *
 * The plugin holds no in-memory state — every method delegates to the
 * storage service on the configured remote, using a single context
 * `accounts`. The storage service maps the context to either a sqlite
 * table or an mdbx DBI (transparent to this plugin).
 */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yengine/engine.h>
#include <yaafc/plugin/storage/storage.h>
#include <yaafc/yclass/rpc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct YAAFC_CLASS_ANNOTATE("class@accounts:store") accounts_store_data {
    char _unused;
};

struct acc_storage_handle {
    struct ctx c;
    struct object *obj;
};
YAAFC_RESULT_DECLARE(acc_storage_handle, struct acc_storage_handle);

static struct acc_storage_handle_result open_storage(void)
{
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return YAAFC_ERR(acc_storage_handle, "accounts: no active engine");
    struct acc_storage_handle h = {.c = yaafc_engine_service_ctx(e, "storage")};
    if (!h.c.session)
        return YAAFC_ERR(acc_storage_handle, "accounts: no 'storage' remote");
    struct object_ptr_result o = storage_db_create(&h.c);
    if (YAAFC_IS_ERR(o)) return YAAFC_ERR(acc_storage_handle, "accounts: storage_db_create failed", o);
    h.obj = o.value;
    return YAAFC_OK(acc_storage_handle, h);
}

#define ACCOUNTS_CTX "accounts"

static void close_storage(struct acc_storage_handle *h)
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

static int64_t kv_get_or(struct acc_storage_handle *h, const char *key, int64_t fallback)
{
    struct yaafc_int64_result r = storage_get(&h->c, h->obj, ACCOUNTS_CTX, key);
    if (YAAFC_IS_ERR(r)) { yaafc_error_destroy(r.error); return fallback; }
    return r.value;
}

static int kv_exists(struct acc_storage_handle *h, const char *key)
{
    struct yaafc_int_result r = storage_exists(&h->c, h->obj, ACCOUNTS_CTX, key);
    int present = YAAFC_IS_OK(r) && r.value;
    if (YAAFC_IS_ERR(r)) yaafc_error_destroy(r.error);
    return present;
}

YAAFC_CLASS_ANNOTATE("override@accounts:store:store_register")
struct yaafc_int_result accounts_store_register_impl(struct ctx *ctx, struct object *obj,
                                                    uint32_t uid)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "accounts_register: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;

    char k[64];
    snprintf(k, sizeof(k), "user:%u", uid);
    if (kv_exists(&h, k)) {
        close_storage(&h);
        ydebug("accounts_register: uid=%u already exists", uid);
        return YAAFC_OK(yaafc_int, 0);
    }
    storage_set(&h.c, h.obj, ACCOUNTS_CTX, k, 1);
    int64_t count = kv_get_or(&h, "count", 0) + 1;
    storage_set(&h.c, h.obj, ACCOUNTS_CTX, "count", count);
    close_storage(&h);
    yinfo("accounts_register: uid=%u (total=%lld)", uid, (long long)count);
    return YAAFC_OK(yaafc_int, 1);
}

YAAFC_CLASS_ANNOTATE("override@accounts:store:store_exists")
struct yaafc_int_result accounts_store_exists_impl(struct ctx *ctx, struct object *obj,
                                                  uint32_t uid)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "accounts_exists: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "user:%u", uid);
    int present = kv_exists(&h, k);
    close_storage(&h);
    return YAAFC_OK(yaafc_int, present);
}

YAAFC_CLASS_ANNOTATE("override@accounts:store:store_set_balance")
struct yaafc_int_result accounts_store_set_balance_impl(struct ctx *ctx, struct object *obj,
                                                        uint32_t uid, int64_t n)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int, "accounts_set_balance: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "user:%u", uid);
    if (!kv_exists(&h, k)) {
        close_storage(&h);
        return YAAFC_ERR(yaafc_int, "accounts_set_balance: unknown uid");
    }
    snprintf(k, sizeof(k), "balance:%u", uid);
    storage_set(&h.c, h.obj, ACCOUNTS_CTX, k, n);
    close_storage(&h);
    ydebug("accounts_set_balance: uid=%u balance=%lld", uid, (long long)n);
    return YAAFC_OK(yaafc_int, 1);
}

YAAFC_CLASS_ANNOTATE("override@accounts:store:store_balance")
struct yaafc_int64_result accounts_store_balance_impl(struct ctx *ctx, struct object *obj,
                                                      uint32_t uid)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_int64, "accounts_balance: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "user:%u", uid);
    if (!kv_exists(&h, k)) {
        close_storage(&h);
        return YAAFC_ERR(yaafc_int64, "accounts_balance: unknown uid");
    }
    snprintf(k, sizeof(k), "balance:%u", uid);
    int64_t bal = kv_get_or(&h, k, 0);
    close_storage(&h);
    return YAAFC_OK(yaafc_int64, bal);
}

YAAFC_CLASS_ANNOTATE("override@accounts:store:store_count")
struct yaafc_size_result accounts_store_count_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (YAAFC_IS_ERR(sr)) return YAAFC_ERR(yaafc_size, "accounts_count: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    int64_t c = kv_get_or(&h, "count", 0);
    close_storage(&h);
    return YAAFC_OK(yaafc_size, (size_t)(c < 0 ? 0 : c));
}

#include "accounts.gen.c"
