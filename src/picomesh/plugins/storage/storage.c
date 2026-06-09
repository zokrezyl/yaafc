/* storage — backend-agnostic string→int64 KV store.
 *
 * Slots: set(context, key, value), get(context, key), exists(context, key),
 *        del(context, key), count(context). The `context` is the logical
 *        namespace — a table in the sqlite backend, a named sub-DBI in
 *        the libmdbx backend. Both backends apply the same validator
 *        (storage_context_is_valid) so the same context name is portable.
 *
 * Backend selection is per-service via config:
 *   storage.backend  = "sqlite" (default) | "mdbx"
 *   storage.db_path  = sqlite file path (sqlite backend)
 *   storage.mdbx_path = mdbx env directory (mdbx backend)
 *
 * The consumer is unaware which backend is active; constructing a
 * storage object is unconditional. The backend is resolved once on
 * first method call against each object. */

#include "backends.h"

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/core/yspan.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/yheaders.h>
#include <picomesh/engine/engine.h>
#include <picomesh/config/config.h>
#include <picomesh/loop/loop.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PICOMESH_CLASS_ANNOTATE("class@storage:db") storage_data {
    enum storage_backend backend;
    const struct backend_ops *vt;
    /* Configured data path for the chosen backend (sqlite file or mdbx dir),
     * resolved once in ensure_backend so the backends never read config on the
     * hot path. NULL when unset (sqlite falls back to :memory:; mdbx fails). */
    const char *data_path;
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

/* Resolves the configured backend as an enum value carried in the Result. An
 * absent `storage.backend` key returns OK+NULL → default sqlite; a genuine
 * config-read failure propagates (defaulting silently could pick the wrong
 * backend and write data to the wrong store). */
static struct picomesh_int_result resolve_configured_backend(void)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_OK(picomesh_int, STORAGE_BACKEND_SQLITE);
    struct config_node_ptr_result backend_node =
        config_get(picomesh_engine_config(engine), "storage.backend");
    PICOMESH_RETURN_IF_ERR(picomesh_int, backend_node, "storage: reading 'storage.backend' failed");
    if (backend_node.value) {
        const char *backend_name = config_node_as_string(backend_node.value, NULL);
        if (backend_name && strcmp(backend_name, "mdbx") == 0)
            return PICOMESH_OK(picomesh_int, STORAGE_BACKEND_MDBX);
    }
    return PICOMESH_OK(picomesh_int, STORAGE_BACKEND_SQLITE);
}

static struct picomesh_void_result ensure_backend(struct storage_data *data)
{
    if (data->backend != STORAGE_BACKEND_UNSET && data->vt) return PICOMESH_OK_VOID();
    struct picomesh_int_result backend_res = resolve_configured_backend();
    PICOMESH_RETURN_IF_ERR(picomesh_void, backend_res, "storage: resolve backend failed");
    enum storage_backend backend = (enum storage_backend)backend_res.value;
    const char *path_key = NULL;
    switch (backend) {
    case STORAGE_BACKEND_SQLITE:
        data->vt = storage_backend_sqlite_ops();
        path_key = "storage.db_path";
        break;
    case STORAGE_BACKEND_MDBX:
        data->vt = storage_backend_mdbx_ops();
        path_key = "storage.mdbx_path";
        break;
    default:
        return PICOMESH_ERR(picomesh_void, "storage: unknown backend");
    }
    /* Resolve the backend's data path here (the only config read) so the
     * hot-path backend code never touches config. A genuine config-read
     * failure (e.g. no active engine/config) must surface here, never be
     * mistaken for an unset path — that would silently drop sqlite into
     * :memory: mode and lose persistence. Absence is fine (path_node.value
     * NULL → sqlite falls back to :memory:; mdbx fails loudly later); the
     * returned string is borrowed from the config tree (engine lifetime). */
    struct picomesh_engine *engine = picomesh_active_engine();
    struct config_node_ptr_result path_node =
        config_get(picomesh_engine_config(engine), path_key);
    PICOMESH_RETURN_IF_ERR(picomesh_void, path_node, "storage: reading data-path config failed");
    data->data_path = path_node.value ? config_node_as_string(path_node.value, NULL) : NULL;
    data->backend = backend;
    ydebug("storage: backend=%s", backend == STORAGE_BACKEND_MDBX ? "mdbx" : "sqlite");
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
 * to libuv's worker pool via loop_run_blocking, suspending only the
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
    struct storage_data *data;
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
    struct storage_work *work = arg;
    const struct backend_ops *vt = work->data->vt;
    switch (work->op) {
    case STORAGE_OP_SET:    work->rc = vt->set(work->data, work->context, work->key, work->value); break;
    case STORAGE_OP_GET:    work->rc = vt->get(work->data, work->context, work->key, &work->out_str); break;
    case STORAGE_OP_EXISTS: work->rc = vt->exists(work->data, work->context, work->key, &work->out_i); break;
    case STORAGE_OP_DEL:    work->rc = vt->del(work->data, work->context, work->key, &work->out_i); break;
    case STORAGE_OP_COUNT:  work->rc = vt->count(work->data, work->context, &work->out_sz); break;
    }
}

