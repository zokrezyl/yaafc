/* loop — libuv-backed event loop + coroutine-yielding streams.
 *
 * One loop owns one uv_loop_t. A serve coroutine runs on the loop's
 * stack via picomesh_coro_resume; when the coro yields (inside loop_read /
 * loop_write), control returns to uv_run, which keeps the loop alive.
 *
 * Read model: we keep `uv_read_start` active for the whole lifetime of
 * the stream. Each callback appends incoming bytes into a per-stream
 * ring; if the serve coro is suspended in loop_read waiting for at
 * least N bytes, we resume it as soon as the ring has N. EOF / error
 * sets sticky flags so subsequent reads return 0 immediately.
 *
 * Write model: loop_write copies the bytes into a uv_buf_t, fires
 * uv_write, then yields. The write callback resumes the coro and
 * delivers the result via the coro's status word. */

#include <picomesh/loop/loop.h>
#include <picomesh/picoco/coro.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>

#include <uv.h>

#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15 /* Linux value; header may hide it without _GNU_SOURCE */
#endif

#define RING_INIT_CAP 4096

/* Coroutines whose entry function has returned can't free their own
 * stack while running on it. They queue themselves here; the reaper
 * uv_check runs once per loop iteration on the loop stack and destroys
 * every queued coro that has actually finished. */
struct coro_zombie {
    struct picomesh_coro *coro;
    struct coro_zombie *next;
};

/* Cross-thread coroutine resume: another thread (e.g. a exec worker) appends a
 * parked coro here and uv_async_sends; the loop thread drains and resumes them.
 * Each node is malloc'd by the poster and freed by the drain. */
struct resume_node {
    struct picomesh_coro *coro;
    struct resume_node *next;
};

struct loop {
    uv_loop_t loop;
    uv_check_t reaper;
    struct coro_zombie *zombies;
    uv_async_t resume_async;
    uv_mutex_t resume_mu;
    struct resume_node *resume_head, *resume_tail;
    int resume_ready;
};

struct loop_listener {
    struct loop *owner;
    uv_tcp_t tcp;
    loop_serve_fn serve;
    void *ud;
};

struct loop_stream {
    struct loop *owner;
    uv_tcp_t tcp;
    /* The coroutine this stream's I/O completions resume. For an
     * accepted (serve) stream this is the coro spawned to run the
     * handler, and the stream OWNS it (owns_coro = 1) — it destroys it
     * on close. An outbound stream opened via loop_connect_tcp from
     * inside a serve coroutine merely BORROWS that coro to resume on
     * connect/read/write (owns_coro = 0): destroying it here would
     * double-free the coro the serve stream already owns. */
    struct picomesh_coro *coro;
    int owns_coro;

    /* receive ring */
    uint8_t *rbuf;
    size_t rcap;
    size_t rlen;
    int eof;
    /* libuv read status: 0 on a clean end-of-stream (UV_EOF) or no error
     * yet; a negative libuv error code when the read failed for any other
     * reason. Lets loop_read/_some distinguish a real read error from a
     * clean EOF rather than collapsing both into a zero byte count. */
    int read_status;

    /* read-wait state. Only the read path uses `coro`; writes track
     * their own waiter per-request (see struct write_req), so multiple
     * coroutines may write the same stream concurrently while one coro
     * is parked in loop_read. */
    size_t want;        /* bytes the suspended coro wants */
    int read_blocked;   /* coro suspended in loop_read? */

    int closing;
};

static void on_reaper_check(uv_check_t *handle)
{
    struct loop *loop = handle->data;
    struct coro_zombie *zombie = loop->zombies;
    loop->zombies = NULL;
    while (zombie) {
        struct coro_zombie *next = zombie->next;
        if (picomesh_coro_is_finished(zombie->coro)) {
            picomesh_coro_destroy(zombie->coro);
            free(zombie);
        } else {
            /* Not finished yet — keep it for the next iteration. */
            zombie->next = loop->zombies;
            loop->zombies = zombie;
        }
        zombie = next;
    }
}

void loop_reap_coro(struct loop *loop, struct picomesh_coro *coro)
{
    if (!loop || !coro) return;
    struct coro_zombie *zombie = calloc(1, sizeof(*zombie));
    if (!zombie) {
        ywarn("loop_reap_coro: calloc failed — coro id=%u leaked",
              picomesh_coro_id(coro));
        return;
    }
    zombie->coro = coro;
    zombie->next = loop->zombies;
    loop->zombies = zombie;
}

