/* picomesh engine — lifecycle owner.
 *
 * Now also owns the yconfig tree and the CLI chain. The driver hands
 * us a parsed `yargv_chain`; we read its --config-file / --config
 * K=V / --env K=V options, feed them to `yconfig_create`, and stash
 * everything on the engine. */

#include <picomesh/yengine/engine.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yco/coro.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yargv/yargv.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/ycore/yperf.h>
#include <picomesh/ycore/ytelemetry.h>
#include <picomesh/ycore/ytelemetry_store.h>
#include <picomesh/ycore/result.h>

#include <uthash.h>

#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linked list of registered remote RPC sessions, keyed by name. Each
 * worker owns its own list; lookups are O(N names) which is fine for the
 * handful of remotes a typical plugin holds. */
struct remote_entry {
    char *name;
    struct peer_channel *peer;
    struct remote_entry *next;
};

/* One in-process worker: its own libuv loop, its own libco coroutine
 * scheduler (the scheduler state is thread-local — see yco/coro.c), and
 * its own backend connection set. Worker 0 runs on the main thread;
 * workers 1..N-1 each run on a spawned pthread. */
struct picomesh_worker {
    struct picomesh_engine *engine;
    struct yloop *loop;
    struct remote_entry *remotes;
    /* In-process default-instance cache (collocated services). Head of a
     * cached_proxy list owned by rpc.c; handed to ctx.local_cache so a
     * locally-resolved service keeps ONE instance per class across calls.
     * Per-worker: a connection is pinned to its worker, and coroutines on
     * one worker are cooperative, so no locking is needed. */
    void *local_instances;
    size_t index;
    pthread_t thread;             /* valid only when `started` */
    int started;                  /* pthread_create succeeded (index > 0) */
    /* Per-worker perf-counter sampler (gh#14), opened against THIS worker's
     * thread + loop when the service config enables `perf`. NULL when the
     * feature is off. Owned here; destroyed at engine teardown. */
    struct yperf *perf;
    /* Periodic telemetry-batch flush timer on this worker's loop, so the
     * span batch (and, on the collector, the ingest arena) never strands a
     * partial buffer when the worker briefly goes idle. */
    struct yloop_timer *flush_timer;
    /* Per-worker cached frontend security pipeline (authn chain + authorizer),
     * built once on this worker's first gated request and reused thereafter.
     * Per-worker so it is thread-confined (no locking) and reuses this
     * worker's own remotes/loop. Owned here; freed via `security_free` at
     * engine teardown. */
    void *security;
    void (*security_free)(void *);
    /* Transient: handed to the spawned thread for its one-shot setup. */
    picomesh_worker_setup_fn setup;
    void *setup_ud;
};

/* A remote whose address was discovered at boot via the registry
 * (`port: auto`). Filled on the main thread before workers spin; read-only
 * afterwards. See picomesh_engine_set/get_resolved_remote. */
struct resolved_remote {
    char name[64];
    char host[64];
    int port;
    int used;
};

#define PICOMESH_MAX_RESOLVED_REMOTES 64

struct picomesh_engine {
    struct yconfig *config;
    struct yargv_chain *cli; /* may be NULL */
    struct picomesh_worker *workers;
    size_t worker_count;
    struct resolved_remote resolved[PICOMESH_MAX_RESOLVED_REMOTES];
    size_t resolved_count;
};

void picomesh_engine_set_resolved_remote(struct picomesh_engine *e, const char *name,
                                         const char *host, int port)
{
    if (!e || !name || !*name) return;
    if (!host || !*host) host = "127.0.0.1";
    for (size_t i = 0; i < e->resolved_count; ++i) {
        if (e->resolved[i].used && strcmp(e->resolved[i].name, name) == 0) {
            snprintf(e->resolved[i].host, sizeof(e->resolved[i].host), "%s", host);
            e->resolved[i].port = port;
            return;
        }
    }
    if (e->resolved_count >= PICOMESH_MAX_RESOLVED_REMOTES) {
        ywarn("engine: resolved-remote table full, dropping '%s'", name);
        return;
    }
    struct resolved_remote *r = &e->resolved[e->resolved_count++];
    snprintf(r->name, sizeof(r->name), "%s", name);
    snprintf(r->host, sizeof(r->host), "%s", host);
    r->port = port;
    r->used = 1;
}

int picomesh_engine_get_resolved_remote(struct picomesh_engine *e, const char *name,
                                        char *host_out, size_t host_cap, int *port_out)
{
    if (!e || !name) return 0;
    for (size_t i = 0; i < e->resolved_count; ++i) {
        if (e->resolved[i].used && strcmp(e->resolved[i].name, name) == 0) {
            if (host_out && host_cap) snprintf(host_out, host_cap, "%s", e->resolved[i].host);
            if (port_out) *port_out = e->resolved[i].port;
            return 1;
        }
    }
    return 0;
}

/* The worker bound to the calling thread. Set at the top of each worker
 * thread (and for worker 0 by run/run_workers); thread-local so every
 * worker resolves its OWN loop and remotes. Unset on bootstrap/test/
 * libuv-pool threads, where we fall back to worker 0. */
