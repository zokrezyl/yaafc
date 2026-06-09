/* yspan — in-memory trace-span collector.
 *
 * A process-global, append-only ring of finished spans (op name +
 * duration). Spans are recorded by the codegen-emitted stub/skel, the
 * gateway dispatch, and the storage DB path — each correlated by the
 * request's trace id, but aggregated here by op so you can see WHERE
 * the time goes without trawling logs.
 *
 * Always on and cheap (a bounded array append on the single loop
 * thread — no IO, no allocation, no lock), distinct from the gated
 * `ydebug` span logging used for per-trace timelines. Query it with
 * yspan_dump (e.g. the gateway's GET /_trace). */

#ifndef PICOMESH_CORE_YSPAN_H
#define PICOMESH_CORE_YSPAN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Record a finished span: `op` (copied, truncated to the slot width) and
 * its duration in microseconds. */
void yspan_record(const char *op, double dur_us);

/* Aggregate the ring by op into `buf` as a text table
 * (op, count, p50, p90, p99, max — microseconds). Returns the number of
 * bytes written (NUL-terminated; truncated if `cap` is too small). */
size_t yspan_dump(char *buf, size_t cap);

/* Forget all recorded spans (e.g. to measure a fresh window). */
void yspan_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_CORE_YSPAN_H */
