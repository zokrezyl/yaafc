/* sharded_storage — write-parallel KV store.
 *
 * Same string→int64 KV API as `storage`, but data is sharded across N
 * independent mdbx envs (one file each → one writer + fsync stream
 * each), keyed by hash(namespace · key). Concurrent writes that hash to
 * different shards commit in parallel on the libuv worker pool, lifting
 * the single-writer fsync ceiling that bounds the one-env `storage`
 * backend (measured: 1 writer ≈ 2.1k durable commits/s, 8 ≈ 9.5k).
 *
 * Within each shard a per-namespace DBI keeps count(namespace) an
 * O(shards) sum of dbi_stat — no scan. DB work runs via
 * yloop_run_blocking so the serving coroutine yields and the loop stays
 * responsive; shards are threads on the pool, NOT separate processes.
 *
 * Config (service block):
 *   sharded_storage.shards  = N        (default 8, max 64)
 *   sharded_storage.path    = base dir (default /tmp/yaafc-sharded) */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/yspan.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yclass/yheaders.h>
#include <yaafc/yengine/engine.h>
#include <yaafc/yconfig/yconfig.h>
#include <yaafc/yloop/yloop.h>

#include <mdbx.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SHARDED_MAX_SHARDS    64
#define SHARDED_DBI_PER_SHARD 32   /* namespaces per shard */

struct YAAFC_CLASS_ANNOTATE("class@sharded_storage:db") sharded_storage_data {
    /* The shard set is process-global (below); the object is just a
     * handle the framework can hand out. */
    char unused;
};

/* Op result codes — local to this plugin. */
enum shard_rc {
    SHARD_OK = 0,
    SHARD_NOT_FOUND,
    SHARD_BAD_CONTEXT,
    SHARD_OPEN_FAILED,
    SHARD_INTERNAL,
};

/* ---- process-global shard set (lazy, opened once) ------------------ */

struct shard_dbi {
    char ns[64];
    MDBX_dbi dbi;
    int used;
};

struct shard {
    MDBX_env *env;
    struct shard_dbi dbi[SHARDED_DBI_PER_SHARD];
    pthread_mutex_t dbi_mu;
};

struct shard_set {
    struct shard shards[SHARDED_MAX_SHARDS];
    int n;
    int ready;
};

/* Lazy-open the N shard envs. NULL on failure. Same lazy-const-init
 * shape the storage backend / yclass accessors use; after init the env
 * handles are immutable and libmdbx is internally multi-threaded. */
static struct shard_set *shard_set(void)
{
    static struct shard_set s = {0};
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int tried = 0;

    if (s.ready) return &s;
    if (tried) return NULL;

    pthread_mutex_lock(&mu);
    if (s.ready) { pthread_mutex_unlock(&mu); return &s; }
    if (tried)   { pthread_mutex_unlock(&mu); return NULL; }
    tried = 1;

    int n = 8;
    const char *base = "/tmp/yaafc-sharded";
    struct yaafc_engine *e = yaafc_active_engine();
    if (e) {
        struct yconfig_node_ptr_result r =
            yconfig_get(yaafc_engine_config(e), "sharded_storage.shards");
        if (YAAFC_IS_OK(r) && r.value) {
            int v = (int)yconfig_node_as_int(r.value, 8);
            if (v > 0 && v <= SHARDED_MAX_SHARDS) n = v;
        }
        struct yconfig_node_ptr_result p =
            yconfig_get(yaafc_engine_config(e), "sharded_storage.path");
        if (YAAFC_IS_OK(p) && p.value) {
            const char *str = yconfig_node_as_string(p.value, NULL);
            if (str && *str) base = str;
        }
    }

