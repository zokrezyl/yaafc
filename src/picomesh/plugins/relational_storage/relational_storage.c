/* relational_storage — generic SQLite-backed, sharded relational engine (gh#18).
 *
 * Where `sharded_storage` is a generic ordered KV engine (mdbx), this is a
 * generic RELATIONAL engine: SQLite with real constraints, indexes, joins and
 * transactions, sharded by a caller-supplied id:
 *
 *     shard_id -> shard = shard_id % N -> <dir>/shard_<i>.db
 *
 * It carries NO application data model. The per-shard schema (CREATE TABLE/
 * INDEX) and the shard count come from CONFIG — picoforge's product schema
 * lives in picoforge.yaml, never here. The shard id is whatever the caller
 * routes by (picoforge uses the namespace = user/org, so a namespace's repos/
 * issues/pipelines land in ONE database and stay joinable + transactional).
 *
 * Generic surface — raw storage, not a product API; the service layer owns
 * routing and correctness:
 *
 *     exec(shard_id, sql, args_json)  -> {"changes":N,"last_insert_rowid":M}
 *     query(shard_id, sql, args_json) -> [ {col: val, …}, … ]
 *
 * `args_json` is a JSON array of bind params for the `?` placeholders (so
 * callers never splice values into SQL). Cross-shard work is the caller's job
 * (IDs + service calls + read models), not SQL joins. */

#define _POSIX_C_SOURCE 200809L

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yjson/yjson.h>

#include <sqlite3.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define REL_MAX_SHARDS 64

/* This engine carries NO application data model. The per-shard schema (the
 * CREATE TABLE/INDEX statements) and the shard count are CONFIG, applied
 * verbatim on shard open — picoforge's product schema lives in its config
 * file, not in this plugin. A node that provides no `schema` gets empty
 * SQLite shards and runs its own DDL through `exec`. */

struct rel_shard {
    sqlite3 *db;
    pthread_mutex_t mu;
};

struct rel_set {
    struct rel_shard shards[REL_MAX_SHARDS];
    int n;
    int ready;     /* 1 once open succeeded; -1 once it permanently failed */
    pthread_mutex_t init_mu;
};

static struct rel_set *rel_set(void)
{
    static struct rel_set s = {.init_mu = PTHREAD_MUTEX_INITIALIZER};
    return &s;
}

/* mkdir -p: create `dir` and any missing parents (best-effort). The configured
 * shard directory may be several levels deep (e.g. <root>/rel/uid), so a single
 * mkdir is not enough. */