static struct picomesh_worker **current_worker_slot(void)
{
    static _Thread_local struct picomesh_worker *w = NULL;
    return &w;
}

static struct picomesh_worker *engine_current_worker(struct picomesh_engine *e)
{
    struct picomesh_worker *w = *current_worker_slot();
    if (w) return w;
    return e->workers; /* worker 0 — main/bootstrap/pool-thread fallback */
}

void *picomesh_engine_worker_security(struct picomesh_engine *e)
{
    if (!e) return NULL;
    struct picomesh_worker *w = engine_current_worker(e);
    return w ? w->security : NULL;
}

void picomesh_engine_worker_set_security(struct picomesh_engine *e, void *ptr,
                                         void (*free_fn)(void *))
{
    if (!e) return;
    struct picomesh_worker *w = engine_current_worker(e);
    if (!w) return;
    if (w->security_free) w->security_free(w->security);
    w->security = ptr;
    w->security_free = free_fn;
}

static void apply_cli_env(const struct yargv_chain *cli)
{
    /* yaapp's `--env K=V` sets environment variables on the engine
     * process before plugins / config substitution runs. Do the same
     * here — every `--env K=V` is setenv()'d, then yconfig's env-var
     * substitution picks them up automatically. */
    if (!cli) return;
    const char *envs[64];
    size_t n = yargv_get_kv_list(cli, "env", envs, sizeof(envs) / sizeof(envs[0]));
    for (size_t i = 0; i < n; ++i) {
        const char *eq = strchr(envs[i], '=');
        if (!eq) continue;
        size_t klen = (size_t)(eq - envs[i]);
        char name[128];
        if (klen >= sizeof(name)) continue;
        memcpy(name, envs[i], klen);
        name[klen] = 0;
        setenv(name, eq + 1, 1);
        ydebug("engine: --env %s=%s", name, eq + 1);
    }
}

struct picomesh_engine_ptr_result picomesh_engine_create(const struct picomesh_engine_args *args)
{
    ytrace_init();
    rpc_init();

    /* Ignore SIGPIPE process-wide. A write to a peer that has closed its
     * end must surface as EPIPE on the syscall, not kill the process.
     * libuv guards its own I/O with MSG_NOSIGNAL, but the blocking-fd
     * write paths (and any other raw write) do not — under load with
     * many short-lived peer connections, one such write to a freshly
     * closed socket would otherwise terminate the whole service. */
    signal(SIGPIPE, SIG_IGN);

    struct picomesh_engine *e = calloc(1, sizeof(*e));
    if (!e) return PICOMESH_ERR(picomesh_engine_ptr, "picomesh_engine_create: calloc failed");

    if (args) e->cli = args->cli;

    apply_cli_env(e->cli);

    /* Feed yconfig from CLI options when available. */
    const char *config_file = args ? args->config_file : NULL;
    if (!config_file && e->cli) {
        config_file = yargv_get_string(e->cli, "config_file", NULL);
    }

    const char *cli_overrides[64];
    size_t cli_n = 0;
    if (e->cli) {
        cli_n = yargv_get_kv_list(e->cli, "config", cli_overrides,
                                  sizeof(cli_overrides) / sizeof(cli_overrides[0]));
    }

    struct yconfig_create_args cfg_args = {
        .config_file = config_file,
        .cli_overrides = cli_n ? cli_overrides : NULL,
        .cli_override_count = cli_n,
        .app_name = args && args->app_name ? args->app_name : "picomesh",
        .no_filesystem_search = args && args->no_filesystem_search,
    };
    struct yconfig_ptr_result cr = yconfig_create(&cfg_args);
    if (PICOMESH_IS_ERR(cr)) {
        free(e);
        return PICOMESH_ERR(picomesh_engine_ptr, "picomesh_engine_create: yconfig_create failed", cr);
    }
    e->config = cr.value;

    /* Service projection (gh#1): if `--name X` matches a service in
     * mesh.services.X, flatten that service's config block onto the
     * root so plugins see their config at natural paths. Example:
     * `mesh.services.storage.config.storage.db_path` becomes reachable
     * as plain `storage.db_path`. Without this, child processes can't
     * find their YAML-supplied config and silently fall back to defaults. */
    const char *self_name = NULL;
    if (e->cli) self_name = yargv_get_string(e->cli, "name", NULL);
    if (self_name && *self_name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.config", self_name);
        struct picomesh_void_result pr = yconfig_promote_subtree(e->config, path);
        if (PICOMESH_IS_ERR(pr)) {
            ywarn("engine: projecting %s onto root failed (continuing)", path);
            picomesh_error_destroy(pr.error);
        } else {
            ydebug("engine: projected %s onto root", path);
        }
    }

    /* Worker 0: created here (the main-thread loop). Additional workers
     * are spawned lazily by picomesh_engine_run_workers when a service opts
     * into `workers: N`. */
    e->workers = calloc(1, sizeof(struct picomesh_worker));
    if (!e->workers) {
        yconfig_destroy(e->config);
        free(e);
        return PICOMESH_ERR(picomesh_engine_ptr, "picomesh_engine_create: calloc(workers) failed");
    }
    e->worker_count = 1;
    e->workers[0].engine = e;
    e->workers[0].index = 0;

