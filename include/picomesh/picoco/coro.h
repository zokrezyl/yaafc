/* picomesh/picoco/coro.h - Coroutine primitive.
 *
 * Real stackful coroutines via libco. spawn allocates a stack; resume
 * switches into it; yield switches back to whoever resumed.
 *
 * Resume must be called on the loop thread. Cross-thread wakeups (e.g.
 * a worker thread reporting I/O readiness) post a request via the
 * event loop and the loop thread invokes resume.
 *
 * Cancellation is not modelled — if an owner is destroyed while a
 * coroutine is suspended in an `_await` wrapper, the resume callback
 * touches freed memory. Match the lifetime of the coroutine to the
 * thing it operates on.
 *
 * Every fallible entry returns a Result (see <picomesh/core/result.h>). */

#ifndef PICOMESH_PICOCO_CORO_H
#define PICOMESH_PICOCO_CORO_H

#include <picomesh/core/result.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct picomesh_coro;

PICOMESH_RESULT_DECLARE(picomesh_coro_ptr, struct picomesh_coro *);

typedef void (*picomesh_coro_entry)(void *arg);

/* Spawn a coroutine. Does not start it; call picomesh_coro_resume to run.
 * stack_hint of 0 → libco default. name is copied; may be NULL. */
struct picomesh_coro_ptr_result picomesh_coro_spawn(picomesh_coro_entry entry, void *arg,
                                              size_t stack_hint, const char *name);

void picomesh_coro_yield(void);
void picomesh_coro_resume(struct picomesh_coro *coro);
void picomesh_coro_destroy(struct picomesh_coro *coro);

struct picomesh_coro *picomesh_coro_current(void);
int picomesh_coro_is_finished(const struct picomesh_coro *coro);

unsigned int picomesh_coro_id(const struct picomesh_coro *coro);
const char *picomesh_coro_name(const struct picomesh_coro *coro);

void picomesh_coro_set_status(struct picomesh_coro *coro, int status);
int picomesh_coro_get_status(const struct picomesh_coro *coro);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_PICOCO_CORO_H */
