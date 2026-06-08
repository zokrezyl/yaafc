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

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/ycore/yspan.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yloop/yloop.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PICOMESH_CLASS_ANNOTATE("class@storage:db") storage_data {
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
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return STORAGE_BACKEND_SQLITE;
    struct yconfig_node_ptr_result backend_node_res =
        yconfig_get(picomesh_engine_config(engine), "storage.backend");
    if (PICOMESH_IS_ERR(backend_node_res)) {
        /* An absent key returns OK+NULL; an error here is a real config-read
         * failure. Defaulting silently could pick the wrong backend and write
         * data to the wrong store, so make it loud (and don't leak the chain). */
        yerror("storage: reading 'storage.backend' failed: %s",
               backend_node_res.error.msg ? backend_node_res.error.msg : "?");
        picomesh_error_destroy(backend_node_res.error);
        return STORAGE_BACKEND_SQLITE;
    }
    if (backend_node_res.value) {
        const char *backend_name = yconfig_node_as_string(backend_node_res.value, NULL);
        if (backend_name && strcmp(backend_name, "mdbx") == 0) return STORAGE_BACKEND_MDBX;
    }
    return STORAGE_BACKEND_SQLITE;
}

static struct picomesh_void_result ensure_backend(struct storage_data *d)
{
    if (d->backend != STORAGE_BACKEND_UNSET && d->vt) return PICOMESH_OK_VOID();
    enum storage_backend b = resolve_configured_backend();
    switch (b) {
    case STORAGE_BACKEND_SQLITE:
        d->vt = storage_backend_sqlite_ops();
        break;
    case STORAGE_BACKEND_MDBX:
        d->vt = storage_backend_mdbx_ops();
        break;
    default:
        return PICOMESH_ERR(picomesh_void, "storage: unknown backend");
    }
    d->backend = b;
    ydebug("storage: backend=%s", b == STORAGE_BACKEND_MDBX ? "mdbx" : "sqlite");
    return PICOMESH_OK_VOID();
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

/* ---- DB work offload --------------------------------------------------
 *
 * sqlite/mdbx calls block; running them on the loop thread freezes the
 * whole backend process for every other peer. Each op is instead handed
 * to libuv's worker pool via yloop_run_blocking, suspending only the
 * serving coroutine until it returns — the loop keeps serving other
 * peers meanwhile (asyncio run_in_executor shape).
 *
 * Thread-safety: each storage object owns its own backend connection
 * (opened lazily inside the op) and is driven by one coroutine at a
 * time, so a single connection is never touched from two threads at
 * once. Distinct objects use distinct connections — safe to run in
 * parallel on the pool (sqlite does its own file-level locking). */
enum storage_op {
    STORAGE_OP_SET, STORAGE_OP_GET, STORAGE_OP_EXISTS,
    STORAGE_OP_DEL, STORAGE_OP_COUNT,
};

struct storage_work {
    struct storage_data *d;
    enum storage_op op;
    const char *context;
    const char *key;
    const char *value; /* in:  set (opaque NUL-terminated string/bytes) */
    char *out_str;     /* out: get — heap, NUL-terminated, owned by caller */
    int out_i;         /* out: exists / del */
    size_t out_sz;     /* out: count */
    enum storage_rc rc;
};

/* Runs on a worker-pool thread — touches only its own `arg`. */
static void storage_work_fn(void *arg)
{
    struct storage_work *w = arg;
    const struct backend_ops *vt = w->d->vt;
    switch (w->op) {
    case STORAGE_OP_SET:    w->rc = vt->set(w->d, w->context, w->key, w->value); break;
    case STORAGE_OP_GET:    w->rc = vt->get(w->d, w->context, w->key, &w->out_str); break;
    case STORAGE_OP_EXISTS: w->rc = vt->exists(w->d, w->context, w->key, &w->out_i); break;
    case STORAGE_OP_DEL:    w->rc = vt->del(w->d, w->context, w->key, &w->out_i); break;
    case STORAGE_OP_COUNT:  w->rc = vt->count(w->d, w->context, &w->out_sz); break;
    }
}

/* Offload `w` to the loop's worker pool (suspending the serving coro)
 * if we're on a loop coroutine; otherwise run inline. */
static void storage_run(struct storage_work *w)
{
    struct picomesh_engine *e = picomesh_active_engine();
    struct yloop *l = e ? picomesh_engine_loop(e) : NULL;
    if (!l) { storage_work_fn(w); return; }
    struct picomesh_void_result r = yloop_run_blocking(l, storage_work_fn, w);
    if (PICOMESH_IS_ERR(r)) {
        /* Offload failed — run inline so the op still completes, but log it:
         * it can indicate a degraded event loop / worker pool. */
        ywarn("storage: worker-pool offload failed (%s) — running inline",
              r.error.msg ? r.error.msg : "?");
        picomesh_error_destroy(r.error);
        storage_work_fn(w);
    }
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:set")
struct picomesh_int_result storage_set_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                         const char *context, const char *key,
                                         const char *value)
{
    (void)ctx;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct storage_data *d = sd(obj);
    struct picomesh_void_result eb = ensure_backend(d);
    if (PICOMESH_IS_ERR(eb)) return PICOMESH_ERR(picomesh_int, "storage_set: backend init failed", eb);
    struct storage_work w = {.d = d, .op = STORAGE_OP_SET,
                             .context = context, .key = key, .value = value};
    double span_start = picomesh_ytime_monotonic_sec();
    storage_run(&w);
    double span_us = (picomesh_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=db.set.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("db.set", span_us);
    if (w.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_int, rc_msg(w.rc));
    yinfo("storage_set: %s/%s=%s", context, key, value ? value : "");
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:get")
struct picomesh_string_result storage_get_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                            const char *context, const char *key)
{
    (void)ctx;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct storage_data *d = sd(obj);
    struct picomesh_void_result eb = ensure_backend(d);
    if (PICOMESH_IS_ERR(eb)) return PICOMESH_ERR(picomesh_string, "storage_get: backend init failed", eb);
    struct storage_work w = {.d = d, .op = STORAGE_OP_GET, .context = context, .key = key};
    double span_start = picomesh_ytime_monotonic_sec();
    storage_run(&w);
    double span_us = (picomesh_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=db.get.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("db.get", span_us);
    if (w.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_string, rc_msg(w.rc));
    return PICOMESH_OK(picomesh_string, w.out_str);
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:exists")
struct picomesh_int_result storage_exists_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                            const char *context, const char *key)
{
    (void)ctx;
    struct storage_data *d = sd(obj);
    struct picomesh_void_result eb = ensure_backend(d);
    if (PICOMESH_IS_ERR(eb)) return PICOMESH_ERR(picomesh_int, "storage_exists: backend init failed", eb);
    struct storage_work w = {.d = d, .op = STORAGE_OP_EXISTS, .context = context, .key = key};
    storage_run(&w);
    if (w.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_int, rc_msg(w.rc));
    return PICOMESH_OK(picomesh_int, w.out_i);
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:del")
struct picomesh_int_result storage_del_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                         const char *context, const char *key)
{
    (void)ctx;
    struct storage_data *d = sd(obj);
    struct picomesh_void_result eb = ensure_backend(d);
    if (PICOMESH_IS_ERR(eb)) return PICOMESH_ERR(picomesh_int, "storage_del: backend init failed", eb);
    struct storage_work w = {.d = d, .op = STORAGE_OP_DEL, .context = context, .key = key};
    storage_run(&w);
    if (w.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_int, rc_msg(w.rc));
    return PICOMESH_OK(picomesh_int, w.out_i);
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:count")
struct picomesh_size_result storage_count_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                            const char *context)
{
    (void)ctx;
    struct storage_data *d = sd(obj);
    struct picomesh_void_result eb = ensure_backend(d);
    if (PICOMESH_IS_ERR(eb)) return PICOMESH_ERR(picomesh_size, "storage_count: backend init failed", eb);
    struct storage_work w = {.d = d, .op = STORAGE_OP_COUNT, .context = context};
    storage_run(&w);
    if (w.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_size, rc_msg(w.rc));
    return PICOMESH_OK(picomesh_size, w.out_sz);
}

/* The two backend translation units are #included so they remain
 * file-local to this plugin and share the same private `storage_data`
 * layout via backends.h. The codegen never sees them — it walks only
 * the annotated impls above. */
#include "backend_sqlite.c"
#include "backend_mdbx.c"

#include "storage.gen.c"
