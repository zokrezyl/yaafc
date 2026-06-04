/* relational_sql — thin helpers for plugins that store their state as real SQL
 * rows in the `relational_storage` service (instead of faking columns with
 * prefixed keys in a KV store). Header-only static inline so each plugin TU
 * gets its own copy; no link coupling beyond the relational_storage stubs.
 *
 *   rel_open(&h)                     -> open the relational_storage service
 *   h.shard = <key>                  -> route subsequent ops to shard <key>%N
 *   rel_exec(&h,hdrs,sql,args)       -> write/DDL; result {changes,last_insert_rowid}
 *   rel_query(&h,hdrs,sql,args)      -> SELECT; result [{col:val}, …]
 *   rel_exec_changes(...)            -> the `changes` count of a write
 *   rel_query_int(...,col,fallback)  -> first row's `col` as int64 (or fallback)
 *   rel_query_str(...,col)           -> first row's `col` as owned string (or NULL)
 *   rel_query_int_all(...,col)       -> sum `col` across EVERY shard (aggregate)
 *   rel_query_all(...)               -> concat row arrays across EVERY shard
 *   rel_args_take(writer)            -> finish a bind-args array writer -> owned JSON
 *
 * SHARDING. The relational engine keeps N SQLite shards and serializes each
 * shard behind its own mutex, so throughput scales only when rows SPREAD across
 * shards. The caller sets the routing key with `h.shard = <key>` before an op;
 * the engine routes `key % N` and never interprets the key. Two patterns (see
 * docs/relational-storage-sharding.md):
 *   - data tables shard by the owning `uid` (`h.shard = uid`), co-locating a
 *     user's rows in one shard;
 *   - identity-lookup tables shard by `hash(external_id)`
 *     (`h.shard = hash(username|session_id|token)`) and map that id to a uid —
 *     tokens stay opaque/random; NOTHING is embedded in them for routing.
 * The per-plugin DDL is created on EVERY shard (rel_ensure_schema); the rare
 * cross-shard aggregates (count, list) fan out over all shards. */

#ifndef PICOMESH_PLUGIN_RELATIONAL_STORAGE_RELATIONAL_SQL_H
#define PICOMESH_PLUGIN_RELATIONAL_STORAGE_RELATIONAL_SQL_H

#include <picomesh/plugin/relational_storage/relational_storage.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/ycore/result.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shard 0. Use only for a table that is intentionally single-shard global; the
 * default for sharded state is `h.shard = uid`. */
#define REL_SHARD_GLOBAL 0u

struct rel_handle {
    struct ctx c;
    struct object *obj;
    uint32_t shard;        /* routing key for the next op (uid for user state) */
};

/* Open a NAMED relational_storage cluster instance (a mesh service). One
 * instance == one cluster == one shard set. `instance` is the service name the
 * caller is wired to (e.g. "rstore_uid", "rstore_session"). */
static inline struct picomesh_void_result rel_open(struct rel_handle *out, const char *instance)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(picomesh_void, "relational: no active engine");
    out->c = picomesh_engine_service_ctx(engine, instance ? instance : "relational_storage");
    out->shard = REL_SHARD_GLOBAL;
    struct object_ptr_result o = relational_storage_db_create(&out->c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_void, "relational: db_create failed", o);
    out->obj = o.value;
    return PICOMESH_OK_VOID();
}

/* The configured shard count (default 8). Cheap config read; only the once-per-
 * worker DDL broadcast and the rare aggregates call it — never the hot path. */
static inline int rel_shard_count(void)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    const struct yconfig *cfg = engine ? picomesh_engine_config(engine) : NULL;
    int shards = 8;
    if (cfg) {
        struct yconfig_node_ptr_result sr = yconfig_get(cfg, "relational_storage.shards");
        if (PICOMESH_IS_OK(sr) && sr.value) shards = (int)yconfig_node_as_int(sr.value, 8);
        else if (PICOMESH_IS_ERR(sr)) picomesh_error_destroy(sr.error);
    }
    return shards < 1 ? 1 : shards;
}

