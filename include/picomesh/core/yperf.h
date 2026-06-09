/* yperf — config-driven, per-node Linux perf_event_open(2) profiling.
 *
 * A Picomesh process can ask the kernel to count hardware/software events
 * (cycles, instructions, cache-misses, …) for itself, driven entirely by
 * its own projected config — no separate `perf record` launch wrapper.
 * The engine starts one sampler per serving worker thread, against that
 * worker's own task (perf_event_open pid=0, cpu=-1), and reports periodic
 * counter deltas through the normal log stream.
 *
 * Config shape (under the service's projected config, i.e.
 * `mesh.services.<svc>.config.perf`):
 *
 *   perf:
 *     enabled: true
 *     mode: counters          # only "counters" is implemented today
 *     events: [cycles, instructions, cache-misses, branch-misses]
 *     interval_ms: 1000       # how often to log a delta (default 1000)
 *     log: true               # emit periodic log lines (default true)
 *     exclude_kernel: false   # measure user-space only (default false);
 *                             # set true on hosts with a restrictive
 *                             # kernel.perf_event_paranoid
 *
 * Scope of the milestone (see issue): counters only; current worker
 * thread as the target; the common hardware + software events; periodic
 * deltas in the log; loud failure on permission/config problems; no
 * measurable cost when `enabled` is false; Linux-only (other platforms
 * compile the syscall path out and report "unsupported").
 *
 * This is distinct from the trace/telemetry side (ytelemetry/yspan):
 * yperf measures CPU micro-architecture counters for one process; it does
 * not produce spans and never crosses the mesh boundary. */

#ifndef PICOMESH_CORE_YPERF_H
#define PICOMESH_CORE_YPERF_H

#include <picomesh/core/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yperf;
struct loop;
struct config_node;

PICOMESH_RESULT_DECLARE(yperf_ptr, struct yperf *);

/* Build a perf-counter sampler from a projected `perf:` config map.
 *
 * `perf_node` is the `perf` subtree of the current service's projected
 * config, or NULL when the service has none. `loop` is the loop the
 * periodic reporter runs on — it MUST be the loop of the same thread the
 * counters are opened against, so the counts reflect that worker's own
 * CPU work. `label` is a short tag for log lines (e.g. "gateway w0");
 * copied, may be NULL.
 *
 * Outcomes:
 *   - perf_node NULL, or `perf.enabled` false/absent → OK, *value = NULL
 *     (feature off: nothing opened, zero overhead on the hot path).
 *   - enabled, Linux, counters open                  → OK, *value = handle.
 *   - enabled but perf_event_open() is refused         → ERR naming the
 *     failing event and the likely cause (kernel.perf_event_paranoid /
 *     CAP_PERFMON). Profiling was requested and could NOT be silently
 *     dropped — the caller decides whether to abort or log-and-continue.
 *   - enabled on a non-Linux build                     → OK, *value = NULL
 *     after a clear "unsupported on this platform" warning.
 *
 * On OK the caller owns the (possibly NULL) handle and must yperf_destroy
 * it. */
struct yperf_ptr_result yperf_create(const struct config_node *perf_node,
                                     struct loop *loop, const char *label);

/* Stop sampling, read + log a final cumulative total, close the counter
 * fds, free the handle. NULL-safe (the disabled-feature case). */
void yperf_destroy(struct yperf *p);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_CORE_YPERF_H */
