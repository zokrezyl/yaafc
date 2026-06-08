/* yrpc — binary RPC frontend with per-request multiplexing.
 *
 * The serve coroutine is a pure reader: it pulls request frames off the
 * connection and, for each one, spawns a handler coroutine that
 * dispatches the call and writes the response (tagged with the request's
 * req_id) back. Because each handler runs in its own coro, a request that
 * blocks — a DB query, a downstream RPC — no longer stalls the requests
 * behind it on the same connection; responses may complete out of order
 * and the req_id lets the client demultiplex them.
 *
 * The returned `struct yrpc_frontend` is a tiny handle so callers can
 * stop the listener cleanly. */

#include <picomesh/frontends/yrpc/yrpc.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yco/coro.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YRPC_FRAME_MAX 65536

struct yrpc_frontend {
    struct picomesh_engine *engine;
};

/* Per-connection state, owned by (and living on the stack of) the reader
 * coroutine. Handler coros borrow it while they run and reference it only
 * up to their last write; the reader does not tear it down until every
 * handler has finished (inflight == 0), so the borrow is always valid. */
struct yrpc_conn {
    struct yloop *loop;
    struct yloop_stream *stream;
    struct picomesh_coro *reader;
    int inflight;       /* handler coros not yet finished */
    int draining;       /* reader saw EOF, waiting for handlers to drain */
    int reader_parked;  /* reader yielded in the drain loop */
};

/* One in-flight request handed from the reader to its handler coro. */
struct yrpc_req {
    struct yrpc_conn *conn;
    uint32_t header;
    uint32_t req_id;
    uint8_t *body;     /* owned; freed by the handler */
    uint32_t body_len;
};

static void yrpc_handler_entry(void *arg)
{
    struct yrpc_req *request = arg;
    struct yrpc_conn *conn = request->conn;

    uint8_t *resp = malloc(YRPC_FRAME_MAX);
    size_t resp_len = 0;
    if (resp)
        resp_len = rpc_dispatch_one(request->header, request->body, request->body_len, resp,
                                    YRPC_FRAME_MAX);

    /* Frame the reply as req_id | resp_len | resp and write it in one
     * shot — libuv keeps a single write contiguous, so concurrent
     * handlers' responses never interleave on the wire. */
    size_t frame_len = 8 + resp_len;
    uint8_t *frame = malloc(frame_len);
    if (frame) {
        uint32_t resp_len_u32 = (uint32_t)resp_len;
        memcpy(frame, &request->req_id, 4);
        memcpy(frame + 4, &resp_len_u32, 4);
        if (resp_len) memcpy(frame + 8, resp, resp_len);
        size_t wrote = yloop_write(conn->stream, frame, frame_len);
        if (wrote != frame_len)
            yerror("yrpc: short/failed response write (%zu/%zu bytes)", wrote, frame_len);
        free(frame);
    }
    free(resp);
    free(request->body);
    free(request);

    conn->inflight--;
    int wake_reader = conn->draining && conn->reader_parked && conn->inflight == 0;
    struct picomesh_coro *reader = conn->reader;
    struct yloop *loop = conn->loop;

    /* Queue self for destruction (can't free our own stack) and, if the
     * reader is draining and we were the last handler, hand control back
     * so it can finish teardown. Touch nothing on `conn` after this. */
    yloop_reap_coro(loop, picomesh_coro_current());
    if (wake_reader) {
        conn->reader_parked = 0;
        picomesh_coro_resume(reader);
    }
}

static void serve_one(struct yloop *loop, struct yloop_stream *stream, void *ud)
{
    (void)ud;
    yinfo("yrpc: peer connected");

    struct yrpc_conn conn = {0};
    conn.loop = loop;
    conn.stream = stream;
    conn.reader = picomesh_coro_current();

    for (;;) {
        uint32_t header = 0, req_id = 0, body_len = 0;
        if (yloop_read(stream, &header, 4) != 4) break;
        if (yloop_read(stream, &req_id, 4) != 4) break;
        if (yloop_read(stream, &body_len, 4) != 4) break;
        if (body_len > YRPC_FRAME_MAX) break;

        uint8_t *body = NULL;
        if (body_len) {
            body = malloc(body_len);
            if (!body) break;
            if (yloop_read(stream, body, body_len) != body_len) { free(body); break; }
        }

        struct yrpc_req *request = calloc(1, sizeof(*request));
        if (!request) { free(body); break; }
        request->conn = &conn;
        request->header = header;
        request->req_id = req_id;
        request->body = body;
        request->body_len = body_len;

        struct picomesh_coro_ptr_result handler_res =
            picomesh_coro_spawn(yrpc_handler_entry, request, 0, "yrpc-handler");
        if (PICOMESH_IS_ERR(handler_res)) {
            /* Root admission failure: a handler that can't be scheduled (e.g.
             * resource exhaustion) must be visible, not a silent disconnect. */
            yerror("yrpc: failed to spawn handler coroutine — dropping connection");
            picomesh_error_destroy(handler_res.error);
            free(body);
            free(request);
            break;
        }
        conn.inflight++;
        picomesh_coro_resume(handler_res.value); /* runs until its first yield (or completion) */
    }

    /* Peer hung up. In-flight handlers still reference this stream and
     * conn (on our stack), so wait for them to finish before returning —
     * which closes the stream and lets yloop reap this serve coro. */
    conn.draining = 1;
    while (conn.inflight > 0) {
        conn.reader_parked = 1;
        picomesh_coro_yield();
    }

    yinfo("yrpc: peer disconnected");
}

struct yrpc_frontend_ptr_result yrpc_start(struct picomesh_engine *e,
                                           const struct yrpc_config *cfg)
{
    if (!e) return PICOMESH_ERR(yrpc_frontend_ptr, "yrpc_start: NULL engine");
    const char *host = (cfg && cfg->host) ? cfg->host : "127.0.0.1";
    int port = (cfg && cfg->port > 0) ? cfg->port : 7777;

    struct yloop *loop = picomesh_engine_loop(e);
    if (!loop) return PICOMESH_ERR(yrpc_frontend_ptr, "yrpc_start: engine has no loop");

    struct picomesh_void_result listen_res = yloop_listen_tcp(loop, host, port, serve_one, NULL);
    if (PICOMESH_IS_ERR(listen_res)) {
        return PICOMESH_ERR(yrpc_frontend_ptr, "yrpc_start: yloop_listen_tcp failed", listen_res);
    }

    struct yrpc_frontend *frontend = calloc(1, sizeof(*frontend));
    if (!frontend) return PICOMESH_ERR(yrpc_frontend_ptr, "yrpc_start: calloc failed");
    frontend->engine = e;
    yinfo("yrpc: listening on %s:%d", host, port);
    return PICOMESH_OK(yrpc_frontend_ptr, frontend);
}

void yrpc_stop(struct yrpc_frontend *frontend)
{
    /* The listener is owned by the yloop and will be torn down when
     * the loop closes. The frontend handle itself just goes away. */
    if (!frontend) return;
    free(frontend);
}