static void rel_mkdir_p(const char *dir)
{
    char tmp[600];
    size_t n = strlen(dir);
    if (n == 0 || n >= sizeof(tmp)) return;
    memcpy(tmp, dir, n + 1);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

/* Lazy one-shot open: read config, open N shard DBs, apply the schema.
 * `relational_storage.path` is REQUIRED (a data-location path must be
 * explicit — no silent /tmp fallback). Returns NULL on failure. */
static struct rel_set *rel_init(void)
{
    struct rel_set *s = rel_set();
    pthread_mutex_lock(&s->init_mu);
    if (s->ready != 0) { pthread_mutex_unlock(&s->init_mu); return s->ready > 0 ? s : NULL; }

    struct picomesh_engine *e = picomesh_active_engine();
    const struct yconfig *cfg = e ? picomesh_engine_config(e) : NULL;
    const char *dir = NULL;
    const char *schema = NULL; /* config-provided DDL, applied verbatim */
    int shards = 8;
    if (cfg) {
        struct yconfig_node_ptr_result pr = yconfig_get(cfg, "relational_storage.path");
        if (PICOMESH_IS_OK(pr) && pr.value) dir = yconfig_node_as_string(pr.value, NULL);
        else if (PICOMESH_IS_ERR(pr)) picomesh_error_destroy(pr.error);
        struct yconfig_node_ptr_result sr = yconfig_get(cfg, "relational_storage.shards");
        if (PICOMESH_IS_OK(sr) && sr.value) shards = (int)yconfig_node_as_int(sr.value, 8);
        else if (PICOMESH_IS_ERR(sr)) picomesh_error_destroy(sr.error);
        struct yconfig_node_ptr_result cr = yconfig_get(cfg, "relational_storage.schema");
        if (PICOMESH_IS_OK(cr) && cr.value) schema = yconfig_node_as_string(cr.value, NULL);
        else if (PICOMESH_IS_ERR(cr)) picomesh_error_destroy(cr.error);
    }
    if (!dir || !*dir) {
        ytrace_output("error", __FILE__, __LINE__, __func__,
                      "relational_storage: config key relational_storage.path is REQUIRED "
                      "(the directory for the shard SQLite files) — refusing to start");
        s->ready = -1;
        pthread_mutex_unlock(&s->init_mu);
        return NULL;
    }
    if (shards < 1) shards = 1;
    if (shards > REL_MAX_SHARDS) shards = REL_MAX_SHARDS;
    rel_mkdir_p(dir); /* best-effort; sqlite3_open errors if it truly can't */

    for (int i = 0; i < shards; ++i) {
        char path[600];
        snprintf(path, sizeof(path), "%s/shard_%d.db", dir, i);
        sqlite3 *db = NULL;
        if (sqlite3_open(path, &db) != SQLITE_OK) {
            ywarn("relational_storage: open %s failed: %s", path,
                  db ? sqlite3_errmsg(db) : "(null)");
            sqlite3_close(db);
            s->ready = -1;
            pthread_mutex_unlock(&s->init_mu);
            return NULL;
        }
        /* WAL + FK pragmas are engine policy; the table schema is whatever the
         * node configured (or nothing — then the caller runs its own DDL). */
        /* WAL + FK are engine policy; a failure silently weakens guarantees, so
         * surface it loudly (do not fail the open — some filesystems reject WAL,
         * and the caller may not need FKs). */
        char *pragma_err = NULL;
        if (sqlite3_exec(db, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;", NULL, NULL, &pragma_err) != SQLITE_OK) {
            ywarn("relational_storage: PRAGMA WAL/FK on %s failed: %s", path, pragma_err ? pragma_err : "?");
            sqlite3_free(pragma_err);
        }
        if (schema && *schema) {
            char *err = NULL;
            if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK) {
                ywarn("relational_storage: configured schema on %s failed: %s", path, err ? err : "?");
                sqlite3_free(err);
                sqlite3_close(db);
                s->ready = -1;
                pthread_mutex_unlock(&s->init_mu);
                return NULL;
            }
        }
        s->shards[i].db = db;
        pthread_mutex_init(&s->shards[i].mu, NULL);
    }
    s->n = shards;
    s->ready = 1;
    yinfo("relational_storage: opened %d SQLite shard(s) under %s", shards, dir);
    pthread_mutex_unlock(&s->init_mu);
    return s;
}

#define REL_MAX_BINDS 64

/* A bind param extracted from the JSON args on the COROUTINE thread, so the
 * worker thread never calls into the (non-thread-safe) simdjson document.
 * `str` points into the parsed doc, which outlives the offloaded work. */
enum rel_bind_kind { RB_INT, RB_FLOAT, RB_TEXT, RB_NULL };
struct rel_bind { enum rel_bind_kind kind; int64_t i64; double f64; const char *str; };

/* Bind the extracted params onto a prepared statement (1-based). Returns the
 * first non-OK sqlite rc (so the caller fails the statement rather than stepping
 * a partially-bound query), or SQLITE_OK. */
static int rel_bind_args(sqlite3_stmt *st, const struct rel_bind *b, int n)
{
    for (int i = 0; i < n; ++i) {
        int idx = i + 1, rc;
        switch (b[i].kind) {
        case RB_INT:   rc = sqlite3_bind_int64(st, idx, b[i].i64); break;
        case RB_FLOAT: rc = sqlite3_bind_double(st, idx, b[i].f64); break;
        case RB_NULL:  rc = sqlite3_bind_null(st, idx); break;
        case RB_TEXT:  rc = sqlite3_bind_text(st, idx, b[i].str ? b[i].str : "", -1, SQLITE_TRANSIENT); break;
        default:       rc = SQLITE_OK; break;
        }
        if (rc != SQLITE_OK) return rc;
    }
    return SQLITE_OK;
}

/* Append one result column as a JSON value. */
static void rel_emit_col(struct yjson_writer *w, sqlite3_stmt *st, int col)
{
    switch (sqlite3_column_type(st, col)) {
    case SQLITE_INTEGER: yjson_writer_int(w, sqlite3_column_int64(st, col)); break;
    case SQLITE_FLOAT:   yjson_writer_float(w, sqlite3_column_double(st, col)); break;
    case SQLITE_NULL:    yjson_writer_null(w); break;
    default: {
        const unsigned char *t = sqlite3_column_text(st, col);
        yjson_writer_string(w, t ? (const char *)t : "");
        break;
    }
    }
}

enum rel_op { REL_EXEC, REL_QUERY };

struct rel_work {
    enum rel_op op;
    uint32_t shard_key;
    const char *sql;
    const struct rel_bind *binds;   /* plain-C bind params (no JSON on worker) */
    int nbinds;
    char *out_json;                 /* owned heap result */
    int ok;
    char err[256];
};

/* Runs on a worker-pool thread: prepare + bind + step on the shard_key's shard,
 * serialized by the shard mutex; build the JSON result. */
static void rel_work_fn(void *arg)
{
    struct rel_work *w = arg;
    w->ok = 0;
    struct rel_set *s = rel_init();
    if (!s) { snprintf(w->err, sizeof(w->err), "relational_storage: not configured"); return; }
    struct rel_shard *sh = &s->shards[(uint64_t)w->shard_key % (uint64_t)s->n];

    pthread_mutex_lock(&sh->mu);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(sh->db, w->sql, -1, &st, NULL) != SQLITE_OK) {
        snprintf(w->err, sizeof(w->err), "prepare: %s", sqlite3_errmsg(sh->db));
        pthread_mutex_unlock(&sh->mu);
        return;
    }
    /* The number of supplied binds must match the statement's `?` placeholder
     * count exactly — too few would leave trailing params silently NULL, too
     * many would bind past the statement. Reject the mismatch rather than
     * stepping a half-bound query. */
    int want = sqlite3_bind_parameter_count(st);
    if (want != w->nbinds) {
        snprintf(w->err, sizeof(w->err),
                 "bind count mismatch: SQL has %d parameter(s), got %d", want, w->nbinds);
        sqlite3_finalize(st);
        pthread_mutex_unlock(&sh->mu);
        return;
    }
    if (rel_bind_args(st, w->binds, w->nbinds) != SQLITE_OK) {
        snprintf(w->err, sizeof(w->err), "bind: %s", sqlite3_errmsg(sh->db));
        sqlite3_finalize(st);
        pthread_mutex_unlock(&sh->mu);
        return;
    }

    struct yjson_writer *jw = yjson_writer_new();
    if (!jw) { sqlite3_finalize(st); pthread_mutex_unlock(&sh->mu);
               snprintf(w->err, sizeof(w->err), "writer alloc"); return; }

    if (w->op == REL_QUERY) {
        yjson_writer_begin_array(jw);
        int ncol = sqlite3_column_count(st);
        int rc;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            yjson_writer_begin_object(jw);
            for (int c = 0; c < ncol; ++c) {
                yjson_writer_key(jw, sqlite3_column_name(st, c));
                rel_emit_col(jw, st, c);
            }
            yjson_writer_end_object(jw);
        }
        yjson_writer_end_array(jw);
        if (rc != SQLITE_DONE) {
            snprintf(w->err, sizeof(w->err), "step: %s", sqlite3_errmsg(sh->db));
            yjson_writer_free(jw); sqlite3_finalize(st); pthread_mutex_unlock(&sh->mu);
            return;
        }
    } else { /* REL_EXEC */
        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            snprintf(w->err, sizeof(w->err), "exec: %s", sqlite3_errmsg(sh->db));
            yjson_writer_free(jw); sqlite3_finalize(st); pthread_mutex_unlock(&sh->mu);
            return;
        }
        yjson_writer_begin_object(jw);
        yjson_writer_key(jw, "changes");           yjson_writer_int(jw, sqlite3_changes(sh->db));
        yjson_writer_key(jw, "last_insert_rowid"); yjson_writer_int(jw, sqlite3_last_insert_rowid(sh->db));
        yjson_writer_end_object(jw);
    }

    size_t len = 0;
    const char *data = yjson_writer_data(jw, &len);
    w->out_json = strdup(data ? data : (w->op == REL_QUERY ? "[]" : "{}"));
    yjson_writer_free(jw);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&sh->mu);
    w->ok = w->out_json != NULL;
}

