/* RPC runtime — packed-header wire, op enum, uthash translations.
 *
 * I/O is still blocking POSIX read/write here. The async-aware variant
 * that yields the calling coroutine on EAGAIN comes in the next layer
 * (see src/picomesh/yco/) — this file stays portable. */

#include <picomesh/yclass/rpc.h>
#include <picomesh/msgpack/msgpack.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <uthash.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_OBJECTS 256
#define BUF_MAX 65536

struct object_entry {
    uint64_t handle;
    void *ptr;
};

struct skel_lookup_node {
    skel_lookup_fn fn;
    struct skel_lookup_node *next;
};

struct skel_cache_entry {
    method_slot slot;
    rpc_skel_fn fn;
    UT_hash_handle hh;
};

/* The skel lookup chain is registered exclusively by codegen
 * __attribute__((constructor)) hooks, which run on the single main
 * thread before main() — and therefore before any worker thread is
 * spawned. Once the program is running it is strictly read-only, so
 * every worker thread can walk it concurrently without a lock. It is
 * the ONE piece of RPC server state that is shared process-wide. */
static struct skel_lookup_node **skel_lookup_chain(void)
{
    static struct skel_lookup_node *head = NULL;
    return &head;
}

/* Per-worker RPC server state. Each worker thread owns its own libuv
 * loop and its own listening fd (SO_REUSEPORT), so the kernel pins every
 * TCP connection — and thus every CREATE/CALL/DESTROY for the proxy
 * objects that connection carries — to a single worker for its lifetime.
 * That connection affinity means the handle table never needs to be
 * shared across threads: each worker registers, resolves, and releases
 * its own handles. Making it thread-local keeps the per-call resolve on
 * the hot path lock-free. The skel cache is likewise per-worker (a tiny
 * read-mostly memo of the shared lookup chain).
 *
 * Handles start at 1 (0 is the reserved "no handle" sentinel). A worker
 * thread's thread-local state is zero-initialised, so next_handle is
 * lazily bumped to 1 on first use. */
struct rpc_worker_state {
    struct object_entry objects[MAX_OBJECTS];
    size_t object_count;
    uint64_t next_handle;
    struct skel_cache_entry *skel_cache;
};

static struct rpc_worker_state *worker_state(void)
{
    static _Thread_local struct rpc_worker_state s;
    if (s.next_handle == 0) s.next_handle = 1;
    return &s;
}

void rpc_init(void)
{
    struct rpc_worker_state *s = worker_state();
    s->object_count = 0;
    s->next_handle = 1;
}

void rpc_add_skel_lookup(skel_lookup_fn fn)
{
    if (!fn) return;
    struct skel_lookup_node *node = calloc(1, sizeof(*node));
    if (!node) return;
    struct skel_lookup_node **head = skel_lookup_chain();
    node->fn = fn;
    node->next = *head;
    *head = node;
}

rpc_skel_fn rpc_skel_for(method_slot slot)
{
    struct rpc_worker_state *s = worker_state();
    if (slot == METHOD_SLOT_UNDEFINED) return NULL;
    struct skel_cache_entry *e = NULL;
    HASH_FIND(hh, s->skel_cache, &slot, sizeof(slot), e);
    if (e) return e->fn;

    rpc_skel_fn fn = NULL;
    for (struct skel_lookup_node *n = *skel_lookup_chain(); n; n = n->next) {
        fn = n->fn(slot);
        if (fn) break;
    }
    if (!fn) return NULL;

    e = calloc(1, sizeof(*e));
    if (!e) return fn;
    e->slot = slot;
    e->fn = fn;
    HASH_ADD(hh, s->skel_cache, slot, sizeof(slot), e);
    return fn;
}

uint64_t rpc_register_object(void *obj)
{
    struct rpc_worker_state *s = worker_state();
    if (s->object_count >= MAX_OBJECTS) return 0;
    uint64_t h = s->next_handle++;
    s->objects[s->object_count].handle = h;
    s->objects[s->object_count].ptr = obj;
    s->object_count++;
    return h;
}

void *rpc_handle_resolve(uint64_t h)
{
    if (!h) return NULL;
    struct rpc_worker_state *s = worker_state();
    for (size_t i = 0; i < s->object_count; ++i) {
        if (s->objects[i].handle == h) return s->objects[i].ptr;
    }
    return NULL;
}

/* Per-request caller context — populated by the yhttp /_rpc handler
 * from HTTP headers before invoking a skel. Cooperative-coroutine
 * safe so long as skels don't yield between reading these and
 * starting their work (they read at the top, then run). */
static __thread uint32_t g_caller_uid = 0;
static __thread uint32_t g_caller_sid = 0;

void rpc_set_current_caller(uint32_t uid, uint32_t sid)
{
    g_caller_uid = uid;
    g_caller_sid = sid;
}

void rpc_current_caller(uint32_t *out_uid, uint32_t *out_sid)
{
    if (out_uid) *out_uid = g_caller_uid;
    if (out_sid) *out_sid = g_caller_sid;
}

/* Drop a handle from the table and free the underlying object. Used by
 * RPC_OP_DESTROY so server-side proxy objects don't accumulate when
 * the client releases its proxy. Returns 1 if a handle was removed. */
