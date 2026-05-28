/* yloop — libuv-backed event loop + coroutine-yielding streams.
 *
 * One yloop owns one uv_loop_t. A serve coroutine runs on the loop's
 * stack via yaafc_coro_resume; when the coro yields (inside yloop_read /
 * yloop_write), control returns to uv_run, which keeps the loop alive.
 *
 * Read model: we keep `uv_read_start` active for the whole lifetime of
 * the stream. Each callback appends incoming bytes into a per-stream
 * ring; if the serve coro is suspended in yloop_read waiting for at
 * least N bytes, we resume it as soon as the ring has N. EOF / error
 * sets sticky flags so subsequent reads return 0 immediately.
 *
 * Write model: yloop_write copies the bytes into a uv_buf_t, fires
 * uv_write, then yields. The write callback resumes the coro and
 * delivers the result via the coro's status word. */

#include <yaafc/yloop/yloop.h>
#include <yaafc/yco/coro.h>
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>

#include <uv.h>

#include <stdlib.h>
#include <string.h>

#define RING_INIT_CAP 4096

struct yloop {
    uv_loop_t loop;
};

struct yloop_listener {
    struct yloop *owner;
    uv_tcp_t tcp;
    yloop_serve_fn serve;
    void *ud;
};

struct yloop_stream {
    struct yloop *owner;
    uv_tcp_t tcp;
    struct yaafc_coro *coro; /* owns the serve coroutine */

    /* receive ring */
    uint8_t *rbuf;
    size_t rcap;
    size_t rlen;
    int eof;

    /* read-wait state */
    size_t want;        /* bytes the suspended coro wants */
    int read_blocked;   /* coro suspended in yloop_read? */

    /* write-wait state */
    int write_blocked;
    int write_result;   /* libuv status of last write */

    int closing;
};

static void on_alloc(uv_handle_t *h, size_t suggested, uv_buf_t *out)
{
    (void)h;
    out->base = malloc(suggested);
    out->len  = out->base ? suggested : 0;
}

static void ring_append(struct yloop_stream *s, const uint8_t *data, size_t n)
{
    if (!s->rbuf) {
        s->rcap = RING_INIT_CAP;
        s->rbuf = malloc(s->rcap);
    }
    if (s->rlen + n > s->rcap) {
        size_t ncap = s->rcap ? s->rcap : RING_INIT_CAP;
        while (s->rlen + n > ncap) ncap *= 2;
        uint8_t *nb = realloc(s->rbuf, ncap);
        if (!nb) {
            ywarn("yloop: ring realloc failed");
            return;
        }
        s->rbuf = nb;
        s->rcap = ncap;
    }
    memcpy(s->rbuf + s->rlen, data, n);
    s->rlen += n;
}

static void on_read(uv_stream_t *st, ssize_t nread, const uv_buf_t *buf)
{
    struct yloop_stream *s = st->data;
    if (nread > 0) {
        ring_append(s, (const uint8_t *)buf->base, (size_t)nread);
    } else if (nread < 0) {
        /* UV_EOF or any other read error → terminal. */
        s->eof = 1;
    }
    free(buf->base);
    if (s->read_blocked && (s->rlen >= s->want || s->eof)) {
        s->read_blocked = 0;
        yaafc_coro_resume(s->coro);
    }
}

size_t yloop_read_some(struct yloop_stream *s, void *buf, size_t cap)
{
    if (!s || !buf || cap == 0) return 0;
    while (s->rlen == 0 && !s->eof) {
        s->want = 1;
        s->read_blocked = 1;
        yaafc_coro_yield();
    }
    if (s->rlen == 0) return 0; /* EOF */
    size_t take = s->rlen < cap ? s->rlen : cap;
    memcpy(buf, s->rbuf, take);
    if (take < s->rlen) {
        memmove(s->rbuf, s->rbuf + take, s->rlen - take);
    }
    s->rlen -= take;
    return take;
}

size_t yloop_read(struct yloop_stream *s, void *buf, size_t n)
{
    if (!s || !buf || n == 0) return 0;
    while (s->rlen < n && !s->eof) {
        s->want = n;
        s->read_blocked = 1;
        yaafc_coro_yield();
    }
    size_t take = s->rlen < n ? s->rlen : n;
    memcpy(buf, s->rbuf, take);
    if (take < s->rlen) {
        memmove(s->rbuf, s->rbuf + take, s->rlen - take);
    }
    s->rlen -= take;
    return take;
}

struct write_req {
    uv_write_t req;
    struct yloop_stream *s;
    char *data; /* owned copy */
};

static void on_write(uv_write_t *req, int status)
{
    struct write_req *wr = (struct write_req *)req;
    struct yloop_stream *s = wr->s;
    s->write_result = status;
    s->write_blocked = 0;
    free(wr->data);
    free(wr);
    yaafc_coro_resume(s->coro);
}

