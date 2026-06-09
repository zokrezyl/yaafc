# Per-node CPU profiling — `perf_event_open` counters (`yperf`)

Picomesh can ask the Linux kernel to count hardware/software performance
events (CPU cycles, retired instructions, cache misses, branch misses, …)
for a node **from that node's own config** — no separate `perf record`
launch wrapper. This is issue #14.

> Naming: this is **distinct** from `docs/perf.md`, which covers the load
> harness (`picoforge-perf`) and the `/_perf` op-latency aggregate. That
> "perf" is about request latency across the mesh; *this* "perf" is the
> kernel PMU counters for one process. The runtime component is `yperf`
> (`core/yperf.{h,c}`).

## Why config-driven, not `perf record`

Wrapping a child with `perf record` is a *launch* concern — you change the
command line, and you profile the whole process the same way for every run.
Picomesh wants the opposite: a scenario should be able to turn profiling on
for **one** service (or several, or all) by editing YAML, with each process
opening its own counters and reporting them in picomesh-native ways. The two
are complementary: `perf record` still works for full flamegraph workflows;
`yperf` adds always-available, per-node, scenario-driven counters.

## Config

Profiling is controlled by a `perf:` block in the node's **projected**
config — i.e. under `mesh.services.<svc>.config.perf` in the scenario YAML
(service projection promotes that onto the root at startup), or a top-level
`perf:` for a standalone single-service process.

```yaml
mesh:
  services:
    gateway:
      config:
        perf:
          enabled: true            # off by default; nothing opens when false
          mode: counters           # only "counters" today (see milestone)
          events:                  # the counters to open
            - cycles
            - instructions
            - cache-references
            - cache-misses
            - branch-misses
          interval_ms: 1000        # log a delta this often (default 1000, min 10)
          log: true                # emit periodic log lines (default true)
          exclude_kernel: false    # measure user-space only (default false)
```

| key | default | meaning |
|---|---|---|
| `enabled` | `false` | master switch. `false`/absent ⇒ nothing opens, zero hot-path cost. |
| `mode` | `counters` | only `counters` is implemented; `sampling`/callgraph warn and fall back to counters. |
| `events` | — | **required when enabled**: list of event names (below). |
| `interval_ms` | `1000` | how often the periodic delta line is logged (clamped to ≥ 10). |
| `log` | `true` | when `false`, counters still open but no periodic line is logged. |
| `exclude_kernel` | `false` | set `true` on hosts with a restrictive `perf_event_paranoid` to measure user-space only. |

### Supported events

Hardware: `cycles` (a.k.a. `cpu-cycles`), `instructions`, `cache-references`,
`cache-misses`, `branch-instructions` (a.k.a. `branches`), `branch-misses`,
`bus-cycles`, `ref-cycles`, `stalled-cycles-frontend`,
`stalled-cycles-backend`.

Software: `cpu-clock`, `task-clock`, `page-faults` (a.k.a. `faults`),
`minor-faults`, `major-faults`, `context-switches` (a.k.a. `cs`),
`cpu-migrations` (a.k.a. `migrations`).

An unknown event name is a loud failure (see below), not a silent skip.

## What it measures (scope)

Each **serving worker thread** opens its own counters against its own task
(`perf_event_open(2)` with `pid = 0, cpu = -1`) and samples them on its own
event loop. So:

- A single-worker service (most backends) reports counters that are
  effectively process-wide for the serving thread.
- A multi-worker service (e.g. the gateway with `workers: 8`) reports **one
  line per worker**, tagged `perf[<svc> wN]`. Each worker handles the
  connections the kernel pinned to it (SO_REUSEPORT), so each line is that
  worker's real share of the load.
- Work offloaded to the libuv thread pool via `loop_run_blocking`
  (libgit2 / MDBX on `git_repo`, `sharded_storage`) runs on **other**
  threads and is **not** counted. The counters reflect the loop/coroutine
  thread's own CPU work. For end-to-end libgit2 profiling, use `perf record`.