/* Offload the (potentially fsync-blocking) DB work to the worker pool so the
 * serving coroutine yields and the loop keeps running — mirrors
 * sharded_storage's shard_run. Falls back to inline outside a loop. */
static void rel_run(struct rel_work *w)
{
    struct picomesh_engine *e = picomesh_active_engine();
    struct yloop *l = e ? picomesh_engine_loop(e) : NULL;
    if (!l) { rel_work_fn(w); return; }
    struct picomesh_void_result r = yloop_run_blocking(l, rel_work_fn, w);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); rel_work_fn(w); }
}

/* ---- class + methods ------------------------------------------------- */

struct PICOMESH_CLASS_ANNOTATE("class@relational_storage:db") relational_storage_data {
    char _unused;
};

/* Shared by exec + query: parse args_json, dispatch, return the JSON result. */
static struct picomesh_json_result rel_call(enum rel_op op, uint32_t shard_key,
                                            const char *sql, const char *args_json)
{
    if (!sql || !*sql) return PICOMESH_ERR(picomesh_json, "relational_storage: empty SQL");

    /* Extract the bind params HERE (coroutine thread) into plain C so the
     * offloaded worker never touches the simdjson doc. The doc (and its
     * string bytes the binds point at) stays alive until after rel_run.
     *
     * Malformed input is a client error, not silently zero binds: reject a
     * non-JSON body, a JSON value that is not an array, and an array longer
     * than REL_MAX_BINDS. (The bind count is checked against the prepared
     * statement's `?` placeholder count on the worker thread, before step.) */
    struct yjson_doc *doc = NULL;
    struct rel_bind binds[REL_MAX_BINDS];
    int nbinds = 0;
    if (args_json && *args_json) {
        doc = yjson_parse(args_json, strlen(args_json));
        if (!doc)
            return PICOMESH_ERR(picomesh_json, "relational_storage: malformed args_json (not valid JSON)");
        const struct yjson_value *arr = yjson_doc_root(doc);
        if (!arr || !yjson_is_array(arr)) {
            yjson_doc_free(doc);
            return PICOMESH_ERR(picomesh_json, "relational_storage: args_json must be a JSON array");
        }
        size_t n = yjson_array_size(arr);
        if (n > REL_MAX_BINDS) {
            yjson_doc_free(doc);
            char msg[96];
            snprintf(msg, sizeof(msg), "relational_storage: too many bind args (%zu > max %d)", n, REL_MAX_BINDS);
            return PICOMESH_ERR(picomesh_json, msg);
        }
        for (size_t i = 0; i < n; ++i) {
            const struct yjson_value *a = yjson_array_at(arr, i);
            struct rel_bind *b = &binds[nbinds++];
            if (yjson_is_int(a))        { b->kind = RB_INT;   b->i64 = yjson_as_int(a, 0); }
            else if (yjson_is_float(a)) { b->kind = RB_FLOAT; b->f64 = yjson_as_float(a, 0); }
            else if (yjson_is_bool(a))  { b->kind = RB_INT;   b->i64 = yjson_as_bool(a, 0); }
            else if (yjson_is_null(a))  { b->kind = RB_NULL;  b->str = NULL; }
            else                        { b->kind = RB_TEXT;  b->str = yjson_as_string(a, ""); }
        }
    }

    struct rel_work w = {.op = op, .shard_key = shard_key, .sql = sql,
                         .binds = binds, .nbinds = nbinds};
    rel_run(&w);
    if (doc) yjson_doc_free(doc);

    if (!w.ok) {
        free(w.out_json);
        char msg[300];
        snprintf(msg, sizeof(msg), "relational_storage: %s",
                 w.err[0] ? w.err : "query failed");
        return PICOMESH_ERR(picomesh_json, msg);
    }
    return PICOMESH_OK(picomesh_json, w.out_json);
}

/* Run a write/DDL statement against namespace `shard_key`'s shard. Returns
 * {"changes":N,"last_insert_rowid":M}. `args_json` binds the `?` params. */
PICOMESH_CLASS_ANNOTATE("override@relational_storage:db:db_exec")
struct picomesh_json_result relational_storage_db_exec_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs, uint32_t shard_key,
                                                            const char *sql, const char *args_json)
{
    (void)ctx; (void)obj; (void)hdrs;
    return rel_call(REL_EXEC, shard_key, sql, args_json);
}

/* Run a SELECT against namespace `shard_key`'s shard. Returns a JSON array of
 * row objects ({column: value, …}). `args_json` binds the `?` params. */
PICOMESH_CLASS_ANNOTATE("override@relational_storage:db:db_query")
struct picomesh_json_result relational_storage_db_query_impl(struct ctx *ctx, struct object *obj,
                                                             struct yheaders *hdrs, uint32_t shard_key,
                                                             const char *sql, const char *args_json)
{
    (void)ctx; (void)obj; (void)hdrs;
    return rel_call(REL_QUERY, shard_key, sql, args_json);
}

#include "relational_storage.gen.c"
