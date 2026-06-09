/* relational_sql — thin helpers for plugins that store their state as real SQL
 * rows in the `relational_storage` service (instead of faking columns with
 * prefixed keys in a KV store). Header-only static inline so each plugin TU
 * gets its own copy; no link coupling beyond the relational_storage stubs.
 *
 *   rel_open(&h)                     -> open the relational_storage service
 *   h.shard = <key>                  -> route subsequent ops to shard <key>%N
 *   rel_exec(&h,hdrs,sql,args)       -> write/DDL; result
 * {changes,last_insert_rowid} rel_query(&h,hdrs,sql,args)      -> SELECT;
 * result [{col:val}, …] rel_exec_changes(...)            -> the `changes` count
 * of a write rel_query_int(...,col,fallback)  -> first row's `col` as int64 (or
 * fallback) rel_query_str(...,col)           -> first row's `col` as owned
 * string (or NULL) rel_query_int_all(...,col)       -> sum `col` across EVERY
 * shard (aggregate) rel_query_page(...,col,desc,off,lim) -> fan-out + GLOBAL
 * sort/offset/limit (limit<=0 = every row, still ordered) rel_args_take(writer)
 * -> finish a bind-args array writer -> owned JSON
 *
 * SHARDING. The relational engine keeps N SQLite shards and serializes each
 * shard behind its own mutex, so throughput scales only when rows SPREAD across
 * shards. The caller sets the routing key with `h.shard = <key>` before an op;
 * the engine routes `key % N` and never interprets the key. Two patterns (see
 * docs/sharded-relational-storage.md):
 *   - data tables shard by the owning `uid` (`h.shard = uid`), co-locating a
 *     user's rows in one shard;
 *   - identity-lookup tables shard by `hash(external_id)`
 *     (`h.shard = hash(username|session_id|token)`) and map that id to a uid —
 *     tokens stay opaque/random; NOTHING is embedded in them for routing.
 * The per-plugin DDL is created on EVERY shard (rel_ensure_schema); the rare
 * cross-shard aggregates (count, list) fan out over all shards. Fan-out always
 * iterates the OPENED instance's shard count (rel_handle_shard_count), never
 * the caller process's local config. See docs/sharded-relational-storage.md. */

#ifndef PICOMESH_PLUGIN_RELATIONAL_STORAGE_RELATIONAL_SQL_H
#define PICOMESH_PLUGIN_RELATIONAL_STORAGE_RELATIONAL_SQL_H

#include <picomesh/config/config.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/engine/engine.h>
#include <picomesh/json/json.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/plugin/relational_storage/relational_storage.h>

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
  const char *db;  /* named logical database this handle routes to */
  uint32_t shard;  /* routing key for the next op (uid for user state) */
  int shard_count; /* opened database's shard count, fetched lazily (0 = not
                      yet) */
};

/* Open a handle onto a named logical database. `service` is the mesh service
 * the caller is wired to (the relational_storage instance — local in collocated
 * mode, a remote `rstore_*` process in the split mesh); `db` is the logical
 * database WITHIN that instance to route every op to. One instance hosts many
 * databases, each its own shard set/count/schema. In the split mesh a service
 * serves a single (default) database from its flat config, so `db` is just the
 * name its `relational_storage.databases.<db>.*` block uses (or is ignored when
 * the service has only the flat config). */
static inline struct picomesh_void_result
rel_open(struct rel_handle *out, const char *service, const char *db) {
  struct picomesh_engine *engine = picomesh_active_engine();
  if (!engine)
    return PICOMESH_ERR(picomesh_void, "relational: no active engine");
  out->c = picomesh_engine_service_ctx(engine, service ? service
                                                       : "relational_storage");
  out->db = db ? db : "default";
  out->shard = REL_SHARD_GLOBAL;
  out->shard_count = 0;
  struct object_ptr_result o = relational_storage_db_create(&out->c);
  if (PICOMESH_IS_ERR(o))
    return PICOMESH_ERR(picomesh_void, "relational: db_create failed", o);
  out->obj = o.value;
  return PICOMESH_OK_VOID();
}