static void on_alloc(uv_handle_t *handle, size_t suggested, uv_buf_t *out)
{
    (void)handle;
    out->base = malloc(suggested);
    out->len  = out->base ? suggested : 0;
}

static void ring_append(struct loop_stream *stream, const uint8_t *data, size_t len)
{
    if (!stream->rbuf) {
        stream->rcap = RING_INIT_CAP;
        stream->rbuf = malloc(stream->rcap);
    }
    if (stream->rlen + len > stream->rcap) {
        size_t new_cap = stream->rcap ? stream->rcap : RING_INIT_CAP;
        while (stream->rlen + len > new_cap) new_cap *= 2;
        uint8_t *new_buf = realloc(stream->rbuf, new_cap);
        if (!new_buf) {
            ywarn("loop: ring realloc failed");
            return;
        }
        stream->rbuf = new_buf;
        stream->rcap = new_cap;
    }
    memcpy(stream->rbuf + stream->rlen, data, len);
    stream->rlen += len;
}

static void on_read(uv_stream_t *uv_stream, ssize_t nread, const uv_buf_t *buf)
{
    struct loop_stream *stream = uv_stream->data;
    if (nread > 0) {
        ring_append(stream, (const uint8_t *)buf->base, (size_t)nread);
    } else if (nread < 0) {
        /* Terminal: a clean end-of-stream (UV_EOF) or a real read error.
         * Record the libuv status for the latter so a reader can surface it
         * as a Result error instead of an indistinguishable zero count. */
        stream->eof = 1;
        if (nread != UV_EOF) stream->read_status = (int)nread;
    }
    free(buf->base);
    if (stream->read_blocked && (stream->rlen >= stream->want || stream->eof)) {
        stream->read_blocked = 0;
        picomesh_coro_resume(stream->coro);
    }
}

struct picomesh_size_result loop_read_some(struct loop_stream *stream, void *buf, size_t cap)
{
    if (!stream || !buf) return PICOMESH_ERR(picomesh_size, "loop_read_some: bad args");
    if (cap == 0) return PICOMESH_OK(picomesh_size, 0); /* zero-length read is a no-op */
    /* Resume whoever is calling now — a stream may be driven by
     * different coroutines over its lifetime (e.g. a pooled/cached
     * outbound RPC connection). Callers must serialise access to one
     * stream; this just makes the wakeup target correct. */
    stream->coro = picomesh_coro_current();
    while (stream->rlen == 0 && !stream->eof) {
        stream->want = 1;
        stream->read_blocked = 1;
        picomesh_coro_yield();
    }
    if (stream->rlen == 0) {
        /* No buffered bytes and the stream ended: a real read error surfaces
         * as ERR; a clean end-of-stream is OK with a zero count. */
        if (stream->read_status != 0)
            return PICOMESH_ERR(picomesh_size, uv_strerror(stream->read_status));
        return PICOMESH_OK(picomesh_size, 0); /* clean EOF */
    }
    size_t take = stream->rlen < cap ? stream->rlen : cap;
    memcpy(buf, stream->rbuf, take);
    if (take < stream->rlen) {
        memmove(stream->rbuf, stream->rbuf + take, stream->rlen - take);
    }
    stream->rlen -= take;
    return PICOMESH_OK(picomesh_size, take);
}

struct picomesh_size_result loop_read(struct loop_stream *stream, void *buf, size_t n)
{
    if (!stream || !buf) return PICOMESH_ERR(picomesh_size, "loop_read: bad args");
    if (n == 0) return PICOMESH_OK(picomesh_size, 0); /* zero-length read is a no-op */
    stream->coro = picomesh_coro_current(); /* resume the current caller (see loop_read_some) */
    while (stream->rlen < n && !stream->eof) {
        stream->want = n;
        stream->read_blocked = 1;
        picomesh_coro_yield();
    }
    size_t take = stream->rlen < n ? stream->rlen : n;
    memcpy(buf, stream->rbuf, take);
    if (take < stream->rlen) {
        memmove(stream->rbuf, stream->rbuf + take, stream->rlen - take);
    }
    stream->rlen -= take;
    /* Short read at end-of-stream caused by a real read error surfaces as ERR
     * only once the buffered bytes are drained (take == 0); any bytes we did
     * read are returned first so no data is dropped. */
    if (take == 0 && stream->read_status != 0)
        return PICOMESH_ERR(picomesh_size, uv_strerror(stream->read_status));
    return PICOMESH_OK(picomesh_size, take);
}

