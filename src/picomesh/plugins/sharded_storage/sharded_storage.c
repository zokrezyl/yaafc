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
 * loop_run_blocking so the serving coroutine yields and the loop stays
 * responsive; shards are threads on the pool, NOT separate processes.
 *
 * Config (service block):
 *   sharded_storage.shards  = N        (optional, default 8, max 64)
 *   sharded_storage.path    = base dir (REQUIRED — no default; a missing
 *                             path fails loudly rather than silently writing
 *                             shards to a shared fallback location) */

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/core/yspan.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/yheaders.h>
#include <picomesh/engine/engine.h>
#include <picomesh/config/config.h>
#include <picomesh/loop/loop.h>
#include <picomesh/json/json.h>

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

struct PICOMESH_CLASS_ANNOTATE("class@sharded_storage:db") sharded_storage_data {
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

PICOMESH_RESULT_DECLARE(shard_set_ptr, struct shard_set *);

/* Lazy-open the N shard envs. NULL on failure. Same lazy-const-init
 * shape the storage backend / picoclass accessors use; after init the env
 * handles are immutable and libmdbx is internally multi-threaded. */
static struct shard_set_ptr_result shard_set(void)
{
    static struct shard_set s = {0};
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static int tried = 0;

    if (s.ready) return PICOMESH_OK(shard_set_ptr, &s);
    /* `tried` means a previous init attempt PERMANENTLY FAILED — not "in
     * progress". It is set only on a failure return below, never before the
     * (slow) env-open. A concurrent caller during init therefore finds
     * tried==0 here, blocks on the mutex, and re-checks `ready` after the
     * initializer unlocks — instead of spuriously getting NULL while the
     * envs are still opening (which surfaced as `shard open failed` under a
     * concurrent cold-start burst). */
    if (tried) return PICOMESH_ERR(shard_set_ptr, "sharded_storage: shard set previously failed to initialize");

    pthread_mutex_lock(&mu);
    if (s.ready) { pthread_mutex_unlock(&mu); return PICOMESH_OK(shard_set_ptr, &s); }
    if (tried)   { pthread_mutex_unlock(&mu); return PICOMESH_ERR(shard_set_ptr, "sharded_storage: shard set previously failed to initialize"); }

    /* The data directory is REQUIRED config — there is no hardcoded default.
     * A silent fallback would let a misconfigured node write its shards to a
     * shared/wrong path with nobody noticing (data in the wrong place, or
     * two nodes colliding on one dir). Fail loudly instead. */
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) {
        tried = 1; pthread_mutex_unlock(&mu);
        return PICOMESH_ERR(shard_set_ptr, "sharded_storage: no active engine — cannot resolve required config 'sharded_storage.path'");
    }
    /* The path has no default — a missing key is a real error. */
    struct config_node_ptr_result path_res =
        config_require(picomesh_engine_config(e), "sharded_storage.path");
    if (PICOMESH_IS_ERR(path_res)) {
        tried = 1; pthread_mutex_unlock(&mu);
        return PICOMESH_ERR(shard_set_ptr,
                            "sharded_storage: required config 'sharded_storage.path' is missing — refusing to fall back to a shared default",
                            path_res);
    }
    const char *base = config_node_as_string(path_res.value, NULL);
    if (!base || !*base) {
        tried = 1; pthread_mutex_unlock(&mu);
        return PICOMESH_ERR(shard_set_ptr, "sharded_storage: required config 'sharded_storage.path' is empty");
    }
    /* Shard count: optional tuning knob, documented default 8 — absence is not
     * an error, so the default-aware getter returns the value directly. */
    int n = (int)config_get_int(picomesh_engine_config(e), "sharded_storage.shards", 8);
    if (n < 1 || n > SHARDED_MAX_SHARDS) n = 8;