static inline struct picomesh_json_result
rel_exec(struct rel_handle *h, struct yheaders *hdrs, const char *sql, const char *args_json)
{
    return relational_storage_db_exec(&h->c, h->obj, hdrs, h->shard, sql, args_json ? args_json : "[]");
}

static inline struct picomesh_json_result
rel_query(struct rel_handle *h, struct yheaders *hdrs, const char *sql, const char *args_json)
{
    return relational_storage_db_query(&h->c, h->obj, hdrs, h->shard, sql, args_json ? args_json : "[]");
}

/* Finish a JSON bind-args array (the caller filled via yjson_writer_*). Returns
 * an owned JSON string the caller frees; frees the writer. */
static inline char *rel_args_take(struct yjson_writer *writer)
{
    yjson_writer_end_array(writer);
    size_t len = 0;
    const char *data = yjson_writer_data(writer, &len);
    char *out = strdup(data ? data : "[]");
    yjson_writer_free(writer);
    return out;
}

/* Ensure the plugin's own table exists on EVERY shard — rows spread across
 * shards by uid, so the table must exist in each. Runs the `ddl` (a single
 * CREATE TABLE IF NOT EXISTS) once per worker, guarded by `*ensured`, so there
 * is no per-call DDL on the hot path. */
static inline struct picomesh_void_result
rel_ensure_schema(struct rel_handle *h, struct yheaders *hdrs, int *ensured, const char *ddl)
{
    if (ensured && *ensured) return PICOMESH_OK_VOID();
    int shards = rel_shard_count();
    uint32_t saved = h->shard;
    for (int i = 0; i < shards; ++i) {
        h->shard = (uint32_t)i;
        struct picomesh_json_result r = rel_exec(h, hdrs, ddl, "[]");
        if (PICOMESH_IS_ERR(r)) { h->shard = saved; return PICOMESH_ERR(picomesh_void, "relational: schema create failed", r); }
        free(r.value);
    }
    h->shard = saved;
    if (ensured) *ensured = 1;
    return PICOMESH_OK_VOID();
}

/* Convenience bind-args builders (owned JSON, caller frees). */
static inline char *rel_args1s(const char *s)
{
    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_array(w);
    yjson_writer_string(w, s ? s : "");
    return rel_args_take(w);
}
static inline char *rel_args1i(int64_t i)
{
    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_array(w);
    yjson_writer_int(w, i);
    return rel_args_take(w);
}
static inline char *rel_args2i(int64_t a, int64_t b)
{
    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_array(w);
    yjson_writer_int(w, a);
    yjson_writer_int(w, b);
    return rel_args_take(w);
}

/* `changes` count of a write (rows affected), or -1 on backend error. */
static inline int rel_exec_changes(struct rel_handle *h, struct yheaders *hdrs,
                                   const char *sql, const char *args_json)
{
    struct picomesh_json_result r = rel_exec(h, hdrs, sql, args_json);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return -1; }
    int changes = -1;
    struct yjson_doc *doc = yjson_parse(r.value ? r.value : "{}", r.value ? strlen(r.value) : 2);
    if (doc) {
        changes = (int)yjson_as_int(yjson_object_get(yjson_doc_root(doc), "changes"), -1);
        yjson_doc_free(doc);
    }
    free(r.value);
    return changes;
}

/* First row's `col` as int64. `*found` (if non-NULL) is set to 1 when a row
 * matched. Returns `fallback` when no row / error / null. */
static inline int64_t rel_query_int(struct rel_handle *h, struct yheaders *hdrs,
                                    const char *sql, const char *args_json,
                                    const char *col, int64_t fallback, int *found)
{
    if (found) *found = 0;
    struct picomesh_json_result r = rel_query(h, hdrs, sql, args_json);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return fallback; }
    int64_t value = fallback;
    struct yjson_doc *doc = yjson_parse(r.value ? r.value : "[]", r.value ? strlen(r.value) : 2);
    if (doc) {
        const struct yjson_value *arr = yjson_doc_root(doc);
        if (arr && yjson_array_size(arr) > 0) {
            if (found) *found = 1;
            value = yjson_as_int(yjson_object_get(yjson_array_at(arr, 0), col), fallback);
        }
        yjson_doc_free(doc);
    }
    free(r.value);
    return value;
}