static int rpc_handle_release(uint64_t h)
{
    if (!h) return 0;
    struct rpc_worker_state *s = worker_state();
    for (size_t i = 0; i < s->object_count; ++i) {
        if (s->objects[i].handle == h) {
            free(s->objects[i].ptr);
            /* Compact: swap with the tail. The order isn't meaningful. */
            s->objects[i] = s->objects[s->object_count - 1];
            s->object_count--;
            return 1;
        }
    }
    return 0;
}

static size_t handle_destroy(const void *body, size_t body_len, void *resp, size_t resp_max)
{
    if (body_len < sizeof(uint64_t) || resp_max < 1) return 0;
    uint64_t h;
    memcpy(&h, body, sizeof(h));
    int removed = rpc_handle_release(h);
    ((uint8_t *)resp)[0] = removed ? 1 : 0;
    return 1;
}

/* Forward declarations for the admin op handlers, plus a unified
 * dispatcher used by the HTTP /_rpc endpoint. */
static size_t handle_resolve_slot(const void *body, size_t body_len, void *resp, size_t resp_max);
static size_t handle_get_class(const void *body, size_t body_len, void *resp, size_t resp_max);
static size_t handle_create(const void *body, size_t body_len, void *resp, size_t resp_max);

size_t rpc_handle_admin_op(enum rpc_op op,
                           const void *body, size_t body_len,
                           void *resp, size_t resp_max)
{
    switch (op) {
    case RPC_OP_RESOLVE_SLOT: return handle_resolve_slot(body, body_len, resp, resp_max);
    case RPC_OP_GET_CLASS:    return handle_get_class(body, body_len, resp, resp_max);
    case RPC_OP_CREATE:       return handle_create(body, body_len, resp, resp_max);
    case RPC_OP_DESTROY:      return handle_destroy(body, body_len, resp, resp_max);
    default:                  return 0;
    }
}

struct fd_io {
    int fd;
};