/* Offload `work` to the loop's worker pool (suspending the serving coro)
 * if we're on a loop coroutine; otherwise run inline. A worker-pool offload
 * failure is recovered by running inline (the op still completes) and the
 * degraded-loop signal is surfaced as the Result. */
static struct picomesh_void_result storage_run(struct storage_work *work)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    struct loop *loop = engine ? picomesh_engine_loop(engine) : NULL;
    if (!loop) { storage_work_fn(work); return PICOMESH_OK_VOID(); }
    struct picomesh_void_result offload_res = loop_run_blocking(loop, storage_work_fn, work);
    if (PICOMESH_IS_ERR(offload_res)) {
        ywarn("storage: worker-pool offload failed (%s) — running inline",
              offload_res.error.msg ? offload_res.error.msg : "?");
        picomesh_error_destroy(offload_res.error);
        storage_work_fn(work);
    }
    return PICOMESH_OK_VOID();
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:set")
struct picomesh_int_result storage_set_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                         const char *context, const char *key,
                                         const char *value)
{
    (void)ctx;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct storage_data *data = sd(obj);
    struct picomesh_void_result backend_init = ensure_backend(data);
    if (PICOMESH_IS_ERR(backend_init)) return PICOMESH_ERR(picomesh_int, "storage_set: backend init failed", backend_init);
    struct storage_work work = {.data = data, .op = STORAGE_OP_SET,
                                .context = context, .key = key, .value = value};
    double span_start = picomesh_ytime_monotonic_sec();
    storage_run(&work);
    double span_us = (picomesh_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=db.set.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("db.set", span_us);
    if (work.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_int, rc_msg(work.rc));
    yinfo("storage_set: %s/%s=%s", context, key, value ? value : "");
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:get")
struct picomesh_string_result storage_get_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                            const char *context, const char *key)
{
    (void)ctx;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct storage_data *data = sd(obj);
    struct picomesh_void_result backend_init = ensure_backend(data);
    if (PICOMESH_IS_ERR(backend_init)) return PICOMESH_ERR(picomesh_string, "storage_get: backend init failed", backend_init);
    struct storage_work work = {.data = data, .op = STORAGE_OP_GET, .context = context, .key = key};
    double span_start = picomesh_ytime_monotonic_sec();
    storage_run(&work);
    double span_us = (picomesh_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=db.get.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("db.get", span_us);
    if (work.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_string, rc_msg(work.rc));
    return PICOMESH_OK(picomesh_string, work.out_str);
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:exists")
struct picomesh_int_result storage_exists_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                            const char *context, const char *key)
{
    (void)ctx;
    struct storage_data *data = sd(obj);
    struct picomesh_void_result backend_init = ensure_backend(data);
    if (PICOMESH_IS_ERR(backend_init)) return PICOMESH_ERR(picomesh_int, "storage_exists: backend init failed", backend_init);
    struct storage_work work = {.data = data, .op = STORAGE_OP_EXISTS, .context = context, .key = key};
    storage_run(&work);
    if (work.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_int, rc_msg(work.rc));
    return PICOMESH_OK(picomesh_int, work.out_i);
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:del")
struct picomesh_int_result storage_del_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                         const char *context, const char *key)
{
    (void)ctx;
    struct storage_data *data = sd(obj);
    struct picomesh_void_result backend_init = ensure_backend(data);
    if (PICOMESH_IS_ERR(backend_init)) return PICOMESH_ERR(picomesh_int, "storage_del: backend init failed", backend_init);
    struct storage_work work = {.data = data, .op = STORAGE_OP_DEL, .context = context, .key = key};
    storage_run(&work);
    if (work.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_int, rc_msg(work.rc));
    return PICOMESH_OK(picomesh_int, work.out_i);
}

PICOMESH_CLASS_ANNOTATE("override@storage:db:count")
struct picomesh_size_result storage_count_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                            const char *context)
{
    (void)ctx;
    struct storage_data *data = sd(obj);
    struct picomesh_void_result backend_init = ensure_backend(data);
    if (PICOMESH_IS_ERR(backend_init)) return PICOMESH_ERR(picomesh_size, "storage_count: backend init failed", backend_init);
    struct storage_work work = {.data = data, .op = STORAGE_OP_COUNT, .context = context};
    storage_run(&work);
    if (work.rc != STORAGE_RC_OK) return PICOMESH_ERR(picomesh_size, rc_msg(work.rc));
    return PICOMESH_OK(picomesh_size, work.out_sz);
}

/* The two backend translation units are #included so they remain
 * file-local to this plugin and share the same private `storage_data`
 * layout via backends.h. The codegen never sees them — it walks only
 * the annotated impls above. */
#include "backend_sqlite.c"
#include "backend_mdbx.c"

#include "storage.gen.c"