    if (mkdir(base, 0700) != 0 && errno != EEXIST) {
        ywarn("sharded_storage: mkdir(%s) failed: %s", base, strerror(errno));
        tried = 1;
        pthread_mutex_unlock(&mu);
        return PICOMESH_ERR(shard_set_ptr, "sharded_storage: mkdir of the base data directory failed");
    }
    for (int i = 0; i < n; ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s/shard-%d", base, i);
        if (mkdir(path, 0700) != 0 && errno != EEXIST) {
            ywarn("sharded_storage: mkdir(%s) failed: %s", path, strerror(errno));
            tried = 1;
            pthread_mutex_unlock(&mu);
            return PICOMESH_ERR(shard_set_ptr, "sharded_storage: mkdir of a shard directory failed");
        }
        MDBX_env *env = NULL;
        if (mdbx_env_create(&env) != MDBX_SUCCESS) {
            tried = 1; pthread_mutex_unlock(&mu);
            return PICOMESH_ERR(shard_set_ptr, "sharded_storage: mdbx_env_create failed");
        }
        mdbx_env_set_maxdbs(env, SHARDED_DBI_PER_SHARD);
        mdbx_env_set_geometry(env, 1 << 20, -1, 1 << 30, 1 << 20, -1, -1);
        /* Same durability as the storage mdbx backend (WRITEMAP +
         * NOMETASYNC: data fsync'd per commit, metadata deferred) so the
         * sharded-vs-single comparison isolates the parallelism win. */
        if (mdbx_env_open(env, path, MDBX_WRITEMAP | MDBX_NOMETASYNC, 0644) != MDBX_SUCCESS) {
            ywarn("sharded_storage: env_open(%s) failed", path);
            mdbx_env_close(env);
            tried = 1;
            pthread_mutex_unlock(&mu);
            return PICOMESH_ERR(shard_set_ptr, "sharded_storage: mdbx_env_open of a shard failed");
        }
        s.shards[i].env = env;
        pthread_mutex_init(&s.shards[i].dbi_mu, NULL);
    }
    s.n = n;
    s.ready = 1;
    ydebug("sharded_storage: opened %d shard envs at %s", n, base);
    pthread_mutex_unlock(&mu);
    return PICOMESH_OK(shard_set_ptr, &s);
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

enum shard_op {
    OP_SET, OP_GET, OP_EXISTS, OP_DEL, OP_COUNT,
    OP_INCR,          /* read int64 + delta + write, one txn → new value */
    OP_PUT_IF_ABSENT, /* insert only if key absent, one txn → inserted? */
    OP_CAS,           /* swap only if current bytes == expected, one txn */
    OP_LIST,          /* cursor-scan a namespace across all shards → JSON */
    OP_LIST_ALL_NS,   /* cursor-scan EVERY namespace in every shard → JSON */
};

struct shard_work {
    enum shard_op op;
    const char *ns;
    const char *key;
    const char *value;    /* in:  set / put_if_absent / cas-replacement */
    const char *expected; /* in:  cas — exact current bytes to match */
    int64_t in_delta;     /* in:  incr */
    int64_t in_offset;    /* in:  list — skip this many matches first */
    int64_t in_limit;     /* in:  list — stop after this many (< 0 == unbounded) */
    char *out_str;        /* out: get — heap, NUL-terminated, owned by caller */
    int out_i;            /* out: exists/del/put_if_absent/cas */
    int64_t out_i64;      /* out: incr — value after the add */
    size_t out_sz;        /* out: count */
    enum shard_rc rc;
};

/* Runs on a worker-pool thread — touches only `arg` + the shard env it
 * routes to. Different shards → different envs → parallel commits. */
PICOMESH_EXTERNAL_CALLBACK
static void shard_work_fn(void *arg)
{
    struct shard_work *w = arg;
    /* Worker-pool thread start routine (fixed void signature). A shard_set
     * init failure has no caller to propagate to — render the chain to the log
     * and flag the op via w->rc (the calling _impl turns rc into its Result). */
    struct shard_set_ptr_result set_res = shard_set();
    if (PICOMESH_IS_ERR(set_res)) {
        picomesh_error_print(stderr, "sharded_storage: shard_set", set_res.error);
        picomesh_error_destroy(set_res.error);
        w->rc = SHARD_OPEN_FAILED;
        return;
    }
    struct shard_set *s = set_res.value;

    if (w->op == OP_LIST_ALL_NS) {
        /* List EVERY key in EVERY namespace, across all shards. Each shard's
         * unnamed (main) DBI holds the names of its named sub-DBs (one per
         * namespace), so we cursor the main DBI to enumerate namespaces, then
         * scan each. Entries carry the namespace so the caller knows where a
         * key lives. No namespace argument is required (and none is valid). */
        const char *prefix = w->key ? w->key : "";
        size_t plen = strlen(prefix);
        int64_t limit = w->in_limit; /* < 0 == unbounded */
        int64_t emitted = 0;
        struct json_writer *jw = json_writer_new();
        if (!jw) { w->rc = SHARD_INTERNAL; return; }
        json_writer_begin_array(jw);
        for (int i = 0; i < s->n && (limit < 0 || emitted < limit); ++i) {
            MDBX_txn *txn = NULL;
            if (mdbx_txn_begin(s->shards[i].env, NULL, MDBX_TXN_RDONLY, &txn) != MDBX_SUCCESS)
                continue;
            /* Collect this shard's namespace names first (cursoring the main
             * DBI while opening sub-DBIs in the same txn is best avoided). */
            char namespaces[SHARDED_DBI_PER_SHARD][64];
            int ns_count = 0;
            MDBX_dbi main_dbi = 0;
            MDBX_cursor *ns_cur = NULL;
            if (mdbx_dbi_open(txn, NULL, 0, &main_dbi) == MDBX_SUCCESS &&
                mdbx_cursor_open(txn, main_dbi, &ns_cur) == MDBX_SUCCESS) {
                MDBX_val nk, nv;
                int ncr = mdbx_cursor_get(ns_cur, &nk, &nv, MDBX_FIRST);
                while (ncr == MDBX_SUCCESS && ns_count < SHARDED_DBI_PER_SHARD) {
                    size_t nlen = nk.iov_len < sizeof(namespaces[0]) - 1 ? nk.iov_len : sizeof(namespaces[0]) - 1;
                    memcpy(namespaces[ns_count], nk.iov_base, nlen);
                    namespaces[ns_count][nlen] = 0;
                    ns_count++;
                    ncr = mdbx_cursor_get(ns_cur, &nk, &nv, MDBX_NEXT);
                }
            }
            if (ns_cur) mdbx_cursor_close(ns_cur);

            for (int n = 0; n < ns_count && (limit < 0 || emitted < limit); ++n) {
                MDBX_dbi dbi = 0;
                MDBX_cursor *cur = NULL;
                if (mdbx_dbi_open(txn, namespaces[n], 0, &dbi) != MDBX_SUCCESS ||
                    mdbx_cursor_open(txn, dbi, &cur) != MDBX_SUCCESS) {
                    if (cur) mdbx_cursor_close(cur);
                    continue;
                }
                MDBX_val k, v;
                int cr = mdbx_cursor_get(cur, &k, &v, MDBX_FIRST);
                while (cr == MDBX_SUCCESS && (limit < 0 || emitted < limit)) {
                    if (plen && (k.iov_len < plen || memcmp(k.iov_base, prefix, plen) != 0)) {
                        cr = mdbx_cursor_get(cur, &k, &v, MDBX_NEXT);
                        continue;
                    }
                    char kbuf[256];
                    size_t klen = k.iov_len < sizeof(kbuf) - 1 ? k.iov_len : sizeof(kbuf) - 1;
                    memcpy(kbuf, k.iov_base, klen); kbuf[klen] = 0;
                    char *vbuf = malloc(v.iov_len + 1);
                    if (vbuf) { memcpy(vbuf, v.iov_base, v.iov_len); vbuf[v.iov_len] = 0; }
                    json_writer_begin_object(jw);
                    json_writer_key(jw, "namespace"); json_writer_string(jw, namespaces[n]);
                    json_writer_key(jw, "key");       json_writer_string(jw, kbuf);
                    json_writer_key(jw, "value");     json_writer_string(jw, vbuf ? vbuf : "");
                    json_writer_end_object(jw);
                    free(vbuf);
                    ++emitted;
                    cr = mdbx_cursor_get(cur, &k, &v, MDBX_NEXT);
                }
                mdbx_cursor_close(cur);
            }
            mdbx_txn_abort(txn);
        }
        json_writer_end_array(jw);
        size_t jlen = 0;
        const char *jdata = json_writer_data(jw, &jlen);
        w->out_str = strdup(jdata ? jdata : "[]");
        json_writer_free(jw);
        w->rc = w->out_str ? SHARD_OK : SHARD_INTERNAL;
        return;
    }

    if (!ns_valid(w->ns)) { w->rc = SHARD_BAD_CONTEXT; return; }

    if (w->op == OP_COUNT) {
        /* Sum dbi_stat across all shards for this namespace (read-only;
         * shards never written for this ns simply report 0). */
        size_t total = 0;
        for (int i = 0; i < s->n; ++i) {
            MDBX_txn *txn = NULL;
            if (mdbx_txn_begin(s->shards[i].env, NULL, MDBX_TXN_RDONLY, &txn) != MDBX_SUCCESS) {
                /* A shard we cannot read makes the aggregate count unreliable —
                 * fail rather than report a silently-low total. */
                w->rc = SHARD_INTERNAL;
                return;
            }
            MDBX_dbi dbi = 0;
            int dbi_open_rc = mdbx_dbi_open(txn, w->ns, 0, &dbi);
            if (dbi_open_rc == MDBX_SUCCESS) {
                MDBX_stat dbi_stat;
                if (mdbx_dbi_stat(txn, dbi, &dbi_stat, sizeof(dbi_stat)) != MDBX_SUCCESS) {
                    mdbx_txn_abort(txn);
                    w->rc = SHARD_INTERNAL;
                    return;
                }
                total += (size_t)dbi_stat.ms_entries;
            } else if (dbi_open_rc != MDBX_NOTFOUND) {
                /* NOTFOUND == this shard simply has no rows for the namespace
                 * (counts as 0); any other open error is a real failure. */
                mdbx_txn_abort(txn);
                w->rc = SHARD_INTERNAL;
                return;
            }
            mdbx_txn_abort(txn);
        }
        w->out_sz = total;
        w->rc = SHARD_OK;
        return;
    }

    if (w->op == OP_LIST) {
        /* A namespace's keys are spread across every shard, so list = scan
         * each shard's ns DBI with a cursor and concatenate. Build a JSON
         * array of {"key":…,"value":…} directly (the values are opaque
         * bytes; we expose them as strings — that's how they were stored). */
        /* Optional key-prefix filter (w->key): callers pass their object
         * prefix (e.g. "repo:") to list only objects, skipping the
         * namespace's bookkeeping keys (count, next_id, indexes). Empty
         * prefix lists every entry. */
        const char *prefix = w->key ? w->key : "";
        size_t plen = strlen(prefix);
        int64_t skip = w->in_offset > 0 ? w->in_offset : 0; /* matches still to skip */
        int64_t limit = w->in_limit;                        /* < 0 == unbounded */
        int64_t emitted = 0;
        struct json_writer *jw = json_writer_new();
        if (!jw) { w->rc = SHARD_INTERNAL; return; }
        json_writer_begin_array(jw);
        for (int i = 0; i < s->n && (limit < 0 || emitted < limit); ++i) {
            MDBX_txn *txn = NULL;
            if (mdbx_txn_begin(s->shards[i].env, NULL, MDBX_TXN_RDONLY, &txn) != MDBX_SUCCESS) {
                /* A shard we cannot read would silently truncate the list —
                 * fail rather than return a partial result as authoritative. */
                json_writer_free(jw);
                w->rc = SHARD_INTERNAL;
                return;
            }
            MDBX_dbi dbi = 0;
            MDBX_cursor *cur = NULL;
            int dbi_open_rc = mdbx_dbi_open(txn, w->ns, 0, &dbi);
            if (dbi_open_rc != MDBX_SUCCESS && dbi_open_rc != MDBX_NOTFOUND) {
                /* NOTFOUND == no rows for this namespace on this shard (skip);
                 * any other open error is a real backend failure. */
                mdbx_txn_abort(txn);
                json_writer_free(jw);
                w->rc = SHARD_INTERNAL;
                return;
            }
            if (dbi_open_rc == MDBX_SUCCESS && mdbx_cursor_open(txn, dbi, &cur) != MDBX_SUCCESS) {
                mdbx_txn_abort(txn);
                json_writer_free(jw);
                w->rc = SHARD_INTERNAL;
                return;
            }
            if (cur) {
                MDBX_val k, v;
                int cr = mdbx_cursor_get(cur, &k, &v, MDBX_FIRST);
                while (cr == MDBX_SUCCESS && (limit < 0 || emitted < limit)) {
                    if (plen && (k.iov_len < plen || memcmp(k.iov_base, prefix, plen) != 0)) {
                        cr = mdbx_cursor_get(cur, &k, &v, MDBX_NEXT);
                        continue;
                    }
                    if (skip > 0) { /* a match, but inside the offset window */
                        --skip;
                        cr = mdbx_cursor_get(cur, &k, &v, MDBX_NEXT);
                        continue;
                    }
                    char kbuf[256];
                    size_t klen = k.iov_len < sizeof(kbuf) - 1 ? k.iov_len : sizeof(kbuf) - 1;
                    memcpy(kbuf, k.iov_base, klen); kbuf[klen] = 0;
                    char *vbuf = malloc(v.iov_len + 1);
                    if (vbuf) { memcpy(vbuf, v.iov_base, v.iov_len); vbuf[v.iov_len] = 0; }
                    json_writer_begin_object(jw);
                    json_writer_key(jw, "key");   json_writer_string(jw, kbuf);
                    json_writer_key(jw, "value"); json_writer_string(jw, vbuf ? vbuf : "");
                    json_writer_end_object(jw);
                    free(vbuf);
                    ++emitted;
                    cr = mdbx_cursor_get(cur, &k, &v, MDBX_NEXT);
                }
            }
            if (cur) mdbx_cursor_close(cur);
            mdbx_txn_abort(txn);
        }
        json_writer_end_array(jw);
        size_t jlen = 0;
        const char *jdata = json_writer_data(jw, &jlen);
        w->out_str = strdup(jdata ? jdata : "[]");
        json_writer_free(jw);
        w->rc = w->out_str ? SHARD_OK : SHARD_INTERNAL;
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
        MDBX_val found_value = {0};
        int get_rc = mdbx_get(txn, dbi, &k, &found_value);
        if (get_rc == MDBX_SUCCESS) {
            w->out_i = 1;
            w->rc = SHARD_OK;
        } else if (get_rc == MDBX_NOTFOUND) {
            w->out_i = 0;
            w->rc = SHARD_OK;
        } else {
            /* Corruption / I/O / txn errors must NOT masquerade as "key absent":
             * a false negative here would silently break dedupe, idempotency,
             * and authorization decisions. */
            w->rc = SHARD_INTERNAL;
        }
        mdbx_txn_abort(txn);
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
    case OP_INCR: {
        /* read-add-write inside ONE write txn. MDBX serializes write txns
         * per env, and a given (ns,key) always routes to this one shard, so
         * the whole RMW is atomic against every other writer — no lost
         * updates, no duplicate monotonic ids under concurrency. */
        MDBX_val v = {0};
        int64_t cur = 0;
        int r = mdbx_get(txn, dbi, &k, &v);
        if (r == MDBX_SUCCESS) {
            char tmp[32];
            size_t n = v.iov_len < sizeof(tmp) - 1 ? v.iov_len : sizeof(tmp) - 1;
            memcpy(tmp, v.iov_base, n);
            tmp[n] = 0;
            cur = strtoll(tmp, NULL, 10);
        } else if (r != MDBX_NOTFOUND) {
            mdbx_txn_abort(txn); w->rc = SHARD_INTERNAL; return;
        }
        int64_t next = cur + w->in_delta;
        char nbuf[32];
        int nlen = snprintf(nbuf, sizeof(nbuf), "%lld", (long long)next);
        MDBX_val nv = {.iov_base = nbuf, .iov_len = (size_t)nlen};
        if (mdbx_put(txn, dbi, &k, &nv, MDBX_UPSERT) != MDBX_SUCCESS ||
            mdbx_txn_commit(txn) != MDBX_SUCCESS) {
            mdbx_txn_abort(txn); w->rc = SHARD_INTERNAL; return;
        }
        w->out_i64 = next;
        w->rc = SHARD_OK;
        return;
    }
    case OP_PUT_IF_ABSENT: {
        /* MDBX_NOOVERWRITE makes the insert-or-fail atomic: KEYEXIST when
         * the key is already present, SUCCESS when this txn created it. */
        MDBX_val v = {.iov_base = (void *)w->value,
                      .iov_len = w->value ? strlen(w->value) : 0};
        int r = mdbx_put(txn, dbi, &k, &v, MDBX_NOOVERWRITE);
        if (r == MDBX_SUCCESS) {
            if (mdbx_txn_commit(txn) != MDBX_SUCCESS) { w->rc = SHARD_INTERNAL; return; }
            w->out_i = 1;  /* inserted */
            w->rc = SHARD_OK;
        } else if (r == MDBX_KEYEXIST) {
            mdbx_txn_abort(txn);
            w->out_i = 0;  /* already existed */
            w->rc = SHARD_OK;
        } else {
            mdbx_txn_abort(txn);
            w->rc = SHARD_INTERNAL;
        }
        return;
    }
    case OP_CAS: {
        /* Optimistic swap: replace only if the stored bytes still equal
         * `expected`. Lets a caller read a record, then commit a mutation
         * guaranteed not to clobber a concurrent change (a pipeline runner
         * leasing a job exactly once, an owner-index append-without-loss).
         * An EMPTY `expected` matches an absent or empty key, so the same
         * primitive both creates the first value and updates an existing
         * one. */
        MDBX_val v = {0};
        int r = mdbx_get(txn, dbi, &k, &v);
        if (r != MDBX_SUCCESS && r != MDBX_NOTFOUND) {
            mdbx_txn_abort(txn); w->rc = SHARD_INTERNAL; return;
        }
        size_t elen = w->expected ? strlen(w->expected) : 0;
        int cur_empty = (r == MDBX_NOTFOUND) || (v.iov_len == 0);
        int match = (elen == 0)
                    ? cur_empty
                    : (r == MDBX_SUCCESS && v.iov_len == elen &&
                       memcmp(v.iov_base, w->expected, elen) == 0);
        if (!match) {
            mdbx_txn_abort(txn);
            w->out_i = 0;  /* not swapped */
            w->rc = SHARD_OK;
            return;
        }
        MDBX_val nv = {.iov_base = (void *)w->value,
                       .iov_len = w->value ? strlen(w->value) : 0};
        if (mdbx_put(txn, dbi, &k, &nv, MDBX_UPSERT) != MDBX_SUCCESS ||
            mdbx_txn_commit(txn) != MDBX_SUCCESS) {
            mdbx_txn_abort(txn); w->rc = SHARD_INTERNAL; return;
        }
        w->out_i = 1;  /* swapped */
        w->rc = SHARD_OK;
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
static struct picomesh_void_result shard_run(struct shard_work *w)
{
    struct picomesh_engine *e = picomesh_active_engine();
    struct loop *l = e ? picomesh_engine_loop(e) : NULL;
    if (!l) { shard_work_fn(w); return PICOMESH_OK_VOID(); }
    struct picomesh_void_result r = loop_run_blocking(l, shard_work_fn, w);
    if (PICOMESH_IS_ERR(r)) {
        /* Offload failed — fall back to running inline so the op still
         * completes, but make the failure visible: it can signal a degraded
         * event loop / worker pool, not just a transient hiccup. The op's own
         * outcome flows via w->rc, so this recovered error is logged, not
         * propagated. */
        picomesh_error_print(stderr, "sharded_storage: worker-pool offload failed — running inline", r.error);
        picomesh_error_destroy(r.error);
        shard_work_fn(w);
    }
    return PICOMESH_OK_VOID();
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

PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_set")
struct picomesh_int_result sharded_storage_db_set_impl(struct ctx *ctx, struct object *obj,
                                                    struct yheaders *hdrs,
                                                    const char *context, const char *key,
                                                    const char *value)
{
    (void)ctx; (void)obj;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct shard_work w = {.op = OP_SET, .ns = context, .key = key, .value = value};
    double span_start = picomesh_ytime_monotonic_sec();
    PICOMESH_RETURN_IF_ERR(picomesh_int, shard_run(&w), "sharded_storage: dispatch failed");
    double span_us = (picomesh_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=shard.set.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("shard.set", span_us);
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_int, shard_err_key(w.rc, context, key));
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_get")
struct picomesh_string_result sharded_storage_db_get_impl(struct ctx *ctx, struct object *obj,
                                                       struct yheaders *hdrs,
                                                       const char *context, const char *key)
{
    (void)ctx; (void)obj;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct shard_work w = {.op = OP_GET, .ns = context, .key = key};
    double span_start = picomesh_ytime_monotonic_sec();
    PICOMESH_RETURN_IF_ERR(picomesh_string, shard_run(&w), "sharded_storage: dispatch failed");
    double span_us = (picomesh_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=shard.get.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("shard.get", span_us);
    if (w.rc == SHARD_NOT_FOUND) {
        /* Not-found is a NORMAL result, DISTINCT from a backend failure.
         * Return an empty string on success so a caller can tell "absent"
         * (value[0]==0) from a real storage error (IS_ERR) — instead of the
         * old behaviour where both came back as an error and callers
         * silently treated every failure as "missing". A genuinely
         * empty-valued key is indistinguishable from absent here; callers
         * that must tell those apart use db_exists. */
        char *empty = calloc(1, 1);
        if (!empty) return PICOMESH_ERR(picomesh_string, "db_get: out of memory");
        return PICOMESH_OK(picomesh_string, empty);
    }
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_string, shard_err_key(w.rc, context, key));
    return PICOMESH_OK(picomesh_string, w.out_str);
}

PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_exists")
struct picomesh_int_result sharded_storage_db_exists_impl(struct ctx *ctx, struct object *obj,
                                                       struct yheaders *hdrs,
                                                       const char *context, const char *key)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct shard_work w = {.op = OP_EXISTS, .ns = context, .key = key};
    PICOMESH_RETURN_IF_ERR(picomesh_int, shard_run(&w), "sharded_storage: dispatch failed");
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_int, shard_rc_msg(w.rc));
    return PICOMESH_OK(picomesh_int, w.out_i);
}

PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_del")
struct picomesh_int_result sharded_storage_db_del_impl(struct ctx *ctx, struct object *obj,
                                                    struct yheaders *hdrs,
                                                    const char *context, const char *key)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct shard_work w = {.op = OP_DEL, .ns = context, .key = key};
    PICOMESH_RETURN_IF_ERR(picomesh_int, shard_run(&w), "sharded_storage: dispatch failed");
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_int, shard_rc_msg(w.rc));
    return PICOMESH_OK(picomesh_int, w.out_i);
}

PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_count")
struct picomesh_size_result sharded_storage_db_count_impl(struct ctx *ctx, struct object *obj,
                                                       struct yheaders *hdrs,
                                                       const char *context)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct shard_work w = {.op = OP_COUNT, .ns = context, .key = ""};
    PICOMESH_RETURN_IF_ERR(picomesh_size, shard_run(&w), "sharded_storage: dispatch failed");
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_size, shard_rc_msg(w.rc));
    return PICOMESH_OK(picomesh_size, w.out_sz);
}