/* Each write carries its own waiter and result slot rather than parking
 * on the shared stream, so concurrent writers (many handler coros
 * responding on one multiplexed connection) don't clobber each other.
 * libuv flushes queued uv_write requests FIFO and never interleaves the
 * buffers of distinct requests — so a caller that emits a whole frame in
 * ONE loop_write is guaranteed contiguous on the wire. */
struct write_req {
    uv_write_t req;
    struct picomesh_coro *coro; /* the writer to resume on completion */
    int *status_out;         /* points at the caller's stack slot */
    char *data;              /* owned copy */
};

static void on_write(uv_write_t *req, int status)
{
    struct write_req *write_request = (struct write_req *)req;
    struct picomesh_coro *coro = write_request->coro;
    if (write_request->status_out) *write_request->status_out = status;
    free(write_request->data);
    free(write_request);
    picomesh_coro_resume(coro);
}

struct picomesh_size_result loop_write(struct loop_stream *stream, const void *buf, size_t n)
{
    if (!stream || !buf) return PICOMESH_ERR(picomesh_size, "loop_write: bad args");
    if (n == 0) return PICOMESH_OK(picomesh_size, 0); /* zero-length write is a no-op */
    struct write_req *write_request = calloc(1, sizeof(*write_request));
    if (!write_request) return PICOMESH_ERR(picomesh_size, "loop_write: write_req alloc failed");
    int status = -1; /* lives on this coro's stack until on_write resumes us */
    write_request->coro = picomesh_coro_current();
    write_request->status_out = &status;
    write_request->data = malloc(n);
    if (!write_request->data) { free(write_request); return PICOMESH_ERR(picomesh_size, "loop_write: payload alloc failed"); }
    memcpy(write_request->data, buf, n);
    uv_buf_t uv_buf = uv_buf_init(write_request->data, (unsigned)n);
    int rc = uv_write(&write_request->req, (uv_stream_t *)&stream->tcp, &uv_buf, 1, on_write);
    if (rc < 0) {
        free(write_request->data);
        free(write_request);
        return PICOMESH_ERR(picomesh_size, uv_strerror(rc));
    }
    picomesh_coro_yield();
    if (status != 0) return PICOMESH_ERR(picomesh_size, uv_strerror(status));
    return PICOMESH_OK(picomesh_size, n);
}

static void on_handle_close(uv_handle_t *handle)
{
    /* uv_close completion: the handle is detached but we still own
     * the surrounding struct. Free the stream wrapper here. */
    struct loop_stream *stream = handle->data;
    if (!stream) return;
    /* Only the stream that OWNS the coroutine destroys it. A borrowed
     * coro (outbound loop_connect_tcp stream) must not — the owning
     * serve stream will, and a second destroy here is a double free. */
    if (stream->owns_coro && stream->coro && picomesh_coro_is_finished(stream->coro)) {
        picomesh_coro_destroy(stream->coro);
    }
    free(stream->rbuf);
    free(stream);
}

/* uv_close completion for a listener handle: the embedded uv_tcp_t is now off
 * the loop, so the surrounding listener struct can be freed. */
static void on_listener_close(uv_handle_t *handle)
{
    struct loop_listener *listener = handle->data;
    free(listener);
}

void loop_close(struct loop_stream *stream)
{
    if (!stream || stream->closing) return;
    stream->closing = 1;
    uv_read_stop((uv_stream_t *)&stream->tcp);
    uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
}

/* The serve function and ud aren't available inside a static entry —
 * we pack them into a tiny closure laid out immediately after the
 * stream so the trampoline can reach it with a fixed offset. */
struct serve_closure {
    loop_serve_fn fn;
    void *ud;
};

static void serve_trampoline(void *arg)
{
    struct loop_stream *stream = arg;
    struct serve_closure *closure = (struct serve_closure *)((char *)stream + sizeof(*stream));
    closure->fn(stream->owner, stream, closure->ud);
    /* If the handler forgot to close, close now so libuv can clean up. */
    loop_close(stream);
}

/* libuv connection callback — its void signature is fixed by uv_connection_cb
 * and there is no caller to propagate a Result to, so a coro-spawn failure is
 * absorbed here (full cause chain rendered to the log). */