    if (mkdir(base, 0700) != 0 && errno != EEXIST) {
        ywarn("sharded_storage: mkdir(%s) failed: %s", base, strerror(errno));
        pthread_mutex_unlock(&mu);
        return NULL;
    }
    for (int i = 0; i < n; ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s/shard-%d", base, i);
        if (mkdir(path, 0700) != 0 && errno != EEXIST) {
            ywarn("sharded_storage: mkdir(%s) failed: %s", path, strerror(errno));
            pthread_mutex_unlock(&mu);
            return NULL;
        }
        MDBX_env *env = NULL;
        if (mdbx_env_create(&env) != MDBX_SUCCESS) { pthread_mutex_unlock(&mu); return NULL; }
        mdbx_env_set_maxdbs(env, SHARDED_DBI_PER_SHARD);
        mdbx_env_set_geometry(env, 1 << 20, -1, 1 << 30, 1 << 20, -1, -1);
        /* Same durability as the storage mdbx backend (WRITEMAP +
         * NOMETASYNC: data fsync'd per commit, metadata deferred) so the
         * sharded-vs-single comparison isolates the parallelism win. */
        if (mdbx_env_open(env, path, MDBX_WRITEMAP | MDBX_NOMETASYNC, 0644) != MDBX_SUCCESS) {
            ywarn("sharded_storage: env_open(%s) failed", path);
            mdbx_env_close(env);
            pthread_mutex_unlock(&mu);
            return NULL;
        }
        s.shards[i].env = env;
        pthread_mutex_init(&s.shards[i].dbi_mu, NULL);
    }
    s.n = n;
    s.ready = 1;
    ydebug("sharded_storage: opened %d shard envs at %s", n, base);
    pthread_mutex_unlock(&mu);
    return &s;
}

static int ns_valid(const char *ns)
{
    if (!ns || !*ns) return 0;
    for (size_t i = 0; ns[i]; ++i) {
        if (i >= 63) return 0;
        char c = ns[i];
        int ok = (c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (i > 0 && c >= '0' && c <= '9');
        if (!ok) return 0;
    }
    return 1;
}

/* FNV-1a over namespace + NUL + key → shard index. Hashing the full
 * key spreads a hot namespace's keys across all shards. */
static int shard_for(const struct shard_set *s, const char *ns, const char *key)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)ns; *p; ++p) { h ^= *p; h *= 16777619u; }
    h ^= 0u; h *= 16777619u;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p) { h ^= *p; h *= 16777619u; }
    return (int)(h % (uint32_t)s->n);
}

/* Resolve (open-or-create + cache) the per-namespace DBI inside one
 * shard. First touch per (shard, ns) costs a short write txn; cached
 * thereafter. */
static MDBX_dbi shard_dbi(struct shard *sh, const char *ns, enum shard_rc *rc)
{
    pthread_mutex_lock(&sh->dbi_mu);
    for (int i = 0; i < SHARDED_DBI_PER_SHARD; ++i)
        if (sh->dbi[i].used && strcmp(sh->dbi[i].ns, ns) == 0) {
            MDBX_dbi d = sh->dbi[i].dbi;
            pthread_mutex_unlock(&sh->dbi_mu);
            *rc = SHARD_OK;
            return d;
        }
    MDBX_txn *txn = NULL;
    if (mdbx_txn_begin(sh->env, NULL, MDBX_TXN_READWRITE, &txn) != MDBX_SUCCESS) {
        pthread_mutex_unlock(&sh->dbi_mu); *rc = SHARD_INTERNAL; return 0;
    }
    MDBX_dbi dbi = 0;
    if (mdbx_dbi_open(txn, ns, MDBX_CREATE, &dbi) != MDBX_SUCCESS ||
        mdbx_txn_commit(txn) != MDBX_SUCCESS) {
        mdbx_txn_abort(txn);
        pthread_mutex_unlock(&sh->dbi_mu); *rc = SHARD_INTERNAL; return 0;
    }
    for (int i = 0; i < SHARDED_DBI_PER_SHARD; ++i)
        if (!sh->dbi[i].used) {
            strncpy(sh->dbi[i].ns, ns, sizeof(sh->dbi[i].ns) - 1);
            sh->dbi[i].dbi = dbi;
            sh->dbi[i].used = 1;
            break;
        }
    pthread_mutex_unlock(&sh->dbi_mu);
    *rc = SHARD_OK;
    return dbi;
}

/* ---- worker-pool ops ----------------------------------------------- */

enum shard_op { OP_SET, OP_GET, OP_EXISTS, OP_DEL, OP_COUNT };

struct shard_work {
    enum shard_op op;
    const char *ns;
    const char *key;
    const char *value; /* in:  set (opaque string/bytes, NUL-terminated) */
    char *out_str;     /* out: get — heap, NUL-terminated, owned by caller */
    int out_i;         /* out: exists/del */
    size_t out_sz;     /* out: count */
    enum shard_rc rc;
};

/* Runs on a worker-pool thread — touches only `arg` + the shard env it
 * routes to. Different shards → different envs → parallel commits. */