    struct yloop_ptr_result lr = yloop_create();
    if (PICOMESH_IS_ERR(lr)) {
        free(e->workers);
        yconfig_destroy(e->config);
        free(e);
        return PICOMESH_ERR(picomesh_engine_ptr, "picomesh_engine_create: yloop_create failed", lr);
    }
    e->workers[0].loop = lr.value;
    yinfo("picomesh_engine: ready (config=%s)",
          config_file ? config_file : "(defaults+search)");
    return PICOMESH_OK(picomesh_engine_ptr, e);
}

void picomesh_engine_destroy(struct picomesh_engine *e)
{
    if (!e) return;
    for (size_t i = 0; i < e->worker_count; ++i) {
        if (e->workers[i].security_free) e->workers[i].security_free(e->workers[i].security);
        yperf_destroy(e->workers[i].perf);
        struct remote_entry *r = e->workers[i].remotes;
        while (r) {
            struct remote_entry *next = r->next;
            peer_channel_destroy(r->peer);
            free(r->name);
            free(r);
            r = next;
        }
        rpc_local_cache_destroy(&e->workers[i].local_instances);
        if (e->workers[i].flush_timer) yloop_timer_stop(e->workers[i].flush_timer);
        yloop_destroy(e->workers[i].loop);
    }
    free(e->workers);
    yconfig_destroy(e->config);
    yargv_chain_destroy(e->cli);
    free(e);
}

/* ---- remote sessions ---------------------------------------------- */

/* Async, yloop-backed outbound RPC transport with request multiplexing.
 *
 * The connection is a yloop_stream, so read/write yield the calling
 * coroutine instead of freezing the event loop. Connect is lazy —
 * deferred to the first call, which always runs inside a serve coroutine
 * (where yloop_connect_tcp is legal).
 *
 * One session is shared by every coroutine talking to a given backend.
 * Rather than serialise calls behind a single-in-flight lock, every
 * request carries a unique req_id and many calls fly concurrently on the
 * one connection. A dedicated reader coroutine owns all reads: it pulls
 * each response frame, looks the req_id up in the pending map, copies the
 * payload into that caller's buffer, and resumes it. Writers each emit a
 * whole request frame in one yloop_write (libuv keeps the bytes of a
 * single write contiguous, so frames never interleave on the wire). No
 * cooperative lock, no head-of-line blocking. */
struct coro_waiter {
    struct picomesh_coro *coro;
    struct coro_waiter *next;
};

struct pending_call {
    uint32_t req_id;
    struct picomesh_coro *coro; /* caller parked waiting for this response */
    void *resp;              /* caller's output buffer */
    size_t resp_max;
    size_t resp_len;         /* set by the deliverer before resuming */
    int done;                /* delivered (success or failure) */
    int waiting;             /* caller is parked on the response yield */
    struct pending_call *next_dead; /* transient list link during fail-all */
    UT_hash_handle hh;
};

struct rpc_async_client {
    struct yloop *loop;
    char *host;
    int port;
    struct yloop_stream *stream;  /* NULL until connected / after a drop */
    uint32_t next_req_id;
    struct pending_call *pending; /* uthash by req_id */
    struct picomesh_coro *reader;    /* per-connection demux reader coro */
    int connecting;               /* a connect is in flight */
    struct coro_waiter *connect_wq_head, *connect_wq_tail; /* parked on connect */
    struct peer_channel *owner;   /* channel whose proxy cache we flush on reconnect */
};

/* Drain `n` bytes off the stream into a scratch buffer. Returns 1 on
 * success, 0 if the stream broke mid-drain. */
static int rpc_async_drain(struct yloop_stream *stream, uint32_t n)
{
    uint8_t scratch[512];
    while (n) {
        size_t chunk = n > sizeof(scratch) ? sizeof(scratch) : n;
        if (yloop_read(stream, scratch, chunk) != chunk) return 0;
        n -= (uint32_t)chunk;
    }
    return 1;
}

/* Hand a completed (or failed) response to its waiter: record the length,
 * unlink from the map, and resume IFF the caller has actually parked on
 * the response yield. A response can race ahead of the caller's own write
 * completion (loopback is fast); resuming then would wake the caller while
 * it is still inside yloop_write. Instead we stash the result and let the
 * caller observe `done` once its write completes and it reaches the await
 * loop. The caller owns `p` and frees it. */
static void rpc_async_deliver(struct rpc_async_client *c, struct pending_call *p,
                              size_t resp_len)
{
    p->resp_len = resp_len;
    p->done = 1;
    HASH_DEL(c->pending, p);
    if (p->waiting) picomesh_coro_resume(p->coro);
}

/* Fail every outstanding call — used when the reader sees EOF. Detach the
 * whole map first so callers resumed here can register fresh requests on a
 * reconnected stream without us iterating a mutating table. Callers parked
 * on the response yield are resumed; callers still inside yloop_write are
 * left for their (now-cancelled) write completion to unwind. */