PICOMESH_EXTERNAL_CALLBACK
static void on_connection(uv_stream_t *server, int status)
{
    struct loop_listener *listener = server->data;
    if (status < 0) {
        ywarn("loop: accept failed: %s", uv_strerror(status));
        return;
    }

    /* Stream + closure laid out back-to-back so the trampoline can find
     * the closure with a single offset. */
    struct loop_stream *stream = calloc(1, sizeof(*stream) + sizeof(struct serve_closure));
    if (!stream) return;
    stream->owner = listener->owner;
    struct serve_closure *closure = (struct serve_closure *)((char *)stream + sizeof(*stream));
    closure->fn = listener->serve;
    closure->ud = listener->ud;

    int rc = uv_tcp_init(&listener->owner->loop, &stream->tcp);
    if (rc < 0) {
        ywarn("loop: uv_tcp_init failed: %s", uv_strerror(rc));
        free(stream);
        return;
    }
    stream->tcp.data = stream;

    rc = uv_accept(server, (uv_stream_t *)&stream->tcp);
    if (rc < 0) {
        ywarn("loop: uv_accept failed: %s", uv_strerror(rc));
        uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
        return;
    }
    /* Kill Nagle on every accepted peer. Without this, request/response
     * traffic on loopback eats a ~40 ms delayed-ACK timer per RTT. */
    uv_tcp_nodelay(&stream->tcp, 1);

    rc = uv_read_start((uv_stream_t *)&stream->tcp, on_alloc, on_read);
    if (rc < 0) {
        ywarn("loop: uv_read_start failed: %s", uv_strerror(rc));
        uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
        return;
    }

    struct picomesh_coro_ptr_result spawn_res =
        picomesh_coro_spawn(serve_trampoline, stream, 0, "loop-serve");
    if (PICOMESH_IS_ERR(spawn_res)) {
        picomesh_error_print(stderr, "loop: serve coro spawn failed", spawn_res.error);
        picomesh_error_destroy(spawn_res.error);
        uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
        return;
    }
    stream->coro = spawn_res.value;
    stream->owns_coro = 1; /* the accepted stream owns the serve coro */
    picomesh_coro_resume(stream->coro);
}

/* Runs on the loop thread when another thread posts a resume. Drain the queue
 * under the lock, then resume each coro OUTSIDE the lock (resume can run
 * arbitrary coroutine code, including another loop_post_resume). */
static void on_resume_async(uv_async_t *handle)
{
    struct loop *loop = handle->data;
    uv_mutex_lock(&loop->resume_mu);
    struct resume_node *node = loop->resume_head;
    loop->resume_head = loop->resume_tail = NULL;
    uv_mutex_unlock(&loop->resume_mu);
    while (node) {
        struct resume_node *next = node->next;
        struct picomesh_coro *coro = node->coro;
        free(node);
        picomesh_coro_resume(coro);
        node = next;
    }
}

void loop_post_resume(struct loop *loop, struct picomesh_coro *coro)
{
    if (!loop || !loop->resume_ready || !coro) return;
    struct resume_node *node = calloc(1, sizeof(*node));
    if (!node) return; /* OOM: the coro will not be resumed — fatal-ish, but we
                     * cannot recover here; the offloading path treats a
                     * never-resumed coro as a hung request. */
    node->coro = coro;
    uv_mutex_lock(&loop->resume_mu);
    if (loop->resume_tail) loop->resume_tail->next = node;
    else loop->resume_head = node;
    loop->resume_tail = node;
    uv_mutex_unlock(&loop->resume_mu);
    uv_async_send(&loop->resume_async);
}

struct loop_ptr_result loop_create(void)
{
    struct loop *loop = calloc(1, sizeof(*loop));
    if (!loop) return PICOMESH_ERR(loop_ptr, "loop_create: calloc failed");
    int rc = uv_loop_init(&loop->loop);
    if (rc < 0) {
        free(loop);
        return PICOMESH_ERR(loop_ptr, "loop_create: uv_loop_init failed");
    }
    /* Zombie-coro reaper: runs at the tail of every loop iteration but
     * is unref'd so it never by itself keeps uv_run alive. */
    uv_check_init(&loop->loop, &loop->reaper);
    loop->reaper.data = loop;
    uv_check_start(&loop->reaper, on_reaper_check);
    uv_unref((uv_handle_t *)&loop->reaper);
    /* Cross-thread resume channel (unref'd: it never keeps the loop alive on
     * its own, but uv_async_send still wakes it to deliver a resume). */
    if (uv_async_init(&loop->loop, &loop->resume_async, on_resume_async) == 0) {
        loop->resume_async.data = loop;
        uv_unref((uv_handle_t *)&loop->resume_async);
        uv_mutex_init(&loop->resume_mu);
        loop->resume_ready = 1;
    }
    return PICOMESH_OK(loop_ptr, loop);
}

