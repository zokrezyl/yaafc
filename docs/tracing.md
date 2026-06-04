# Distributed tracing — the Picomesh trace collector

Picomesh ships a small, in-memory, OpenTelemetry-shaped **trace collector**
(issue #11). It is native to picomesh and depends on no external system
(Jaeger/Tempo/OTLP) for v1 — but uses standard trace concepts so an OTLP
exporter can be added later without reworking propagation.

It replaces the old `yspan` + `/_trace` story, which was a *local op-latency
aggregate* misnamed as tracing. That aggregate still exists (now at `/_perf`,
see `docs/perf.md`); this document covers the real distributed tracer.

## Shape

```
service / gateway process
  └─ ytelemetry (sender, core module: ycore/ytelemetry.{h,c})
       ├─ W3C traceparent in/out + yheaders trace context
       └─ on span end → fire-and-forget yrpc call to the collector's
          trace_collector_ingest method, over THIS worker's own connection
                                                   │
trace_collector plugin (a normal yrpc backend)      ▼
  ├─ trace_collector_ingest(span_json)  receives spans
  ├─ in-memory bounded store        (ycore/ytelemetry_store.{h,c})
  └─ query methods                  trace_collector_traces / _get_trace /
                                    _services / _operations / _latency /
                                    _errors / _stats
```

The collector is a **service-driven backend plugin**, not a hardcoded HTTP
surface. Senders reach it the same way they reach any backend — it is listed
in their `remotes`. Queries go through the gateway's `/_rpc` (and show up in
`/_describe`), exactly like every other service. There are **no bespoke
`/v1/spans` or `/traces/...` HTTP routes**, and **no separate exporter
thread/process** — the previous design had those; the implemented one does not.

## Trace context propagation

- **HTTP (gateway boundary).** A request to the gateway either carries a W3C
  `traceparent` (`00-<32hex trace>-<16hex span>-<2hex flags>`) — which is
  continued — or starts a fresh trace. The gateway echoes the resulting
  `traceparent` on the response so clients/UIs can correlate.
- **Internal (yrpc).** Context rides in the request-header bag (`yheaders`),
  keys `trace_id`, `parent_span_id`, `sampled`, serialized ahead of the business
  args (same carrier as `uid`). No JWT, no secrets.

On each hop a service:

1. reads the inbound context,
2. mints a new `span_id` (server span), parented to the incoming current span,
3. rewrites the bag's `parent_span_id` to its own span so the impl's downstream
   calls nest beneath it,
4. for each downstream call, the generated client stub opens a CLIENT span and
   tells the remote peer to parent to it,
5. ends/ships the span when the op finishes.

All of step 1–5 is done by `ytelemetry_*` helpers called from the gateway and
the generated RPC client stubs / server skels — application code is untouched.

## Span ship-out (sender)

When a span ends, the sender ships it to the collector by calling
`trace_collector.trace_collector_ingest(<span json>)` **fire-and-forget over the current
worker's own connection** (`rpc_call_oneway`, req_id 0; the async reader drains
the unwanted reply). No shared queue, no background thread, no mutex on the
sender path. If the collector is not a remote of this process (or this *is* the
collector), the span is simply dropped. `ytelemetry_span_end` also feeds the
local `/_perf` aggregate.

> **Performance caveat (known).** Shipping one span per finished span over yrpc
> puts a real socket write on the request path; under high load a slow/single-
> worker collector backpressures and collapses throughput. Mitigations:
> run the collector multi-worker (`workers: N`, see below — gh#13) and move the
> sender to per-worker batched, drop-under-pressure emission. Tracked
> separately; do not treat the current per-span path as the final design.

## Span schema

Each ingested span (one JSON object passed to `trace_collector_ingest`):

| field | meaning |
|---|---|
| `trace_id` | 128-bit, 32 hex — the whole distributed request |
| `span_id` | 64-bit, 16 hex — this operation |
| `parent_span_id` | caller span, empty for the root |
| `name` | `gateway.<svc.class.method>` / `rpc.<method>` / `skel.<method>` |
| `kind` | `server` / `client` / `internal` |
| `service_name` | `gateway`, `git_repo`, `sharded_storage`, … (the `--name`) |
| `node_id` | `<service>:<pid>` |
| `start_time_ns` / `duration_ns` | wall-clock start, monotonic duration |
| `status` | `ok` / `error` (+ `error_message`) |
| `attributes` | bounded: `rpc.system`, `picomesh.uid` |

Attributes are kept small and carry **no secrets, tokens, or bodies**.

## Query API (through the gateway `/_rpc`)

The collector exposes these methods on its `trace_collector.store` class; call
them via the gateway like any backend. Each returns a JSON string in the
`/_rpc` `result` field.

```sh
# recent traces, filterable by service/status and lookback seconds
curl -s -XPOST :8090/_rpc -d '{"path":"trace_collector.trace_collector.traces","args":["gateway","",3600]}'
# whole trace as a parent/child tree
curl -s -XPOST :8090/_rpc -d '{"path":"trace_collector.trace_collector.get_trace","args":["<trace_id>"]}'
# services seen + span counts
curl -s -XPOST :8090/_rpc -d '{"path":"trace_collector.trace_collector.services","args":[]}'
# operations for a service
curl -s -XPOST :8090/_rpc -d '{"path":"trace_collector.trace_collector.operations","args":["git_repo"]}'
# p50/p90/p99/max for a service[/operation], window in seconds (0 = all)
curl -s -XPOST :8090/_rpc -d '{"path":"trace_collector.trace_collector.latency","args":["gateway","",0]}'
# recent error spans, since = lookback seconds (0 = all)
curl -s -XPOST :8090/_rpc -d '{"path":"trace_collector.trace_collector.errors","args":[0]}'
# health counters: ingested / malformed / evicted / stored / capacity
curl -s -XPOST :8090/_rpc -d '{"path":"trace_collector.trace_collector.stats","args":[]}'
```

`get_trace` returns `{trace_id, root_span_id, duration_ns, span_count, spans:[…]}`.
String responses are bounded by the yrpc frame (64 KiB ≈ a few hundred spans);
a paged API for pathologically large traces is a future refinement.

## Picotrace

`picotrace` is the internal trace browser in
`src/picomesh/core-apps/picotrace`. It is not part of the public Picoforge
webapp and does not add gateway HTTP trace routes. By default it binds
`127.0.0.1:8232` and talks to the internal yhttp bridge at
`http://127.0.0.1:8230`:

```sh
build-desktop-release/picotrace --upstream-url http://127.0.0.1:8230
```

## Configuration (`assets/picoforge/config/picoforge.yaml`)

- The `trace_collector` service: a yrpc backend with `plugins: [trace_collector]`,
  `workers: N` (multi-worker like the gateway, via SO_REUSEPORT — a single
  worker is a bottleneck), and `config.telemetry: { max_spans, max_age_seconds }`
  for the in-memory store's bounded retention (read at first ingest →
  `ytelemetry_store_init`).
- Every span-emitting service lists `trace_collector` in its `remotes` so its
  workers each hold a connection to ship spans over. A service with no
  `trace_collector` remote keeps only its local `/_perf` aggregate.

Retention is bounded: `max_spans` (ring; oldest evicted when full) and
`max_age_seconds` (skipped on query). No durable storage in v1.

## Not in v1 (deliberately)

Durable trace DB, full Jaeger/Tempo, a complete OpenTelemetry SDK,
high-cardinality attributes, and OTLP export. The span shape is OTel-compatible,
so OTLP export is an additive exporter later — propagation does not change.