size_t yloop_write(struct yloop_stream *s, const void *buf, size_t n)
{
    if (!s || !buf || n == 0) return 0;
    struct write_req *wr = calloc(1, sizeof(*wr));
    if (!wr) return 0;
    wr->s = s;
    wr->data = malloc(n);
    if (!wr->data) { free(wr); return 0; }
    memcpy(wr->data, buf, n);
    uv_buf_t b = uv_buf_init(wr->data, (unsigned)n);
    s->write_blocked = 1;
    int rc = uv_write(&wr->req, (uv_stream_t *)&s->tcp, &b, 1, on_write);
    if (rc < 0) {
        s->write_blocked = 0;
        free(wr->data);
        free(wr);
        return 0;
    }
    yaafc_coro_yield();
    return s->write_result == 0 ? n : 0;
}

static void on_handle_close(uv_handle_t *h)
{
    /* uv_close completion: the handle is detached but we still own
     * the surrounding struct. Free the stream wrapper here. */
    struct yloop_stream *s = h->data;
    if (!s) return;
    if (s->coro && yaafc_coro_is_finished(s->coro)) {
        yaafc_coro_destroy(s->coro);
    }
    free(s->rbuf);
    free(s);
}

void yloop_close(struct yloop_stream *s)
{
    if (!s || s->closing) return;
    s->closing = 1;
    uv_read_stop((uv_stream_t *)&s->tcp);
    uv_close((uv_handle_t *)&s->tcp, on_handle_close);
}

/* The serve function and ud aren't available inside a static entry —
 * we pack them into a tiny closure laid out immediately after the
 * stream so the trampoline can reach it with a fixed offset. */
struct serve_closure {
    yloop_serve_fn fn;
    void *ud;
};

static void serve_trampoline(void *arg)
{
    struct yloop_stream *s = arg;
    struct serve_closure *cl = (struct serve_closure *)((char *)s + sizeof(*s));
    cl->fn(s->owner, s, cl->ud);
    /* If the handler forgot to close, close now so libuv can clean up. */
    yloop_close(s);
}

static void on_connection(uv_stream_t *server, int status)
{
    struct yloop_listener *L = server->data;
    if (status < 0) {
        ywarn("yloop: accept failed: %s", uv_strerror(status));
        return;
    }

    /* Stream + closure laid out back-to-back so the trampoline can find
     * the closure with a single offset. */
    struct yloop_stream *s = calloc(1, sizeof(*s) + sizeof(struct serve_closure));
    if (!s) return;
    s->owner = L->owner;
    struct serve_closure *cl = (struct serve_closure *)((char *)s + sizeof(*s));
    cl->fn = L->serve;
    cl->ud = L->ud;

    int rc = uv_tcp_init(&L->owner->loop, &s->tcp);
    if (rc < 0) {
        ywarn("yloop: uv_tcp_init failed: %s", uv_strerror(rc));
        free(s);
        return;
    }
    s->tcp.data = s;

    rc = uv_accept(server, (uv_stream_t *)&s->tcp);
    if (rc < 0) {
        ywarn("yloop: uv_accept failed: %s", uv_strerror(rc));
        uv_close((uv_handle_t *)&s->tcp, on_handle_close);
        return;
    }
    /* Kill Nagle on every accepted peer. Without this, request/response
     * traffic on loopback eats a ~40 ms delayed-ACK timer per RTT. */
    uv_tcp_nodelay(&s->tcp, 1);

    rc = uv_read_start((uv_stream_t *)&s->tcp, on_alloc, on_read);
    if (rc < 0) {
        ywarn("yloop: uv_read_start failed: %s", uv_strerror(rc));
        uv_close((uv_handle_t *)&s->tcp, on_handle_close);
        return;
    }

    struct yaafc_coro_ptr_result sr =
        yaafc_coro_spawn(serve_trampoline, s, 0, "yloop-serve");
    if (YAAFC_IS_ERR(sr)) {
        ywarn("yloop: coro spawn failed");
        yaafc_error_destroy(sr.error);
        uv_close((uv_handle_t *)&s->tcp, on_handle_close);
        return;
    }
    s->coro = sr.value;
    yaafc_coro_resume(s->coro);
}

struct yloop_ptr_result yloop_create(void)
{
    struct yloop *l = calloc(1, sizeof(*l));
    if (!l) return YAAFC_ERR(yloop_ptr, "yloop_create: calloc failed");
    int rc = uv_loop_init(&l->loop);
    if (rc < 0) {
        free(l);
        return YAAFC_ERR(yloop_ptr, "yloop_create: uv_loop_init failed");
    }
    return YAAFC_OK(yloop_ptr, l);
}

void yloop_destroy(struct yloop *l)
{
    if (!l) return;
    uv_loop_close(&l->loop);
    free(l);
}