static void rpc_async_fail_all(struct rpc_async_client *c)
{
    struct pending_call *p, *tmp, *dead = NULL;
    HASH_ITER(hh, c->pending, p, tmp) {
        HASH_DEL(c->pending, p);
        p->resp_len = 0;
        p->done = 1;
        if (p->waiting) {
            p->next_dead = dead;
            dead = p;
        }
    }
    while (dead) {
        struct pending_call *next = dead->next_dead; /* capture before resume frees it */
        picomesh_coro_resume(dead->coro);
        dead = next;
    }
}

static void rpc_async_reader_entry(void *arg)
{
    struct rpc_async_client *c = arg;
    struct yloop_stream *stream = c->stream;

    for (;;) {
        uint32_t req_id = 0, resp_len = 0;
        if (yloop_read(stream, &req_id, 4) != 4) break;
        if (yloop_read(stream, &resp_len, 4) != 4) break;

        struct pending_call *p = NULL;
        HASH_FIND(hh, c->pending, &req_id, sizeof(req_id), p);
        if (!p) {
            /* No waiter. req_id 0 is the reserved fire-and-forget id
             * (telemetry span ship-out): the reply is expected and
             * unwanted, so drain it silently. Any other unknown id means a
             * caller gave up — worth a warning. */
            if (req_id != 0)
                ywarn("yrpc-reader: response for unknown req_id=%u, draining %u", req_id, resp_len);
            if (!rpc_async_drain(stream, resp_len)) break;
            continue;
        }
        if (resp_len > p->resp_max) {
            /* Caller's buffer too small — same as the legacy "return 0". */
            if (!rpc_async_drain(stream, resp_len)) break;
            rpc_async_deliver(c, p, 0);
            continue;
        }
        if (resp_len && yloop_read(stream, p->resp, resp_len) != resp_len) break;
        rpc_async_deliver(c, p, resp_len);
    }

    /* Stream broke. Fail every outstanding call, tear the stream down so
     * the next call reconnects, and finish — the reader coro is reaped
     * lazily on the next connect. */
    ydebug("mux: backend connection dropped, failing %u pending call(s)",
           HASH_COUNT(c->pending));
    rpc_async_fail_all(c);
    if (c->stream == stream) {
        yloop_close(c->stream);
        c->stream = NULL;
    }
}

/* Establish the connection (lazy) and start the reader. Concurrent
 * callers that arrive mid-connect park until it completes. Returns 1 if
 * the stream is ready, 0 on connect failure. */
static int rpc_async_ensure_connected(struct rpc_async_client *c)
{
    if (c->stream) return 1;

    if (c->connecting) {
        struct coro_waiter w = {.coro = picomesh_coro_current(), .next = NULL};
        if (c->connect_wq_tail) c->connect_wq_tail->next = &w;
        else c->connect_wq_head = &w;
        c->connect_wq_tail = &w;
        picomesh_coro_yield();
        return c->stream != NULL;
    }

    c->connecting = 1;

    /* Reap a reader coro left finished by a previous connection drop. */
    if (c->reader && picomesh_coro_is_finished(c->reader)) {
        picomesh_coro_destroy(c->reader);
        c->reader = NULL;
    }

    int ok = 0;
    struct yloop_stream_ptr_result sr = yloop_connect_tcp(c->loop, c->host, c->port);
    if (PICOMESH_IS_ERR(sr)) {
        picomesh_error_destroy(sr.error);
    } else {
        c->stream = sr.value;
        struct picomesh_coro_ptr_result rr =
            picomesh_coro_spawn(rpc_async_reader_entry, c, 0, "yrpc-reader");
        if (PICOMESH_IS_ERR(rr)) {
            picomesh_error_destroy(rr.error);
            yloop_close(c->stream);
            c->stream = NULL;
        } else {
            c->reader = rr.value;
            ok = 1;
        }
    }

    c->connecting = 0;

    /* Fresh connection ⇒ any cached remote proxies belong to the previous
     * (now-dead) backend process. Drop them so the next acquire re-creates
     * against this connection. (First connect: cache is empty, no-op.) */
    if (ok && c->owner) peer_channel_flush_proxy_cache(c->owner);

    /* Start the reader (it runs to its first yloop_read and parks). */
    if (ok) picomesh_coro_resume(c->reader);

    /* Wake everyone who parked waiting for this connect. */
    struct coro_waiter *wq = c->connect_wq_head;
    c->connect_wq_head = c->connect_wq_tail = NULL;
    while (wq) {
        struct coro_waiter *next = wq->next;
        picomesh_coro_resume(wq->coro);
        wq = next;
    }
    return ok;
}

