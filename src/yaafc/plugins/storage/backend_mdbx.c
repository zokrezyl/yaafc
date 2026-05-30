/* libmdbx backend for the storage plugin.
 *
 * Shape: one shared MDBX_env per process (path from yconfig
 * `storage.mdbx_path`, default a per-service directory under
 * /tmp), one named MDBX_dbi per logical context. The env is opened
 * lazily on first use and held for the lifetime of the process.
 *
 * Each storage object cheap-references the shared env. DBIs are
 * opened lazily on first reference to a context name, via a short-
 * lived write txn — the libmdbx way to register a sub-DB.
 *
 * Note on shared state: the rule against file-scope mutable state has
 * an established exception for "lazy const init" patterns (the same
 * shape the yclass codegen uses for class accessor caches). The MDBX
 * env is opened-once and treated as an immutable handle for the rest
 * of the process. Same for the DBI cache — once a DBI handle is
 * registered it stays valid for the env's lifetime. */

#include "backends.h"

#include <yaafc/ycore/ytrace.h>
#include <yaafc/yengine/engine.h>
#include <yaafc/yconfig/yconfig.h>

#include <mdbx.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Small cache of (name → dbi) pairs. 64 contexts is generous for the
 * yaafc plugin set (accounts, session, password_authn, …); raise if
 * a deployment hits the limit. */
#define MDBX_DBI_CACHE_MAX 64

struct mdbx_dbi_entry {
    char name[64];
    MDBX_dbi dbi;
    int used;
};

/* Returns the shared env, opening it lazily on first call. NULL on
 * failure (the per-call ops surface that as STORAGE_RC_OPEN_FAILED).
 *
 * Thread-safety: we serialize the open path with a pthread_once-style
 * guard. After init, the env handle is read-only and can be shared
 * freely (libmdbx is internally multi-threaded). */
static MDBX_env *mdbx_shared_env(void)
{
    static MDBX_env *env = NULL;
    static pthread_mutex_t init_mu = PTHREAD_MUTEX_INITIALIZER;
    static int tried = 0;

    if (env || tried) return env;

    pthread_mutex_lock(&init_mu);
    if (env || tried) { pthread_mutex_unlock(&init_mu); return env; }

    const char *path = NULL;
    struct yaafc_engine *e = yaafc_active_engine();
    if (e) {
        struct yconfig_node_ptr_result r =
            yconfig_get(yaafc_engine_config(e), "storage.mdbx_path");
        if (YAAFC_IS_OK(r) && r.value) {
            const char *s = yconfig_node_as_string(r.value, NULL);
            if (s && *s) path = s;
        }
    }
    if (!path) path = "/tmp/yaafc-storage.mdbx";

    if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        ywarn("storage[mdbx]: mkdir(%s) failed: %s", path, strerror(errno));
        tried = 1;
        pthread_mutex_unlock(&init_mu);
        return NULL;
    }

    MDBX_env *new_env = NULL;
    int rc = mdbx_env_create(&new_env);
    if (rc != MDBX_SUCCESS) {
        ywarn("storage[mdbx]: env_create failed: %s", mdbx_strerror(rc));
        tried = 1;
        pthread_mutex_unlock(&init_mu);
        return NULL;
    }
    rc = mdbx_env_set_maxdbs(new_env, MDBX_DBI_CACHE_MAX);
    if (rc != MDBX_SUCCESS) {
        ywarn("storage[mdbx]: set_maxdbs failed: %s", mdbx_strerror(rc));
        mdbx_env_close(new_env);
        tried = 1;
        pthread_mutex_unlock(&init_mu);
        return NULL;
    }
    /* Geometry: start at 1 MiB, grow to 1 GiB by 1 MiB increments, page
     * size left at the platform default. Good middle ground for the
     * tiny-row KV traffic the storage plugin sees. */
    rc = mdbx_env_set_geometry(new_env,
                               /*size_lower*/   1 << 20,
                               /*size_now*/     -1,
                               /*size_upper*/   1 << 30,
                               /*growth_step*/  1 << 20,
                               /*shrink_thr*/   -1,
                               /*pagesize*/     -1);
    if (rc != MDBX_SUCCESS) {
        ywarn("storage[mdbx]: set_geometry failed: %s", mdbx_strerror(rc));
        mdbx_env_close(new_env);
        tried = 1;
        pthread_mutex_unlock(&init_mu);
        return NULL;
    }
    rc = mdbx_env_open(new_env, path,
                       MDBX_WRITEMAP | MDBX_NOMETASYNC, 0644);
    if (rc != MDBX_SUCCESS) {
        ywarn("storage[mdbx]: env_open(%s) failed: %s", path, mdbx_strerror(rc));
        if (new_env) mdbx_env_close(new_env);
        tried = 1;
        pthread_mutex_unlock(&init_mu);
        return NULL;
    }
    env = new_env;
    tried = 1;
    ydebug("storage[mdbx]: env opened at %s", path);
    pthread_mutex_unlock(&init_mu);
    return env;
}

