/* ytelemetry — Picomesh telemetry: distributed-trace span context + a
 * simple, fire-and-forget span sender.
 *
 * This is the SENDER side (a plain module, no threads, no shared queue,
 * no mutex). It mints span ids, carries W3C / yheaders trace context, and
 * — when a span finishes — ships it to the trace collector by calling the
 * collector plugin's `ingest` method over the CURRENT worker's own
 * connection, fire-and-forget (async coroutine write, no reply waited on).
 * Each worker therefore writes its own spans on its own socket; there is
 * no cross-thread shared state on the hot path.
 *
 * The RECEIVER is the `trace_collector` plugin (annotated picoclass methods
 * + in-memory store). The collector is reached like any other backend —
 * it must be listed in this process's `remotes`; if it is not (or this IS
 * the collector), span ship-out is simply dropped.
 *
 * Distinct from two neighbours:
 *   - yspan (core/yspan.h) — a process-local latency aggregate
 *     (/_perf). ytelemetry_span_end still feeds it.
 *   - ydebug (core/ytrace.h) — gated line logging. */

#ifndef PICOMESH_CORE_YTELEMETRY_H
#define PICOMESH_CORE_YTELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yheaders;

enum ytelemetry_kind {
  YTELEMETRY_KIND_INTERNAL = 0,
  YTELEMETRY_KIND_SERVER = 1, /* this process received the call */
  YTELEMETRY_KIND_CLIENT = 2, /* this process is making a downstream call */
};

/* A span being timed, lived on the caller's stack. Id fields are
 * lowercase-hex, NUL-terminated strings. */
struct ytelemetry_span {
  char trace_id[33];  /* 32 hex (128-bit) */
  char span_id[17];   /* 16 hex (64-bit) */
  char parent_id[17]; /* 16 hex, "" when root */
  char name[64];
  uint8_t kind;
  int sampled;
  uint64_t start_mono_ns; /* for duration */
  uint64_t start_unix_ns; /* wall-clock start */
  uint32_t uid;           /* picomesh.uid attribute (0 = anon) */
};

/* ---- W3C traceparent --------------------------------------------------
 * "<version>-<32hex trace>-<16hex span>-<2hex flags>", version 00. */

int ytelemetry_traceparent_parse(const char *s, char *trace_id_hex,
                                 char *span_id_hex, int *sampled);
void ytelemetry_traceparent_format(char *buf, size_t cap,
                                   const char *trace_id_hex,
                                   const char *span_id_hex, int sampled);

void ytelemetry_mint_trace_id(char out[33]);
void ytelemetry_mint_span_id(char out[17]);

/* ---- trace context on yheaders (keys: trace_id, parent_span_id, sampled) --
 */

/* Seed a fresh root context for an inbound request: continue an inbound
 * W3C traceparent if valid, else mint a new trace id with no parent. */
void ytelemetry_hdrs_seed_root(struct yheaders *hdrs,
                               const char *inbound_traceparent);

/* Begin a SERVER span: take trace_id + parent_span_id from hdrs (minting a
 * trace_id if absent), mint this hop's span_id, and set hdrs.parent_span_id
 * = span_id so the impl's downstream calls nest under it. */
void ytelemetry_server_span_begin(struct ytelemetry_span *sp,
                                  struct yheaders *hdrs, const char *name);

/* Begin a CLIENT span for a downstream call: take trace_id + parent from
 * hdrs, mint this span's id. Does NOT mutate hdrs. */
void ytelemetry_client_span_begin(struct ytelemetry_span *sp,
                                  struct yheaders *hdrs, const char *name);

/* Serialize hdrs for the wire with parent_span_id temporarily set to this
 * client span's id (restored before return), so the remote peer parents its
 * server span to this client span. Same contract as yheaders_serialize. */
size_t ytelemetry_client_serialize_headers(const struct ytelemetry_span *sp,
                                           struct yheaders *hdrs, void *buf,
                                           size_t cap);

/* Finish a span: compute the duration, feed the local /_perf aggregate, and
 * ship the span to the collector (fire-and-forget, best-effort). ok=0 marks
 * status=error; `err` (optional) is a short message. */
void ytelemetry_span_end(struct ytelemetry_span *sp, int ok, const char *err);

/* Ship the calling thread's pending span batch to the collector now. Ends in a
 * yielding write, so the caller MUST be a coroutine (the engine runs the
 * periodic flush inside a spawned coro for this reason). No-op if empty. */
void ytelemetry_flush_local(void);

/* Number of spans buffered in the calling thread's batch (lets the flush timer
 * skip spawning a coroutine when there is nothing to ship). */
int ytelemetry_pending_local(void);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_CORE_YTELEMETRY_H */
