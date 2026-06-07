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
 * routes by: a data cluster routes by the owning `uid` (a user's rows land in
 * ONE shard and stay joinable + transactional), a lookup cluster by
 * `hash(external_id)` (see docs/sharded-relational-storage.md). The engine never
 * interprets the id — it only does `shard_id % N`.
 *
 * One instance hosts MULTIPLE named logical databases, each its own shard set,
 * shard count and schema, selected per call by `db_name` (see rel_set_for()).
 * The split mesh runs a process per `rstore_*` service, each serving its sole
 * (default) database from the flat `relational_storage.*` config; the
 * collocated/all-in-one deployment hosts every logical database
 * (uid/username/session/token) in ONE process via the
 * `relational_storage.databases.<name>.*` config map.
 *
 * Generic surface — raw storage, not a product API; the service layer owns
 * routing and correctness:
 *
 *     exec(db, shard_id, sql, args_json)  -> {"changes":N,"last_insert_rowid":M}
 *     query(db, shard_id, sql, args_json) -> [ {col: val, …}, … ]
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
#include <strings.h> /* strcasecmp */
#include <sys/stat.h>

#define REL_MAX_SHARDS  64
#define REL_MAX_DBS     16
#define REL_DB_NAME_MAX 48

/* This engine carries NO application data model. The per-shard schema (the
 * CREATE TABLE/INDEX statements) and the shard count are CONFIG, applied
 * verbatim on shard open — picoforge's product schema lives in its config
 * file, not in this plugin. A node that provides no `schema` gets empty
 * SQLite shards and runs its own DDL through `exec`. */

struct rel_shard {
    sqlite3 *db;
    pthread_mutex_t mu;
};

/* One NAMED logical database == one shard set. A single relational_storage
 * instance hosts several of these, each with its OWN shard count and schema, so
 * each store shards by whatever routing key its consumer supplies (data stores
 * by the owning `uid`, identity-lookup stores by `hash(external_id)`) entirely
 * independently of the others. The `name` selects which set an op routes to. */
struct rel_set {
    char name[REL_DB_NAME_MAX];
    struct rel_shard shards[REL_MAX_SHARDS];
    int n;
    int ready;     /* 0 = not opened yet; 1 = open; -1 = permanently failed */
    pthread_mutex_t init_mu;
};

/* Process-global registry of named shard sets. ONE relational_storage instance
 * (process) hosts MULTIPLE logical databases — each its own shard set, shard
 * count and schema — selected per op by `db_name`. This lifts the former
 * one-cluster-per-process limit (gh#29) so the collocated/all-in-one deployment
 * can host uid/username/session/token in a single process; the split mesh still
 * runs a process per `rstore_*` service, each serving its sole (default)
 * database from the flat `relational_storage.*` config.
 *
 * The registry mutex guards name lookup + slot allocation; each set then opens
 * lazily behind its OWN init mutex. Slots are never moved or freed, so a
 * returned `struct rel_set *` stays valid for the process lifetime.
 *
 * This is the one sanctioned file-scope datum here: a process-lifetime
 * subsystem singleton guarded by its own mutex, with no per-engine slot to hang
 * it off of. */
struct rel_registry {
    struct rel_set sets[REL_MAX_DBS];
    int count;
    pthread_mutex_t mu;
};

static struct rel_registry *rel_registry(void)
{
    static struct rel_registry r = {.mu = PTHREAD_MUTEX_INITIALIZER};
    return &r;
}

/* Find the shard set for `db_name`, allocating a fresh (unopened) slot the
 * first time a name is seen. A NULL/empty name maps to "default" — the legacy
 * single-database identity served from the flat `relational_storage.*` config,
 * which keeps every existing split-mesh `rstore_*` process working unchanged.
 * Returns NULL only if more than REL_MAX_DBS distinct names are requested. */
static struct rel_set *rel_set_for(const char *db_name)
{
    if (!db_name || !*db_name) db_name = "default";
    struct rel_registry *reg = rel_registry();
    pthread_mutex_lock(&reg->mu);
    struct rel_set *found = NULL;
    for (int i = 0; i < reg->count; ++i) {
        if (strcmp(reg->sets[i].name, db_name) == 0) { found = &reg->sets[i]; break; }
    }
    if (!found && reg->count < REL_MAX_DBS) {
        found = &reg->sets[reg->count++];
        snprintf(found->name, sizeof(found->name), "%s", db_name);
        found->n = 0;
        found->ready = 0;
        pthread_mutex_init(&found->init_mu, NULL);
    }
    pthread_mutex_unlock(&reg->mu);
    return found;
}