void loop_destroy(struct loop *loop)
{
    if (!loop) return;
    /* Final sweep: destroy any coro still queued for reaping. */
    on_reaper_check(&loop->reaper);
    struct coro_zombie *zombie = loop->zombies;
    while (zombie) {
        struct coro_zombie *next = zombie->next;
        free(zombie);
        zombie = next;
    }
    uv_check_stop(&loop->reaper);
    if (loop->resume_ready) {
        uv_close((uv_handle_t *)&loop->resume_async, NULL);
        struct resume_node *node = loop->resume_head;
        while (node) { struct resume_node *next = node->next; free(node); node = next; }
        uv_mutex_destroy(&loop->resume_mu);
    }
    uv_loop_close(&loop->loop);
    free(loop);
}

struct picomesh_void_result loop_run(struct loop *loop)
{
    if (!loop) return PICOMESH_ERR(picomesh_void, "loop_run: NULL loop");
    uv_run(&loop->loop, UV_RUN_DEFAULT);
    return PICOMESH_OK_VOID();
}

void loop_stop(struct loop *loop)
{
    if (!loop) return;
    uv_stop(&loop->loop);
}

struct sleep_state {
    uv_timer_t timer;
    struct picomesh_coro *coro;
};

static void on_sleep_closed(uv_handle_t *handle)
{
    free(handle->data); /* the heap sleep_state, once uv is fully done with the timer */
}

static void on_sleep_timer(uv_timer_t *handle)
{
    struct sleep_state *sleep = handle->data;
    struct picomesh_coro *coro = sleep->coro;
    uv_timer_stop(handle);
    /* Close the handle and free its state in the close callback — never
     * inline, never on the caller's stack. A coro that sleeps in a tight
     * loop pops the loop_sleep_ms frame and reuses that stack the instant
     * it resumes; a stack-embedded timer would still sit in uv's
     * closing-handles queue and uv_run would dereference freed stack. Heap
     * + close-cb keeps the handle alive until uv has finished with it. */
    uv_close((uv_handle_t *)handle, on_sleep_closed);
    picomesh_coro_resume(coro);
}

void loop_sleep_ms(struct loop *loop, unsigned int ms)
{
    if (!loop) return;
    struct picomesh_coro *self = picomesh_coro_current();
    if (!self) {
        ywarn("loop_sleep_ms: not in a coroutine — refusing to block");
        return;
    }
    struct sleep_state *sleep = calloc(1, sizeof(*sleep));
    if (!sleep) return;
    sleep->coro = self;
    uv_timer_init(&loop->loop, &sleep->timer);
    sleep->timer.data = sleep;
    uv_timer_start(&sleep->timer, on_sleep_timer, ms, 0);
    picomesh_coro_yield();
}

/* ---- repeating timer (not coroutine-bound) -------------------------- */

struct loop_timer {
    uv_timer_t timer;
    loop_timer_cb cb;
    void *ud;
};

static void on_repeating_timer(uv_timer_t *handle)
{
    struct loop_timer *timer = handle->data;
    if (timer->cb) timer->cb(timer->ud);
}

struct loop_timer_ptr_result loop_timer_start(struct loop *loop, unsigned int interval_ms,
                                                loop_timer_cb cb, void *ud)
{
    if (!loop) return PICOMESH_ERR(loop_timer_ptr, "loop_timer_start: NULL loop");
    if (!cb) return PICOMESH_ERR(loop_timer_ptr, "loop_timer_start: NULL callback");
    struct loop_timer *timer = calloc(1, sizeof(*timer));
    if (!timer) return PICOMESH_ERR(loop_timer_ptr, "loop_timer_start: calloc failed");
    timer->cb = cb;
    timer->ud = ud;
    uv_timer_init(&loop->loop, &timer->timer);
    timer->timer.data = timer;
    uv_timer_start(&timer->timer, on_repeating_timer, interval_ms, interval_ms);
    return PICOMESH_OK(loop_timer_ptr, timer);
}

