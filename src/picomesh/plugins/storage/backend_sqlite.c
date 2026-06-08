/* sqlite backend for the storage plugin.
 *
 * Each context maps to a table named `kv_<context>` in a single sqlite
 * file (path from yconfig `storage.db_path`, default `:memory:`). The
 * `kv_` prefix keeps the SQL identifier disjoint from anything sqlite
 * might reserve and makes accidental collisions with non-storage tables
 * impossible.
 *
 * Each storage object holds its own sqlite3 *db (the connection); the
 * table is CREATE-IF-NOT-EXISTS'd on first reference to a context. */

#include "backends.h"

#include <picomesh/ycore/ytrace.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>

#include <sqlite3.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *sqlite_resolve_db_path(void)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return ":memory:";
    struct yconfig_node_ptr_result db_path_node_res =
        yconfig_get(picomesh_engine_config(e), "storage.db_path");
    if (PICOMESH_IS_ERR(db_path_node_res)) {
        /* A config-read failure (not an absent key, which returns OK+NULL) must
         * not silently drop us into in-memory mode and lose persistence. */
        yerror("storage[sqlite]: reading 'storage.db_path' failed: %s",
               db_path_node_res.error.msg ? db_path_node_res.error.msg : "?");
        picomesh_error_destroy(db_path_node_res.error);
        return ":memory:";
    }
    if (db_path_node_res.value) {
        const char *db_path = yconfig_node_as_string(db_path_node_res.value, NULL);
        if (db_path && *db_path) return db_path;
    }
    return ":memory:";
}

static enum storage_rc sqlite_ensure_open(struct storage_data *d)
{
    if (d->be.sqlite.opened) return STORAGE_RC_OK;
    const char *path = sqlite_resolve_db_path();
    ydebug("storage[sqlite]: opening %s", path);
    int rc = sqlite3_open(path, &d->be.sqlite.db);
    if (rc != SQLITE_OK) {
        ywarn("storage[sqlite]: sqlite3_open failed: %s",
              sqlite3_errmsg(d->be.sqlite.db));
        return STORAGE_RC_OPEN_FAILED;
    }
    d->be.sqlite.opened = 1;
    return STORAGE_RC_OK;
}

static enum storage_rc sqlite_ensure_table(struct storage_data *d,
                                           const char *context)
{
    if (!storage_context_is_valid(context)) return STORAGE_RC_BAD_CONTEXT;
    char ddl[256];
    snprintf(ddl, sizeof(ddl),
             "CREATE TABLE IF NOT EXISTS kv_%s ("
             "  k TEXT PRIMARY KEY,"
             "  v BLOB NOT NULL"
             ");", context);
    char *err = NULL;
    int rc = sqlite3_exec(d->be.sqlite.db, ddl, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        ywarn("storage[sqlite]: DDL '%s' failed: %s",
              ddl, err ? err : "(no msg)");
        sqlite3_free(err);
        return STORAGE_RC_INTERNAL;
    }
    return STORAGE_RC_OK;
}

static enum storage_rc sqlite_set(struct storage_data *d, const char *context,
                                  const char *key, const char *value)
{
    enum storage_rc rc = sqlite_ensure_open(d);
    if (rc != STORAGE_RC_OK) return rc;
    rc = sqlite_ensure_table(d, context);
    if (rc != STORAGE_RC_OK) return rc;

    char sql[128];
    snprintf(sql, sizeof(sql),
             "INSERT OR REPLACE INTO kv_%s(k, v) VALUES(?, ?);", context);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(d->be.sqlite.db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return STORAGE_RC_INTERNAL;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value ? value : "", -1, SQLITE_TRANSIENT);
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE) return STORAGE_RC_INTERNAL;
    return STORAGE_RC_OK;
}

static enum storage_rc sqlite_get(struct storage_data *d, const char *context,
                                  const char *key, char **out)
{
    enum storage_rc rc = sqlite_ensure_open(d);
    if (rc != STORAGE_RC_OK) return rc;
    rc = sqlite_ensure_table(d, context);
    if (rc != STORAGE_RC_OK) return rc;

    char sql[128];
    snprintf(sql, sizeof(sql),
             "SELECT v FROM kv_%s WHERE k=?;", context);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(d->be.sqlite.db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return STORAGE_RC_INTERNAL;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    int step = sqlite3_step(stmt);
    enum storage_rc result = STORAGE_RC_NOT_FOUND;
    if (step == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(stmt, 0);
        int n = sqlite3_column_bytes(stmt, 0);
        char *copy = malloc((size_t)(n < 0 ? 0 : n) + 1);
        if (!copy) {
            result = STORAGE_RC_INTERNAL;
        } else {
            if (n > 0 && blob) memcpy(copy, blob, (size_t)n);
            copy[n < 0 ? 0 : n] = 0;
            *out = copy;
            result = STORAGE_RC_OK;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

static enum storage_rc sqlite_exists(struct storage_data *d, const char *context,
                                     const char *key, int *out)
{
    enum storage_rc rc = sqlite_ensure_open(d);
    if (rc != STORAGE_RC_OK) return rc;
    rc = sqlite_ensure_table(d, context);
    if (rc != STORAGE_RC_OK) return rc;

    char sql[128];
    snprintf(sql, sizeof(sql),
             "SELECT 1 FROM kv_%s WHERE k=?;", context);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(d->be.sqlite.db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return STORAGE_RC_INTERNAL;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    int step = sqlite3_step(stmt);
    *out = (step == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    return STORAGE_RC_OK;
}

static enum storage_rc sqlite_del(struct storage_data *d, const char *context,
                                  const char *key, int *out)
{
    enum storage_rc rc = sqlite_ensure_open(d);
    if (rc != STORAGE_RC_OK) return rc;
    rc = sqlite_ensure_table(d, context);
    if (rc != STORAGE_RC_OK) return rc;

    char sql[128];
    snprintf(sql, sizeof(sql),
             "DELETE FROM kv_%s WHERE k=?;", context);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(d->be.sqlite.db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return STORAGE_RC_INTERNAL;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE) return STORAGE_RC_INTERNAL;
    *out = sqlite3_changes(d->be.sqlite.db) > 0 ? 1 : 0;
    return STORAGE_RC_OK;
}

static enum storage_rc sqlite_count(struct storage_data *d, const char *context,
                                    size_t *out)
{
    enum storage_rc rc = sqlite_ensure_open(d);
    if (rc != STORAGE_RC_OK) return rc;
    rc = sqlite_ensure_table(d, context);
    if (rc != STORAGE_RC_OK) return rc;

    char sql[128];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM kv_%s;", context);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(d->be.sqlite.db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return STORAGE_RC_INTERNAL;
    int step = sqlite3_step(stmt);
    int64_t n = (step == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : 0;
    sqlite3_finalize(stmt);
    *out = (size_t)n;
    return STORAGE_RC_OK;
}

const struct backend_ops *storage_backend_sqlite_ops(void)
{
    static const struct backend_ops ops = {
        .set    = sqlite_set,
        .get    = sqlite_get,
        .exists = sqlite_exists,
        .del    = sqlite_del,
        .count  = sqlite_count,
    };
    return &ops;
}