/* Read a string config value for database `db_name`, preferring the
 * per-database key `relational_storage.databases.<name>.<leaf>` and falling
 * back to the flat legacy key `relational_storage.<leaf>` when the named form
 * is absent (so the split mesh's per-process flat config still applies). NULL
 * if neither is set. */
static const char *rel_cfg_str(const struct yconfig *cfg, const char *db_name, const char *leaf)
{
    if (!cfg) return NULL;
    char key[160];
    snprintf(key, sizeof(key), "relational_storage.databases.%s.%s", db_name, leaf);
    struct yconfig_node_ptr_result pr = yconfig_get(cfg, key);
    if (PICOMESH_IS_OK(pr) && pr.value) return yconfig_node_as_string(pr.value, NULL);
    if (PICOMESH_IS_ERR(pr)) picomesh_error_destroy(pr.error);
    snprintf(key, sizeof(key), "relational_storage.%s", leaf);
    pr = yconfig_get(cfg, key);
    if (PICOMESH_IS_OK(pr) && pr.value) return yconfig_node_as_string(pr.value, NULL);
    if (PICOMESH_IS_ERR(pr)) picomesh_error_destroy(pr.error);
    return NULL;
}

/* Integer counterpart to rel_cfg_str (same per-database → flat fallback). */
static int rel_cfg_int(const struct yconfig *cfg, const char *db_name, const char *leaf, int fallback)
{
    if (!cfg) return fallback;
    char key[160];
    snprintf(key, sizeof(key), "relational_storage.databases.%s.%s", db_name, leaf);
    struct yconfig_node_ptr_result pr = yconfig_get(cfg, key);
    if (PICOMESH_IS_OK(pr) && pr.value) return (int)yconfig_node_as_int(pr.value, fallback);
    if (PICOMESH_IS_ERR(pr)) picomesh_error_destroy(pr.error);
    snprintf(key, sizeof(key), "relational_storage.%s", leaf);
    pr = yconfig_get(cfg, key);
    if (PICOMESH_IS_OK(pr) && pr.value) return (int)yconfig_node_as_int(pr.value, fallback);
    if (PICOMESH_IS_ERR(pr)) picomesh_error_destroy(pr.error);
    return fallback;
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

/* Read a single-row, single-column PRAGMA value as text into `out`. Returns 1
 * if a row was produced, 0 otherwise (out is left empty). Used to VERIFY a
 * PRAGMA actually took effect: setting one can "succeed" (sqlite3_exec returns
 * OK) yet be silently downgraded — journal_mode returns the resulting mode as a
 * row and falls back off WAL on filesystems that reject it, and a non-enforcing
 * foreign_keys leaves the relational guarantees off without an error. */
static int rel_pragma_text(sqlite3 *db, const char *pragma, char *out, size_t cap)
{
    if (cap) out[0] = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, pragma, -1, &st, NULL) != SQLITE_OK) return 0;
    int got = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(st, 0);
        if (text) { snprintf(out, cap, "%s", (const char *)text); got = 1; }
    }
    sqlite3_finalize(st);
    return got;
}

/* Lazy one-shot open of the named database `db_name`: read its config, open N
 * shard DBs, apply its schema. The path is REQUIRED (a data-location path must
 * be explicit — no silent /tmp fallback), read from
 * `relational_storage.databases.<name>.path` or the flat `relational_storage.path`.
 * Returns NULL on failure (or if the registry is full). */
static struct rel_set *rel_init(const char *db_name)
{
    struct rel_set *s = rel_set_for(db_name);
    if (!s) {
        ytrace_output("error", __FILE__, __LINE__, __func__,
                      "relational_storage: too many distinct databases (raise REL_MAX_DBS)");
        return NULL;
    }
    pthread_mutex_lock(&s->init_mu);
    if (s->ready != 0) { pthread_mutex_unlock(&s->init_mu); return s->ready > 0 ? s : NULL; }