/* uv_close completion: the handle (embedded in `t`) is now off the loop,
 * so the owning struct can be released. */
static void on_timer_closed(uv_handle_t *handle)
{
    free(handle->data);
}

void loop_timer_stop(struct loop_timer *timer)
{
    if (!timer) return;
    timer->cb = NULL; /* belt-and-braces: no callback even if a tick is mid-flight */
    uv_timer_stop(&timer->timer);
    uv_close((uv_handle_t *)&timer->timer, on_timer_closed);
}

/* ---- subprocess (uv_spawn) ----------------------------------- */

struct loop_process {
    uv_process_t proc;
    loop_process_exit_cb cb;
    void *ud;
    int pid;
};

static void on_proc_exit(uv_process_t *process, int64_t exit_status, int term_signal)
{
    struct loop_process *self = process->data;
    if (self->cb) {
        self->cb(self, exit_status, term_signal, self->ud);
    }
    uv_close((uv_handle_t *)process, NULL);
    /* Defer free via the loop tick: uv_close completion can run
     * after the close cb, so we can't free self immediately without
     * potentially racing with libuv's internal state. The runtime
     * outlives the handle; leaking a few bytes per spawn is fine for
     * now. A close-cb-driven free is the right cleanup. */
}

struct picomesh_int_result loop_spawn(struct loop *loop, const char *file, char *const argv[],
                loop_process_exit_cb on_exit, void *ud)
{
    if (!loop || !file || !argv) return PICOMESH_ERR(picomesh_int, "loop_spawn: bad args");

    struct loop_process *self = calloc(1, sizeof(*self));
    if (!self) return PICOMESH_ERR(picomesh_int, "loop_spawn: calloc failed");
    self->cb = on_exit;
    self->ud = ud;
    self->proc.data = self;

    /* stdio inheritance — parent stdin/out/err pass straight through. */
    uv_stdio_container_t io[3] = {
        {.flags = UV_INHERIT_FD, .data.fd = 0},
        {.flags = UV_INHERIT_FD, .data.fd = 1},
        {.flags = UV_INHERIT_FD, .data.fd = 2},
    };

    uv_process_options_t opts = {0};
    opts.file = file;
    /* libuv's args is `char **` and is not mutated; argv is `char *const *`. */
    opts.args = (char **)argv;
    opts.exit_cb = on_proc_exit;
    opts.stdio_count = 3;
    opts.stdio = io;
    /* Inherit env (NULL → libuv defaults to parent env). */

    int rc = uv_spawn(&loop->loop, &self->proc, &opts);
    if (rc < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "loop_spawn: uv_spawn failed: %s", uv_strerror(rc));
        free(self);
        return PICOMESH_ERR(picomesh_int, msg);
    }
    self->pid = self->proc.pid;
    yinfo("loop_spawn: pid=%d cmd=%s", self->pid, file);
    return PICOMESH_OK(picomesh_int, self->pid);
}

struct picomesh_void_result loop_kill(struct loop *loop, int pid, int signum)
{
    if (!loop || pid <= 0) return PICOMESH_ERR(picomesh_void, "loop_kill: bad args");
    int rc = uv_kill(pid, signum);
    if (rc < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "loop_kill: uv_kill(pid=%d, sig=%d) failed: %s", pid, signum, uv_strerror(rc));
        return PICOMESH_ERR(picomesh_void, msg);
    }
    return PICOMESH_OK_VOID();
}

/* ---- outgoing TCP (loop_connect_tcp) ------------------------------- */

struct connect_state {
    uv_connect_t req;
    struct picomesh_coro *coro;
    int status;
};

static void on_connect(uv_connect_t *req, int status)
{
    struct connect_state *connect = req->data;
    connect->status = status;
    picomesh_coro_resume(connect->coro);
}

struct resolve_state {
    uv_getaddrinfo_t req;
    struct picomesh_coro *coro;
    int status;
    struct sockaddr_in addr;
    int got_addr;
};