/* The OPENED instance's shard count. Fan-out (DDL broadcast, cross-shard
 * aggregates, pagination) must iterate the serving instance's real shard count,
 * NOT the caller process's local `relational_storage.shards` config: in the
 * split mesh the consumer is wired to a remote cluster and its local config
 * need not match. A wrong count misses shards (undercount) or wraps onto
 * already-queried shards (double-count). So the count is fetched once per
 * handle from the instance itself (db_shard_count) and cached on the handle.
 * Point reads/writes never call this — they set h->shard directly — so the hot
 * path pays nothing; only the once-per-worker DDL broadcast and the rare
 * aggregates do.
 *
 * On a backend error this PROPAGATES the error rather than degrading to a
 * guessed count: a silent fallback to 1 would make the DDL broadcast create the
 * table on shard 0 only (then mark the schema "ensured"), and make aggregates
 * scan a single shard — both fail silently and partially once rows spread. The
 * count is cached on the handle ONLY on success, so a transient failure is
 * retried on the next call. */
static inline struct picomesh_int_result
rel_handle_shard_count(struct rel_handle *h, struct yheaders *hdrs) {
  if (h->shard_count > 0)
    return PICOMESH_OK(picomesh_int, h->shard_count);
  struct picomesh_int_result r =
      relational_storage_db_shard_count(&h->c, h->obj, hdrs, h->db);
  if (PICOMESH_IS_ERR(r))
    return PICOMESH_ERR(picomesh_int, "relational: shard count unavailable", r);
  h->shard_count = r.value < 1 ? 1 : r.value;
  return PICOMESH_OK(picomesh_int, h->shard_count);
}

static inline struct picomesh_json_result rel_exec(struct rel_handle *h,
                                                   struct yheaders *hdrs,
                                                   const char *sql,
                                                   const char *args_json) {
  return relational_storage_db_exec(&h->c, h->obj, hdrs, h->db, h->shard, sql,
                                    args_json ? args_json : "[]");
}

static inline struct picomesh_json_result rel_query(struct rel_handle *h,
                                                    struct yheaders *hdrs,
                                                    const char *sql,
                                                    const char *args_json) {
  return relational_storage_db_query(&h->c, h->obj, hdrs, h->db, h->shard, sql,
                                     args_json ? args_json : "[]");
}

/* Finish a JSON bind-args array (the caller filled via json_writer_*). Returns
 * an owned JSON string the caller frees; frees the writer. */
static inline char *rel_args_take(struct json_writer *writer) {
  json_writer_end_array(writer);
  size_t len = 0;
  const char *data = json_writer_data(writer, &len);
  char *out = strdup(data ? data : "[]");
  json_writer_free(writer);
  return out;
}

/* Ensure the plugin's own table exists on EVERY shard — rows spread across
 * shards by uid, so the table must exist in each. Runs the `ddl` (a single
 * CREATE TABLE IF NOT EXISTS) once per worker, guarded by `*ensured`, so there
 * is no per-call DDL on the hot path. */
static inline struct picomesh_void_result
rel_ensure_schema(struct rel_handle *h, struct yheaders *hdrs, int *ensured,
                  const char *ddl) {
  if (ensured && *ensured)
    return PICOMESH_OK_VOID();
  struct picomesh_int_result sc = rel_handle_shard_count(h, hdrs);
  if (PICOMESH_IS_ERR(sc))
    return PICOMESH_ERR(picomesh_void,
                        "relational: schema fan-out shard count failed", sc);
  int shards = sc.value;
  uint32_t saved = h->shard;
  for (int i = 0; i < shards; ++i) {
    h->shard = (uint32_t)i;
    struct picomesh_json_result r = rel_exec(h, hdrs, ddl, "[]");
    if (PICOMESH_IS_ERR(r)) {
      h->shard = saved;
      return PICOMESH_ERR(picomesh_void, "relational: schema create failed", r);
    }
    free(r.value);
  }
  h->shard = saved;
  if (ensured)
    *ensured = 1;
  return PICOMESH_OK_VOID();
}