    struct picomesh_engine *e = picomesh_active_engine();
    const struct yconfig *cfg = e ? picomesh_engine_config(e) : NULL;
    const char *dir = rel_cfg_str(cfg, s->name, "path");          /* shard dir */
    const char *schema = rel_cfg_str(cfg, s->name, "schema");     /* DDL, verbatim */
    int shards = rel_cfg_int(cfg, s->name, "shards", 8);
    /* Durability/throughput knobs. Default is WAL (gh#29 engine policy). An
     * EPHEMERAL deploy on tmpfs — the webasm/qemu demo, where the whole DB tree
     * is in RAM and discarded on reboot — can set journal_mode=MEMORY +
     * synchronous=OFF to skip the -wal/-shm files and every fsync, which under
     * CPU emulation is the dominant boot/seed cost. Read with the same
     * per-db/flat fallback as path/shards (so `relational_storage.journal_mode`
     * sets it for all databases at once). */
    const char *journal = rel_cfg_str(cfg, s->name, "journal_mode");
    const char *synchronous = rel_cfg_str(cfg, s->name, "synchronous");
    if (!journal || !*journal) journal = "WAL";
    if (!dir || !*dir) {
        char msg[220];
        snprintf(msg, sizeof(msg),
                 "relational_storage: path is REQUIRED for database '%s' "
                 "(set relational_storage.databases.%s.path or relational_storage.path) "
                 "— refusing to start", s->name, s->name);
        ytrace_output("error", __FILE__, __LINE__, __func__, "%s", msg);
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
        /* WAL + FK are engine policy. A PRAGMA can "succeed" (sqlite3_exec
         * returns OK) yet not take effect, so set each and READ IT BACK.
         *
         * Explicit policy decision (gh#29):
         *   - WAL is BEST-EFFORT. A rejected WAL (some filesystems do) falls
         *     back to a rollback journal, which is still ACID — only concurrency
         *     suffers — so we log and continue.
         *   - FK enforcement is REQUIRED. A configured schema may declare foreign
         *     keys and depend on them for referential integrity, and unlike WAL,
         *     `PRAGMA foreign_keys=ON` is not filesystem-dependent — it only
         *     fails to stick on a SQLite built without FK support. Running with
         *     it silently off is a correctness downgrade, so we FAIL the open. */
        char *pragma_err = NULL;
        char journal_sql[64];
        snprintf(journal_sql, sizeof(journal_sql), "PRAGMA journal_mode=%s;", journal);
        if (sqlite3_exec(db, journal_sql, NULL, NULL, &pragma_err) != SQLITE_OK) {
            ywarn("relational_storage: set journal_mode=%s on %s failed: %s", journal, path, pragma_err ? pragma_err : "?");
            sqlite3_free(pragma_err);
            pragma_err = NULL;
        }
        char journal_mode[16];
        if (rel_pragma_text(db, "PRAGMA journal_mode;", journal_mode, sizeof(journal_mode))
            && strcasecmp(journal_mode, journal) != 0)
            ywarn("relational_storage: %s journal_mode is '%s', not %s — durability/concurrency reduced",
                  path, journal_mode[0] ? journal_mode : "?", journal);
        /* Optional synchronous override (e.g. OFF on ephemeral tmpfs — no
         * fsync). Left at the SQLite default (FULL under WAL) when unset. */
        if (synchronous && *synchronous) {
            char sync_sql[64];
            snprintf(sync_sql, sizeof(sync_sql), "PRAGMA synchronous=%s;", synchronous);
            if (sqlite3_exec(db, sync_sql, NULL, NULL, &pragma_err) != SQLITE_OK) {
                ywarn("relational_storage: set synchronous=%s on %s failed: %s", synchronous, path, pragma_err ? pragma_err : "?");
                sqlite3_free(pragma_err);
                pragma_err = NULL;
            }
        }

        if (sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, &pragma_err) != SQLITE_OK) {
            ywarn("relational_storage: set foreign_keys=ON on %s failed: %s", path, pragma_err ? pragma_err : "?");
            sqlite3_free(pragma_err);
            pragma_err = NULL;
        }
        char foreign_keys[8];
        if (!rel_pragma_text(db, "PRAGMA foreign_keys;", foreign_keys, sizeof(foreign_keys))
            || strcmp(foreign_keys, "1") != 0) {
            ywarn("relational_storage: %s foreign_keys could NOT be enabled (got '%s') — refusing "
                  "to start with FK enforcement off", path, foreign_keys[0] ? foreign_keys : "?");
            sqlite3_close(db);
            s->ready = -1;
            pthread_mutex_unlock(&s->init_mu);
            return NULL;
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
    yinfo("relational_storage: database '%s' opened %d SQLite shard(s) under %s", s->name, shards, dir);
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
    const char *db_name;            /* which named database to route to */
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
    struct rel_set *s = rel_init(w->db_name);
    if (!s) { snprintf(w->err, sizeof(w->err), "relational_storage: database '%s' not configured",
                       w->db_name ? w->db_name : "default"); return; }
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

    /* The shard mutex guards ONLY the SQLite statement (prepare/bind/step and,
     * for a query, the live column reads — a sqlite3* is not concurrency-safe).
     * It must NOT be held across result serialization or heap allocation: those
     * touch no shard state, and holding the DB lock across malloc/strdup/JSON
     * building (and across the cross-shard fan-out that drives this) needlessly
     * serializes every shard behind one lock. For a query the rows are read into
     * the writer under the lock (columns are only valid while stepping); the
     * final strdup happens after unlock. For an exec the counters are captured
     * into locals and the JSON is built entirely after unlock. */
    struct yjson_writer *jw = NULL;
    int64_t exec_changes = 0, exec_rowid = 0;

    if (w->op == REL_QUERY) {
        jw = yjson_writer_new();
        if (!jw) { sqlite3_finalize(st); pthread_mutex_unlock(&sh->mu);
                   snprintf(w->err, sizeof(w->err), "writer alloc"); return; }
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
            sqlite3_finalize(st); pthread_mutex_unlock(&sh->mu);
            return;
        }
        exec_changes = sqlite3_changes(sh->db);
        exec_rowid   = sqlite3_last_insert_rowid(sh->db);
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&sh->mu);
    /* --- DB lock released: serialize + allocate the result OUTSIDE it. --- */