static void shard_work_fn(void *arg)
{
    struct shard_work *w = arg;
    struct shard_set *s = shard_set();
    if (!s) { w->rc = SHARD_OPEN_FAILED; return; }
    if (!ns_valid(w->ns)) { w->rc = SHARD_BAD_CONTEXT; return; }

    if (w->op == OP_COUNT) {
        /* Sum dbi_stat across all shards for this namespace (read-only;
         * shards never written for this ns simply report 0). */
        size_t total = 0;
        for (int i = 0; i < s->n; ++i) {
            MDBX_txn *txn = NULL;
            if (mdbx_txn_begin(s->shards[i].env, NULL, MDBX_TXN_RDONLY, &txn) != MDBX_SUCCESS)
                continue;
            MDBX_dbi dbi = 0;
            if (mdbx_dbi_open(txn, w->ns, 0, &dbi) == MDBX_SUCCESS) {
                MDBX_stat st;
                if (mdbx_dbi_stat(txn, dbi, &st, sizeof(st)) == MDBX_SUCCESS)
                    total += (size_t)st.ms_entries;
            }
            mdbx_txn_abort(txn);
        }
        w->out_sz = total;
        w->rc = SHARD_OK;
        return;
    }

    struct shard *sh = &s->shards[shard_for(s, w->ns, w->key)];
    enum shard_rc rc = SHARD_OK;
    MDBX_dbi dbi = shard_dbi(sh, w->ns, &rc);
    if (rc != SHARD_OK) { w->rc = rc; return; }

    int rdonly = (w->op == OP_GET || w->op == OP_EXISTS);
    MDBX_txn *txn = NULL;
    if (mdbx_txn_begin(sh->env, NULL, rdonly ? MDBX_TXN_RDONLY : MDBX_TXN_READWRITE,
                       &txn) != MDBX_SUCCESS) {
        w->rc = SHARD_INTERNAL; return;
    }
    MDBX_val k = {.iov_base = (void *)w->key, .iov_len = strlen(w->key)};

    switch (w->op) {
    case OP_SET: {
        /* Store the value's raw bytes (no trailing NUL — the length is
         * carried by mdbx). get re-adds a NUL when it copies out. */
        MDBX_val v = {.iov_base = (void *)w->value,
                      .iov_len = w->value ? strlen(w->value) : 0};
        int r = mdbx_put(txn, dbi, &k, &v, MDBX_UPSERT);
        if (r != MDBX_SUCCESS || mdbx_txn_commit(txn) != MDBX_SUCCESS) {
            if (r != MDBX_SUCCESS) mdbx_txn_abort(txn);
            w->rc = SHARD_INTERNAL; return;
        }
        w->rc = SHARD_OK;
        return;
    }
    case OP_GET: {
        MDBX_val v = {0};
        int r = mdbx_get(txn, dbi, &k, &v);
        if (r == MDBX_SUCCESS) {
            char *out = malloc(v.iov_len + 1);
            if (!out) {
                w->rc = SHARD_INTERNAL;
            } else {
                if (v.iov_len) memcpy(out, v.iov_base, v.iov_len);
                out[v.iov_len] = 0;
                w->out_str = out;
                w->rc = SHARD_OK;
            }
        } else if (r == MDBX_NOTFOUND) {
            w->rc = SHARD_NOT_FOUND;
        } else {
            w->rc = SHARD_INTERNAL;
        }
        mdbx_txn_abort(txn);
        return;
    }
    case OP_EXISTS: {
        MDBX_val v = {0};
        w->out_i = (mdbx_get(txn, dbi, &k, &v) == MDBX_SUCCESS) ? 1 : 0;
        mdbx_txn_abort(txn);
        w->rc = SHARD_OK;
        return;
    }
    case OP_DEL: {
        int r = mdbx_del(txn, dbi, &k, NULL);
        if (r == MDBX_SUCCESS) {
            w->out_i = 1;
            if (mdbx_txn_commit(txn) != MDBX_SUCCESS) { w->rc = SHARD_INTERNAL; return; }
            w->rc = SHARD_OK;
        } else {
            mdbx_txn_abort(txn);
            w->out_i = 0;
            w->rc = (r == MDBX_NOTFOUND) ? SHARD_OK : SHARD_INTERNAL;
        }
        return;
    }
    default:
        mdbx_txn_abort(txn);
        w->rc = SHARD_INTERNAL;
        return;
    }
}

/* Offload the op to the loop's worker pool (suspending the serving
 * coroutine); inline if not on a loop. */
static void shard_run(struct shard_work *w)
{
    struct yaafc_engine *e = yaafc_active_engine();
    struct yloop *l = e ? yaafc_engine_loop(e) : NULL;
    if (!l) { shard_work_fn(w); return; }
    struct yaafc_void_result r = yloop_run_blocking(l, shard_work_fn, w);
    if (YAAFC_IS_ERR(r)) { yaafc_error_destroy(r.error); shard_work_fn(w); }
}

