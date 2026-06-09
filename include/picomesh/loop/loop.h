/* loop — libuv-backed event loop + coroutine-yielding streams.
 *
 * The handler you register (loop_listen_tcp's `serve`) is invoked once
 * per accepted connection, running inside its own coroutine. From that
 * coroutine you call loop_read / loop_write as if they were blocking;
 * they yield the coro on EAGAIN and resume when libuv signals readiness
 * / write completion. */

#ifndef PICOMESH_LOOP_LOOP_H
#define PICOMESH_LOOP_LOOP_H

#include <picomesh/core/result.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct loop;
struct loop_stream;
struct picomesh_coro;

PICOMESH_RESULT_DECLARE(loop_ptr, struct loop *);

/* Queue a coroutine whose entry has returned for deferred destruction.
 * A coro can't free its own stack while running on it, so a handler
 * coro's last act is to hand itself here; the loop destroys it on the
 * next iteration once it observes the coro as finished. Safe to call
 * from inside the coro being reaped. */
void loop_reap_coro(struct loop *l, struct picomesh_coro *coro);

typedef void (*loop_serve_fn)(struct loop *l, struct loop_stream *s, void *ud);

/* Owner of the libuv loop. Single-threaded; all callbacks fire on the
 * loop thread. */
struct loop_ptr_result loop_create(void);
void loop_destroy(struct loop *l);

/* Run the loop until loop_stop() is called or there's nothing left. */
struct picomesh_void_result loop_run(struct loop *l);

/* Run `work(arg)` on libuv's worker thread pool, suspending the calling
 * coroutine until it completes (asyncio `run_in_executor` shape). Keeps
 * blocking work — DB queries, libgit2, filesystem — off the loop thread
 * so other coroutines keep running. `work` runs on another thread: it
 * must touch only `arg`. Outside a coroutine it runs inline. */
struct picomesh_void_result loop_run_blocking(struct loop *l, void (*work)(void *), void *arg);

/* Resume `coro` (parked via picomesh_coro_yield) from ANOTHER thread: queue it
 * and wake the loop; the loop thread performs the actual picomesh_coro_resume.
 * Coroutines are thread-confined (libco scheduler is thread-local), so this is
 * the only safe way to resume one from off-loop — the handoff a worker-thread
 * executor (exec) uses to wake the coroutine that offloaded work to it. */
void loop_post_resume(struct loop *l, struct picomesh_coro *coro);

/* Drain pending work, then exit loop_run. Safe to call from a serve coro. */
void loop_stop(struct loop *l);

/* Bind + listen on host:port. For each accepted connection, spawn a
 * coroutine running `serve(loop, stream, ud)`. The serve function owns
 * the stream and must call loop_close before returning (or loop will
 * leak it). */
struct picomesh_void_result loop_listen_tcp(struct loop *l, const char *host, int port,
                                          loop_serve_fn serve, void *ud);

/* Outgoing TCP. Connects to host:port; the calling coroutine yields
 * until the handshake completes. On success returns the stream (caller
 * owns; must loop_close); on failure returns an error result. Must be
 * called from inside a coroutine running on `l`. */
PICOMESH_RESULT_DECLARE(loop_stream_ptr, struct loop_stream *);
struct loop_stream_ptr_result loop_connect_tcp(struct loop *l,
                                                 const char *host, int port);

/* Read exactly n bytes into buf. Yields the calling coro until satisfied.
 * OK carries the number of bytes actually read — `n` normally, fewer (down to
 * 0) only at a clean end-of-stream. ERR carries a real read error (libuv
 * status text) so callers can tell a transport failure from a clean EOF. */
struct picomesh_size_result loop_read(struct loop_stream *s, void *buf, size_t n);

/* Read up to `cap` bytes into buf — OK carries whatever is available once at
 * least one byte arrives (0 only at a clean end-of-stream). ERR carries a real
 * read error. For stream parsers (HTTP, line protocols) where you don't know
 * the exact frame length ahead of time. */
struct picomesh_size_result loop_read_some(struct loop_stream *s, void *buf, size_t cap);

/* Write all n bytes. Yields the calling coro until the write completes.
 * OK carries `n` on success; ERR carries the libuv write failure (status
 * text) — allocation failure, dispatch failure, or a failed completion. */
struct picomesh_size_result loop_write(struct loop_stream *s, const void *buf, size_t n);

/* Tear the stream down. Idempotent. */
void loop_close(struct loop_stream *s);

/* Suspend the current coroutine for `ms` milliseconds. Backed by
 * uv_timer_t — the loop thread keeps servicing other I/O while this
 * coro waits. Must be called from inside a coroutine spawned on the
 * loop (e.g. a serve coro from loop_listen_tcp). */
void loop_sleep_ms(struct loop *l, unsigned int ms);

/* ---- repeating timer (not coroutine-bound) -------------------------- */

struct loop_timer;

PICOMESH_RESULT_DECLARE(loop_timer_ptr, struct loop_timer *);

/* Fired by loop_timer_start on every tick, directly on the loop thread.
 * It runs OUTSIDE any coroutine, so it must not call the coroutine-yielding
 * stream ops (loop_read/_write/_sleep_ms). Keep it short and non-blocking
 * — read a counter, log a line, post a message. */
typedef void (*loop_timer_cb)(void *ud);

/* Start a repeating timer that fires `cb(ud)` on the loop thread every
 * `interval_ms` (first fire one interval from now). Returns a handle the
 * caller owns; stop and free it with loop_timer_stop. Used by long-lived
 * housekeeping (e.g. periodic perf-counter sampling) that wants a wakeup
 * without standing up a coroutine. */
struct loop_timer_ptr_result loop_timer_start(struct loop *l, unsigned int interval_ms,
                                                loop_timer_cb cb, void *ud);

/* Stop the timer and release it. After this no further `cb` runs. The
 * underlying libuv handle is closed asynchronously and freed on a later
 * loop tick — safe to call even while tearing the owner down, as long as
 * it precedes freeing whatever `ud` points at. Idempotent on NULL. */
void loop_timer_stop(struct loop_timer *t);

/* ---- subprocess spawn (libuv uv_spawn wrapper) ----------------- */

struct loop_process;

/* Called when the child exits or is killed. `exit_status` is the
 * child's exit code; `term_signal` is non-zero if killed by signal.
 * The loop_process is freed AFTER the callback returns. */
typedef void (*loop_process_exit_cb)(struct loop_process *p,
                                      int64_t exit_status,
                                      int term_signal,
                                      void *ud);

/* Spawn `file` with `argv` (NULL-terminated). OK carries the child PID; ERR
 * carries the libuv spawn failure (with uv_strerror text). The loop owns the
 * uv_process_t until the exit callback fires.
 *
 * stdin/stdout/stderr are inherited from the parent — the child can
 * print to the parent's terminal. */
struct picomesh_int_result loop_spawn(struct loop *l, const char *file, char *const argv[],
                loop_process_exit_cb on_exit, void *ud);

/* Send `signum` to the named pid (which the loop's spawn returned). OK on
 * success; ERR carries the libuv error (e.g. ESRCH/EPERM) with text. */
struct picomesh_void_result loop_kill(struct loop *l, int pid, int signum);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_LOOP_LOOP_H */