    if (w->op == REL_EXEC) {
        jw = yjson_writer_new();
        if (!jw) { snprintf(w->err, sizeof(w->err), "writer alloc"); return; }
        yjson_writer_begin_object(jw);
        yjson_writer_key(jw, "changes");           yjson_writer_int(jw, exec_changes);
        yjson_writer_key(jw, "last_insert_rowid"); yjson_writer_int(jw, exec_rowid);
        yjson_writer_end_object(jw);
    }

    size_t len = 0;
    const char *data = yjson_writer_data(jw, &len);
    w->out_json = strdup(data ? data : (w->op == REL_QUERY ? "[]" : "{}"));
    yjson_writer_free(jw);
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
static struct picomesh_json_result rel_call(enum rel_op op, const char *db_name, uint32_t shard_key,
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

    struct rel_work w = {.op = op, .db_name = db_name, .shard_key = shard_key, .sql = sql,
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

/* Run a write/DDL statement against database `db_name`, shard `shard_key`%N.
 * Returns {"changes":N,"last_insert_rowid":M}. `args_json` binds the `?` params. */
PICOMESH_CLASS_ANNOTATE("override@relational_storage:db:db_exec")
struct picomesh_json_result relational_storage_db_exec_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs, const char *db_name,
                                                            uint32_t shard_key,
                                                            const char *sql, const char *args_json)
{
    (void)ctx; (void)obj; (void)hdrs;
    return rel_call(REL_EXEC, db_name, shard_key, sql, args_json);
}

/* Run a SELECT against database `db_name`, shard `shard_key`%N. Returns a JSON
 * array of row objects ({column: value, …}). `args_json` binds the `?` params. */
PICOMESH_CLASS_ANNOTATE("override@relational_storage:db:db_query")
struct picomesh_json_result relational_storage_db_query_impl(struct ctx *ctx, struct object *obj,
                                                             struct yheaders *hdrs, const char *db_name,
                                                             uint32_t shard_key,
                                                             const char *sql, const char *args_json)
{
    (void)ctx; (void)obj; (void)hdrs;
    return rel_call(REL_QUERY, db_name, shard_key, sql, args_json);
}

/* The number of shards database `db_name` actually opened. Fan-out (DDL
 * broadcast, cross-shard aggregates and pagination) must iterate the serving
 * database's shard count — in the split mesh a consumer is wired to a remote
 * cluster whose shard count it cannot read from its own local config, and even
 * collocated each named database may have a different count. Reading the count
 * from the caller's config instead would miss shards (undercount) or wrap onto
 * already-queried shards (double-count). So consumers ask the instance. */
PICOMESH_CLASS_ANNOTATE("override@relational_storage:db:db_shard_count")
struct picomesh_int_result relational_storage_db_shard_count_impl(struct ctx *ctx, struct object *obj,
                                                                  struct yheaders *hdrs, const char *db_name)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct rel_set *s = rel_init(db_name);
    if (!s) return PICOMESH_ERR(picomesh_int, "relational_storage: database not configured");
    return PICOMESH_OK(picomesh_int, s->n);
}

#include "relational_storage.gen.c"