/* The generic "show objects" surface the service console renders as a table.
 * Each backend exposes its own `list`/`list_all` that delegate here with
 * their namespace + object prefix.
 *
 * Default page size when a caller passes limit <= 0 — keeps `list` bounded
 * so it can never dump a whole (potentially huge) namespace in one call. */
#define SHARDED_LIST_DEFAULT_LIMIT 100

/* Paginated: return at most `limit` entries (defaulted/capped) starting at
 * `offset`. The unbounded scan lives in db_list_all. */
PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_list")
struct picomesh_json_result sharded_storage_db_list_impl(struct ctx *ctx, struct object *obj,
                                                         struct yheaders *hdrs,
                                                         const char *context, const char *prefix,
                                                         int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj; (void)hdrs;
    if (limit <= 0) limit = SHARDED_LIST_DEFAULT_LIMIT;
    struct shard_work w = {.op = OP_LIST, .ns = context, .key = prefix ? prefix : "",
                           .in_offset = offset, .in_limit = limit};
    PICOMESH_RETURN_IF_ERR(picomesh_json, shard_run(&w), "sharded_storage: dispatch failed");
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_json, shard_rc_msg(w.rc));
    return PICOMESH_OK(picomesh_json, w.out_str ? w.out_str : strdup("[]"));
}