/* Convenience bind-args builders (owned JSON, caller frees). */
static inline char *rel_args1s(const char *s) {
  struct json_writer *w = json_writer_new();
  json_writer_begin_array(w);
  json_writer_string(w, s ? s : "");
  return rel_args_take(w);
}
static inline char *rel_args1i(int64_t i) {
  struct json_writer *w = json_writer_new();
  json_writer_begin_array(w);
  json_writer_int(w, i);
  return rel_args_take(w);
}
static inline char *rel_args2i(int64_t a, int64_t b) {
  struct json_writer *w = json_writer_new();
  json_writer_begin_array(w);
  json_writer_int(w, a);
  json_writer_int(w, b);
  return rel_args_take(w);
}

/* `changes` count of a write (rows affected) as a Result. A backend failure
 * propagates; a successful write yields the affected-row count. */
static inline struct picomesh_int_result
rel_exec_changes(struct rel_handle *h, struct yheaders *hdrs, const char *sql,
                 const char *args_json) {
  struct picomesh_json_result r = rel_exec(h, hdrs, sql, args_json);
  if (PICOMESH_IS_ERR(r))
    return PICOMESH_ERR(picomesh_int, "relational exec failed", r);
  int changes = -1;
  struct json_doc *doc =
      json_parse(r.value ? r.value : "{}", r.value ? strlen(r.value) : 2);
  if (doc) {
    changes =
        (int)json_as_int(json_object_get(json_doc_root(doc), "changes"), -1);
    json_doc_free(doc);
  }
  free(r.value);
  return PICOMESH_OK(picomesh_int, changes);
}

/* First row's `col` as int64, AS A RESULT. `*found` (if non-NULL) is set to 1
 * when a row matched. A backend failure propagates; a query that ran but
 * matched no row (or a null value) is NOT an error and yields `fallback`. */
static inline struct picomesh_int64_result
rel_query_int(struct rel_handle *h, struct yheaders *hdrs, const char *sql,
              const char *args_json, const char *col, int64_t fallback,
              int *found) {
  if (found)
    *found = 0;
  struct picomesh_json_result r = rel_query(h, hdrs, sql, args_json);
  if (PICOMESH_IS_ERR(r))
    return PICOMESH_ERR(picomesh_int64, "relational query failed", r);
  int64_t value = fallback;
  struct json_doc *doc =
      json_parse(r.value ? r.value : "[]", r.value ? strlen(r.value) : 2);
  if (doc) {
    const struct json_value *arr = json_doc_root(doc);
    if (arr && json_array_size(arr) > 0) {
      if (found)
        *found = 1;
      value =
          json_as_int(json_object_get(json_array_at(arr, 0), col), fallback);
    }
    json_doc_free(doc);
  }
  free(r.value);
  return PICOMESH_OK(picomesh_int64, value);
}

/* First row's `col` as int64, AS A RESULT. Unlike rel_query_int (which
 * collapses a backend/query/parse failure to the fallback so a point read can
 * use the fallback as "no row"), this PROPAGATES any such failure — for the
 * aggregate fan-out where a single broken shard must fail the whole total, not
 * silently contribute 0. A query that ran but matched no row (or a null value)
 * is NOT an error: it yields `fallback`. */
static inline struct picomesh_int64_result
rel_query_int_result(struct rel_handle *h, struct yheaders *hdrs,
                     const char *sql, const char *args_json, const char *col,
                     int64_t fallback) {
  struct picomesh_json_result r = rel_query(h, hdrs, sql, args_json);
  if (PICOMESH_IS_ERR(r))
    return PICOMESH_ERR(picomesh_int64, "relational: query failed", r);
  struct json_doc *doc =
      json_parse(r.value ? r.value : "[]", r.value ? strlen(r.value) : 2);
  if (!doc) {
    free(r.value);
    return PICOMESH_ERR(picomesh_int64, "relational: malformed query result");
  }
  int64_t value = fallback;
  const struct json_value *arr = json_doc_root(doc);
  if (arr && json_array_size(arr) > 0)
    value = json_as_int(json_object_get(json_array_at(arr, 0), col), fallback);
  json_doc_free(doc);
  free(r.value);
  return PICOMESH_OK(picomesh_int64, value);
}

