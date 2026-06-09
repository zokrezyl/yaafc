/* picomesh engine — lifecycle owner.
 *
 * Now also owns the config tree and the CLI chain. The driver hands
 * us a parsed `argv_chain`; we read its --config-file / --config
 * K=V / --env K=V options, feed them to `config_create`, and stash
 * everything on the engine. */

#include <picomesh/engine/engine.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/loop/loop.h>
#include <picomesh/picoco/coro.h>
#include <picomesh/config/config.h>
#include <picomesh/argv/argv.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/core/yperf.h>
#include <picomesh/core/ytelemetry.h>
#include <picomesh/core/ytelemetry_store.h>
#include <picomesh/core/result.h>

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
 * scheduler (the scheduler state is thread-local — see picoco/coro.c), and
 * its own backend connection set. Worker 0 runs on the main thread;
 * workers 1..N-1 each run on a spawned pthread. */
struct picomesh_worker {
    struct picomesh_engine *engine;
    struct loop *loop;
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
    struct loop_timer *flush_timer;
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
    struct config *config;
    struct argv_chain *cli; /* may be NULL */
    struct picomesh_worker *workers;
    size_t worker_count;
    struct resolved_remote resolved[PICOMESH_MAX_RESOLVED_REMOTES];
    size_t resolved_count;
};

void picomesh_engine_set_resolved_remote(struct picomesh_engine *engine, const char *name,
                                         const char *host, int port)
{
    if (!engine || !name || !*name) return;
    if (!host || !*host) host = "127.0.0.1";
    for (size_t i = 0; i < engine->resolved_count; ++i) {
        if (engine->resolved[i].used && strcmp(engine->resolved[i].name, name) == 0) {
            snprintf(engine->resolved[i].host, sizeof(engine->resolved[i].host), "%s", host);
            engine->resolved[i].port = port;
            return;
        }
    }
    if (engine->resolved_count >= PICOMESH_MAX_RESOLVED_REMOTES) {
        ywarn("engine: resolved-remote table full, dropping '%s'", name);
        return;
    }
    struct resolved_remote *remote = &engine->resolved[engine->resolved_count++];
    snprintf(remote->name, sizeof(remote->name), "%s", name);
    snprintf(remote->host, sizeof(remote->host), "%s", host);
    remote->port = port;
    remote->used = 1;
}