/* First row's `col` as an owned string (caller frees), or NULL when no row /
 * error / null value. */
static inline char *rel_query_str(struct rel_handle *h, struct yheaders *hdrs,
                                  const char *sql, const char *args_json, const char *col)
{
    struct picomesh_json_result r = rel_query(h, hdrs, sql, args_json);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return NULL; }
    char *out = NULL;
    struct yjson_doc *doc = yjson_parse(r.value ? r.value : "[]", r.value ? strlen(r.value) : 2);
    if (doc) {
        const struct yjson_value *arr = yjson_doc_root(doc);
        if (arr && yjson_array_size(arr) > 0) {
            const char *s = yjson_as_string(yjson_object_get(yjson_array_at(arr, 0), col), NULL);
            if (s) out = strdup(s);
        }
        yjson_doc_free(doc);
    }
    free(r.value);
    return out;
}

/* Aggregate fan-out: sum an integer column (e.g. COUNT(*)) over EVERY shard.
 * Cross-user totals are rare (admin / metrics), so the per-shard scatter is
 * acceptable; per-user reads never do this. */
static inline int64_t rel_query_int_all(struct rel_handle *h, struct yheaders *hdrs,
                                        const char *sql, const char *args_json, const char *col)
{
    int shards = rel_shard_count();
    uint32_t saved = h->shard;
    int64_t total = 0;
    for (int i = 0; i < shards; ++i) {
        h->shard = (uint32_t)i;
        total += rel_query_int(h, hdrs, sql, args_json, col, 0, NULL);
    }
    h->shard = saved;
    return total;
}

/* Aggregate fan-out: concatenate the row arrays of `sql` across EVERY shard into
 * one owned JSON array. Each shard returns a compact `[...]`; we splice their
 * inner contents. Ordering is per-shard then global (good enough for listings).
 */
static inline struct picomesh_json_result rel_query_all(struct rel_handle *h, struct yheaders *hdrs,
                                                        const char *sql, const char *args_json)
{
    int shards = rel_shard_count();
    uint32_t saved = h->shard;
    char *inner = NULL;
    size_t len = 0, cap = 0;
    int any = 0;
    for (int i = 0; i < shards; ++i) {
        h->shard = (uint32_t)i;
        struct picomesh_json_result r = rel_query(h, hdrs, sql, args_json);
        if (PICOMESH_IS_ERR(r)) { h->shard = saved; free(inner); return r; }
        const char *s = r.value ? r.value : "[]";
        size_t sl = strlen(s);
        if (sl >= 2 && s[0] == '[' && s[sl - 1] == ']' && sl > 2) {
            size_t add = sl - 2;                 /* contents between [ ] */
            size_t need = len + (any ? 1 : 0) + add + 1;
            if (need > cap) {
                size_t ncap = cap ? cap * 2 : 256;
                while (ncap < need) ncap *= 2;
                char *grown = realloc(inner, ncap);
                if (!grown) { free(inner); free(r.value); h->shard = saved;
                              return PICOMESH_ERR(picomesh_json, "relational: fan-out out of memory"); }
                inner = grown; cap = ncap;
            }
            if (any) inner[len++] = ',';
            memcpy(inner + len, s + 1, add);
            len += add;
            any = 1;
        }
        free(r.value);
    }
    h->shard = saved;
    char *out = malloc(len + 3);
    if (!out) { free(inner); return PICOMESH_ERR(picomesh_json, "relational: fan-out out of memory"); }
    out[0] = '[';
    if (len) memcpy(out + 1, inner, len);
    out[len + 1] = ']';
    out[len + 2] = 0;
    free(inner);
    return PICOMESH_OK(picomesh_json, out);
}

#endif /* PICOMESH_PLUGIN_RELATIONAL_STORAGE_RELATIONAL_SQL_H */