/* First row's `col` as an owned string (caller frees), AS A RESULT. A backend
 * failure propagates; the OK value is the owned string, or NULL when no row /
 * null value. */
static inline struct picomesh_string_result
rel_query_str(struct rel_handle *h, struct yheaders *hdrs, const char *sql,
              const char *args_json, const char *col) {
  struct picomesh_json_result r = rel_query(h, hdrs, sql, args_json);
  if (PICOMESH_IS_ERR(r))
    return PICOMESH_ERR(picomesh_string, "relational query failed", r);
  char *out = NULL;
  struct json_doc *doc =
      json_parse(r.value ? r.value : "[]", r.value ? strlen(r.value) : 2);
  if (doc) {
    const struct json_value *arr = json_doc_root(doc);
    if (arr && json_array_size(arr) > 0) {
      const char *s =
          json_as_string(json_object_get(json_array_at(arr, 0), col), NULL);
      if (s)
        out = strdup(s);
    }
    json_doc_free(doc);
  }
  free(r.value);
  return PICOMESH_OK(picomesh_string, out);
}

/* Aggregate fan-out: sum an integer column (e.g. COUNT(*)) over EVERY shard.
 * Cross-user totals are rare (admin / metrics), so the per-shard scatter is
 * acceptable; per-user reads never do this. FAILS on the first error — a wrong
 * shard count OR any per-shard query/parse failure — so a broken shard surfaces
 * as an error instead of silently dropping that shard's rows from the total. */
static inline struct picomesh_int64_result
rel_query_int_all(struct rel_handle *h, struct yheaders *hdrs, const char *sql,
                  const char *args_json, const char *col) {
  struct picomesh_int_result sc = rel_handle_shard_count(h, hdrs);
  if (PICOMESH_IS_ERR(sc))
    return PICOMESH_ERR(picomesh_int64,
                        "relational: aggregate shard count failed", sc);
  int shards = sc.value;
  uint32_t saved = h->shard;
  int64_t total = 0;
  for (int i = 0; i < shards; ++i) {
    h->shard = (uint32_t)i;
    struct picomesh_int64_result part =
        rel_query_int_result(h, hdrs, sql, args_json, col, 0);
    if (PICOMESH_IS_ERR(part)) {
      h->shard = saved;
      return PICOMESH_ERR(picomesh_int64,
                          "relational: per-shard aggregate query failed", part);
    }
    total += part.value;
  }
  h->shard = saved;
  return PICOMESH_OK(picomesh_int64, total);
}

/* ---- cross-shard pagination ------------------------------------------ */

/* One merged row: its integer sort key, plus a borrowed span into the shard
 * result string the row text lives in (kept alive until the merged output is
 * built). `seq` is the global append order, used only as a stable tiebreaker so
 * equal keys produce a deterministic page. */
struct rel_page_row {
  int64_t key;
  uint32_t seq;
  const char *json;
  size_t json_len;
};

static inline int rel_page_row_cmp_asc(const void *lhs, const void *rhs) {
  const struct rel_page_row *a = lhs, *b = rhs;
  if (a->key < b->key)
    return -1;
  if (a->key > b->key)
    return 1;
  return a->seq < b->seq ? -1 : (a->seq > b->seq ? 1 : 0);
}
static inline int rel_page_row_cmp_desc(const void *lhs, const void *rhs) {
  const struct rel_page_row *a = lhs, *b = rhs;
  if (a->key > b->key)
    return -1;
  if (a->key < b->key)
    return 1;
  return a->seq < b->seq
             ? -1
             : (a->seq > b->seq ? 1 : 0); /* ties: stable append order */
}

/* Split a compact JSON array string into the [ptr,len) spans of its top-level
 * elements (objects). Skips string literals so braces/commas inside string
 * values do not confuse the scan. Writes up to `max` spans; returns the count.
 */