static void on_resolve(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
    struct resolve_state *resolve = req->data;
    resolve->status = status;
    if (status == 0 && res) {
        /* Take the first IPv4 result; ignore everything else. */
        for (struct addrinfo *addr = res; addr; addr = addr->ai_next) {
            if (addr->ai_family == AF_INET && addr->ai_addrlen >= sizeof(resolve->addr)) {
                memcpy(&resolve->addr, addr->ai_addr, sizeof(resolve->addr));
                resolve->got_addr = 1;
                break;
            }
        }
        uv_freeaddrinfo(res);
    }
    picomesh_coro_resume(resolve->coro);
}

struct loop_stream_ptr_result loop_connect_tcp(struct loop *loop,
                                                 const char *host, int port)
{
    if (!loop || !host || port <= 0)
        return PICOMESH_ERR(loop_stream_ptr, "loop_connect_tcp: bad args");
    struct picomesh_coro *self = picomesh_coro_current();
    if (!self)
        return PICOMESH_ERR(loop_stream_ptr,
                         "loop_connect_tcp: must be called from a coroutine");

    struct loop_stream *stream = calloc(1, sizeof(*stream));
    if (!stream) return PICOMESH_ERR(loop_stream_ptr, "loop_connect_tcp: calloc failed");
    stream->owner = loop;
    stream->coro  = self;

    int rc = uv_tcp_init(&loop->loop, &stream->tcp);
    if (rc < 0) {
        free(stream);
        return PICOMESH_ERR(loop_stream_ptr, "loop_connect_tcp: uv_tcp_init failed");
    }
    stream->tcp.data = stream;

    struct sockaddr_in addr;
    rc = uv_ip4_addr(host, port, &addr);
    if (rc < 0) {
        /* uv_ip4_addr fails on hostnames; resolve asynchronously via
         * uv_getaddrinfo so the libuv loop keeps servicing other
         * sockets while DNS is in flight. A synchronous getaddrinfo
         * here would freeze every other connection on the loop while
         * the resolver waits — even a misconfigured local DNS turns
         * one slow lookup into a full event-loop stall. */
        struct resolve_state resolve = {0};
        resolve.coro = self;
        resolve.req.data = &resolve;
        struct addrinfo hints = {0};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char portbuf[16];
        snprintf(portbuf, sizeof(portbuf), "%d", port);
        int dispatch_rc = uv_getaddrinfo(&loop->loop, &resolve.req, on_resolve,
                                host, portbuf, &hints);
        if (dispatch_rc < 0) {
            uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
            return PICOMESH_ERR(loop_stream_ptr,
                             "loop_connect_tcp: uv_getaddrinfo dispatch failed");
        }
        picomesh_coro_yield();
        if (resolve.status < 0 || !resolve.got_addr) {
            uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
            return PICOMESH_ERR(loop_stream_ptr,
                             "loop_connect_tcp: getaddrinfo failed");
        }
        addr = resolve.addr;
    }

    struct connect_state connect = {0};
    connect.coro = self;
    connect.req.data = &connect;
    rc = uv_tcp_connect(&connect.req, &stream->tcp, (const struct sockaddr *)&addr, on_connect);
    if (rc < 0) {
        uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
        return PICOMESH_ERR(loop_stream_ptr, "loop_connect_tcp: uv_tcp_connect dispatch failed");
    }
    picomesh_coro_yield();
    if (connect.status < 0) {
        uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
        return PICOMESH_ERR(loop_stream_ptr, "loop_connect_tcp: connect failed");
    }
    uv_tcp_nodelay(&stream->tcp, 1);

    rc = uv_read_start((uv_stream_t *)&stream->tcp, on_alloc, on_read);
    if (rc < 0) {
        uv_close((uv_handle_t *)&stream->tcp, on_handle_close);
        return PICOMESH_ERR(loop_stream_ptr, "loop_connect_tcp: uv_read_start failed");
    }
    return PICOMESH_OK(loop_stream_ptr, stream);
}

/* ---- blocking-work executor (libuv thread pool) --------------------
 *
 * Run a blocking function on libuv's worker pool and suspend the
 * calling coroutine until it finishes — the loop thread stays free to
 * service other coroutines meanwhile. This is asyncio's
 * `run_in_executor` shape (offload → await → resume), the mechanism for
 * keeping blocking work (sqlite/mdbx queries, libgit2, filesystem) off
 * the event-loop hot path.
 *
 * `work` runs on a POOL thread: it must touch only its own `arg` and no
 * loop-thread state. The completion callback resumes the coro on the
 * loop thread. Called outside a coroutine (bootstrap / tests), it runs
 * inline. */
