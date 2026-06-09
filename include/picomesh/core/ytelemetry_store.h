/* ytelemetry_store — the trace collector's in-memory span store.
 *
 * A process-global, bounded ring of structured spans ingested from the
 * mesh (one JSON object per span, posted to the collector's /v1/spans).
 * Indexing is a linear scan over the ring — O(n) over a bounded set,
 * which is plenty for a dev/in-memory collector; there is deliberately
 * no durable storage and no secondary index. When the ring is full the
 * oldest span is evicted; on query, spans older than max_age are skipped.
 *
 * The query writers emit JSON straight into a json_writer the HTTP
 * layer owns, in shapes a waterfall/aggregate UI can render. */

#ifndef PICOMESH_CORE_YTELEMETRY_STORE_H
#define PICOMESH_CORE_YTELEMETRY_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct json_writer;

/* Size the ring and set retention. Safe to call once; calling again is a
 * no-op (the first sizing wins). If a query/ingest runs before init, the
 * store lazily inits with defaults. max_spans 0 ⇒ default; max_age 0 ⇒ no
 * age limit. */
/* Tunables for the in-memory store. Any field left 0 takes its built-in
 * default. All are read from the collector's `telemetry:` config block. */
struct ytelemetry_store_config {
    size_t max_spans;         /* total capacity across all shards (default 50000) */
    uint64_t max_age_seconds; /* drop spans older than this (0 = no age limit) */
    unsigned shards;          /* parallel ingest partitions (default 16) */
    size_t bucket_spans;      /* per-shard arena batch before a flush (default 256) */
    uint64_t flush_ms;        /* periodic arena flush interval (default 50) */
};

/* Size and configure the store. First call wins (idempotent). */
void ytelemetry_store_init_config(const struct ytelemetry_store_config *config);

/* Back-compat shorthand: only the two retention knobs, defaults for the rest. */
void ytelemetry_store_init(size_t max_spans, uint64_t max_age_seconds);

/* Ingest one JSON span object (the body of one NDJSON line). Returns 1 if
 * accepted, 0 if malformed/missing required fields. Accepted spans land in the
 * calling thread's lock-free arena and become visible to queries on the next
 * flush (bucket-full, the periodic time flush, or ytelemetry_store_flush_local). */
int ytelemetry_store_ingest_json(const char *json, size_t len);

/* Ingest a batch payload: a JSON array of span objects (the batched sender
 * path), or a single span object (back-compat). The whole payload is parsed
 * once. Returns the number of spans accepted. */
int ytelemetry_store_ingest_batch_json(const char *json, size_t len);

/* Flush the calling thread's accumulation arena into the shared store. A
 * collector worker calls this from a periodic timer on its own event loop to
 * bound query staleness; it is thread-confined and takes only the brief
 * per-shard flush locks. No-op if the calling thread has no pending spans. */
void ytelemetry_store_flush_local(void);

/* ---- query writers (emit into a caller-owned json_writer) ----------- */

/* {trace_id, root_span_id, duration_ns, span_count, spans:[...]} */
void ytelemetry_store_write_trace(struct json_writer *w, const char *trace_id);
/* {traces:[{trace_id, root_name, service_name, start_time_ns, duration_ns,
 *           span_count, status}...]} filtered by service/since/status. Any
 * filter NULL/0 means "any". */
void ytelemetry_store_write_traces(struct json_writer *w, const char *service,
                             uint64_t since_ns, const char *status);
/* {services:[{service_name, span_count}...]} */
void ytelemetry_store_write_services(struct json_writer *w);
/* {service, operations:[{name, count}...]} */
void ytelemetry_store_write_operations(struct json_writer *w, const char *service);
/* {service, operation, window_ns, count, p50_ns, p90_ns, p99_ns, max_ns} */
void ytelemetry_store_write_latency(struct json_writer *w, const char *service,
                             const char *operation, uint64_t window_ns);
/* {errors:[{...span...}...]} with status=error and start >= since_ns. */
void ytelemetry_store_write_errors(struct json_writer *w, uint64_t since_ns);
/* {ingested, malformed, evicted, stored, capacity, max_age_seconds} — health
 * counters so dropped/malformed spans are not invisible. */
void ytelemetry_store_write_stats(struct json_writer *w);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_CORE_YTELEMETRY_STORE_H */
