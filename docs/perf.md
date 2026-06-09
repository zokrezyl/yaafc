# Performance: load testing & request tracing

Two related tools for understanding picoforge/picomesh performance:

1. **`picoforge-perf`** — a load generator that drives the gateway and reports
   throughput + latency percentiles (this doc, Part 1).
2. **Tracing** — two complementary layers (Part 2): a per-process op-latency
   aggregate (`GET /_perf`) for *where does time go across many requests*, and
   the **distributed trace collector** (issue #11) for *reconstruct this one
   request as a parent/child span tree* — standard W3C trace context, structured
   spans, and an HTTP query API.

The per-tool reference for the harness lives at
`tests/performance/picoforge/README.md`; this doc is the fuller guide and the
home of the tracing docs.

> Not to be confused with **`docs/perf-counters.md`**, which covers a
> different "perf": config-driven Linux `perf_event_open(2)` CPU counters
> (cycles/instructions/cache-misses) per node (`yperf`, issue #14). That is
> about a process's own PMU counters; this doc is about request throughput
> and latency across the mesh.

---

# Part 1 — The load harness (`picoforge-perf`)

## Quick start

```sh
make build-desktop-release          # builds build-desktop-release/picoforge-perf
make perf-picoforge                 # turnkey: independent mesh, 50k seed, 32 workers, 60s
```

`make perf-picoforge` is the easy path: it stands up a **dedicated, port-shifted
mesh** (so it never collides with a running dev stack), runs the realistic
`mixed` workload, and tears the mesh down afterwards. Override any knob:

```sh
make perf-picoforge DURATION=30 CONNECTIONS=64 SEED_USERS=20000 REPOS_PER_WORKER=4
```

| make var | default | meaning |
|---|---|---|
| `SEED_USERS` | `50000` | stage-1 account population to pre-create |
| `CONNECTIONS` | `32` | concurrent worker threads (= concurrent users) |
| `DURATION` | `60` | timed mixed-load seconds |
| `REPOS_PER_WORKER` | `8` | cap on repos each worker creates |

## What it is

- **Gateway-only.** It hits the gateway exactly as a real client would
  (`/login`, `/register`, `/_rpc`, the action POSTs) and **never touches a
  backend port directly** — same invariant as `mesh-up.sh`. That's deliberate:
  per `CLAUDE.md`, the gateway→backend round-trip is the production hot path, so
  that's what we measure.
- **Real threads, real sockets.** Each `--connections` worker is an OS thread
  with its own keep-alive TCP connection doing blocking request/response
  round-trips. Independent threads give honest concurrency and per-request
  latency without sharing — or being throttled by — the gateway's event loop.
- **Self-contained.** Plain C + pthreads + POSIX sockets, links no picomesh
  libraries. The same binary benchmarks the picomesh C gateway *or* yaapp's
  Python gateway — the HTTP contract is identical.

Source: `tests/performance/picoforge/picoforge-perf.c`.

## Running it by hand

The Makefile target is just a wrapper around the binary + a bring-up script. To
drive an **already-running** stack (e.g. your dev stack on the default ports):

```sh
./build-desktop-release/picoforge-perf --host 127.0.0.1 --port 8090 \
    --scenario rpc_count --connections 16 --duration 10
```

| option | default | meaning |
|---|---|---|
| `--host H` | `127.0.0.1` | gateway host |
| `--port P` | `8090` | gateway port |
| `--connections N` | `8` | concurrent worker threads |
| `--duration SECS` | `10` | run for this long |
| `--requests R` | — | fixed requests **per worker** (overrides `--duration`) |
| `--scenario NAME` | `rpc_count` | which workload (below) |
| `--seed-users N` | `0` | `mixed`: pre-create N account records (stage 1) |
| `--repos-per-worker K` | `8` | `mixed`: cap on repos a worker creates |

## Scenarios

| name | what it times (per iteration) | exercises |
|---|---|---|
| `rpc_count` | `POST /_rpc git_repo.git_repo.count_total` | read; gateway → 1 backend |
| `login` | `POST /login` | auth composite (≈4 backends) |
| `repo_create` | `POST /repos/new` (authenticated) | write; storage + git_repo (libgit2) |
| `register` | `POST /register` (new user each iter) | write; accounts + authn |
| `full` | register → login → repo create → list | end-to-end user journey |
| `mixed` | one weighted-random user action per iter | **realistic blended load (all backends)** |

Each worker establishes a session (register + login) before the timed loop for
the scenarios that need one (`rpc_count`, `repo_create`, `mixed`).

### The `mixed` scenario

This is the "lots of users doing lots of things" workload. It runs in two
stages:

**Stage 1 — population seed** (`--seed-users N`): pre-creates N account records
by calling `accounts.accounts.register` directly over `/_rpc`. This is an O(1) KV
write per account that deliberately **bypasses the gateway's user-name index**
(which is an O(n) append, see "Gotchas"), so it scales to tens of thousands of
users. Reported separately as the "seed stage".

**Stage 2 — mixed op stream** (timed, `--duration`): each worker logs in as its
own user, pre-creates one repo, then loops issuing a **weighted-random** real
user action each iteration. Every op carries the worker's session cookie, so the
gateway resolves sid→uid on every call (the true per-call cost). The mix
(weights sum to 100, in `OP_WEIGHTS`):

| op | weight | call |
|---|---|---|
| `read_count` | 30 | `/_rpc git_repo.git_repo.count_total` |
| `read_list` | 20 | `/_rpc git_repo.git_repo.list_for_owner` |
| `put_file` | 25 | `/_rpc git_repo.git_repo.put_file` (libgit2 commit) |
| `open_issue` | 10 | `/_rpc issues.issues.open` |
| `enqueue_run` | 7 | `/_rpc git_pipeline.git_pipeline.enqueue` |
| `make_repo` | 5 | `/_rpc git_repo.git_repo.make` (bounded by `--repos-per-worker`) |
| `login` | 3 | `/login` |

Every write goes through a business service: `put_file` is a real libgit2
commit into the on-disk **bare repo** (only the repo's metadata — path/owner —
is read from the KV store); `make_repo`/`open_issue`/`enqueue_run` go through
`git_repo`/`issues`/`git_pipeline`. Clients never address a storage node
directly — there is deliberately no raw `sharded_storage.db.set` op, because
the gateway does not expose internal storage services on `/_rpc`.

The harness derives the owner `uid` and `repo_id` with the same FNV hashes the
gateway uses (`hash_username`, `hash_repo`), so it can address backend methods
directly. Reads/KV/make don't owner-check; `put_file` does (owner-only writes) —
see the concurrency gotcha below.

## Output

```
scenario       : mixed
seed stage     : 50000 accounts in 7.462 s (6701 reg/s, 0 errors)
connections    : 32 (32 sessions established)
wall time      : 60.004 s
requests       : 725373 ok, 140218 errors
throughput     : 14425.5 req/s
latency (ms)   : mean=2.216 min=0.348 p50=2.075 p90=2.766 p99=5.178 p99.9=7.709 max=20.695
op breakdown   : (op = ok / err, share of total)
    read_count   :   216070 ok / 0 err  (25.0%, 3601/s)
    ...
```

- **"sessions established" < connections** is a red flag: results for a
  session-bound scenario aren't trustworthy (the mesh buckled during setup).
- For `mixed`, the **op breakdown** shows per-op ok/err and rate — read it to
  see which ops scale and which saturate/fail.
- Exit status: non-zero if any request errored (single-op CI scenarios) — except
  `mixed`, which only fails if *nothing* succeeded (it's a load test, not a gate).

## How the independent mesh works (`perf-run.sh`)

`make perf-picoforge` runs `tests/performance/picoforge/perf-run.sh`, which:

1. **Port-shifts the config**: `sed` transforms `assets/picoforge/config/picoforge.yaml`
   so every `8xxx` port → `9xxx` and `/tmp/picoforge` → `/tmp/picoforge-perf`.
   It transforms the canonical config (rather than keeping a fork) so it can't
   drift. Result: control `:9800`, gateway `:9090`, backends `:92xx`, storage
   under `/tmp/picoforge-perf`. Nothing collides with a dev stack on the default
   `8xxx` ports / `/tmp/picoforge`.
2. **Brings up the mesh** (control parent + `mesh_mesh_reconcile_from_config`),
   waits for the gateway on `:9090`, settles.
3. **Runs the load** (`picoforge-perf --scenario mixed …`).
4. **Tears down** through the control parent: `SIGTERM` the parent → its reaper
   takes the spawned backends down. Never `pkill`, never kill a child directly
   (per `CLAUDE.md`'s process-lifecycle rule).

The mesh log is `tmp/perf-mesh.log` (all nodes — children inherit the parent's
stdout/stderr).

## Fixed: `put_file` "forbidden" under concurrency (was a session-id race)

`put_file` (the only owner-checked write) used to be rejected as
`forbidden (not repo owner)` for the legitimate owner at a rate that climbed
with concurrency (0% at 1–2 connections → ~85–90% at 16–48). Root cause was
**not** in git_repo: `session_session_start` allocated the session id with a
non-atomic `get(next_sid) → +1 → set` over two `sharded_storage` RPCs (each a
coroutine yield). Concurrent logins read the same `next_sid`, were handed the
**same sid**, and the later `uid:<sid>` write clobbered the earlier — so a token
resolved to the wrong user, and the gateway stamped the wrong uid into the
request, failing the owner check.

Fixed by making the session token a **128-bit opaque random hex string**
(`session.c:alloc_token`): `session.session.start` returns a string token,
`lookup`/`destroy` take a string, and the `picomesh-sid` cookie carries it
verbatim. No shared counter to race on, a 2^128 space makes collisions
impossible in practice, and the token is unguessable (a bearer secret —
never logged). The gateway resolves token→uid and forwards only the uid to
backends; the raw token never crosses the mesh boundary. Token generation
**fails closed** — if the kernel cannot supply secure random bytes the login
errors out rather than minting a weak cookie.

The same non-atomic `get→+1→set` class of bug was then removed from the rest
of the stack by adding atomic storage primitives to `sharded_storage`:

- **`db_incr(context, key, delta) → new_value`** — one MDBX write txn per
  shard does read+add+write, so monotonic ids and counters never collide or
  lose updates. Used for `issues.next_id`, `git_pipeline.next_id`, repo
  `count`, and the issue/pipeline state counters.
- **`db_put_if_absent(context, key, value) → inserted?`** — atomic unique
  insert. `git_repo.make` uses it on `repo:<rid>` to elect exactly one
  creator, so concurrent creates can't double-count or duplicate the owner
  index.
- **`db_compare_and_set(context, key, expected, replacement) → swapped?`** —
  optimistic concurrency. `git_pipeline.lease`/`complete` use it to claim or
  finalise a job exactly once (no double-lease); issue `close` uses it to flip
  open→closed once; the repo owner index uses a CAS-retry append/remove so it
  never loses an entry.

After these, every multi-worker op is concurrency-safe: `put_file` errors are
0% under load, and ids/counters stay consistent.

## Measured (local, 24-core box, load generator co-located)

The whole mesh is multi-worker by default (gateway/session/sharded_storage/
git_repo); every backend keeps its state in `sharded_storage`, so this is
correct, not just fast.

Read hot path (`rpc_count`):

| connections | throughput |
|---|---|
| 64 | ~47k req/s |
| 96 | ~51k req/s |

For contrast, the same read forced single-worker (every `workers:` commented)
tops out around **15.6k** at 32 connections — the multi-worker default is ~3×.

`mixed` (realistic blend), 48 connections: ~**12k ops/s** end-to-end, all op
types now clean (`put_file` included, after the atomic-storage fixes). The write ops are honest
multi-round-trip storage operations. The 50k account seed runs at ~**6.7k
creates/s**.

Co-location caveat: the load generator shares the 24 cores with the mesh, so
these are a floor — running `picoforge-perf` from a separate host (point
`--host` at the gateway) frees all cores for the mesh and shows higher numbers.

---

# Part 2 — Tracing

Two layers, answering two different questions:

- *Across many requests, where does time go?* → the per-process op-latency
  aggregate at `GET /_perf` (always on).
- *For this one request, what did each hop do, and how do they nest?* → the
  **distributed trace collector** (`trace_collector.trace_collector.get_trace` via the
  gateway `/_rpc`), which stores structured spans with
  `trace_id`/`span_id`/`parent_span_id` and returns the whole request as a
  parent/child tree.

The full reference for the collector — context propagation, span schema, query
API, retention — is **`docs/tracing.md`**. This section is the perf-debugging
view of it.

## `GET /_perf` — per-op latency aggregate (always on)

`yspan` is a process-global, always-on, lock-light ring of finished spans
(`include/picomesh/core/yspan.h`). It aggregates by **op name** so you see
where time goes without trawling logs or a collector. Query it on the
**gateway** (it listens HTTP; backends are yrpc-only). Unauthenticated.

```sh
curl -s http://127.0.0.1:8090/_perf            # dev stack
curl -s http://127.0.0.1:9090/_perf            # perf mesh (port-shifted)
curl -s 'http://127.0.0.1:8090/_perf?reset'    # clear first → measure a fresh window
```

Output is a text table — op, count, and **microsecond** percentiles:

```
op                                    count    p50_us    p90_us    p99_us    max_us
gateway.git_repo.git_repo.count_total    216070       180       260       540      4100
rpc.git_repo_git_repo_count_total        216070       150       220       470      3900
rpc.session_session_lookup              721000       120       190       410      8200
...
```

Typical workflow: `curl …/_perf?reset`, run a load (or a real workload), then
`curl …/_perf` to see the per-op breakdown for that window. (`GET /_trace` is
retired — it was a misnomer for this local aggregate; it now just points here
and at the collector.)

## Span naming — reading a chain

Each span's **name** tells you which side recorded it. The same names appear in
the `/_perf` aggregate AND as span names in the collector:

| span name | recorded by | measures |
|---|---|---|
| `gateway.<service.class.method>` | the gateway dispatch (root SERVER span) | the whole downstream call as seen at the gateway |
| `rpc.<method>` | the **caller** (client method stub, CLIENT span) | the yrpc round-trip to a remote service (network + queue + remote work) |
| `skel.<method>` | the **callee** (server skel, SERVER span) | the backend's own processing of that call |

For a single request the tree reads top-down: `gateway.X` → `rpc.Y` → `skel.Y` →
(`rpc.Z` → `skel.Z`)… The gap **`rpc.Y` − `skel.Y` = transport + queueing
overhead** between the caller and that backend; `skel.Y` is the backend's pure
service time. In the collector these are linked explicitly by
`parent_span_id`, not inferred from names.

## Tracing one request end-to-end (the collector)

Every request entering the gateway accepts an inbound **W3C `traceparent`**
header or, if absent, starts a fresh trace. The gateway echoes the resulting
`traceparent` on the response, and every hop ships a structured span to the
`trace_collector` backend plugin (a fire-and-forget yrpc call to its
`trace_collector_ingest` method). Query it back **through the gateway `/_rpc`** like any
service — see `docs/tracing.md` for the full method list.

```sh
# Send a request with a known trace id (32 hex). The gateway continues it.
curl -s -D - -H 'traceparent: 00-0123456789abcdef0123456789abcdef-1122334455667788-01' \
     -H 'Content-Type: application/json' \
     http://127.0.0.1:8090/_rpc \
     -d '{"path":"git_repo.git_repo.count_total","args":[]}'

# Fetch the whole trace as a parent/child tree (result is a JSON string).
curl -s -XPOST http://127.0.0.1:8090/_rpc \
     -d '{"path":"trace_collector.trace_collector.get_trace","args":["0123456789abcdef0123456789abcdef"]}'
```

The `result` carries `{trace_id, root_span_id, duration_ns, span_count, spans:[…]}`
where each span has `service_name`, `name`, `kind`, `start_time_ns`,
`duration_ns`, `status`, and `parent_span_id` — enough to render a waterfall.
Other query methods: `store.services`, `store.operations`, `store.latency`,
`store.errors`, `store.stats`.

## Combining the two

- Use **`/_perf`** to find *which op* dominates across a load run (run the perf
  harness, then `curl …/_perf`). The op names line up with the harness's
  breakdown. `?reset` before a measured window — the harness does not reset it.
- Use the **collector** to drill into *one* request: its exact span tree, the
  transport-vs-service split per hop, and recent error traces.

> **Cost.** Span ship-out is a per-span yrpc call on the worker's own
> connection. It is *not* free: a single-worker or overloaded collector
> backpressures into the request path (measured: gateway read throughput
> ~49k req/s drops to a few k with tracing on). Run the collector multi-worker
> (`workers: N`) and treat per-span emission as provisional — batched,
> drop-under-pressure emission is the intended end state.