static size_t rpc_async_client_call(void *vc, enum rpc_op op, uint32_t id,
                                    const void *body, size_t body_len,
                                    void *resp, size_t resp_max)
{
    struct rpc_async_client *c = vc;

    if (!rpc_async_ensure_connected(c)) return 0;

    uint32_t req_id = ++c->next_req_id;
    if (req_id == 0) req_id = ++c->next_req_id; /* keep 0 reserved */

    struct pending_call *p = calloc(1, sizeof(*p));
    if (!p) return 0;
    p->req_id = req_id;
    p->coro = picomesh_coro_current();
    p->resp = resp;
    p->resp_max = resp_max;
    HASH_ADD(hh, c->pending, req_id, sizeof(req_id), p);

    /* Frame: header | req_id | body_len | body — emitted in one write so
     * libuv keeps it contiguous and it can't interleave with other
     * writers on this connection. */
    uint32_t header = RPC_HDR_MAKE(op, id);
    uint32_t blen = (uint32_t)body_len;
    size_t frame_len = 12 + body_len;
    uint8_t *frame = malloc(frame_len);
    if (!frame) { HASH_DEL(c->pending, p); free(p); return 0; }
    memcpy(frame, &header, 4);
    memcpy(frame + 4, &req_id, 4);
    memcpy(frame + 8, &blen, 4);
    if (body_len) memcpy(frame + 12, body, body_len);

    size_t wrote = yloop_write(c->stream, frame, frame_len);
    free(frame);
    if (wrote != frame_len) {
        /* Write failed: drop our own pending and bail. The broken socket
         * will surface as EOF on the reader, which fails the rest and
         * reconnects on the next call. (If a teardown already delivered
         * `p`, it's off the map and done — we just free it.) */
        if (!p->done) HASH_DEL(c->pending, p);
        free(p);
        return 0;
    }

    /* Park until the reader delivers our response. Set `waiting` so the
     * reader knows it may resume us; the loop also covers the case where
     * the response already landed during our write completion. */
    while (!p->done) {
        p->waiting = 1;
        picomesh_coro_yield();
    }

    size_t result_len = p->resp_len;
    free(p);
    return result_len;
}

/* Fire-and-forget: write the request frame and return. No pending entry,
 * no park — the caller never waits for a reply. The frame carries req_id 0
 * (reserved); the reader drains the peer's reply silently. Used for
 * telemetry span ship-out so it never adds a round-trip to a request. */
static void rpc_async_client_oneway(void *vc, enum rpc_op op, uint32_t id,
                                    const void *body, size_t body_len)
{
    struct rpc_async_client *c = vc;
    if (!rpc_async_ensure_connected(c)) return;

    uint32_t header = RPC_HDR_MAKE(op, id);
    uint32_t req_id = 0; /* reserved: "no reply expected" */
    uint32_t blen = (uint32_t)body_len;
    size_t frame_len = 12 + body_len;
    uint8_t *frame = malloc(frame_len);
    if (!frame) return;
    memcpy(frame, &header, 4);
    memcpy(frame + 4, &req_id, 4);
    memcpy(frame + 8, &blen, 4);
    if (body_len) memcpy(frame + 12, body, body_len);
    yloop_write(c->stream, frame, frame_len); /* yields; result ignored */
    free(frame);
}

static void rpc_async_client_destroy(void *vc)
{
    struct rpc_async_client *c = vc;
    if (!c) return;
    /* Shutdown path: the loop has stopped, so don't resume anyone — just
     * free outstanding state. */
    struct pending_call *p, *tmp;
    HASH_ITER(hh, c->pending, p, tmp) {
        HASH_DEL(c->pending, p);
        free(p);
    }
    if (c->reader && picomesh_coro_is_finished(c->reader)) picomesh_coro_destroy(c->reader);
    if (c->stream) yloop_close(c->stream);
    free(c->host);
    free(c);
}

static struct rpc_async_client *rpc_async_client_create(struct yloop *loop,
                                                        const char *host, int port)
{
    struct rpc_async_client *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->loop = loop;
    c->port = port;
    c->host = strdup(host);
    if (!c->host) { free(c); return NULL; }
    return c;
}

/* Build an async-backed session for a remote: a fd-less peer_channel
 * whose calls are carried by the yloop client above. */
static struct peer_channel *open_async_session(struct yloop *loop,
                                              const char *host, int port)
{
    struct rpc_async_client *c = rpc_async_client_create(loop, host, port);
    if (!c) return NULL;
    struct peer_channel *s = peer_channel_create(-1); /* no fd; async carries IO */
    if (!s) { rpc_async_client_destroy(c); return NULL; }
    c->owner = s; /* so reconnects can flush the channel's proxy cache */
    peer_channel_set_async(s, c, rpc_async_client_call, rpc_async_client_destroy);
    peer_channel_set_async_oneway(s, rpc_async_client_oneway);
    return s;
}

struct picomesh_void_result picomesh_engine_add_remote(struct picomesh_engine *e,
                                                  const char *name,
                                                  const char *host, int port)
{
    if (!e || !name) return PICOMESH_ERR(picomesh_void, "add_remote: bad args");
    if (!host) host = "127.0.0.1";

    /* Per-worker: register against the calling worker's loop and list. */
    struct picomesh_worker *w = engine_current_worker(e);

    /* Replace any previous registration with the same name. The async
     * session connects lazily on first use, so there's no dial here —
     * just swap the session in. */
    for (struct remote_entry *r = w->remotes; r; r = r->next) {
        if (strcmp(r->name, name) == 0) {
            peer_channel_destroy(r->peer);
            r->peer = open_async_session(w->loop, host, port);
            if (!r->peer) return PICOMESH_ERR(picomesh_void, "add_remote: session_create");
            yinfo("engine[w%zu]: reopened remote '%s' → %s:%d (async)",
                  w->index, name, host, port);
            return PICOMESH_OK_VOID();
        }
    }

    struct peer_channel *s = open_async_session(w->loop, host, port);
    if (!s) return PICOMESH_ERR(picomesh_void, "add_remote: session_create");

    struct remote_entry *node = calloc(1, sizeof(*node));
    if (!node) { peer_channel_destroy(s); return PICOMESH_ERR(picomesh_void, "add_remote: calloc"); }
    node->name = strdup(name);
    if (!node->name) { peer_channel_destroy(s); free(node); return PICOMESH_ERR(picomesh_void, "add_remote: strdup"); }
    node->peer = s;
    node->next = w->remotes;
    w->remotes = node;
    yinfo("engine[w%zu]: opened remote '%s' → %s:%d", w->index, name, host, port);
    return PICOMESH_OK_VOID();
}