/* DBI cache. Serialized with its own mutex; cache entries grow monotonically
 * (DBIs are never closed before process exit). */
static MDBX_dbi mdbx_resolve_dbi(MDBX_env *env, const char *context,
                                 enum storage_rc *out_rc)
{
    static struct mdbx_dbi_entry cache[MDBX_DBI_CACHE_MAX];
    static pthread_mutex_t cache_mu = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&cache_mu);
    for (size_t i = 0; i < MDBX_DBI_CACHE_MAX; ++i) {
        if (cache[i].used && strcmp(cache[i].name, context) == 0) {
            MDBX_dbi r = cache[i].dbi;
            pthread_mutex_unlock(&cache_mu);
            *out_rc = STORAGE_RC_OK;
            return r;
        }
    }
    /* Need to open it. A short write txn registers the named sub-DB. */
    MDBX_txn *txn = NULL;
    int rc = mdbx_txn_begin(env, NULL, MDBX_TXN_READWRITE, &txn);
    if (rc != MDBX_SUCCESS) {
        ywarn("storage[mdbx]: txn_begin (dbi-open) failed: %s",
              mdbx_strerror(rc));
        pthread_mutex_unlock(&cache_mu);
        *out_rc = STORAGE_RC_INTERNAL;
        return 0;
    }
    MDBX_dbi dbi = 0;
    rc = mdbx_dbi_open(txn, context, MDBX_CREATE, &dbi);
    if (rc != MDBX_SUCCESS) {
        ywarn("storage[mdbx]: dbi_open(%s) failed: %s",
              context, mdbx_strerror(rc));
        mdbx_txn_abort(txn);
        pthread_mutex_unlock(&cache_mu);
        *out_rc = STORAGE_RC_INTERNAL;
        return 0;
    }
    rc = mdbx_txn_commit(txn);
    if (rc != MDBX_SUCCESS) {
        ywarn("storage[mdbx]: txn_commit (dbi-open) failed: %s",
              mdbx_strerror(rc));
        pthread_mutex_unlock(&cache_mu);
        *out_rc = STORAGE_RC_INTERNAL;
        return 0;
    }
    for (size_t i = 0; i < MDBX_DBI_CACHE_MAX; ++i) {
        if (!cache[i].used) {
            strncpy(cache[i].name, context, sizeof(cache[i].name) - 1);
            cache[i].dbi = dbi;
            cache[i].used = 1;
            pthread_mutex_unlock(&cache_mu);
            *out_rc = STORAGE_RC_OK;
            return dbi;
        }
    }
    /* Cache full — return the dbi anyway, just don't cache it; caller
     * will re-open next time. */
    pthread_mutex_unlock(&cache_mu);
    *out_rc = STORAGE_RC_OK;
    return dbi;
}

static enum storage_rc bmdbx_setup(struct storage_data *d, const char *context,
                                   MDBX_env **out_env, MDBX_dbi *out_dbi)
{
    if (!storage_context_is_valid(context)) return STORAGE_RC_BAD_CONTEXT;
    MDBX_env *env = mdbx_shared_env();
    if (!env) return STORAGE_RC_OPEN_FAILED;
    d->be.mdbx.opened = 1;
    enum storage_rc rc = STORAGE_RC_OK;
    MDBX_dbi dbi = mdbx_resolve_dbi(env, context, &rc);
    if (rc != STORAGE_RC_OK) return rc;
    *out_env = env;
    *out_dbi = dbi;
    return STORAGE_RC_OK;
}

static enum storage_rc bmdbx_set(struct storage_data *d, const char *context,
                                 const char *key, const char *value)
{
    MDBX_env *env;
    MDBX_dbi dbi;
    enum storage_rc rc = bmdbx_setup(d, context, &env, &dbi);
    if (rc != STORAGE_RC_OK) return rc;
    MDBX_txn *txn = NULL;
    if (mdbx_txn_begin(env, NULL, MDBX_TXN_READWRITE, &txn) != MDBX_SUCCESS)
        return STORAGE_RC_INTERNAL;
    MDBX_val k = {.iov_base = (void *)key, .iov_len = strlen(key)};
    MDBX_val v = {.iov_base = (void *)(value ? value : ""),
                  .iov_len = value ? strlen(value) : 0};
    int r = mdbx_put(txn, dbi, &k, &v, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        mdbx_txn_abort(txn);
        ywarn("storage[mdbx]: put(%s/%s) failed: %s",
              context, key, mdbx_strerror(r));
        return STORAGE_RC_INTERNAL;
    }
    if (mdbx_txn_commit(txn) != MDBX_SUCCESS) return STORAGE_RC_INTERNAL;
    return STORAGE_RC_OK;
}