int picomesh_engine_get_resolved_remote(struct picomesh_engine *engine, const char *name,
                                        char *host_out, size_t host_cap, int *port_out)
{
    if (!engine || !name) return 0;
    for (size_t i = 0; i < engine->resolved_count; ++i) {
        if (engine->resolved[i].used && strcmp(engine->resolved[i].name, name) == 0) {
            if (host_out && host_cap) snprintf(host_out, host_cap, "%s", engine->resolved[i].host);
            if (port_out) *port_out = engine->resolved[i].port;
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
    static _Thread_local struct picomesh_worker *worker = NULL;
    return &worker;
}

static struct picomesh_worker *engine_current_worker(struct picomesh_engine *engine)
{
    struct picomesh_worker *worker = *current_worker_slot();
    if (worker) return worker;
    return engine->workers; /* worker 0 — main/bootstrap/pool-thread fallback */
}

void *picomesh_engine_worker_security(struct picomesh_engine *engine)
{
    if (!engine) return NULL;
    struct picomesh_worker *worker = engine_current_worker(engine);
    return worker ? worker->security : NULL;
}

void picomesh_engine_worker_set_security(struct picomesh_engine *engine, void *ptr,
                                         void (*free_fn)(void *))
{
    if (!engine) return;
    struct picomesh_worker *worker = engine_current_worker(engine);
    if (!worker) return;
    if (worker->security_free) worker->security_free(worker->security);
    worker->security = ptr;
    worker->security_free = free_fn;
}

static void apply_cli_env(const struct argv_chain *cli)
{
    /* yaapp's `--env K=V` sets environment variables on the engine
     * process before plugins / config substitution runs. Do the same
     * here — every `--env K=V` is setenv()'d, then config's env-var
     * substitution picks them up automatically. */
    if (!cli) return;
    const char *envs[64];
    size_t count = argv_get_kv_list(cli, "env", envs, sizeof(envs) / sizeof(envs[0]));
    for (size_t i = 0; i < count; ++i) {
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

    struct picomesh_engine *engine = calloc(1, sizeof(*engine));
    if (!engine) return PICOMESH_ERR(picomesh_engine_ptr, "picomesh_engine_create: calloc failed");

    if (args) engine->cli = args->cli;

    apply_cli_env(engine->cli);

    /* Feed config from CLI options when available. */
    const char *config_file = args ? args->config_file : NULL;
    if (!config_file && engine->cli) {
        config_file = argv_get_string(engine->cli, "config_file", NULL);
    }

    const char *cli_overrides[64];
    size_t cli_override_count = 0;
    if (engine->cli) {
        cli_override_count = argv_get_kv_list(engine->cli, "config", cli_overrides,
                                  sizeof(cli_overrides) / sizeof(cli_overrides[0]));
    }

    struct config_create_args cfg_args = {
        .config_file = config_file,
        .cli_overrides = cli_override_count ? cli_overrides : NULL,
        .cli_override_count = cli_override_count,
        .app_name = args && args->app_name ? args->app_name : "picomesh",
        .no_filesystem_search = args && args->no_filesystem_search,
    };
    struct config_ptr_result config_res = config_create(&cfg_args);
    if (PICOMESH_IS_ERR(config_res)) {
        free(engine);
        return PICOMESH_ERR(picomesh_engine_ptr, "picomesh_engine_create: config_create failed", config_res);
    }
    engine->config = config_res.value;

    /* Service projection (gh#1): if `--name X` matches a service in
     * mesh.services.X, flatten that service's config block onto the
     * root so plugins see their config at natural paths. Example:
     * `mesh.services.storage.config.storage.db_path` becomes reachable
     * as plain `storage.db_path`. Without this, child processes can't
     * find their YAML-supplied config and silently fall back to defaults. */
    const char *self_name = NULL;
    if (engine->cli) self_name = argv_get_string(engine->cli, "name", NULL);
    if (self_name && *self_name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.config", self_name);
        struct picomesh_void_result promote_res = config_promote_subtree(engine->config, path);
        if (PICOMESH_IS_ERR(promote_res)) {
            ywarn("engine: projecting %s onto root failed (continuing)", path);
            picomesh_error_destroy(promote_res.error);
        } else {
            ydebug("engine: projected %s onto root", path);
        }
    }

    /* Worker 0: created here (the main-thread loop). Additional workers
     * are spawned lazily by picomesh_engine_run_workers when a service opts
     * into `workers: N`. */
    engine->workers = calloc(1, sizeof(struct picomesh_worker));
    if (!engine->workers) {
        config_destroy(engine->config);
        free(engine);
        return PICOMESH_ERR(picomesh_engine_ptr, "picomesh_engine_create: calloc(workers) failed");
    }
    engine->worker_count = 1;
    engine->workers[0].engine = engine;
    engine->workers[0].index = 0;

    struct loop_ptr_result loop_res = loop_create();
    if (PICOMESH_IS_ERR(loop_res)) {
        free(engine->workers);
        config_destroy(engine->config);
        free(engine);
        return PICOMESH_ERR(picomesh_engine_ptr, "picomesh_engine_create: loop_create failed", loop_res);
    }
    engine->workers[0].loop = loop_res.value;
    yinfo("picomesh_engine: ready (config=%s)",
          config_file ? config_file : "(defaults+search)");
    return PICOMESH_OK(picomesh_engine_ptr, engine);
}

void picomesh_engine_destroy(struct picomesh_engine *engine)
{
    if (!engine) return;
    for (size_t i = 0; i < engine->worker_count; ++i) {
        if (engine->workers[i].security_free) engine->workers[i].security_free(engine->workers[i].security);
        yperf_destroy(engine->workers[i].perf);
        struct remote_entry *remote = engine->workers[i].remotes;
        while (remote) {
            struct remote_entry *next = remote->next;
            peer_channel_destroy(remote->peer);
            free(remote->name);
            free(remote);
            remote = next;
        }
        rpc_local_cache_destroy(&engine->workers[i].local_instances);
        if (engine->workers[i].flush_timer) loop_timer_stop(engine->workers[i].flush_timer);
        loop_destroy(engine->workers[i].loop);
    }
    free(engine->workers);
    config_destroy(engine->config);
    argv_chain_destroy(engine->cli);
    free(engine);
}

/* ---- remote sessions ---------------------------------------------- */

/* Async, loop-backed outbound RPC transport with request multiplexing.
 *
 * The connection is a loop_stream, so read/write yield the calling
 * coroutine instead of freezing the event loop. Connect is lazy —
 * deferred to the first call, which always runs inside a serve coroutine
 * (where loop_connect_tcp is legal).
 *
 * One session is shared by every coroutine talking to a given backend.
 * Rather than serialise calls behind a single-in-flight lock, every
 * request carries a unique req_id and many calls fly concurrently on the
 * one connection. A dedicated reader coroutine owns all reads: it pulls
 * each response frame, looks the req_id up in the pending map, copies the
 * payload into that caller's buffer, and resumes it. Writers each emit a
 * whole request frame in one loop_write (libuv keeps the bytes of a
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
    struct loop *loop;
    char *host;
    int port;
    struct loop_stream *stream;  /* NULL until connected / after a drop */
    uint32_t next_req_id;
    struct pending_call *pending; /* uthash by req_id */
    struct picomesh_coro *reader;    /* per-connection demux reader coro */
    int connecting;               /* a connect is in flight */
    struct coro_waiter *connect_wq_head, *connect_wq_tail; /* parked on connect */
    struct peer_channel *owner;   /* channel whose proxy cache we flush on reconnect */
};

/* Drain `n` bytes off the stream into a scratch buffer. Returns 1 on
 * success, 0 if the stream broke mid-drain. */
static int rpc_async_drain(struct loop_stream *stream, uint32_t remaining)
{
    uint8_t scratch[512];
    while (remaining) {
        size_t chunk = remaining > sizeof(scratch) ? sizeof(scratch) : remaining;
        struct picomesh_size_result drain_read = loop_read(stream, scratch, chunk);
        if (PICOMESH_IS_ERR(drain_read)) { picomesh_error_destroy(drain_read.error); return 0; }
        if (drain_read.value != chunk) return 0;
        remaining -= (uint32_t)chunk;
    }
    return 1;
}

/* Hand a completed (or failed) response to its waiter: record the length,
 * unlink from the map, and resume IFF the caller has actually parked on
 * the response yield. A response can race ahead of the caller's own write
 * completion (loopback is fast); resuming then would wake the caller while
 * it is still inside loop_write. Instead we stash the result and let the
 * caller observe `done` once its write completes and it reaches the await
 * loop. The caller owns `p` and frees it. */
static void rpc_async_deliver(struct rpc_async_client *client, struct pending_call *call,
                              size_t resp_len)
{
    call->resp_len = resp_len;
    call->done = 1;
    HASH_DEL(client->pending, call);
    if (call->waiting) picomesh_coro_resume(call->coro);
}

/* Fail every outstanding call — used when the reader sees EOF. Detach the
 * whole map first so callers resumed here can register fresh requests on a
 * reconnected stream without us iterating a mutating table. Callers parked
 * on the response yield are resumed; callers still inside loop_write are
 * left for their (now-cancelled) write completion to unwind. */
static void rpc_async_fail_all(struct rpc_async_client *client)
{
    struct pending_call *call, *tmp, *dead = NULL;
    HASH_ITER(hh, client->pending, call, tmp) {
        HASH_DEL(client->pending, call);
        call->resp_len = 0;
        call->done = 1;
        if (call->waiting) {
            call->next_dead = dead;
            dead = call;
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
    struct rpc_async_client *client = arg;
    struct loop_stream *stream = client->stream;

    for (;;) {
        uint32_t req_id = 0, resp_len = 0;
        struct picomesh_size_result reqid_read = loop_read(stream, &req_id, 4);
        if (PICOMESH_IS_ERR(reqid_read)) { picomesh_error_destroy(reqid_read.error); break; }
        if (reqid_read.value != 4) break;
        struct picomesh_size_result resplen_read = loop_read(stream, &resp_len, 4);
        if (PICOMESH_IS_ERR(resplen_read)) { picomesh_error_destroy(resplen_read.error); break; }
        if (resplen_read.value != 4) break;

        struct pending_call *call = NULL;
        HASH_FIND(hh, client->pending, &req_id, sizeof(req_id), call);
        if (!call) {
            /* No waiter. req_id 0 is the reserved fire-and-forget id
             * (telemetry span ship-out): the reply is expected and
             * unwanted, so drain it silently. Any other unknown id means a
             * caller gave up — worth a warning. */
            if (req_id != 0)
                ywarn("yrpc-reader: response for unknown req_id=%u, draining %u", req_id, resp_len);
            if (!rpc_async_drain(stream, resp_len)) break;
            continue;
        }
        if (resp_len > call->resp_max) {
            /* Caller's buffer too small — same as the legacy "return 0". */
            if (!rpc_async_drain(stream, resp_len)) break;
            rpc_async_deliver(client, call, 0);
            continue;
        }
        if (resp_len) {
            struct picomesh_size_result resp_read = loop_read(stream, call->resp, resp_len);
            if (PICOMESH_IS_ERR(resp_read)) { picomesh_error_destroy(resp_read.error); break; }
            if (resp_read.value != resp_len) break;
        }
        rpc_async_deliver(client, call, resp_len);
    }

    /* Stream broke. Fail every outstanding call, tear the stream down so
     * the next call reconnects, and finish — the reader coro is reaped
     * lazily on the next connect. */
    ydebug("mux: backend connection dropped, failing %u pending call(s)",
           HASH_COUNT(client->pending));
    rpc_async_fail_all(client);
    if (client->stream == stream) {
        loop_close(client->stream);
        client->stream = NULL;
    }
}

/* Establish the connection (lazy) and start the reader. Concurrent
 * callers that arrive mid-connect park until it completes. Returns 1 if
 * the stream is ready, 0 on connect failure. */
static struct picomesh_int_result rpc_async_ensure_connected(struct rpc_async_client *client)
{
    if (client->stream) return PICOMESH_OK(picomesh_int, 1);

    if (client->connecting) {
        struct coro_waiter waiter = {.coro = picomesh_coro_current(), .next = NULL};
        if (client->connect_wq_tail) client->connect_wq_tail->next = &waiter;
        else client->connect_wq_head = &waiter;
        client->connect_wq_tail = &waiter;
        picomesh_coro_yield();
        return PICOMESH_OK(picomesh_int, client->stream != NULL);
    }

    client->connecting = 1;

    /* Reap a reader coro left finished by a previous connection drop. */
    if (client->reader && picomesh_coro_is_finished(client->reader)) {
        picomesh_coro_destroy(client->reader);
        client->reader = NULL;
    }

    int ok = 0;
    struct loop_stream_ptr_result stream_res = loop_connect_tcp(client->loop, client->host, client->port);
    if (PICOMESH_IS_ERR(stream_res)) {
        /* Backend unreachable is a recoverable, degraded state (the next call
         * retries), but render the chain so a persistent outage is visible. */
        picomesh_error_print(stderr, "rpc async: connect failed", stream_res.error);
        picomesh_error_destroy(stream_res.error);
    } else {
        client->stream = stream_res.value;
        struct picomesh_coro_ptr_result reader_res =
            picomesh_coro_spawn(rpc_async_reader_entry, client, 0, "yrpc-reader");
        if (PICOMESH_IS_ERR(reader_res)) {
            picomesh_error_print(stderr, "rpc async: reader coro spawn failed", reader_res.error);
            picomesh_error_destroy(reader_res.error);
            loop_close(client->stream);
            client->stream = NULL;
        } else {
            client->reader = reader_res.value;
            ok = 1;
        }
    }

    client->connecting = 0;

    /* Fresh connection ⇒ any cached remote proxies belong to the previous
     * (now-dead) backend process. Drop them so the next acquire re-creates
     * against this connection. (First connect: cache is empty, no-op.) */
    if (ok && client->owner) peer_channel_flush_proxy_cache(client->owner);

    /* Start the reader (it runs to its first loop_read and parks). */
    if (ok) picomesh_coro_resume(client->reader);

    /* Wake everyone who parked waiting for this connect. */
    struct coro_waiter *waiter = client->connect_wq_head;
    client->connect_wq_head = client->connect_wq_tail = NULL;
    while (waiter) {
        struct coro_waiter *next = waiter->next;
        picomesh_coro_resume(waiter->coro);
        waiter = next;
    }
    return PICOMESH_OK(picomesh_int, ok);
}

/* Registered transport `call` op — its size_t signature is fixed by
 * peer_channel_set_async; a connect failure is reported via a 0-byte return
 * (the rpc layer's "call failed" contract), with the chain already rendered
 * inside ensure_connected. */
PICOMESH_EXTERNAL_CALLBACK
static size_t rpc_async_client_call(void *vclient, enum rpc_op op, uint32_t id,
                                    const void *body, size_t body_len,
                                    void *resp, size_t resp_max)
{
    struct rpc_async_client *client = vclient;

    struct picomesh_int_result connected = rpc_async_ensure_connected(client);
    if (PICOMESH_IS_ERR(connected)) { picomesh_error_destroy(connected.error); return 0; }
    if (!connected.value) return 0;

    uint32_t req_id = ++client->next_req_id;
    if (req_id == 0) req_id = ++client->next_req_id; /* keep 0 reserved */

    struct pending_call *call = calloc(1, sizeof(*call));
    if (!call) return 0;
    call->req_id = req_id;
    call->coro = picomesh_coro_current();
    call->resp = resp;
    call->resp_max = resp_max;
    HASH_ADD(hh, client->pending, req_id, sizeof(req_id), call);

    /* Frame: header | req_id | body_len | body — emitted in one write so
     * libuv keeps it contiguous and it can't interleave with other
     * writers on this connection. */
    uint32_t header = RPC_HDR_MAKE(op, id);
    uint32_t blen = (uint32_t)body_len;
    size_t frame_len = 12 + body_len;
    uint8_t *frame = malloc(frame_len);
    if (!frame) { HASH_DEL(client->pending, call); free(call); return 0; }
    memcpy(frame, &header, 4);
    memcpy(frame + 4, &req_id, 4);
    memcpy(frame + 8, &blen, 4);
    if (body_len) memcpy(frame + 12, body, body_len);

    struct picomesh_size_result write_res = loop_write(client->stream, frame, frame_len);
    free(frame);
    if (PICOMESH_IS_ERR(write_res)) {
        /* Write failed: drop our own pending and bail. The broken socket
         * will surface as EOF on the reader, which fails the rest and
         * reconnects on the next call. (If a teardown already delivered
         * `call`, it's off the map and done — we just free it.) */
        picomesh_error_destroy(write_res.error);
        if (!call->done) HASH_DEL(client->pending, call);
        free(call);
        return 0;
    }

    /* Park until the reader delivers our response. Set `waiting` so the
     * reader knows it may resume us; the loop also covers the case where
     * the response already landed during our write completion. */
    while (!call->done) {
        call->waiting = 1;
        picomesh_coro_yield();
    }

    size_t result_len = call->resp_len;
    free(call);
    return result_len;
}

/* Fire-and-forget: write the request frame and return. No pending entry,
 * no park — the caller never waits for a reply. The frame carries req_id 0
 * (reserved); the reader drains the peer's reply silently. Used for
 * telemetry span ship-out so it never adds a round-trip to a request. */
/* Registered transport `oneway` op — fixed void signature (peer_channel_set_
 * async_oneway). A connect failure drops the fire-and-forget send; the chain is
 * rendered inside ensure_connected. */
PICOMESH_EXTERNAL_CALLBACK
static void rpc_async_client_oneway(void *vclient, enum rpc_op op, uint32_t id,
                                    const void *body, size_t body_len)
{
    struct rpc_async_client *client = vclient;
    struct picomesh_int_result connected = rpc_async_ensure_connected(client);
    if (PICOMESH_IS_ERR(connected)) { picomesh_error_destroy(connected.error); return; }
    if (!connected.value) return;

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
    loop_write(client->stream, frame, frame_len); /* yields; result ignored */
    free(frame);
}

static void rpc_async_client_destroy(void *vclient)
{
    struct rpc_async_client *client = vclient;
    if (!client) return;
    /* Shutdown path: the loop has stopped, so don't resume anyone — just
     * free outstanding state. */
    struct pending_call *call, *tmp;
    HASH_ITER(hh, client->pending, call, tmp) {
        HASH_DEL(client->pending, call);
        free(call);
    }
    if (client->reader && picomesh_coro_is_finished(client->reader)) picomesh_coro_destroy(client->reader);
    if (client->stream) loop_close(client->stream);
    free(client->host);
    free(client);
}

static struct rpc_async_client *rpc_async_client_create(struct loop *loop,
                                                        const char *host, int port)
{
    struct rpc_async_client *client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->loop = loop;
    client->port = port;
    client->host = strdup(host);
    if (!client->host) { free(client); return NULL; }
    return client;
}

/* Build an async-backed session for a remote: a fd-less peer_channel
 * whose calls are carried by the loop client above. */
static struct peer_channel *open_async_session(struct loop *loop,
                                              const char *host, int port)
{
    struct rpc_async_client *client = rpc_async_client_create(loop, host, port);
    if (!client) return NULL;
    struct peer_channel *channel = peer_channel_create(-1); /* no fd; async carries IO */
    if (!channel) { rpc_async_client_destroy(client); return NULL; }
    client->owner = channel; /* so reconnects can flush the channel's proxy cache */
    peer_channel_set_async(channel, client, rpc_async_client_call, rpc_async_client_destroy);
    peer_channel_set_async_oneway(channel, rpc_async_client_oneway);
    return channel;
}

/* ---- async MessagePack outbound transport (issue #22) ----------------
 *
 * The loop-aware twin of open_async_session, but speaking the Picomesh msgpack
 * envelope wire (the same one the msgpack frontend serves) instead of the
 * native binary RPC. Because that envelope carries NO req_id, calls on one
 * channel cannot be multiplexed like the yrpc client — they are SERIALISED: a
 * coroutine that finds the stream busy parks on a FIFO and is resumed when the
 * in-flight call finishes. Connect is lazy and reconnects after a drop. This
 * lets a C service on the event loop reach a foreign msgpack service declared
 * as `remotes: [{transport: msgpack}]` without blocking the loop. */
struct msgpack_async_client {
    struct loop *loop;
    char *host;
    int port;
    struct loop_stream *stream;           /* NULL until connected / after a drop */
    int busy;                              /* a call is in flight on the stream */
    struct coro_waiter *wq_head, *wq_tail; /* callers parked behind the in-flight one */
};

/* Cooperative mutex over the serial stream: take it, or park until the holder
 * leaves and hands it over. */
static void msgpack_async_gate_enter(struct msgpack_async_client *client)
{
    if (!client->busy) {
        client->busy = 1;
        return;
    }
    struct coro_waiter waiter = {.coro = picomesh_coro_current(), .next = NULL};
    if (client->wq_tail) client->wq_tail->next = &waiter;
    else client->wq_head = &waiter;
    client->wq_tail = &waiter;
    picomesh_coro_yield(); /* resumed by leave() once it's our turn */
    client->busy = 1;
}

static void msgpack_async_gate_leave(struct msgpack_async_client *client)
{
    client->busy = 0;
    struct coro_waiter *next = client->wq_head;
    if (next) {
        client->wq_head = next->next;
        if (!client->wq_head) client->wq_tail = NULL;
        picomesh_coro_resume(next->coro);
    }
}

static void msgpack_async_wr_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t msgpack_async_rd_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

#define MSGPACK_ASYNC_RESP_MAX (1u << 20)

/* One framed request/response over the (lazily-connected) stream. Caller holds
 * the gate. On any I/O error the stream is closed so the next call reconnects. */
static struct picomesh_int_result msgpack_async_roundtrip(struct msgpack_async_client *client, const uint8_t *env,
                                   size_t env_len, uint8_t **resp_out, size_t *resp_len_out,
                                   char *err, size_t err_cap)
{
    if (!client->stream) {
        struct loop_stream_ptr_result connect_res =
            loop_connect_tcp(client->loop, client->host, client->port);
        if (PICOMESH_IS_ERR(connect_res)) {
            snprintf(err, err_cap, "msgpack async: connect %s:%d failed", client->host,
                     client->port);
            picomesh_error_print(stderr, "msgpack async: connect failed", connect_res.error);
            picomesh_error_destroy(connect_res.error);
            return PICOMESH_OK(picomesh_int, 0);
        }
        client->stream = connect_res.value;
    }

    uint8_t lenbuf[4];
    msgpack_async_wr_be32(lenbuf, (uint32_t)env_len);
    struct picomesh_size_result lenbuf_write = loop_write(client->stream, lenbuf, 4);
    struct picomesh_size_result env_write =
        PICOMESH_IS_OK(lenbuf_write) ? loop_write(client->stream, env, env_len)
                                     : PICOMESH_OK(picomesh_size, 0);
    if (PICOMESH_IS_ERR(lenbuf_write) || PICOMESH_IS_ERR(env_write)) {
        if (PICOMESH_IS_ERR(lenbuf_write)) picomesh_error_destroy(lenbuf_write.error);
        if (PICOMESH_IS_ERR(env_write)) picomesh_error_destroy(env_write.error);
        snprintf(err, err_cap, "msgpack async: write failed");
        loop_close(client->stream);
        client->stream = NULL;
        return PICOMESH_OK(picomesh_int, 0);
    }

    uint8_t resp_lenbuf[4];
    struct picomesh_size_result resplen_read = loop_read(client->stream, resp_lenbuf, 4);
    if (PICOMESH_IS_ERR(resplen_read) || resplen_read.value != 4) {
        if (PICOMESH_IS_ERR(resplen_read)) picomesh_error_destroy(resplen_read.error);
        snprintf(err, err_cap, "msgpack async: no response");
        loop_close(client->stream);
        client->stream = NULL;
        return PICOMESH_OK(picomesh_int, 0);
    }
    uint32_t resp_len = msgpack_async_rd_be32(resp_lenbuf);
    if (resp_len == 0 || resp_len > MSGPACK_ASYNC_RESP_MAX) {
        snprintf(err, err_cap, "msgpack async: bad response length");
        loop_close(client->stream);
        client->stream = NULL;
        return PICOMESH_OK(picomesh_int, 0);
    }
    uint8_t *resp = malloc(resp_len);
    if (!resp) {
        snprintf(err, err_cap, "msgpack async: out of memory");
        return PICOMESH_OK(picomesh_int, 0);
    }
    struct picomesh_size_result resp_read = loop_read(client->stream, resp, resp_len);
    if (PICOMESH_IS_ERR(resp_read) || resp_read.value != resp_len) {
        if (PICOMESH_IS_ERR(resp_read)) picomesh_error_destroy(resp_read.error);
        free(resp);
        snprintf(err, err_cap, "msgpack async: short response");
        loop_close(client->stream);
        client->stream = NULL;
        return PICOMESH_OK(picomesh_int, 0);
    }
    *resp_out = resp;
    *resp_len_out = resp_len;
    return PICOMESH_OK(picomesh_int, 1);
}

/* Registered msgpack transport `io` op — its int signature is fixed by
 * peer_channel_set_msgpack_async; a roundtrip Result failure can't be carried
 * up (the contract is 1/0 + an err string), so it's absorbed here with the
 * chain rendered. */
PICOMESH_EXTERNAL_CALLBACK
static int msgpack_async_io(void *ctx, const uint8_t *env, size_t env_len, uint8_t **resp_out,
                            size_t *resp_len_out, char *err, size_t err_cap)
{
    struct msgpack_async_client *client = ctx;
    *resp_out = NULL;
    *resp_len_out = 0;
    msgpack_async_gate_enter(client);
    struct picomesh_int_result roundtrip_res =
        msgpack_async_roundtrip(client, env, env_len, resp_out, resp_len_out, err, err_cap);
    msgpack_async_gate_leave(client);
    if (PICOMESH_IS_ERR(roundtrip_res)) {
        picomesh_error_print(stderr, "msgpack async io: roundtrip", roundtrip_res.error);
        picomesh_error_destroy(roundtrip_res.error);
        return 0;
    }
    return roundtrip_res.value;
}

static void msgpack_async_client_destroy(void *ctx)
{
    struct msgpack_async_client *client = ctx;
    if (!client) return;
    if (client->stream) loop_close(client->stream);
    free(client->host);
    free(client);
}

static struct peer_channel *open_async_msgpack_session(struct loop *loop, const char *host,
                                                       int port)
{
    struct msgpack_async_client *client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->loop = loop;
    client->port = port;
    client->host = strdup(host);
    if (!client->host) { free(client); return NULL; }
    struct peer_channel *channel = peer_channel_create_msgpack(-1); /* fd-less; async carries IO */
    if (!channel) { free(client->host); free(client); return NULL; }
    peer_channel_set_msgpack_async(channel, client, msgpack_async_io, msgpack_async_client_destroy);
    return channel;
}

/* Pick the outbound transport for a config remote. */
static struct peer_channel *open_remote_session(struct loop *loop, const char *host, int port,
                                                const char *transport)
{
    if (transport && strcmp(transport, "msgpack") == 0)
        return open_async_msgpack_session(loop, host, port);
    return open_async_session(loop, host, port);
}

/* As picomesh_engine_add_remote, but `transport` selects the outbound wire:
 * NULL/"yrpc" = native async binary RPC; "msgpack" = the async msgpack envelope
 * transport (issue #22). */
struct picomesh_void_result picomesh_engine_add_remote_transport(struct picomesh_engine *engine,
                                                                 const char *name,
                                                                 const char *host, int port,
                                                                 const char *transport)
{
    if (!engine || !name) return PICOMESH_ERR(picomesh_void, "add_remote: bad args");
    if (!host) host = "127.0.0.1";
    const char *transport_label = (transport && strcmp(transport, "msgpack") == 0) ? "msgpack"
                                                                                   : "yrpc";

    /* Per-worker: register against the calling worker's loop and list. */
    struct picomesh_worker *worker = engine_current_worker(engine);

    /* Replace any previous registration with the same name. The async
     * session connects lazily on first use, so there's no dial here —
     * just swap the session in. */
    for (struct remote_entry *remote = worker->remotes; remote; remote = remote->next) {
        if (strcmp(remote->name, name) == 0) {
            peer_channel_destroy(remote->peer);
            remote->peer = open_remote_session(worker->loop, host, port, transport);
            if (!remote->peer) return PICOMESH_ERR(picomesh_void, "add_remote: session_create");
            yinfo("engine[w%zu]: reopened remote '%s' → %s:%d (async %s)",
                  worker->index, name, host, port, transport_label);
            return PICOMESH_OK_VOID();
        }
    }

    struct peer_channel *channel = open_remote_session(worker->loop, host, port, transport);
    if (!channel) return PICOMESH_ERR(picomesh_void, "add_remote: session_create");

    struct remote_entry *node = calloc(1, sizeof(*node));
    if (!node) { peer_channel_destroy(channel); return PICOMESH_ERR(picomesh_void, "add_remote: calloc"); }
    node->name = strdup(name);
    if (!node->name) { peer_channel_destroy(channel); free(node); return PICOMESH_ERR(picomesh_void, "add_remote: strdup"); }
    node->peer = channel;
    node->next = worker->remotes;
    worker->remotes = node;
    yinfo("engine[w%zu]: opened remote '%s' → %s:%d (%s)", worker->index, name, host, port,
          transport_label);
    return PICOMESH_OK_VOID();
}

struct picomesh_void_result picomesh_engine_add_remote(struct picomesh_engine *engine,
                                                       const char *name, const char *host, int port)
{
    return picomesh_engine_add_remote_transport(engine, name, host, port, NULL);
}

struct peer_channel *picomesh_engine_remote(struct picomesh_engine *engine, const char *name)
{
    if (!engine || !name) return NULL;
    struct picomesh_worker *worker = engine_current_worker(engine);
    for (struct remote_entry *remote = worker->remotes; remote; remote = remote->next) {
        if (strcmp(remote->name, name) == 0) return remote->peer;
    }
    return NULL;
}

struct ctx picomesh_engine_service_ctx(struct picomesh_engine *engine, const char *service)
{
    struct ctx ctx = {.peer = NULL, .local_cache = NULL};
    if (!engine || !service) return ctx;
    ctx.peer = picomesh_engine_remote(engine, service);
    if (!ctx.peer) {
        /* Service isn't a remote ⇒ it's collocated in this process. Hand
         * over the current worker's local-instance cache head so the
         * acquired object is reused across calls (state persists). */
        struct picomesh_worker *worker = engine_current_worker(engine);
        if (worker) ctx.local_cache = &worker->local_instances;
    }
    return ctx;
}

/* Look up the bind host/port for a named service by scanning
 * `mesh.services.<svc>.host/port` (scenario shape). Returns 1 on
 * success. */
static struct picomesh_int_result resolve_service_bind(struct picomesh_engine *engine, const char *service,
                                char *host_out, size_t host_cap, int *port_out)
{
    char path[256];
    snprintf(path, sizeof(path), "mesh.services.%s.host", service);
    struct config_node_ptr_result host_res = config_get(picomesh_engine_config(engine), path);
    PICOMESH_RETURN_IF_ERR(picomesh_int, host_res, "resolve_service_bind: host config read failed");
    const char *host = host_res.value ? config_node_as_string(host_res.value, NULL) : NULL;
    snprintf(path, sizeof(path), "mesh.services.%s.port", service);
    struct config_node_ptr_result port_res = config_get(picomesh_engine_config(engine), path);
    PICOMESH_RETURN_IF_ERR(picomesh_int, port_res, "resolve_service_bind: port config read failed");
    int64_t port = port_res.value ? config_node_as_int(port_res.value, -1) : -1;
    if (port <= 0) return PICOMESH_OK(picomesh_int, 0);
    snprintf(host_out, host_cap, "%s", host && *host ? host : "127.0.0.1");
    *port_out = (int)port;
    return PICOMESH_OK(picomesh_int, 1);
}

struct picomesh_int_result picomesh_engine_service_addr(struct picomesh_engine *engine, const char *service,
                              char *host_out, size_t host_cap, int *port_out)
{
    if (!engine || !service || !host_out || !port_out) return PICOMESH_OK(picomesh_int, 0);
    return resolve_service_bind(engine, service, host_out, host_cap, port_out);
}

/* A `remotes:` entry is `{service: <name>, host?: <str>, port?: <int>,
 * transport?: yrpc|msgpack}`. The walk callback fills this struct in by key. */
struct remote_entry_fields {
    const char *svc;
    const char *host;
    int port;
    const char *transport; /* NULL/"yrpc" = native binary; "msgpack" = issue #22 */
};

static int remote_entry_walk_cb(const char *key, const struct config_node *val,
                                void *ud)
{
    struct remote_entry_fields *fields = ud;
    if (strcmp(key, "service") == 0) {
        fields->svc = config_node_as_string(val, NULL);
    } else if (strcmp(key, "host") == 0) {
        fields->host = config_node_as_string(val, NULL);
    } else if (strcmp(key, "port") == 0) {
        fields->port = (int)config_node_as_int(val, 0);
    } else if (strcmp(key, "transport") == 0) {
        fields->transport = config_node_as_string(val, NULL);
    }
    return 0;
}

struct picomesh_size_result picomesh_engine_open_remotes(struct picomesh_engine *engine, const char *plugin)
{
    if (!engine || !plugin) return PICOMESH_OK(picomesh_size, 0);

    char path[256];
    snprintf(path, sizeof(path), "mesh.services.%s.config.remotes", plugin);
    struct config_node_ptr_result list_res =
        config_get(picomesh_engine_config(engine), path);
    PICOMESH_RETURN_IF_ERR(picomesh_size, list_res, "open_remotes: remotes config read failed");
    if (!list_res.value) return PICOMESH_OK(picomesh_size, 0);
    const struct config_node *list = list_res.value;
    if (config_node_kind(list) != CONFIG_LIST) return PICOMESH_OK(picomesh_size, 0);

    size_t opened = 0;
    size_t count = config_node_size(list);
    for (size_t i = 0; i < count; ++i) {
        const struct config_node *entry = config_node_at(list, i);
        if (!entry || config_node_kind(entry) != CONFIG_MAP) continue;

        struct remote_entry_fields fields = {NULL, NULL, 0, NULL};
        config_node_for_each(entry, remote_entry_walk_cb, &fields);
        if (!fields.svc || !*fields.svc) continue;

        const char *host = fields.host;
        int port = fields.port;
        /* `port: auto` (or no port) — the address was discovered through the
         * registry at boot and recorded in the resolved-remote table. */
        char resolved_host[128];
        if (port <= 0) {
            int resolved_port = 0;
            if (picomesh_engine_get_resolved_remote(engine, fields.svc, resolved_host,
                                                    sizeof(resolved_host), &resolved_port) && resolved_port > 0) {
                if (!host || !*host) host = resolved_host;
                port = resolved_port;
            }
        }
        /* Legacy fallback: the static `mesh.services.<svc>.port` shape. */
        if (port <= 0) {
            char bind_host[128];
            int bind_port = 0;
            struct picomesh_int_result bind_res =
                resolve_service_bind(engine, fields.svc, bind_host, sizeof(bind_host), &bind_port);
            PICOMESH_RETURN_IF_ERR(picomesh_size, bind_res, "open_remotes: service bind lookup failed");
            if (bind_res.value) {
                if (!host) host = bind_host;
                port = bind_port;
            }
        }
        if (port <= 0) {
            ywarn("engine: remote '%s' for plugin '%s' — no port (skip)",
                  fields.svc, plugin);
            continue;
        }
        struct picomesh_void_result add_res =
            picomesh_engine_add_remote_transport(engine, fields.svc, host, port, fields.transport);
        if (PICOMESH_IS_OK(add_res)) {
            opened++;
        } else {
            ywarn("engine: failed to open remote '%s' for plugin '%s'",
                  fields.svc, plugin);
            picomesh_error_destroy(add_res.error);
        }
    }
    return PICOMESH_OK(picomesh_size, opened);
}

static struct picomesh_void_result worker_start_flush_timer(struct picomesh_worker *w);

struct picomesh_void_result picomesh_engine_run(struct picomesh_engine *engine)
{
    if (!engine) return PICOMESH_ERR(picomesh_void, "picomesh_engine_run: NULL engine");
    *current_worker_slot() = &engine->workers[0];
    struct picomesh_void_result flush_res = worker_start_flush_timer(&engine->workers[0]);
    PICOMESH_RETURN_IF_ERR(picomesh_void, flush_res, "picomesh_engine_run: start flush timer");
    struct picomesh_void_result run_res = loop_run(engine->workers[0].loop);
    PICOMESH_RETURN_IF_ERR(picomesh_void, run_res, "picomesh_engine_run: loop_run failed");
    return PICOMESH_OK_VOID();
}

/* pthread start routine for workers 1..N-1. External-library signature
 * (void *(void *)) — it can't return a Result, so it absorbs setup
 * errors at this boundary (the worker bows out, the rest keep serving).
 * The worker's loop runs until shutdown. */
/* Long-lived per-worker telemetry-flush coroutine: every 50ms it drains the
 * collector ingest arena and ships this worker's pending span batch. It runs as
 * a coroutine (via loop_sleep_ms) precisely because the batch ship ends in a
 * yielding write — a bare timer callback could not ship at all, which stranded
 * batched spans. Sees this worker's thread-local buffers (one coro per loop). */
static void telemetry_flush_loop(void *arg)
{
    struct loop *loop = arg;
    for (;;) {
        loop_sleep_ms(loop, 1000);
        ytelemetry_store_flush_local();                               /* collector arena (no yield) */
        if (ytelemetry_pending_local() > 0) ytelemetry_flush_local(); /* sender batch (yields) */
    }
}

/* Spawn the flush coroutine on a worker's loop. */
static struct picomesh_void_result worker_start_flush_timer(struct picomesh_worker *worker)
{
    struct picomesh_coro_ptr_result spawn_res =
        picomesh_coro_spawn(telemetry_flush_loop, worker->loop, 0, "ytel-flush");
    PICOMESH_RETURN_IF_ERR(picomesh_void, spawn_res, "worker_start_flush_timer: spawn ytel-flush");
    picomesh_coro_resume(spawn_res.value); /* runs to its first loop_sleep_ms yield */
    return PICOMESH_OK_VOID();
}

PICOMESH_EXTERNAL_CALLBACK
static void *worker_thread_main(void *arg)
{
    struct picomesh_worker *worker = arg;
    *current_worker_slot() = worker;

    struct picomesh_void_result setup_res = worker->setup(worker->engine, (int)worker->index, worker->setup_ud);
    if (PICOMESH_IS_ERR(setup_res)) {
        picomesh_error_print(stderr, "picomesh_engine_run_workers: worker setup", setup_res.error);
        picomesh_error_destroy(setup_res.error);
        return NULL;
    }
    struct picomesh_void_result flush_res = worker_start_flush_timer(worker);
    if (PICOMESH_IS_ERR(flush_res)) {
        picomesh_error_print(stderr, "worker_thread_main: flush timer", flush_res.error);
        picomesh_error_destroy(flush_res.error);
    }
    struct picomesh_void_result run_res = loop_run(worker->loop);
    if (PICOMESH_IS_ERR(run_res)) {
        /* Worker libuv root: a dead worker loop must be visible, not silently
         * dropped while the rest of the process keeps serving. */
        picomesh_error_print(stderr, "worker_thread_main: loop_run", run_res.error);
        picomesh_error_destroy(run_res.error);
    }
    return NULL;
}

struct picomesh_void_result picomesh_engine_run_workers(struct picomesh_engine *engine,
                                                  size_t workers,
                                                  picomesh_worker_setup_fn setup,
                                                  void *ud)
{
    if (!engine) return PICOMESH_ERR(picomesh_void, "run_workers: NULL engine");
    if (!setup) return PICOMESH_ERR(picomesh_void, "run_workers: NULL setup");
    if (workers < 1) workers = 1;

    /* Grow the worker array to N, creating a fresh loop per new worker.
     * Worker 0 (loop created at engine create) is kept as-is. If a loop
     * can't be created we cap the fleet at what we built. */
    if (workers > engine->worker_count) {
        struct picomesh_worker *grown =
            realloc(engine->workers, workers * sizeof(struct picomesh_worker));
        if (!grown) return PICOMESH_ERR(picomesh_void, "run_workers: realloc(workers) failed");
        engine->workers = grown;
        for (size_t i = engine->worker_count; i < workers; ++i) {
            memset(&engine->workers[i], 0, sizeof(struct picomesh_worker));
            engine->workers[i].engine = engine;
            engine->workers[i].index = i;
            struct loop_ptr_result loop_res = loop_create();
            if (PICOMESH_IS_ERR(loop_res)) {
                ywarn("run_workers: loop_create for worker %zu failed — "
                      "capping fleet at %zu", i, i);
                picomesh_error_destroy(loop_res.error);
                break;
            }
            engine->workers[i].loop = loop_res.value;
            engine->worker_count = i + 1;
        }
    }

    /* The libuv worker pool (used by loop_run_blocking for DB / libgit2
     * offload) is process-global, default size 4. With several worker
     * loops all offloading, give the pool at least one thread per worker
     * so blocking work doesn't serialise behind it. Only nudges the
     * default up — an explicit env override always wins (overwrite=0). */
    if (engine->worker_count > 1) {
        char pool[16];
        snprintf(pool, sizeof(pool), "%zu", engine->worker_count * 4);
        setenv("UV_THREADPOOL_SIZE", pool, 0);
    }

    /* Worker 0 first, on THIS thread — its setup runs before any thread
     * is spawned, so a worker-0 setup failure is reported directly with
     * nothing to unwind. */
    *current_worker_slot() = &engine->workers[0];
    struct picomesh_void_result setup_res0 = setup(engine, 0, ud);
    PICOMESH_RETURN_IF_ERR(picomesh_void, setup_res0, "run_workers: worker 0 setup failed");

    /* Spawn workers 1..N-1; each runs its own setup + loop. */
    for (size_t i = 1; i < engine->worker_count; ++i) {
        engine->workers[i].setup = setup;
        engine->workers[i].setup_ud = ud;
        int rc = pthread_create(&engine->workers[i].thread, NULL,
                                worker_thread_main, &engine->workers[i]);
        if (rc != 0) {
            ywarn("run_workers: pthread_create for worker %zu failed (rc=%d) — "
                  "continuing with fewer workers", i, rc);
            continue;
        }
        engine->workers[i].started = 1;
    }
    yinfo("run_workers: %zu worker thread(s) serving", engine->worker_count);

    /* Worker 0 drives this thread's loop until shutdown. */
    struct picomesh_void_result flush_res = worker_start_flush_timer(&engine->workers[0]);
    if (PICOMESH_IS_ERR(flush_res)) {
        picomesh_error_print(stderr, "run_workers: flush timer", flush_res.error);
        picomesh_error_destroy(flush_res.error);
    }
    struct picomesh_void_result run_res = loop_run(engine->workers[0].loop);

    /* Loop exited (clean shutdown). Nudge the others to stop, then join.
     * In practice serve shutdown is signal-driven (the process dies), so
     * this path is mostly for tests / a clean picomesh_engine_stop. */
    for (size_t i = 1; i < engine->worker_count; ++i) {
        if (engine->workers[i].started && engine->workers[i].loop) loop_stop(engine->workers[i].loop);
    }
    for (size_t i = 1; i < engine->worker_count; ++i) {
        if (engine->workers[i].started) pthread_join(engine->workers[i].thread, NULL);
    }

    PICOMESH_RETURN_IF_ERR(picomesh_void, run_res, "run_workers: worker 0 loop failed");
    return PICOMESH_OK_VOID();
}

void picomesh_engine_stop(struct picomesh_engine *engine)
{
    if (!engine) return;
    for (size_t i = 0; i < engine->worker_count; ++i) {
        if (engine->workers[i].loop) loop_stop(engine->workers[i].loop);
    }
}

struct picomesh_void_result picomesh_engine_perf_start(struct picomesh_engine *engine, const char *service)
{
    if (!engine) return PICOMESH_ERR(picomesh_void, "picomesh_engine_perf_start: NULL engine");
    struct picomesh_worker *worker = engine_current_worker(engine);

    /* `perf` resolves at the projected root: service projection promoted
     * mesh.services.<name>.config onto the root at engine create, so the
     * service's `config.perf` block is reachable here as the top-level
     * `perf` section (and a standalone process can set `perf:` at the top
     * level directly). */
    const struct config_node *perf_node = config_section(engine->config, "perf");

    char label[64];
    if (service && *service)
        snprintf(label, sizeof(label), "%s w%zu", service, worker->index);
    else
        snprintf(label, sizeof(label), "w%zu", worker->index);

    struct yperf_ptr_result perf_res = yperf_create(perf_node, worker->loop, label);
    if (PICOMESH_IS_ERR(perf_res))
        return PICOMESH_ERR(picomesh_void, "picomesh_engine_perf_start: yperf_create failed", perf_res);
    worker->perf = perf_res.value; /* NULL when profiling is disabled */
    return PICOMESH_OK_VOID();
}

struct loop *picomesh_engine_loop(struct picomesh_engine *engine)
{
    if (!engine) return NULL;
    return engine_current_worker(engine)->loop;
}

const struct config *picomesh_engine_config(struct picomesh_engine *engine)
{
    return engine ? engine->config : NULL;
}

const struct config_node *picomesh_engine_plugin_config(struct picomesh_engine *engine, const char *plugin)
{
    return config_section(engine ? engine->config : NULL, plugin);
}

struct argv_chain *picomesh_engine_cli(struct picomesh_engine *engine)
{
    return engine ? engine->cli : NULL;
}

struct picomesh_void_result picomesh_engine_for_each_plugin(struct picomesh_engine *engine,
                                                     void (*cb)(const char *qname, void *ud),
                                                     void *ud)
{
    (void)engine; (void)cb; (void)ud;
    return PICOMESH_OK_VOID();
}

static struct picomesh_engine **active_engine_slot(void)
{
    static struct picomesh_engine *engine = NULL;
    return &engine;
}

void picomesh_active_engine_set(struct picomesh_engine *engine)
{
    *active_engine_slot() = engine;
}

struct picomesh_engine *picomesh_active_engine(void)
{
    return *active_engine_slot();
}