/* Unbounded list. With a namespace: every matching entry in that namespace
 * (`[{"key","value"}]`). With NO namespace (empty/NULL context): every entry in
 * EVERY namespace, across all shards (`[{"namespace","key","value"}]`) — this is
 * what a generic console's "list all" means. Use with care: returns the whole
 * set in one response. */
PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_list_all")
struct picomesh_json_result sharded_storage_db_list_all_impl(struct ctx *ctx, struct object *obj,
                                                             struct yheaders *hdrs,
                                                             const char *context, const char *prefix)
{
    (void)ctx; (void)obj; (void)hdrs;
    int all_namespaces = !context || !*context;
    struct shard_work w = {.op = all_namespaces ? OP_LIST_ALL_NS : OP_LIST,
                           .ns = context, .key = prefix ? prefix : "",
                           .in_offset = 0, .in_limit = -1};
    PICOMESH_RETURN_IF_ERR(picomesh_json, shard_run(&w), "sharded_storage: dispatch failed");
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_json, shard_rc_msg(w.rc));
    return PICOMESH_OK(picomesh_json, w.out_str ? w.out_str : strdup("[]"));
}

/* Atomic read-add-write of a decimal-int64 counter; returns the value
 * AFTER the add. The only safe way to allocate monotonic ids or bump
 * shared counters once the gateway/backends run multiple workers — the
 * old get→+1→set pair over two RPCs raced and lost updates / duplicated
 * ids. A missing key counts as 0. */
PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_incr")
struct picomesh_int64_result sharded_storage_db_incr_impl(struct ctx *ctx, struct object *obj,
                                                       struct yheaders *hdrs,
                                                       const char *context, const char *key,
                                                       int64_t delta)
{
    (void)ctx; (void)obj;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;
    struct shard_work w = {.op = OP_INCR, .ns = context, .key = key, .in_delta = delta};
    double span_start = picomesh_ytime_monotonic_sec();
    PICOMESH_RETURN_IF_ERR(picomesh_int64, shard_run(&w), "sharded_storage: dispatch failed");
    double span_us = (picomesh_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%s op=shard.incr.%s dur_us=%.0f", trace ? trace : "-", context, span_us);
    yspan_record("shard.incr", span_us);
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_int64, shard_err_key(w.rc, context, key));
    return PICOMESH_OK(picomesh_int64, w.out_i64);
}

/* Insert (context,key)=value only if the key is absent. Returns 1 when
 * this call created it, 0 when it already existed. Atomic — the basis for
 * unique records like `repo:<rid>` where two concurrent creates must not
 * both believe they won. */
PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_put_if_absent")
struct picomesh_int_result sharded_storage_db_put_if_absent_impl(struct ctx *ctx, struct object *obj,
                                                              struct yheaders *hdrs,
                                                              const char *context, const char *key,
                                                              const char *value)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct shard_work w = {.op = OP_PUT_IF_ABSENT, .ns = context, .key = key, .value = value};
    PICOMESH_RETURN_IF_ERR(picomesh_int, shard_run(&w), "sharded_storage: dispatch failed");
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_int, shard_err_key(w.rc, context, key));
    return PICOMESH_OK(picomesh_int, w.out_i);
}

/* Compare-and-set: replace the value only if the stored bytes still equal
 * `expected`. Returns 1 when swapped, 0 when the current value differed (or
 * the key was absent). Optimistic concurrency for read-decide-write flows
 * such as leasing a pipeline job exactly once. */
PICOMESH_CLASS_ANNOTATE("override@sharded_storage:db:db_compare_and_set")
struct picomesh_int_result sharded_storage_db_compare_and_set_impl(struct ctx *ctx, struct object *obj,
                                                                struct yheaders *hdrs,
                                                                const char *context, const char *key,
                                                                const char *expected,
                                                                const char *replacement)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct shard_work w = {.op = OP_CAS, .ns = context, .key = key,
                           .expected = expected, .value = replacement};
    PICOMESH_RETURN_IF_ERR(picomesh_int, shard_run(&w), "sharded_storage: dispatch failed");
    if (w.rc != SHARD_OK) return PICOMESH_ERR(picomesh_int, shard_err_key(w.rc, context, key));
    return PICOMESH_OK(picomesh_int, w.out_i);
}

#include "sharded_storage.gen.c"