struct peer_channel *picomesh_engine_remote(struct picomesh_engine *e, const char *name)
{
    if (!e || !name) return NULL;
    struct picomesh_worker *w = engine_current_worker(e);
    for (struct remote_entry *r = w->remotes; r; r = r->next) {
        if (strcmp(r->name, name) == 0) return r->peer;
    }
    return NULL;
}

struct ctx picomesh_engine_service_ctx(struct picomesh_engine *e, const char *service)
{
    struct ctx c = {.peer = NULL, .local_cache = NULL};
    if (!e || !service) return c;
    c.peer = picomesh_engine_remote(e, service);
    if (!c.peer) {
        /* Service isn't a remote ⇒ it's collocated in this process. Hand
         * over the current worker's local-instance cache head so the
         * acquired object is reused across calls (state persists). */
        struct picomesh_worker *w = engine_current_worker(e);
        if (w) c.local_cache = &w->local_instances;
    }
    return c;
}

/* Look up the bind host/port for a named service by scanning
 * `mesh.services.<svc>.host/port` (scenario shape). Returns 1 on
 * success. */
static int resolve_service_bind(struct picomesh_engine *e, const char *service,
                                char *host_out, size_t host_cap, int *port_out)
{
    char path[256];
    snprintf(path, sizeof(path), "mesh.services.%s.host", service);
    struct yconfig_node_ptr_result hr = yconfig_get(picomesh_engine_config(e), path);
    const char *h = NULL;
    if (PICOMESH_IS_OK(hr) && hr.value) {
        h = yconfig_node_as_string(hr.value, NULL);
    }
    snprintf(path, sizeof(path), "mesh.services.%s.port", service);
    struct yconfig_node_ptr_result pr = yconfig_get(picomesh_engine_config(e), path);
    int64_t p = -1;
    if (PICOMESH_IS_OK(pr) && pr.value) {
        p = yconfig_node_as_int(pr.value, -1);
    }
    if (p <= 0) return 0;
    snprintf(host_out, host_cap, "%s", h && *h ? h : "127.0.0.1");
    *port_out = (int)p;
    return 1;
}

int picomesh_engine_service_addr(struct picomesh_engine *e, const char *service,
                              char *host_out, size_t host_cap, int *port_out)
{
    if (!e || !service || !host_out || !port_out) return 0;
    return resolve_service_bind(e, service, host_out, host_cap, port_out);
}

/* A `remotes:` entry is `{service: <name>, host?: <str>, port?: <int>}`.
 * The walk callback fills this struct in by key. */
struct remote_entry_fields {
    const char *svc;
    const char *host;
    int port;
};

static int remote_entry_walk_cb(const char *key, const struct yconfig_node *val,
                                void *ud)
{
    struct remote_entry_fields *fc = ud;
    if (strcmp(key, "service") == 0) {
        fc->svc = yconfig_node_as_string(val, NULL);
    } else if (strcmp(key, "host") == 0) {
        fc->host = yconfig_node_as_string(val, NULL);
    } else if (strcmp(key, "port") == 0) {
        fc->port = (int)yconfig_node_as_int(val, 0);
    }
    return 0;
}

size_t picomesh_engine_open_remotes(struct picomesh_engine *e, const char *plugin)
{
    if (!e || !plugin) return 0;

    char path[256];
    snprintf(path, sizeof(path), "mesh.services.%s.config.remotes", plugin);
    struct yconfig_node_ptr_result lr =
        yconfig_get(picomesh_engine_config(e), path);
    if (PICOMESH_IS_ERR(lr) || !lr.value) return 0;
    const struct yconfig_node *list = lr.value;
    if (yconfig_node_kind(list) != YCONFIG_LIST) return 0;

    size_t opened = 0;
    size_t n = yconfig_node_size(list);
    for (size_t i = 0; i < n; ++i) {
        const struct yconfig_node *entry = yconfig_node_at(list, i);
        if (!entry || yconfig_node_kind(entry) != YCONFIG_MAP) continue;

        struct remote_entry_fields fc = {NULL, NULL, 0};
        yconfig_node_for_each(entry, remote_entry_walk_cb, &fc);
        if (!fc.svc || !*fc.svc) continue;

        const char *host = fc.host;
        int port = fc.port;
        /* `port: auto` (or no port) — the address was discovered through the
         * registry at boot and recorded in the resolved-remote table. */
        char resolved_host[128];
        if (port <= 0) {
            int rp = 0;
            if (picomesh_engine_get_resolved_remote(e, fc.svc, resolved_host,
                                                    sizeof(resolved_host), &rp) && rp > 0) {
                if (!host || !*host) host = resolved_host;
                port = rp;
            }
        }
        /* Legacy fallback: the static `mesh.services.<svc>.port` shape. */
        if (port <= 0) {
            char h2[128];
            int p2 = 0;
            if (resolve_service_bind(e, fc.svc, h2, sizeof(h2), &p2)) {
                if (!host) host = h2;
                port = p2;
            }
        }
        if (port <= 0) {
            ywarn("engine: remote '%s' for plugin '%s' — no port (skip)",
                  fc.svc, plugin);
            continue;
        }
        struct picomesh_void_result ar =
            picomesh_engine_add_remote(e, fc.svc, host, port);
        if (PICOMESH_IS_OK(ar)) {
            opened++;
        } else {
            ywarn("engine: failed to open remote '%s' for plugin '%s'",
                  fc.svc, plugin);
            picomesh_error_destroy(ar.error);
        }
    }
    return opened;
}