The counters use `PERF_FORMAT_TOTAL_TIME_ENABLED | …_RUNNING`, so when more
events are configured than the CPU has hardware slots, the reported values
are rescaled for time-multiplexing and the line is suffixed
`[scaled: events multiplexed]`.

## Output

A startup line, then a delta line every `interval_ms`:

```
yperf[gateway w0]: counting 5 event(s) every 1000ms
perf[gateway w0] +1000ms: cycles=12903441 instructions=8450127 cache-references=204113 cache-misses=18022 branch-misses=9123
perf[gateway w1] +1000ms: cycles=11277908 instructions=7980011 cache-references=198443 cache-misses=17004 branch-misses=8771
...
```

Each number is the **delta since the previous tick** (work done in that
window). On a clean shutdown a final cumulative `perf[<svc> wN] totals: …`
line is logged before the fds close.

These lines (and any perf failure) are emitted **unconditionally** — perf was
explicitly enabled in config, so its output does not depend on the global
trace switch (`YTRACE_DEFAULT_ON`). That matters for a load run: you get the
counter lines without turning on per-request tracing, which would otherwise
flood the hot path and distort the very CPU numbers you are measuring.

Logs follow the usual rule — redirect to a file and grep it:

```sh
grep 'perf\[' tmp/mesh.log
```

## Permission / failure behaviour

Reading PMU counters depends on Linux perf permissions
(`kernel.perf_event_paranoid`, `CAP_PERFMON` on newer kernels, sometimes
`CAP_SYS_ADMIN`). **Profiling is never silently dropped after config asked
for it.** When `perf_event_open` is refused, the exact failing event and
errno are logged at `error` level:

```
yperf[gateway w0]: perf_event_open(cycles) failed: Permission denied (errno=13);
  check kernel.perf_event_paranoid or CAP_PERFMON (try perf.exclude_kernel: true
  to measure user-space only)
```

The service then **keeps serving without profiling** (a profiling-permission
problem on one host must not keep the mesh from coming up) — but the failure
is loud, not silent. An unknown event name fails the same way.

To relax host restrictions for development:

```sh
sudo sysctl kernel.perf_event_paranoid=1   # or -1 / 0 depending on need
```

`exclude_kernel: true` lets unprivileged user-space measurement succeed under
the common `perf_event_paranoid = 2` without changing the sysctl.

## Non-Linux builds

`perf_event_open(2)` is Linux-only. On other platforms the syscall path is
compiled out; if a config still sets `perf.enabled: true`, a single clear
warning is logged and the feature is a no-op — it never breaks the build or
the run.

## Milestone / non-goals

This first version (per the issue) is intentionally scoped to **counters**:

- counters only — no sampling, no mmap ring buffers, no callgraphs yet;
- per-worker-thread target;
- the common hardware + software events above;
- periodic deltas in the log;
- loud failure on permission/config problems;
- no measurable cost when `enabled` is false;
- Linux-only.

`mode: sampling` and `/_perf`-style counter exposure are deliberately left
for a follow-up; `mode: sampling` is accepted in config (warns + treated as
counters) so the schema is forward-compatible.

## Try it

The scenario ships a ready-to-flip example on the gateway
(`assets/picoforge/config/picoforge.yaml`, `gateway.config.perf`, `enabled:
false`). Flip it to `true`, bring the mesh up, drive some load, and watch:

```sh
grep 'perf\[gateway' tmp/mesh.log
```

Or exercise it standalone against any single service:

```yaml
# tmp/perf-smoke.yaml
plugins: [calculator]
perf:
  enabled: true
  events: [cycles, instructions, cache-misses, task-clock]
  interval_ms: 500
```

```sh
# No YTRACE_DEFAULT_ON needed — perf lines are unconditional when enabled.
./build-desktop-release/picomesh \
    --config-file tmp/perf-smoke.yaml --port 7799 serve 2>tmp/perf.log &
sleep 2 && grep 'perf\[' tmp/perf.log
```