static enum storage_rc bmdbx_get(struct storage_data *d, const char *context,
                                 const char *key, char **out)
{
    MDBX_env *env;
    MDBX_dbi dbi;
    enum storage_rc rc = bmdbx_setup(d, context, &env, &dbi);
    if (rc != STORAGE_RC_OK) return rc;
    MDBX_txn *txn = NULL;
    if (mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &txn) != MDBX_SUCCESS)
        return STORAGE_RC_INTERNAL;
    MDBX_val k = {.iov_base = (void *)key, .iov_len = strlen(key)};
    MDBX_val v = {0};
    int r = mdbx_get(txn, dbi, &k, &v);
    enum storage_rc result;
    if (r == MDBX_SUCCESS) {
        char *copy = malloc(v.iov_len + 1);
        if (!copy) {
            result = STORAGE_RC_INTERNAL;
        } else {
            if (v.iov_len) memcpy(copy, v.iov_base, v.iov_len);
            copy[v.iov_len] = 0;
            *out = copy;
            result = STORAGE_RC_OK;
        }
    } else if (r == MDBX_NOTFOUND) {
        result = STORAGE_RC_NOT_FOUND;
    } else {
        ywarn("storage[mdbx]: get(%s/%s) failed: %s",
              context, key, mdbx_strerror(r));
        result = STORAGE_RC_INTERNAL;
    }
    mdbx_txn_abort(txn);
    return result;
}

static enum storage_rc bmdbx_exists(struct storage_data *d, const char *context,
                                    const char *key, int *out)
{
    MDBX_env *env;
    MDBX_dbi dbi;
    enum storage_rc rc = bmdbx_setup(d, context, &env, &dbi);
    if (rc != STORAGE_RC_OK) return rc;
    MDBX_txn *txn = NULL;
    if (mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &txn) != MDBX_SUCCESS)
        return STORAGE_RC_INTERNAL;
    MDBX_val k = {.iov_base = (void *)key, .iov_len = strlen(key)};
    MDBX_val v = {0};
    int r = mdbx_get(txn, dbi, &k, &v);
    *out = (r == MDBX_SUCCESS) ? 1 : 0;
    mdbx_txn_abort(txn);
    return STORAGE_RC_OK;
}

static enum storage_rc bmdbx_del(struct storage_data *d, const char *context,
                                 const char *key, int *out)
{
    MDBX_env *env;
    MDBX_dbi dbi;
    enum storage_rc rc = bmdbx_setup(d, context, &env, &dbi);
    if (rc != STORAGE_RC_OK) return rc;
    MDBX_txn *txn = NULL;
    if (mdbx_txn_begin(env, NULL, MDBX_TXN_READWRITE, &txn) != MDBX_SUCCESS)
        return STORAGE_RC_INTERNAL;
    MDBX_val k = {.iov_base = (void *)key, .iov_len = strlen(key)};
    int r = mdbx_del(txn, dbi, &k, NULL);
    if (r == MDBX_SUCCESS) {
        *out = 1;
        if (mdbx_txn_commit(txn) != MDBX_SUCCESS) return STORAGE_RC_INTERNAL;
        return STORAGE_RC_OK;
    }
    mdbx_txn_abort(txn);
    if (r == MDBX_NOTFOUND) {
        *out = 0;
        return STORAGE_RC_OK;
    }
    ywarn("storage[mdbx]: del(%s/%s) failed: %s",
          context, key, mdbx_strerror(r));
    return STORAGE_RC_INTERNAL;
}

static enum storage_rc bmdbx_count(struct storage_data *d, const char *context,
                                   size_t *out)
{
    MDBX_env *env;
    MDBX_dbi dbi;
    enum storage_rc rc = bmdbx_setup(d, context, &env, &dbi);
    if (rc != STORAGE_RC_OK) return rc;
    MDBX_txn *txn = NULL;
    if (mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &txn) != MDBX_SUCCESS)
        return STORAGE_RC_INTERNAL;
    MDBX_stat st = {0};
    int r = mdbx_dbi_stat(txn, dbi, &st, sizeof(st));
    mdbx_txn_abort(txn);
    if (r != MDBX_SUCCESS) {
        ywarn("storage[mdbx]: dbi_stat(%s) failed: %s",
              context, mdbx_strerror(r));
        return STORAGE_RC_INTERNAL;
    }
    *out = (size_t)st.ms_entries;
    return STORAGE_RC_OK;
}

const struct backend_ops *storage_backend_mdbx_ops(void)
{
    static const struct backend_ops ops = {
        .set    = bmdbx_set,
        .get    = bmdbx_get,
        .exists = bmdbx_exists,
        .del    = bmdbx_del,
        .count  = bmdbx_count,
    };
    return &ops;
}