struct blocking_work {
    uv_work_t req;
    struct picomesh_coro *coro;
    void (*work)(void *);
    void *arg;
};

static void on_blocking_work(uv_work_t *req)
{
    struct blocking_work *blocking = req->data;
    blocking->work(blocking->arg);
}

static void on_blocking_done(uv_work_t *req, int status)
{
    (void)status;
    struct blocking_work *blocking = req->data;
    picomesh_coro_resume(blocking->coro);
}

struct picomesh_void_result loop_run_blocking(struct loop *loop, void (*work)(void *), void *arg)
{
    if (!loop || !work)
        return PICOMESH_ERR(picomesh_void, "loop_run_blocking: bad args");

    struct picomesh_coro *self = picomesh_coro_current();
    if (!self) {
        /* Nothing to suspend (mesh parent, unit tests): run it here. */
        work(arg);
        return PICOMESH_OK_VOID();
    }

    struct blocking_work blocking = {.coro = self, .work = work, .arg = arg};
    blocking.req.data = &blocking; /* lives on the suspended coro's stack until resume */
    int rc = uv_queue_work(&loop->loop, &blocking.req, on_blocking_work, on_blocking_done);
    if (rc < 0) {
        /* Pool refused the job. Do NOT silently run inline here — that would
         * block the event loop while masking a degraded worker pool as success.
         * Surface it as an error and let the caller decide (the storage/rel/
         * sharded callers explicitly fall back to inline AND log it). */
        return PICOMESH_ERR(picomesh_void, "loop_run_blocking: uv_queue_work failed (worker pool dispatch)");
    }
    picomesh_coro_yield();
    return PICOMESH_OK_VOID();
}

struct picomesh_void_result loop_listen_tcp(struct loop *loop, const char *host, int port,
                                          loop_serve_fn serve, void *ud)
{
    if (!loop || !host || !serve) {
        return PICOMESH_ERR(picomesh_void, "loop_listen_tcp: bad args");
    }
    /* Validate the address and bind the socket BEFORE allocating/initializing
     * any libuv handle, so these early failures need no handle teardown. */
    struct sockaddr_in addr;
    int rc = uv_ip4_addr(host, port, &addr);
    if (rc < 0) {
        return PICOMESH_ERR(picomesh_void, "loop_listen_tcp: uv_ip4_addr failed");
    }

    /* Bind the socket ourselves with SO_REUSEPORT so several workers
     * (threads or processes) can listen on the SAME port — the kernel
     * then load-balances incoming connections across them, no userspace
     * proxy. SO_REUSEADDR also dodges TIME_WAIT bind failures on restart.
     * The bound fd is handed to libuv via uv_tcp_open. */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return PICOMESH_ERR(picomesh_void, "loop_listen_tcp: socket() failed");
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return PICOMESH_ERR(picomesh_void, "loop_listen_tcp: bind failed");
    }

    struct loop_listener *listener = calloc(1, sizeof(*listener));
    if (!listener) {
        close(fd);
        return PICOMESH_ERR(picomesh_void, "loop_listen_tcp: calloc failed");
    }
    listener->owner = loop;
    listener->serve = serve;
    listener->ud = ud;

    rc = uv_tcp_init(&loop->loop, &listener->tcp);
    if (rc < 0) {
        close(fd);
        free(listener);
        return PICOMESH_ERR(picomesh_void, "loop_listen_tcp: uv_tcp_init failed");
    }
    listener->tcp.data = listener;

    /* From here the handle is initialized: every failure must close it via
     * uv_close (which frees the listener in on_listener_close), not free it
     * directly. */
    rc = uv_tcp_open(&listener->tcp, fd);
    if (rc < 0) {
        close(fd); /* fd not adopted by the handle on failure */
        uv_close((uv_handle_t *)&listener->tcp, on_listener_close);
        return PICOMESH_ERR(picomesh_void, "loop_listen_tcp: uv_tcp_open failed");
    }
    rc = uv_listen((uv_stream_t *)&listener->tcp, 128, on_connection);
    if (rc < 0) {
        uv_close((uv_handle_t *)&listener->tcp, on_listener_close);
        return PICOMESH_ERR(picomesh_void, "loop_listen_tcp: uv_listen failed");
    }
    yinfo("loop: listening on %s:%d", host, port);
    return PICOMESH_OK_VOID();
}