static inline size_t rel_json_array_spans(const char *s, size_t slen,
                                          struct rel_page_row *rows,
                                          size_t base, size_t max) {
  size_t count = 0, idx = 0;
  int depth = 0, in_string = 0, escaped = 0;
  const char *elem = NULL;
  while (idx < slen && s[idx] != '[')
    idx++;
  if (idx >= slen)
    return 0;
  idx++; /* past '[' */
  for (; idx < slen; ++idx) {
    char c = s[idx];
    if (in_string) {
      if (escaped)
        escaped = 0;
      else if (c == '\\')
        escaped = 1;
      else if (c == '"')
        in_string = 0;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!elem)
        continue;
    }
    if (!elem && c != ',' && c != ']')
      elem = &s[idx];
    if (c == '"') {
      in_string = 1;
      continue;
    }
    if (c == '{' || c == '[') {
      depth++;
      continue;
    }
    if (c == '}') {
      if (depth > 0)
        depth--;
      continue;
    }
    if (c == ']') {
      if (depth > 0) {
        depth--;
        continue;
      }
      /* outer close: flush the last element */
      if (elem) {
        size_t n = (size_t)(&s[idx] - elem);
        while (n && (elem[n - 1] == ' ' || elem[n - 1] == '\t' ||
                     elem[n - 1] == '\n' || elem[n - 1] == '\r'))
          n--;
        if (count < max && n) {
          rows[base + count].json = elem;
          rows[base + count].json_len = n;
          count++;
        }
      }
      break;
    }
    if (c == ',' && depth == 0) {
      if (elem) {
        size_t n = (size_t)(&s[idx] - elem);
        while (n && (elem[n - 1] == ' ' || elem[n - 1] == '\t' ||
                     elem[n - 1] == '\n' || elem[n - 1] == '\r'))
          n--;
        if (count < max && n) {
          rows[base + count].json = elem;
          rows[base + count].json_len = n;
          count++;
        }
      }
      elem = NULL;
    }
  }
  return count;
}

/* Cross-shard pagination, done correctly: fan out the same query to EVERY
 * shard with a per-shard top-K cap, merge the rows, sort GLOBALLY by an integer
 * `order_col`, then apply the global `offset`/`limit`. A per-shard LIMIT/OFFSET
 * cannot work — it limits/offsets each shard independently and orders only
 * shard-locally — so this helper takes the BASE query WITHOUT ORDER BY/LIMIT
 * and appends them itself. `order_col` must be one of the columns the base
 * query selects and must be integer-valued (uid, created_at, …); it is a
 * caller-fixed identifier, never client input, so splicing it into the SQL is
 * safe.
 *
 * `limit <= 0` means "no limit" (every row from `offset` on). The per-shard cap
 * is `offset + limit`: the global window [offset, offset+limit) is a subset of
 * each shard's own top `offset+limit` rows under the same ordering, so pulling
 * that many from each shard is sufficient and bounded. Returns an owned compact
 * JSON array (caller frees `.value`). */