static size_t fd_read(void *ud, void *buf, size_t n)
{
    struct fd_io *io = ud;
    char *p = buf;
    size_t left = n;
    while (left) {
        ssize_t r = read(io->fd, p, left);
        if (r > 0) { p += r; left -= (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        return 0;
    }
    return n;
}

static size_t fd_write(void *ud, const void *buf, size_t n)
{
    struct fd_io *io = ud;
    const char *p = buf;
    size_t left = n;
    while (left) {
        ssize_t w = write(io->fd, p, left);
        if (w > 0) { p += w; left -= (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        return 0;
    }
    return n;
}

static int read_full(int fd, void *buf, size_t n)
{
    struct fd_io io = {.fd = fd};
    return fd_read(&io, buf, n) == n ? 0 : -1;
}

static int write_full(int fd, const void *buf, size_t n)
{
    struct fd_io io = {.fd = fd};
    return fd_write(&io, buf, n) == n ? 0 : -1;
}

static size_t handle_resolve_slot(const void *body, size_t body_len, void *resp, size_t resp_max)
{
    char name[128];
    size_t n = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
    memcpy(name, body, n);
    name[n] = 0;
    struct method_slot_result sr = method_slot_by_qname(name);
    uint32_t out;
    if (PICOMESH_IS_ERR(sr)) {
        picomesh_error_destroy(sr.error);
        out = UINT32_MAX;
    } else {
        out = (uint32_t)sr.value;
    }
    if (resp_max < sizeof(out)) return 0;
    memcpy(resp, &out, sizeof(out));
    ydebug("resolve_slot('%s') -> %u", name, out);
    return sizeof(out);
}

struct get_class_ctx {
    uint8_t *out;
    size_t off;
    size_t cap;
};

static void get_class_emit(const char *name, method_slot slot, void *ud)
{
    struct get_class_ctx *gc = ud;
    size_t name_len = strlen(name);
    size_t need = 2 + name_len + 4;
    if (gc->off + need > gc->cap) return;
    uint16_t nl = (uint16_t)name_len;
    memcpy(gc->out + gc->off, &nl, 2);            gc->off += 2;
    memcpy(gc->out + gc->off, name, name_len);    gc->off += name_len;
    uint32_t rid = (uint32_t)slot;
    memcpy(gc->out + gc->off, &rid, 4);           gc->off += 4;
}

static size_t handle_get_class(const void *body, size_t body_len, void *resp, size_t resp_max)
{
    char name[128];
    size_t n = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
    memcpy(name, body, n);
    name[n] = 0;
    struct class_ptr_result cr = class_by_name(name);
    if (PICOMESH_IS_ERR(cr)) {
        picomesh_error_print(stderr, "[server] get_class", cr.error);
        picomesh_error_destroy(cr.error);
        return 0;
    }
    struct get_class_ctx gc = {resp, 0, resp_max};
    class_for_each_slot(cr.value, get_class_emit, &gc);
    ydebug("get_class('%s') -> %zu entries (%zu bytes)", name, gc.off / 6, gc.off);
    return gc.off;
}

static size_t handle_create(const void *body, size_t body_len, void *resp, size_t resp_max)
{
    char name[128];
    size_t n = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
    memcpy(name, body, n);
    name[n] = 0;
    struct class_ptr_result cr = class_by_name(name);
    if (PICOMESH_IS_ERR(cr)) {
        picomesh_error_print(stderr, "[server] create class_by_name", cr.error);
        picomesh_error_destroy(cr.error);
        return 0;
    }
    struct object_ptr_result orr = object_alloc(cr.value);
    if (PICOMESH_IS_ERR(orr)) {
        picomesh_error_print(stderr, "[server] create object_alloc", orr.error);
        picomesh_error_destroy(orr.error);
        return 0;
    }
    uint64_t h = rpc_register_object(orr.value);
    if (resp_max < sizeof(h)) return 0;
    memcpy(resp, &h, sizeof(h));
    ydebug("create('%s') -> handle=%llu", name, (unsigned long long)h);
    return sizeof(h);
}

size_t rpc_dispatch_one(uint32_t header, const void *body, size_t body_len,
                        void *resp, size_t resp_max)
{
    enum rpc_op op = RPC_HDR_OP(header);
    uint32_t id = RPC_HDR_ID(header);

    switch (op) {
    case RPC_OP_CALL: {
        rpc_skel_fn fn = rpc_skel_for((method_slot)id);
        if (fn) {
            ydebug("CALL slot=%u body_len=%zu", id, body_len);
            return fn(body, body_len, resp, resp_max);
        }
        ywarn("CALL slot=%u — no skel", id);
        return 0;
    }
    case RPC_OP_RESOLVE_SLOT:
        return handle_resolve_slot(body, body_len, resp, resp_max);
    case RPC_OP_GET_CLASS:
        return handle_get_class(body, body_len, resp, resp_max);
    case RPC_OP_CREATE:
        return handle_create(body, body_len, resp, resp_max);
    case RPC_OP_DESTROY:
        return handle_destroy(body, body_len, resp, resp_max);
    default:
        ywarn("unknown op=%u", op);
        return 0;
    }
}

void rpc_server_run_io(void *ud, rpc_io_read_fn rd, rpc_io_write_fn wr)
{
    /* Sequential, one-request-at-a-time server over an arbitrary
     * transport (the blocking-fd path). The multiplexing yloop server
     * lives in the yrpc frontend and dispatches each request in its own
     * coroutine; this one stays portable and lock-free by construction. */
    uint8_t *body = malloc(BUF_MAX);
    uint8_t *resp = malloc(BUF_MAX);
    if (!body || !resp) { free(body); free(resp); return; }

    for (;;) {
        uint32_t header = 0, req_id = 0, body_len = 0;
        if (rd(ud, &header, 4) != 4) goto done;
        if (rd(ud, &req_id, 4) != 4) goto done;
        if (rd(ud, &body_len, 4) != 4) goto done;
        if (body_len > BUF_MAX) goto done;
        if (body_len && rd(ud, body, body_len) != body_len) goto done;

        uint32_t resp_len = (uint32_t)rpc_dispatch_one(header, body, body_len, resp, BUF_MAX);

        if (wr(ud, &req_id, 4) != 4) goto done;
        if (wr(ud, &resp_len, 4) != 4) goto done;
        if (resp_len && wr(ud, resp, resp_len) != resp_len) goto done;
    }
done:
    free(body);
    free(resp);
}

void rpc_server_run(int fd)
{
    struct fd_io io = {.fd = fd};
    rpc_server_run_io(&io, fd_read, fd_write);
}

/* -------- client session ------------------------------------------- */

struct translated_class {
    char *name;
    UT_hash_handle hh;
};

struct remote_id_entry {
    method_slot local_slot;
    uint32_t remote_id;
    UT_hash_handle hh;
};

/* One cached remote receiver object per (peer, class). A REMOTE service
 * dependency is created ONCE (RPC_OP_CREATE) and reused for the lifetime
 * of the connection — no per-call CREATE/DESTROY. Flushed on reconnect
 * (the handle belongs to the old backend process). */
struct cached_proxy {
    char *qname;        /* key (class qname) */
    struct object *obj; /* owned: proxy carrying the backend handle */
    UT_hash_handle hh;
};

enum peer_channel_mode {
    RPC_MODE_TCP = 0,     /* legacy yrpc binary on the bare fd */
    RPC_MODE_HTTP = 1,    /* HTTP envelope, auth via headers */
    RPC_MODE_MSGPACK = 2, /* Picomesh msgpack envelope, big-endian length frame */
};

struct peer_channel {
    int fd;
    enum peer_channel_mode mode;
    uint32_t next_req_id; /* RPC_MODE_TCP raw path: per-connection frame id */
    char *http_host;     /* Host header, RPC_MODE_HTTP only */
    uint32_t auth_uid;   /* X-Picomesh-Uid for HTTP requests */
    uint32_t auth_sid;   /* X-Picomesh-Sid for HTTP requests */
    struct remote_id_entry *remote_ids;
    struct translated_class *translated;
    struct cached_proxy *proxy_cache; /* per-class remote receiver objects */
    /* Optional async transport (installed by a yloop-aware layer). When
     * set, rpc_call delegates here instead of doing blocking fd I/O. */
    void *async_ctx;
    rpc_async_call_fn async_call;
    rpc_async_oneway_fn async_oneway; /* fire-and-forget (telemetry) */
    void (*async_destroy)(void *ctx);
};

struct peer_channel *peer_channel_create(int fd)
{
    struct peer_channel *s = calloc(1, sizeof(*s));
    if (s) { s->fd = fd; s->mode = RPC_MODE_TCP; }
    return s;
}

struct peer_channel *peer_channel_create_http(int fd, const char *host)
{
    struct peer_channel *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->fd = fd;
    s->mode = RPC_MODE_HTTP;
    s->http_host = strdup(host ? host : "127.0.0.1");
    return s;
}

/* ---- MessagePack outbound transport ------------------------------- */

#define MSGPACK_CLIENT_RESP_MAX (1u << 20) /* 1 MiB response cap */

static uint32_t msgpack_rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void msgpack_wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

struct peer_channel *peer_channel_create_msgpack(int fd)
{
    struct peer_channel *s = calloc(1, sizeof(*s));
    if (s) { s->fd = fd; s->mode = RPC_MODE_MSGPACK; }
    return s;
}

int peer_channel_is_msgpack(const struct peer_channel *s)
{
    return s && s->mode == RPC_MODE_MSGPACK;
}

int peer_channel_msgpack_call(struct peer_channel *s, const char *path,
                              struct yheaders *hdrs, const void *args, size_t args_len,
                              void *out, size_t out_cap, size_t *out_len,
                              char *err, size_t err_cap)
{
    if (out_len) *out_len = 0;
    if (!s || s->fd < 0) {
        snprintf(err, err_cap, "msgpack call: no channel");
        return 0;
    }
    if (!path) path = "";

    /* Request envelope: a 6-key map with the caller's pre-encoded args array
     * spliced in verbatim (msgpack is concatenative). */
    uint32_t uid = hdrs ? yheaders_get_u32(hdrs, "uid", 0) : 0;
    uint32_t sid = hdrs ? yheaders_get_u32(hdrs, "sid", 0) : 0;
    const char *trace = hdrs ? yheaders_get(hdrs, "trace_id") : NULL;

    size_t env_cap = args_len + strlen(path) + (trace ? strlen(trace) : 0) + 128;
    uint8_t *env = malloc(env_cap);
    if (!env) {
        snprintf(err, err_cap, "msgpack call: out of memory");
        return 0;
    }
    cmp_ctx_t w;
    struct picomesh_msgpack_buffer wb;
    picomesh_msgpack_writer_init(&w, &wb, env, env_cap);
    cmp_write_map(&w, 6);
    cmp_write_str(&w, "v", 1);
    cmp_write_integer(&w, 1);
    cmp_write_str(&w, "op", 2);
    cmp_write_str(&w, "invoke", 6);
    cmp_write_str(&w, "path", 4);
    cmp_write_str(&w, path, (uint32_t)strlen(path));
    cmp_write_str(&w, "args", 4);
    if (wb.offset + args_len > wb.cap) {
        free(env);
        snprintf(err, err_cap, "msgpack call: args too large");
        return 0;
    }
    memcpy(wb.data + wb.offset, args, args_len);
    wb.offset += args_len;
    cmp_write_str(&w, "kwargs", 6);
    cmp_write_map(&w, 0);
    cmp_write_str(&w, "headers", 7);
    cmp_write_map(&w, trace ? 3 : 2);
    cmp_write_str(&w, "uid", 3);
    cmp_write_uinteger(&w, uid);
    cmp_write_str(&w, "sid", 3);
    cmp_write_uinteger(&w, sid);
    if (trace) {
        cmp_write_str(&w, "trace_id", 8);
        cmp_write_str(&w, trace, (uint32_t)strlen(trace));
    }
    size_t env_len = wb.offset;

    uint8_t lenbuf[4];
    msgpack_wr_be32(lenbuf, (uint32_t)env_len);
    if (write_full(s->fd, lenbuf, 4) < 0 || write_full(s->fd, env, env_len) < 0) {
        free(env);
        snprintf(err, err_cap, "msgpack call: write failed");
        return 0;
    }
    free(env);

    uint8_t rlenbuf[4];
    if (read_full(s->fd, rlenbuf, 4) < 0) {
        snprintf(err, err_cap, "msgpack call: no response");
        return 0;
    }
    uint32_t resp_len = msgpack_rd_be32(rlenbuf);
    if (resp_len == 0 || resp_len > MSGPACK_CLIENT_RESP_MAX) {
        snprintf(err, err_cap, "msgpack call: bad response length");
        return 0;
    }
    uint8_t *resp = malloc(resp_len);
    if (!resp) {
        snprintf(err, err_cap, "msgpack call: out of memory");
        return 0;
    }
    if (read_full(s->fd, resp, resp_len) < 0) {
        free(resp);
        snprintf(err, err_cap, "msgpack call: short response");
        return 0;
    }

    cmp_ctx_t r;
    struct picomesh_msgpack_buffer rb;
    picomesh_msgpack_reader_init(&r, &rb, resp, resp_len);
    uint32_t top = 0;
    if (!cmp_read_map(&r, &top)) {
        free(resp);
        snprintf(err, err_cap, "msgpack call: response not a map");
        return 0;
    }
    int ok = 0, have_result = 0;
    size_t result_off = 0, result_len = 0;
    char emsg[8192] = "remote error";
    for (uint32_t i = 0; i < top; ++i) {
        char key[32];
        uint32_t klen = (uint32_t)sizeof(key);
        if (!cmp_read_str(&r, key, &klen))
            break;
        if (strcmp(key, "ok") == 0) {
            bool b = false;
            if (cmp_read_bool(&r, &b))
                ok = b ? 1 : 0;
        } else if (strcmp(key, "result") == 0) {
            result_off = rb.offset;
            if (!cmp_skip_object_no_limit(&r))
                break;
            result_len = rb.offset - result_off;
            have_result = 1;
        } else if (strcmp(key, "error") == 0) {
            uint32_t ec = 0;
            if (cmp_read_map(&r, &ec)) {
                for (uint32_t j = 0; j < ec; ++j) {
                    char ek[32];
                    uint32_t ekl = (uint32_t)sizeof(ek);
                    if (!cmp_read_str(&r, ek, &ekl))
                        break;
                    if (strcmp(ek, "message") == 0) {
                        uint32_t ml = (uint32_t)sizeof(emsg);
                        if (!cmp_read_str(&r, emsg, &ml))
                            break;
                    } else if (!cmp_skip_object_no_limit(&r)) {
                        break;
                    }
                }
            }
        } else if (!cmp_skip_object_no_limit(&r)) {
            break;
        }
    }

    if (!ok) {
        snprintf(err, err_cap, "%s", emsg[0] ? emsg : "remote error");
        free(resp);
        return 0;
    }
    if (have_result) {
        if (result_len > out_cap) {
            free(resp);
            snprintf(err, err_cap, "msgpack call: result too large");
            return 0;
        }
        memcpy(out, resp + result_off, result_len);
        if (out_len)
            *out_len = result_len;
    }
    free(resp);
    return 1;
}

void peer_channel_set_auth(struct peer_channel *s, uint32_t uid, uint32_t sid)
{
    if (!s) return;
    s->auth_uid = uid;
    s->auth_sid = sid;
}

void peer_channel_set_async(struct peer_channel *s, void *ctx,
                           rpc_async_call_fn call, void (*destroy)(void *ctx))
{
    if (!s) return;
    s->async_ctx = ctx;
    s->async_call = call;
    s->async_destroy = destroy;
}

void peer_channel_set_async_oneway(struct peer_channel *s, rpc_async_oneway_fn oneway)
{
    if (!s) return;
    s->async_oneway = oneway;
}

/* Drop every cached remote proxy. Called on reconnect (the handles
 * belong to the now-dead backend process) and at channel teardown. */
void peer_channel_flush_proxy_cache(struct peer_channel *s)
{
    if (!s) return;
    struct cached_proxy *cur, *tmp;
    HASH_ITER(hh, s->proxy_cache, cur, tmp) {
        HASH_DEL(s->proxy_cache, cur);
        free(cur->qname);
        free(cur->obj); /* proxy struct; the backend handle is gone with the conn */
        free(cur);
    }
}

/* Free a worker's in-process default-instance cache (the no-peer side of
 * rpc_object_acquire). `head` is &worker->local_instances. The cached
 * objects are real local service instances, owned here for the worker's
 * lifetime; release them on worker teardown. */
void rpc_local_cache_destroy(void **head)
{
    if (!head) return;
    struct cached_proxy **h = (struct cached_proxy **)head;
    struct cached_proxy *cur, *tmp;
    HASH_ITER(hh, *h, cur, tmp) {
        HASH_DEL(*h, cur);
        free(cur->qname);
        object_free(cur->obj); /* the local service instance */
        free(cur);
    }
    *h = NULL;
}

void peer_channel_destroy(struct peer_channel *s)
{
    if (!s) return;
    if (s->async_destroy && s->async_ctx) s->async_destroy(s->async_ctx);
    peer_channel_flush_proxy_cache(s);
    struct translated_class *cur, *tmp;
    HASH_ITER(hh, s->translated, cur, tmp) {
        HASH_DEL(s->translated, cur);
        free(cur->name);
        free(cur);
    }
    struct remote_id_entry *rcur, *rtmp;
    HASH_ITER(hh, s->remote_ids, rcur, rtmp) {
        HASH_DEL(s->remote_ids, rcur);
        free(rcur);
    }
    free(s->http_host);
    if (s->fd >= 0) close(s->fd);
    free(s);
}

uint32_t peer_channel_remote_id(struct peer_channel *s, method_slot slot)
{
    if (!s) return RPC_REMOTE_ID_UNRESOLVED;
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    return e ? e->remote_id : RPC_REMOTE_ID_UNRESOLVED;
}

void peer_channel_set_remote_id(struct peer_channel *s, method_slot slot, uint32_t remote_id)
{
    if (!s) return;
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e) return;
        e->local_slot = slot;
        HASH_ADD(hh, s->remote_ids, local_slot, sizeof(method_slot), e);
    }
    e->remote_id = remote_id;
}

/* Send an HTTP-wrapped RPC and parse the response body. The HTTP
 * request shape is:
 *
 *   POST /_rpc?op=<op>&id=<id> HTTP/1.1\r\n
 *   Host: <host>\r\n
 *   Content-Length: <body_len>\r\n
 *   Content-Type: application/octet-stream\r\n
 *   X-Picomesh-Uid: <uid>\r\n
 *   X-Picomesh-Sid: <sid>\r\n
 *   Connection: keep-alive\r\n
 *   \r\n
 *   <body_bytes>
 *
 * Response is HTTP/1.1 with a Content-Length-framed binary body —
 * exactly the same shape as the legacy yrpc payload would be. */
static size_t rpc_call_http(struct peer_channel *s, enum rpc_op op, uint32_t id,
                            const void *body, size_t body_len,
                            void *resp, size_t resp_max)
{
    char hdr[512];
    int hn = snprintf(hdr, sizeof(hdr),
        "POST /_rpc?op=%u&id=%u HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: application/octet-stream\r\n"
        "X-Picomesh-Uid: %u\r\n"
        "X-Picomesh-Sid: %u\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        (unsigned)op, id,
        s->http_host ? s->http_host : "127.0.0.1",
        body_len, s->auth_uid, s->auth_sid);
    if (hn <= 0 || (size_t)hn >= sizeof(hdr)) return 0;
    if (write_full(s->fd, hdr, (size_t)hn) < 0) return 0;
    if (body_len && write_full(s->fd, body, body_len) < 0) return 0;

    /* Parse the response. We only need the status line + Content-Length;
     * skip everything else. Read byte-by-byte until \r\n\r\n. */
    char line[1024];
    size_t li = 0;
    int got_blank = 0;
    int saw_cr = 0;
    int blank_state = 0; /* 0=line, 1=after \r, 2=after \r\n, 3=after \r\n\r */
    size_t content_length = 0;
    int status = 0;
    int parsed_status = 0;
    for (;;) {
        char c;
        if (read_full(s->fd, &c, 1) < 0) return 0;
        if (c != '\n') {
            if (li + 1 < sizeof(line)) line[li++] = c;
        }
        if (saw_cr && c == '\n') {
            line[li - 1] = 0; /* drop the \r */
            if (li == 1) {
                got_blank = 1;
                break;
            }
            if (!parsed_status) {
                /* "HTTP/1.1 200 OK" — pull the int */
                const char *p = strchr(line, ' ');
                if (p) status = atoi(p + 1);
                parsed_status = 1;
            } else {
                /* Looking for Content-Length: */
                if (strncasecmp(line, "Content-Length:", 15) == 0) {
                    content_length = (size_t)strtoull(line + 15, NULL, 10);
                }
            }
            li = 0;
            saw_cr = 0;
            blank_state = 2;
            continue;
        }
        saw_cr = (c == '\r');
        (void)blank_state;
    }
    if (!got_blank) return 0;
    if (status < 200 || status >= 300) {
        /* Drain the body so the connection stays usable. */
        size_t remain = content_length;
        uint8_t drain[256];
        while (remain) {
            size_t chunk = remain > sizeof(drain) ? sizeof(drain) : remain;
            if (read_full(s->fd, drain, chunk) < 0) return 0;
            remain -= chunk;
        }
        return 0;
    }
    if (content_length > resp_max) {
        /* Drain & give up — caller's buffer is too small. */
        size_t remain = content_length;
        uint8_t drain[256];
        while (remain) {
            size_t chunk = remain > sizeof(drain) ? sizeof(drain) : remain;
            if (read_full(s->fd, drain, chunk) < 0) return 0;
            remain -= chunk;
        }
        return 0;
    }
    if (content_length && read_full(s->fd, resp, content_length) < 0) return 0;
    return content_length;
}

void rpc_call_oneway(struct peer_channel *s, enum rpc_op op, uint32_t id,
                     const void *body, size_t body_len)
{
    if (!s) return;
    /* Async (yloop) transport: hand off the frame, never wait for a reply. */
    if (s->async_oneway) {
        s->async_oneway(s->async_ctx, op, id, body, body_len);
        return;
    }
    /* Raw-fd fallback: write the frame with the reserved req_id 0 and do
     * NOT read the response (the peer still frames one; it stays buffered
     * until the next read or the socket closes). HTTP mode has no oneway. */
    if (s->mode != RPC_MODE_TCP || s->fd < 0) return;
    uint32_t header = RPC_HDR_MAKE(op, id);
    uint32_t req_id = 0;
    uint32_t bl = (uint32_t)body_len;
    if (write_full(s->fd, &header, 4) < 0) return;
    if (write_full(s->fd, &req_id, 4) < 0) return;
    if (write_full(s->fd, &bl, 4) < 0) return;
    if (body_len) write_full(s->fd, body, body_len);
}

size_t rpc_call(struct peer_channel *s, enum rpc_op op, uint32_t id, const void *body,
                size_t body_len, void *resp, size_t resp_max)
{
    if (!s) return 0;
    ydebug("mode=%d op=%u id=%u body_len=%zu", s->mode, op, id, body_len);

    /* Async transport (yloop-aware, non-blocking) takes precedence when
     * installed — it carries the call over a coroutine-yielding
     * connection instead of blocking the loop on the bare fd. */
    if (s->async_call)
        return s->async_call(s->async_ctx, op, id, body, body_len, resp, resp_max);

    if (s->mode == RPC_MODE_HTTP) {
        return rpc_call_http(s, op, id, body, body_len, resp, resp_max);
    }

    uint32_t header = RPC_HDR_MAKE(op, id);
    uint32_t req_id = ++s->next_req_id;
    uint32_t bl = (uint32_t)body_len;
    if (write_full(s->fd, &header, 4) < 0) return 0;
    if (write_full(s->fd, &req_id, 4) < 0) return 0;
    if (write_full(s->fd, &bl, 4) < 0) return 0;
    if (body_len && write_full(s->fd, body, body_len) < 0) return 0;

    /* Strictly sequential on this transport: read the echoed req_id and
     * confirm it matches before consuming the payload. */
    uint32_t resp_req_id = 0;
    if (read_full(s->fd, &resp_req_id, 4) < 0) return 0;
    if (resp_req_id != req_id) {
        ywarn("rpc_call: req_id mismatch (sent %u got %u)", req_id, resp_req_id);
        return 0;
    }
    uint32_t resp_len = 0;
    if (read_full(s->fd, &resp_len, 4) < 0) return 0;
    if (resp_len > resp_max) {
        uint8_t drain[256];
        size_t remain = resp_len;
        while (remain) {
            size_t chunk = remain > sizeof(drain) ? sizeof(drain) : remain;
            if (read_full(s->fd, drain, chunk) < 0) return 0;
            remain -= chunk;
        }
        return 0;
    }
    if (resp_len && read_full(s->fd, resp, resp_len) < 0) return 0;
    return resp_len;
}

uint32_t peer_channel_ensure_remote_id(struct peer_channel *s, method_slot local_slot)
{
    if (!s) return RPC_REMOTE_ID_UNRESOLVED;
    uint32_t cached = peer_channel_remote_id(s, local_slot);
    if (cached != RPC_REMOTE_ID_UNRESOLVED) return cached;

    struct const_char_ptr_result nr = method_slot_name(local_slot);
    if (PICOMESH_IS_ERR(nr)) {
        picomesh_error_destroy(nr.error);
        return RPC_REMOTE_ID_UNRESOLVED;
    }
    const char *name = nr.value;

    uint32_t remote = RPC_REMOTE_ID_UNRESOLVED;
    size_t n =
        rpc_call(s, RPC_OP_RESOLVE_SLOT, 0, name, strlen(name), &remote, sizeof(remote));
    if (n != sizeof(remote) || remote == RPC_REMOTE_ID_UNRESOLVED) return RPC_REMOTE_ID_UNRESOLVED;

    peer_channel_set_remote_id(s, local_slot, remote);
    ydebug("lazy resolve '%s' local=%u remote=%u", name, local_slot, remote);
    return remote;
}

int peer_channel_translate_class(struct peer_channel *s, const char *class_name)
{
    if (!s || !class_name) return -1;
    struct translated_class *t = NULL;
    HASH_FIND_STR(s->translated, class_name, t);
    if (t) return 0;

    uint8_t buf[BUF_MAX];
    size_t name_len = strlen(class_name);
    size_t resp_len =
        rpc_call(s, RPC_OP_GET_CLASS, 0, class_name, name_len, buf, sizeof(buf));
    if (resp_len == 0) return -1;

    size_t off = 0;
    while (off + 2 + 4 <= resp_len) {
        uint16_t nl;
        memcpy(&nl, buf + off, 2);
        off += 2;
        if (off + nl + 4 > resp_len) break;
        char slot_name[128];
        size_t copy = nl < sizeof(slot_name) - 1 ? nl : sizeof(slot_name) - 1;
        memcpy(slot_name, buf + off, copy);
        slot_name[copy] = 0;
        off += nl;
        uint32_t rid;
        memcpy(&rid, buf + off, 4);
        off += 4;

        struct method_slot_result lr = method_slot_by_qname(slot_name);
        if (PICOMESH_IS_OK(lr)) {
            peer_channel_set_remote_id(s, lr.value, rid);
            ydebug("xlat['%s'] local=%u remote=%u", slot_name, lr.value, rid);
        } else {
            picomesh_error_destroy(lr.error);
        }
    }

    t = calloc(1, sizeof(*t));
    if (!t) return 0;
    t->name = strdup(class_name);
    if (!t->name) {
        free(t);
        return 0;
    }
    HASH_ADD_KEYPTR(hh, s->translated, t->name, strlen(t->name), t);
    return 0;
}

/* Acquire a service dependency's receiver object — the shared primitive
 * behind both `object_create_in_ctx` and every codegen `<class>_create`.
 *
 * A service object is NOT created per call. It is a dependency you hold
 * for your lifetime, exactly like yaapp's default-instance proxy:
 *   - REMOTE  (ctx->peer set): one proxy per (peer, class), created once
 *     via RPC_OP_CREATE and cached on the channel; reused for the whole
 *     connection. Flushed only on reconnect.
 *   - IN-PROCESS (no peer): one default instance per class, allocated
 *     once and cached process-wide.
 * Either way there is no per-call CREATE/DESTROY round-trip. */
struct object_ptr_result rpc_object_acquire(struct ctx *ctx, const struct class *klass,
                                            const char *class_qname)
{
    if (!klass)
        return PICOMESH_ERR(object_ptr, "rpc_object_acquire: NULL class");

    /* In-process dependency (no peer). When the caller gave us a worker
     * cache head (collocated / all-in-one mode), reuse ONE instance per
     * class — exactly like the remote path caches one proxy per channel.
     * This is what lets a collocated service that keeps state in memory
     * (git_repo's repo table, etc.) persist across requests instead of
     * getting a blank instance every call. Without a cache head (CLI /
     * unit tests) fall back to a fresh instance the caller owns. */
    if (!ctx || !ctx->peer) {
        if (ctx && ctx->local_cache && class_qname) {
            struct cached_proxy **head = (struct cached_proxy **)ctx->local_cache;
            struct cached_proxy *li = NULL;
            HASH_FIND_STR(*head, class_qname, li);
            if (li) return PICOMESH_OK(object_ptr, li->obj);
            struct object_ptr_result lo = object_alloc(klass);
            if (PICOMESH_IS_ERR(lo)) return lo;
            li = calloc(1, sizeof(*li));
            if (li) {
                li->qname = strdup(class_qname);
                li->obj = lo.value;
                HASH_ADD_KEYPTR(hh, *head, li->qname, strlen(li->qname), li);
            } /* couldn't cache → caller still gets a usable instance */
            return lo;
        }
        return object_alloc(klass);
    }

    struct peer_channel *p = ctx->peer;
    struct cached_proxy *e = NULL;
    if (class_qname) HASH_FIND_STR(p->proxy_cache, class_qname, e);
    if (e) return PICOMESH_OK(object_ptr, e->obj);
    ydebug("rpc_object_acquire: caching new '%s' proxy on peer=%p",
           class_qname ? class_qname : "?", (void *)p);

    if (class_qname) peer_channel_translate_class(p, class_qname);
    uint64_t handle = 0;
    if (rpc_call(p, RPC_OP_CREATE, 0, class_qname, class_qname ? strlen(class_qname) : 0,
                 &handle, sizeof(handle)) != sizeof(handle) || !handle)
        return PICOMESH_ERR(object_ptr, "rpc_object_acquire: remote create failed");

    void *mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!mem)
        return PICOMESH_ERR(object_ptr, "rpc_object_acquire: calloc(proxy) failed");
    struct object *obj = mem;
    *(const struct class **)obj = klass;
    *(uint64_t *)((char *)obj + sizeof(*obj)) = handle;

    e = calloc(1, sizeof(*e));
    if (e && class_qname) {
        e->qname = strdup(class_qname);
        e->obj = obj;
        HASH_ADD_KEYPTR(hh, p->proxy_cache, e->qname, strlen(e->qname), e);
    } else {
        free(e); /* couldn't cache — caller still gets a usable proxy */
    }
    return PICOMESH_OK(object_ptr, obj);
}

/* Release a dependency acquired via rpc_object_acquire.
 *   - REMOTE (ctx->peer): no-op. The proxy is owned by the per-peer cache
 *     and lives for the connection; destroying it per call would defeat
 *     the cache and bring back the CREATE/DESTROY round-trips.
 *   - IN-PROCESS, CACHED (ctx->local_cache): no-op. The instance is owned
 *     by the worker's local-instance cache and lives for the worker;
 *     freeing it here would dangle the cache (use-after-free next call).
 *   - IN-PROCESS, UNCACHED (CLI / unit tests): free the fresh instance. */
void rpc_object_release(struct ctx *ctx, struct object *obj)
{
    if (!obj) return;
    if (ctx && (ctx->peer || ctx->local_cache)) return; /* cached: owner frees */
    object_free(obj);
}

/* Name-keyed twin of the codegen `<class>_create` — used by callers
 * (the gateway) that link no typed `*_create` for the target class. */
struct object_ptr_result object_create_in_ctx(struct ctx *ctx, const char *class_qname)
{
    if (!class_qname)
        return PICOMESH_ERR(object_ptr, "object_create_in_ctx: NULL class name");
    struct class_ptr_result class_r = class_by_name(class_qname);
    if (PICOMESH_IS_ERR(class_r))
        return PICOMESH_ERR(object_ptr, "object_create_in_ctx: unknown class", class_r);
    if (!class_r.value)
        return PICOMESH_ERR(object_ptr, "object_create_in_ctx: class not registered");
    return rpc_object_acquire(ctx, class_r.value, class_qname);
}

void object_release_in_ctx(struct ctx *ctx, struct object *obj)
{
    rpc_object_release(ctx, obj);
}