static void worker_start_flush_timer(struct picomesh_worker *w);

struct picomesh_void_result picomesh_engine_run(struct picomesh_engine *e)
{
    if (!e) return PICOMESH_ERR(picomesh_void, "picomesh_engine_run: NULL engine");
    *current_worker_slot() = &e->workers[0];
    worker_start_flush_timer(&e->workers[0]);
    struct picomesh_void_result r = yloop_run(e->workers[0].loop);
    PICOMESH_RETURN_IF_ERR(picomesh_void, r, "picomesh_engine_run: yloop_run failed");
    return PICOMESH_OK_VOID();
}

/* pthread start routine for workers 1..N-1. External-library signature
 * (void *(void *)) — it can't return a Result, so it absorbs setup
 * errors at this boundary (the worker bows out, the rest keep serving).
 * The worker's loop runs until shutdown. */
/* Long-lived per-worker telemetry-flush coroutine: every 50ms it drains the
 * collector ingest arena and ships this worker's pending span batch. It runs as
 * a coroutine (via yloop_sleep_ms) precisely because the batch ship ends in a
 * yielding write — a bare timer callback could not ship at all, which stranded
 * batched spans. Sees this worker's thread-local buffers (one coro per loop). */
static void telemetry_flush_loop(void *arg)
{
    struct yloop *loop = arg;
    for (;;) {
        yloop_sleep_ms(loop, 1000);
        ytelemetry_store_flush_local();                               /* collector arena (no yield) */
        if (ytelemetry_pending_local() > 0) ytelemetry_flush_local(); /* sender batch (yields) */
    }
}

/* Spawn the flush coroutine on a worker's loop. */
static void worker_start_flush_timer(struct picomesh_worker *w)
{
    struct picomesh_coro_ptr_result cr =
        picomesh_coro_spawn(telemetry_flush_loop, w->loop, 0, "ytel-flush");
    if (PICOMESH_IS_ERR(cr)) { picomesh_error_destroy(cr.error); return; }
    picomesh_coro_resume(cr.value); /* runs to its first yloop_sleep_ms yield */
}

PICOMESH_EXTERNAL_CALLBACK
static void *worker_thread_main(void *arg)
{
    struct picomesh_worker *w = arg;
    *current_worker_slot() = w;

    struct picomesh_void_result sr = w->setup(w->engine, (int)w->index, w->setup_ud);
    if (PICOMESH_IS_ERR(sr)) {
        picomesh_error_print(stderr, "picomesh_engine_run_workers: worker setup", sr.error);
        picomesh_error_destroy(sr.error);
        return NULL;
    }
    worker_start_flush_timer(w);
    struct picomesh_void_result rr = yloop_run(w->loop);
    if (PICOMESH_IS_ERR(rr)) picomesh_error_destroy(rr.error);
    return NULL;
}