static inline struct picomesh_json_result
rel_query_page(struct rel_handle *h, struct yheaders *hdrs,
               const char *base_sql, const char *args_json,
               const char *order_col, int descending, int64_t offset,
               int64_t limit) {
  struct picomesh_int_result sc = rel_handle_shard_count(h, hdrs);
  if (PICOMESH_IS_ERR(sc))
    return PICOMESH_ERR(picomesh_json,
                        "relational: pagination shard count failed", sc);
  int shards = sc.value;
  if (offset < 0)
    offset = 0;
  int64_t cap = (limit > 0) ? offset + limit : 0; /* 0 ⇒ no per-shard LIMIT */

  char sql[512];
  if (cap > 0)
    snprintf(sql, sizeof(sql), "%s ORDER BY %s %s LIMIT %lld", base_sql,
             order_col, descending ? "DESC" : "ASC", (long long)cap);
  else
    snprintf(sql, sizeof(sql), "%s ORDER BY %s %s", base_sql, order_col,
             descending ? "DESC" : "ASC");

  char **shard_results =
      (char **)calloc(shards > 0 ? (size_t)shards : 1, sizeof(char *));
  if (!shard_results)
    return PICOMESH_ERR(picomesh_json, "relational: pagination out of memory");

  struct rel_page_row *rows = NULL;
  size_t nrows = 0, rows_cap = 0;
  uint32_t saved = h->shard;

  for (int i = 0; i < shards; ++i) {
    h->shard = (uint32_t)i;
    struct picomesh_json_result r = rel_query(h, hdrs, sql, args_json);
    if (PICOMESH_IS_ERR(r)) {
      h->shard = saved;
      for (int j = 0; j < i; ++j)
        free(shard_results[j]);
      free(shard_results);
      free(rows);
      return r;
    }
    shard_results[i] = r.value; /* keep alive: row spans point into it */
    const char *body = r.value ? r.value : "[]";
    size_t body_len = strlen(body);

    struct json_doc *doc = json_parse(body, body_len);
    const struct json_value *arr = doc ? json_doc_root(doc) : NULL;
    if (!doc || !arr || !json_is_array(arr)) {
      /* A shard that returned non-array / malformed JSON must fail the
       * whole listing, not silently contribute zero rows (same posture as
       * the aggregate fan-out). */
      if (doc)
        json_doc_free(doc);
      h->shard = saved;
      for (int j = 0; j <= i; ++j)
        free(shard_results[j]);
      free(shard_results);
      free(rows);
      return PICOMESH_ERR(picomesh_json,
                          "relational: malformed shard result in pagination");
    }
    size_t shard_n = json_array_size(arr);
    if (shard_n) {
      if (nrows + shard_n > rows_cap) {
        size_t ncap = rows_cap ? rows_cap * 2 : 64;
        while (ncap < nrows + shard_n)
          ncap *= 2;
        struct rel_page_row *grown =
            (struct rel_page_row *)realloc(rows, ncap * sizeof(*rows));
        if (!grown) {
          json_doc_free(doc);
          h->shard = saved;
          for (int j = 0; j <= i; ++j)
            free(shard_results[j]);
          free(shard_results);
          free(rows);
          return PICOMESH_ERR(picomesh_json,
                              "relational: pagination out of memory");
        }
        rows = grown;
        rows_cap = ncap;
      }
      size_t got = rel_json_array_spans(body, body_len, rows, nrows, shard_n);
      if (got != shard_n) {
        /* The hand-written span scanner and the JSON parser disagree on
         * the row count — a row would be silently dropped from the page.
         * Fail rather than truncate. */
        json_doc_free(doc);
        h->shard = saved;
        for (int j = 0; j <= i; ++j)
          free(shard_results[j]);
        free(shard_results);
        free(rows);
        return PICOMESH_ERR(picomesh_json,
                            "relational: pagination row-count mismatch");
      }
      for (size_t k = 0; k < got; ++k) {
        rows[nrows + k].key =
            json_as_int(json_object_get(json_array_at(arr, k), order_col), 0);
        rows[nrows + k].seq = (uint32_t)(nrows + k);
      }
      nrows += got;
    }
    json_doc_free(doc);
  }
  h->shard = saved;

  if (nrows > 1)
    qsort(rows, nrows, sizeof(*rows),
          descending ? rel_page_row_cmp_desc : rel_page_row_cmp_asc);

  size_t start = (size_t)offset < nrows ? (size_t)offset : nrows;
  size_t end = nrows;
  if (limit > 0) {
    size_t want = start + (size_t)limit;
    if (want < end)
      end = want;
  }

  size_t out_len = 2; /* "[" + "]" */
  for (size_t i = start; i < end; ++i)
    out_len += rows[i].json_len + (i > start ? 1 : 0);
  char *out = (char *)malloc(out_len + 1);
  if (!out) {
    for (int j = 0; j < shards; ++j)
      free(shard_results[j]);
    free(shard_results);
    free(rows);
    return PICOMESH_ERR(picomesh_json, "relational: pagination out of memory");
  }
  size_t at = 0;
  out[at++] = '[';
  for (size_t i = start; i < end; ++i) {
    if (i > start)
      out[at++] = ',';
    memcpy(out + at, rows[i].json, rows[i].json_len);
    at += rows[i].json_len;
  }
  out[at++] = ']';
  out[at] = 0;

  for (int j = 0; j < shards; ++j)
    free(shard_results[j]);
  free(shard_results);
  free(rows);
  return PICOMESH_OK(picomesh_json, out);
}

#endif /* PICOMESH_PLUGIN_RELATIONAL_STORAGE_RELATIONAL_SQL_H */