struct yaafc_void_result yloop_run(struct yloop *l)
{
    if (!l) return YAAFC_ERR(yaafc_void, "yloop_run: NULL loop");
    uv_run(&l->loop, UV_RUN_DEFAULT);
    return YAAFC_OK_VOID();
}

void yloop_stop(struct yloop *l)
{
    if (!l) return;
    uv_stop(&l->loop);
}

struct sleep_state {
    uv_timer_t timer;
    struct yaafc_coro *coro;
};

static void on_sleep_timer(uv_timer_t *handle)
{
    struct sleep_state *ss = handle->data;
    uv_timer_stop(handle);
    uv_close((uv_handle_t *)handle, NULL);
    /* The timer handle is embedded in ss; closing it doesn't free ss
     * — yloop_sleep_ms owns that on the coroutine's stack and reads
     * it after resume returns. We resume the coro and the rest
     * unwinds naturally. */
    yaafc_coro_resume(ss->coro);
}

void yloop_sleep_ms(struct yloop *l, unsigned int ms)
{
    if (!l) return;
    struct yaafc_coro *self = yaafc_coro_current();
    if (!self) {
        ywarn("yloop_sleep_ms: not in a coroutine — refusing to block");
        return;
    }
    struct sleep_state ss = {0};
    ss.coro = self;
    uv_timer_init(&l->loop, &ss.timer);
    ss.timer.data = &ss;
    uv_timer_start(&ss.timer, on_sleep_timer, ms, 0);
    yaafc_coro_yield();
}

/* ---- subprocess (uv_spawn) ----------------------------------- */

struct yloop_process {
    uv_process_t proc;
    yloop_process_exit_cb cb;
    void *ud;
    int pid;
};

static void on_proc_exit(uv_process_t *p, int64_t exit_status, int term_signal)
{
    struct yloop_process *self = p->data;
    if (self->cb) {
        self->cb(self, exit_status, term_signal, self->ud);
    }
    uv_close((uv_handle_t *)p, NULL);
    /* Defer free via the loop tick: uv_close completion can run
     * after the close cb, so we can't free self immediately without
     * potentially racing with libuv's internal state. The runtime
     * outlives the handle; leaking a few bytes per spawn is fine for
     * now. A close-cb-driven free is the right cleanup. */
}

int yloop_spawn(struct yloop *l, const char *file, char *const argv[],
                yloop_process_exit_cb on_exit, void *ud)
{
    if (!l || !file || !argv) return 0;

    struct yloop_process *self = calloc(1, sizeof(*self));
    if (!self) return 0;
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
    opts.args = argv;
    opts.exit_cb = on_proc_exit;
    opts.stdio_count = 3;
    opts.stdio = io;
    /* Inherit env (NULL → libuv defaults to parent env). */

    int rc = uv_spawn(&l->loop, &self->proc, &opts);
    if (rc < 0) {
        ywarn("yloop_spawn: uv_spawn failed: %s", uv_strerror(rc));
        free(self);
        return 0;
    }
    self->pid = self->proc.pid;
    yinfo("yloop_spawn: pid=%d cmd=%s", self->pid, file);
    return self->pid;
}

int yloop_kill(struct yloop *l, int pid, int signum)
{
    if (!l || pid <= 0) return -1;
    return uv_kill(pid, signum);
}

struct yaafc_void_result yloop_listen_tcp(struct yloop *l, const char *host, int port,
                                          yloop_serve_fn serve, void *ud)
{
    if (!l || !host || !serve) {
        return YAAFC_ERR(yaafc_void, "yloop_listen_tcp: bad args");
    }
    struct yloop_listener *L = calloc(1, sizeof(*L));
    if (!L) return YAAFC_ERR(yaafc_void, "yloop_listen_tcp: calloc failed");
    L->owner = l;
    L->serve = serve;
    L->ud = ud;

    int rc = uv_tcp_init(&l->loop, &L->tcp);
    if (rc < 0) {
        free(L);
        return YAAFC_ERR(yaafc_void, "yloop_listen_tcp: uv_tcp_init failed");
    }
    L->tcp.data = L;

    struct sockaddr_in addr;
    rc = uv_ip4_addr(host, port, &addr);
    if (rc < 0) {
        return YAAFC_ERR(yaafc_void, "yloop_listen_tcp: uv_ip4_addr failed");
    }
    rc = uv_tcp_bind(&L->tcp, (const struct sockaddr *)&addr, 0);
    if (rc < 0) {
        return YAAFC_ERR(yaafc_void, "yloop_listen_tcp: uv_tcp_bind failed");
    }
    rc = uv_listen((uv_stream_t *)&L->tcp, 128, on_connection);
    if (rc < 0) {
        return YAAFC_ERR(yaafc_void, "yloop_listen_tcp: uv_listen failed");
    }
    yinfo("yloop: listening on %s:%d", host, port);
    return YAAFC_OK_VOID();
}