struct picomesh_void_result picomesh_engine_run_workers(struct picomesh_engine *e,
                                                  size_t workers,
                                                  picomesh_worker_setup_fn setup,
                                                  void *ud)
{
    if (!e) return PICOMESH_ERR(picomesh_void, "run_workers: NULL engine");
    if (!setup) return PICOMESH_ERR(picomesh_void, "run_workers: NULL setup");
    if (workers < 1) workers = 1;

    /* Grow the worker array to N, creating a fresh loop per new worker.
     * Worker 0 (loop created at engine create) is kept as-is. If a loop
     * can't be created we cap the fleet at what we built. */
    if (workers > e->worker_count) {
        struct picomesh_worker *grown =
            realloc(e->workers, workers * sizeof(struct picomesh_worker));
        if (!grown) return PICOMESH_ERR(picomesh_void, "run_workers: realloc(workers) failed");
        e->workers = grown;
        for (size_t i = e->worker_count; i < workers; ++i) {
            memset(&e->workers[i], 0, sizeof(struct picomesh_worker));
            e->workers[i].engine = e;
            e->workers[i].index = i;
            struct yloop_ptr_result lr = yloop_create();
            if (PICOMESH_IS_ERR(lr)) {
                ywarn("run_workers: yloop_create for worker %zu failed — "
                      "capping fleet at %zu", i, i);
                picomesh_error_destroy(lr.error);
                break;
            }
            e->workers[i].loop = lr.value;
            e->worker_count = i + 1;
        }
    }

    /* The libuv worker pool (used by yloop_run_blocking for DB / libgit2
     * offload) is process-global, default size 4. With several worker
     * loops all offloading, give the pool at least one thread per worker
     * so blocking work doesn't serialise behind it. Only nudges the
     * default up — an explicit env override always wins (overwrite=0). */
    if (e->worker_count > 1) {
        char pool[16];
        snprintf(pool, sizeof(pool), "%zu", e->worker_count * 4);
        setenv("UV_THREADPOOL_SIZE", pool, 0);
    }

    /* Worker 0 first, on THIS thread — its setup runs before any thread
     * is spawned, so a worker-0 setup failure is reported directly with
     * nothing to unwind. */
    *current_worker_slot() = &e->workers[0];
    struct picomesh_void_result sr0 = setup(e, 0, ud);
    PICOMESH_RETURN_IF_ERR(picomesh_void, sr0, "run_workers: worker 0 setup failed");

    /* Spawn workers 1..N-1; each runs its own setup + loop. */
    for (size_t i = 1; i < e->worker_count; ++i) {
        e->workers[i].setup = setup;
        e->workers[i].setup_ud = ud;
        int rc = pthread_create(&e->workers[i].thread, NULL,
                                worker_thread_main, &e->workers[i]);
        if (rc != 0) {
            ywarn("run_workers: pthread_create for worker %zu failed (rc=%d) — "
                  "continuing with fewer workers", i, rc);
            continue;
        }
        e->workers[i].started = 1;
    }
    yinfo("run_workers: %zu worker thread(s) serving", e->worker_count);

    /* Worker 0 drives this thread's loop until shutdown. */
    worker_start_flush_timer(&e->workers[0]);
    struct picomesh_void_result rr = yloop_run(e->workers[0].loop);

    /* Loop exited (clean shutdown). Nudge the others to stop, then join.
     * In practice serve shutdown is signal-driven (the process dies), so
     * this path is mostly for tests / a clean picomesh_engine_stop. */
    for (size_t i = 1; i < e->worker_count; ++i) {
        if (e->workers[i].started && e->workers[i].loop) yloop_stop(e->workers[i].loop);
    }
    for (size_t i = 1; i < e->worker_count; ++i) {
        if (e->workers[i].started) pthread_join(e->workers[i].thread, NULL);
    }

    PICOMESH_RETURN_IF_ERR(picomesh_void, rr, "run_workers: worker 0 loop failed");
    return PICOMESH_OK_VOID();
}

void picomesh_engine_stop(struct picomesh_engine *e)
{
    if (!e) return;
    for (size_t i = 0; i < e->worker_count; ++i) {
        if (e->workers[i].loop) yloop_stop(e->workers[i].loop);
    }
}

struct picomesh_void_result picomesh_engine_perf_start(struct picomesh_engine *e, const char *service)
{
    if (!e) return PICOMESH_ERR(picomesh_void, "picomesh_engine_perf_start: NULL engine");
    struct picomesh_worker *w = engine_current_worker(e);

    /* `perf` resolves at the projected root: service projection promoted
     * mesh.services.<name>.config onto the root at engine create, so the
     * service's `config.perf` block is reachable here as the top-level
     * `perf` section (and a standalone process can set `perf:` at the top
     * level directly). */
    const struct yconfig_node *perf_node = yconfig_section(e->config, "perf");

    char label[64];
    if (service && *service)
        snprintf(label, sizeof(label), "%s w%zu", service, w->index);
    else
        snprintf(label, sizeof(label), "w%zu", w->index);

    struct yperf_ptr_result pr = yperf_create(perf_node, w->loop, label);
    if (PICOMESH_IS_ERR(pr))
        return PICOMESH_ERR(picomesh_void, "picomesh_engine_perf_start: yperf_create failed", pr);
    w->perf = pr.value; /* NULL when profiling is disabled */
    return PICOMESH_OK_VOID();
}

struct yloop *picomesh_engine_loop(struct picomesh_engine *e)
{
    if (!e) return NULL;
    return engine_current_worker(e)->loop;
}

const struct yconfig *picomesh_engine_config(struct picomesh_engine *e)
{
    return e ? e->config : NULL;
}

const struct yconfig_node *picomesh_engine_plugin_config(struct picomesh_engine *e, const char *plugin)
{
    return yconfig_section(e ? e->config : NULL, plugin);
}

struct yargv_chain *picomesh_engine_cli(struct picomesh_engine *e)
{
    return e ? e->cli : NULL;
}

struct picomesh_void_result picomesh_engine_for_each_plugin(struct picomesh_engine *e,
                                                     void (*cb)(const char *qname, void *ud),
                                                     void *ud)
{
    (void)e; (void)cb; (void)ud;
    return PICOMESH_OK_VOID();
}

static struct picomesh_engine **active_engine_slot(void)
{
    static struct picomesh_engine *p = NULL;
    return &p;
}

void picomesh_active_engine_set(struct picomesh_engine *e)
{
    *active_engine_slot() = e;
}

struct picomesh_engine *picomesh_active_engine(void)
{
    return *active_engine_slot();
}