static const char *shard_rc_msg(enum shard_rc rc)
{
    switch (rc) {
    case SHARD_OK:          return "ok";
    case SHARD_NOT_FOUND:   return "key not found";
    case SHARD_BAD_CONTEXT: return "invalid namespace";
    case SHARD_OPEN_FAILED: return "shard open failed";
    case SHARD_INTERNAL:    return "shard internal error";
    }
    return "unknown";
}

/* Format a storage error that names the offending <namespace>/<key>, so
 * the skel's error log says WHICH key was missing/unwritable instead of a
 * bare "key not found". The buffer is thread-local and read synchronously
 * by the skel (log + wire-pack) right after this impl returns — before any
 * other coroutine runs on this thread — so there's no aliasing across the
 * cooperative yields. */
static const char *shard_err_key(enum shard_rc rc, const char *ns, const char *key)
{
    static _Thread_local char buf[320];
    snprintf(buf, sizeof(buf), "%s [%s/%s]", shard_rc_msg(rc),
             ns ? ns : "?", key ? key : "?");
    return buf;
}

/* ---- methods (same KV API as storage) ------------------------------ */

YAAFC_CLASS_ANNOTATE("override@sharded_storage:db:db_set")
struct yaafc_int_result sharded_storage_db_set_impl(struct ctx *ctx, struct object *obj,
                                                    struct yheaders *hdrs,
                                                    const char *context, const char *key,
                                                    const char *value)
{
    (void)ctx; (void)obj;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct shard_work w = {.op = OP_SET, .ns = context, .key = key, .value = value};
    double span_start = yaafc_ytime_monotonic_sec();
    shard_run(&w);
    double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=shard.set.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("shard.set", span_us);
    if (w.rc != SHARD_OK) return YAAFC_ERR(yaafc_int, shard_err_key(w.rc, context, key));
    return YAAFC_OK(yaafc_int, 1);
}

YAAFC_CLASS_ANNOTATE("override@sharded_storage:db:db_get")
struct yaafc_string_result sharded_storage_db_get_impl(struct ctx *ctx, struct object *obj,
                                                       struct yheaders *hdrs,
                                                       const char *context, const char *key)
{
    (void)ctx; (void)obj;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct shard_work w = {.op = OP_GET, .ns = context, .key = key};
    double span_start = yaafc_ytime_monotonic_sec();
    shard_run(&w);
    double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=shard.get.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("shard.get", span_us);
    if (w.rc != SHARD_OK) return YAAFC_ERR(yaafc_string, shard_err_key(w.rc, context, key));
    return YAAFC_OK(yaafc_string, w.out_str);
}

YAAFC_CLASS_ANNOTATE("override@sharded_storage:db:db_exists")
struct yaafc_int_result sharded_storage_db_exists_impl(struct ctx *ctx, struct object *obj,
                                                       struct yheaders *hdrs,
                                                       const char *context, const char *key)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct shard_work w = {.op = OP_EXISTS, .ns = context, .key = key};
    shard_run(&w);
    if (w.rc != SHARD_OK) return YAAFC_ERR(yaafc_int, shard_rc_msg(w.rc));
    return YAAFC_OK(yaafc_int, w.out_i);
}

YAAFC_CLASS_ANNOTATE("override@sharded_storage:db:db_del")
struct yaafc_int_result sharded_storage_db_del_impl(struct ctx *ctx, struct object *obj,
                                                    struct yheaders *hdrs,
                                                    const char *context, const char *key)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct shard_work w = {.op = OP_DEL, .ns = context, .key = key};
    shard_run(&w);
    if (w.rc != SHARD_OK) return YAAFC_ERR(yaafc_int, shard_rc_msg(w.rc));
    return YAAFC_OK(yaafc_int, w.out_i);
}

YAAFC_CLASS_ANNOTATE("override@sharded_storage:db:db_count")
struct yaafc_size_result sharded_storage_db_count_impl(struct ctx *ctx, struct object *obj,
                                                       struct yheaders *hdrs,
                                                       const char *context)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct shard_work w = {.op = OP_COUNT, .ns = context, .key = ""};
    shard_run(&w);
    if (w.rc != SHARD_OK) return YAAFC_ERR(yaafc_size, shard_rc_msg(w.rc));
    return YAAFC_OK(yaafc_size, w.out_sz);
}

#include "sharded_storage.gen.c"
