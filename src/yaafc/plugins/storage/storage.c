/* storage — backend-agnostic string→int64 KV store.
 *
 * Slots: set(context, key, value), get(context, key), exists(context, key),
 *        del(context, key), count(context). The `context` is the logical
 *        namespace — a table in the sqlite backend, a named sub-DBI in
 *        the libmdbx backend. Both backends apply the same validator
 *        (storage_context_is_valid) so the same context name is portable.
 *
 * Backend selection is per-service via yconfig:
 *   storage.backend  = "sqlite" (default) | "mdbx"
 *   storage.db_path  = sqlite file path (sqlite backend)
 *   storage.mdbx_path = mdbx env directory (mdbx backend)
 *
 * The consumer is unaware which backend is active; constructing a
 * storage object is unconditional. The backend is resolved once on
 * first method call against each object. */

#include "backends.h"

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yengine/engine.h>
#include <yaafc/yconfig/yconfig.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct YAAFC_CLASS_ANNOTATE("class@storage:db") storage_data {
    enum storage_backend backend;
    const struct backend_ops *vt;
    union {
        struct {
            sqlite3 *db;
            int opened;
        } sqlite;
        struct {
            int opened;
        } mdbx;
    } be;
};

static struct storage_data *sd(struct object *obj)
{
    return (struct storage_data *)((char *)obj + sizeof(struct object));
}

int storage_context_is_valid(const char *context)
{
    if (!context || !*context) return 0;
    size_t n = 0;
    for (const char *p = context; *p; ++p, ++n) {
        if (n >= 63) return 0;
        unsigned char c = (unsigned char)*p;
        int ok = (c == '_') || (c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') ||
                 (n > 0 && c >= '0' && c <= '9');
        if (!ok) return 0;
    }
    return 1;
}

static enum storage_backend resolve_configured_backend(void)
{
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return STORAGE_BACKEND_SQLITE;
    struct yconfig_node_ptr_result r =
        yconfig_get(yaafc_engine_config(e), "storage.backend");
    if (YAAFC_IS_OK(r) && r.value) {
        const char *s = yconfig_node_as_string(r.value, NULL);
        if (s && strcmp(s, "mdbx") == 0) return STORAGE_BACKEND_MDBX;
    }
    return STORAGE_BACKEND_SQLITE;
}

static struct yaafc_void_result ensure_backend(struct storage_data *d)
{
    if (d->backend != STORAGE_BACKEND_UNSET && d->vt) return YAAFC_OK_VOID();
    enum storage_backend b = resolve_configured_backend();
    switch (b) {
    case STORAGE_BACKEND_SQLITE:
        d->vt = storage_backend_sqlite_ops();
        break;
    case STORAGE_BACKEND_MDBX:
        d->vt = storage_backend_mdbx_ops();
        break;
    default:
        return YAAFC_ERR(yaafc_void, "storage: unknown backend");
    }
    d->backend = b;
    ydebug("storage: backend=%s", b == STORAGE_BACKEND_MDBX ? "mdbx" : "sqlite");
    return YAAFC_OK_VOID();
}

static const char *rc_msg(enum storage_rc rc)
{
    switch (rc) {
    case STORAGE_RC_OK:           return "ok";
    case STORAGE_RC_NOT_FOUND:    return "key not found";
    case STORAGE_RC_BAD_CONTEXT:  return "invalid context name";
    case STORAGE_RC_OPEN_FAILED:  return "backend open failed";
    case STORAGE_RC_INTERNAL:     return "backend internal error";
    }
    return "unknown rc";
}

YAAFC_CLASS_ANNOTATE("override@storage:db:set")
struct yaafc_int_result storage_set_impl(struct ctx *ctx, struct object *obj,
                                         const char *context, const char *key,
                                         int64_t value)
{
    (void)ctx;
    struct storage_data *d = sd(obj);
    struct yaafc_void_result eb = ensure_backend(d);
    if (YAAFC_IS_ERR(eb)) return YAAFC_ERR(yaafc_int, "storage_set: backend init failed", eb);
    enum storage_rc rc = d->vt->set(d, context, key, value);
    if (rc != STORAGE_RC_OK) return YAAFC_ERR(yaafc_int, rc_msg(rc));
    yinfo("storage_set: %s/%s=%lld", context, key, (long long)value);
    return YAAFC_OK(yaafc_int, 1);
}

YAAFC_CLASS_ANNOTATE("override@storage:db:get")
struct yaafc_int64_result storage_get_impl(struct ctx *ctx, struct object *obj,
                                           const char *context, const char *key)
{
    (void)ctx;
    struct storage_data *d = sd(obj);
    struct yaafc_void_result eb = ensure_backend(d);
    if (YAAFC_IS_ERR(eb)) return YAAFC_ERR(yaafc_int64, "storage_get: backend init failed", eb);
    int64_t v = 0;
    enum storage_rc rc = d->vt->get(d, context, key, &v);
    if (rc != STORAGE_RC_OK) return YAAFC_ERR(yaafc_int64, rc_msg(rc));
    return YAAFC_OK(yaafc_int64, v);
}

YAAFC_CLASS_ANNOTATE("override@storage:db:exists")
struct yaafc_int_result storage_exists_impl(struct ctx *ctx, struct object *obj,
                                            const char *context, const char *key)
{
    (void)ctx;
    struct storage_data *d = sd(obj);
    struct yaafc_void_result eb = ensure_backend(d);
    if (YAAFC_IS_ERR(eb)) return YAAFC_ERR(yaafc_int, "storage_exists: backend init failed", eb);
    int present = 0;
    enum storage_rc rc = d->vt->exists(d, context, key, &present);
    if (rc != STORAGE_RC_OK) return YAAFC_ERR(yaafc_int, rc_msg(rc));
    return YAAFC_OK(yaafc_int, present);
}

YAAFC_CLASS_ANNOTATE("override@storage:db:del")
struct yaafc_int_result storage_del_impl(struct ctx *ctx, struct object *obj,
                                         const char *context, const char *key)
{
    (void)ctx;
    struct storage_data *d = sd(obj);
    struct yaafc_void_result eb = ensure_backend(d);
    if (YAAFC_IS_ERR(eb)) return YAAFC_ERR(yaafc_int, "storage_del: backend init failed", eb);
    int removed = 0;
    enum storage_rc rc = d->vt->del(d, context, key, &removed);
    if (rc != STORAGE_RC_OK) return YAAFC_ERR(yaafc_int, rc_msg(rc));
    return YAAFC_OK(yaafc_int, removed);
}

YAAFC_CLASS_ANNOTATE("override@storage:db:count")
struct yaafc_size_result storage_count_impl(struct ctx *ctx, struct object *obj,
                                            const char *context)
{
    (void)ctx;
    struct storage_data *d = sd(obj);
    struct yaafc_void_result eb = ensure_backend(d);
    if (YAAFC_IS_ERR(eb)) return YAAFC_ERR(yaafc_size, "storage_count: backend init failed", eb);
    size_t n = 0;
    enum storage_rc rc = d->vt->count(d, context, &n);
    if (rc != STORAGE_RC_OK) return YAAFC_ERR(yaafc_size, rc_msg(rc));
    return YAAFC_OK(yaafc_size, n);
}

/* The two backend translation units are #included so they remain
 * file-local to this plugin and share the same private `storage_data`
 * layout via backends.h. The codegen never sees them — it walks only
 * the annotated impls above. */
#include "backend_sqlite.c"
#include "backend_mdbx.c"

#include "storage.gen.c"
