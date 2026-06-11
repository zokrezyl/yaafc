/* RPC runtime — packed-header wire, op enum, uthash translations.
 *
 * I/O is still blocking POSIX read/write here. The async-aware variant
 * that yields the calling coroutine on EAGAIN comes in the next layer
 * (see src/picomesh/picoco/) — this file stays portable. */

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/msgpack/msgpack.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/picoclass/yheaders.h>

#include <uthash.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
static struct skel_lookup_node **skel_lookup_chain(void) {
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

static struct rpc_worker_state *worker_state(void) {
  static _Thread_local struct rpc_worker_state state;
  if (state.next_handle == 0)
    state.next_handle = 1;
  return &state;
}

void rpc_init(void) {
  struct rpc_worker_state *state = worker_state();
  state->object_count = 0;
  state->next_handle = 1;
}

void rpc_add_skel_lookup(skel_lookup_fn fn) {
  if (!fn)
    return;
  struct skel_lookup_node *node = calloc(1, sizeof(*node));
  if (!node)
    return;
  struct skel_lookup_node **head = skel_lookup_chain();
  node->fn = fn;
  node->next = *head;
  *head = node;
}

struct rpc_skel_fn_result rpc_skel_for(method_slot slot) {
  struct rpc_worker_state *state = worker_state();
  if (slot == METHOD_SLOT_UNDEFINED)
    return PICOMESH_OK(rpc_skel_fn, NULL);
  struct skel_cache_entry *entry = NULL;
  HASH_FIND(hh, state->skel_cache, &slot, sizeof(slot), entry);
  if (entry)
    return PICOMESH_OK(rpc_skel_fn, entry->fn);

  /* Resolve slot → registered qname ONCE here — the only Result-returning
   * step — so each per-module hook is a pure name→fn lookup that can't
   * swallow an error. */
  struct const_char_ptr_result name_res = method_slot_name(slot);
  PICOMESH_RETURN_IF_ERR(rpc_skel_fn, name_res, "rpc_skel_for: slot name");
  const char *name = name_res.value;

  rpc_skel_fn fn = NULL;
  for (struct skel_lookup_node *node = *skel_lookup_chain(); node;
       node = node->next) {
    fn = node->fn(name);
    if (fn)
      break;
  }
  if (!fn)
    return PICOMESH_OK(rpc_skel_fn, NULL);

  entry = calloc(1, sizeof(*entry));
  if (!entry)
    return PICOMESH_OK(rpc_skel_fn, fn);
  entry->slot = slot;
  entry->fn = fn;
  HASH_ADD(hh, state->skel_cache, slot, sizeof(slot), entry);
  return PICOMESH_OK(rpc_skel_fn, fn);
}

uint64_t rpc_register_object(void *obj) {
  struct rpc_worker_state *state = worker_state();
  if (state->object_count >= MAX_OBJECTS)
    return 0;
  uint64_t handle = state->next_handle++;
  state->objects[state->object_count].handle = handle;
  state->objects[state->object_count].ptr = obj;
  state->object_count++;
  return handle;
}

void *rpc_handle_resolve(uint64_t handle) {
  if (!handle)
    return NULL;
  struct rpc_worker_state *state = worker_state();
  for (size_t i = 0; i < state->object_count; ++i) {
    if (state->objects[i].handle == handle)
      return state->objects[i].ptr;
  }
  return NULL;
}

/* Per-request caller context — populated by the yhttp /_rpc handler
 * from HTTP headers before invoking a skel. Cooperative-coroutine
 * safe so long as skels don't yield between reading these and
 * starting their work (they read at the top, then run). */
static __thread uint32_t g_caller_uid = 0;
static __thread uint32_t g_caller_sid = 0;

void rpc_set_current_caller(uint32_t uid, uint32_t sid) {
  g_caller_uid = uid;
  g_caller_sid = sid;
}

void rpc_current_caller(uint32_t *out_uid, uint32_t *out_sid) {
  if (out_uid)
    *out_uid = g_caller_uid;
  if (out_sid)
    *out_sid = g_caller_sid;
}

/* Drop a handle from the table and free the underlying object. Used by
 * RPC_OP_DESTROY so server-side proxy objects don't accumulate when
 * the client releases its proxy. Returns 1 if a handle was removed. */
static int rpc_handle_release(uint64_t handle) {
  if (!handle)
    return 0;
  struct rpc_worker_state *state = worker_state();
  for (size_t i = 0; i < state->object_count; ++i) {
    if (state->objects[i].handle == handle) {
      free(state->objects[i].ptr);
      /* Compact: swap with the tail. The order isn't meaningful. */
      state->objects[i] = state->objects[state->object_count - 1];
      state->object_count--;
      return 1;
    }
  }
  return 0;
}

static struct picomesh_size_result
handle_destroy(const void *body, size_t body_len, void *resp, size_t resp_max) {
  if (body_len < sizeof(uint64_t) || resp_max < 1)
    return PICOMESH_ERR(picomesh_size, "handle_destroy: short body / resp");
  uint64_t handle;
  memcpy(&handle, body, sizeof(handle));
  int removed = rpc_handle_release(handle);
  ((uint8_t *)resp)[0] = removed ? 1 : 0;
  return PICOMESH_OK(picomesh_size, 1);
}

/* Forward declarations for the admin op handlers, plus a unified
 * dispatcher used by the HTTP /_rpc endpoint. */
static struct picomesh_size_result handle_resolve_slot(const void *body,
                                                       size_t body_len,
                                                       void *resp,
                                                       size_t resp_max);
static struct picomesh_size_result handle_get_class(const void *body,
                                                    size_t body_len, void *resp,
                                                    size_t resp_max);
static struct picomesh_size_result
handle_create(const void *body, size_t body_len, void *resp, size_t resp_max);

struct picomesh_size_result rpc_handle_admin_op(enum rpc_op op,
                                                const void *body,
                                                size_t body_len, void *resp,
                                                size_t resp_max) {
  switch (op) {
  case RPC_OP_RESOLVE_SLOT:
    return handle_resolve_slot(body, body_len, resp, resp_max);
  case RPC_OP_GET_CLASS:
    return handle_get_class(body, body_len, resp, resp_max);
  case RPC_OP_CREATE:
    return handle_create(body, body_len, resp, resp_max);
  case RPC_OP_DESTROY:
    return handle_destroy(body, body_len, resp, resp_max);
  default:
    return PICOMESH_ERR(picomesh_size, "rpc_handle_admin_op: unknown op");
  }
}

struct fd_io {
  int fd;
};

static size_t fd_read(void *userdata, void *buf, size_t count) {
  struct fd_io *io = userdata;
  char *cursor = buf;
  size_t left = count;
  while (left) {
    ssize_t bytes_read = read(io->fd, cursor, left);
    if (bytes_read > 0) {
      cursor += bytes_read;
      left -= (size_t)bytes_read;
      continue;
    }
    if (bytes_read < 0 && errno == EINTR)
      continue;
    return 0;
  }
  return count;
}

static size_t fd_write(void *userdata, const void *buf, size_t count) {
  struct fd_io *io = userdata;
  const char *cursor = buf;
  size_t left = count;
  while (left) {
    ssize_t bytes_written = write(io->fd, cursor, left);
    if (bytes_written > 0) {
      cursor += bytes_written;
      left -= (size_t)bytes_written;
      continue;
    }
    if (bytes_written < 0 && errno == EINTR)
      continue;
    return 0;
  }
  return count;
}

static int read_full(int fd, void *buf, size_t count) {
  struct fd_io io = {.fd = fd};
  return fd_read(&io, buf, count) == count ? 0 : -1;
}

static int write_full(int fd, const void *buf, size_t count) {
  struct fd_io io = {.fd = fd};
  return fd_write(&io, buf, count) == count ? 0 : -1;
}

static struct picomesh_size_result handle_resolve_slot(const void *body,
                                                       size_t body_len,
                                                       void *resp,
                                                       size_t resp_max) {
  char name[128];
  size_t name_len = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
  memcpy(name, body, name_len);
  name[name_len] = 0;
  /* A name miss is a normal "not found" on this protocol, encoded as the
   * UINT32_MAX sentinel on the wire — not a framework error. */
  struct method_slot_result slot_res = method_slot_by_qname(name);
  uint32_t out;
  if (PICOMESH_IS_ERR(slot_res)) {
    picomesh_error_destroy(slot_res.error);
    out = UINT32_MAX;
  } else {
    out = (uint32_t)slot_res.value;
  }
  if (resp_max < sizeof(out))
    return PICOMESH_ERR(picomesh_size, "handle_resolve_slot: resp too small");
  memcpy(resp, &out, sizeof(out));
  ydebug("resolve_slot('%s') -> %u", name, out);
  return PICOMESH_OK(picomesh_size, sizeof(out));
}

struct get_class_ctx {
  uint8_t *out;
  size_t off;
  size_t cap;
};

static void get_class_emit(const char *name, method_slot slot, void *userdata) {
  struct get_class_ctx *emit_ctx = userdata;
  size_t name_len = strlen(name);
  size_t need = 2 + name_len + 4;
  if (emit_ctx->off + need > emit_ctx->cap)
    return;
  uint16_t name_len16 = (uint16_t)name_len;
  memcpy(emit_ctx->out + emit_ctx->off, &name_len16, 2);
  emit_ctx->off += 2;
  memcpy(emit_ctx->out + emit_ctx->off, name, name_len);
  emit_ctx->off += name_len;
  uint32_t remote_id = (uint32_t)slot;
  memcpy(emit_ctx->out + emit_ctx->off, &remote_id, 4);
  emit_ctx->off += 4;
}

static struct picomesh_size_result handle_get_class(const void *body,
                                                    size_t body_len, void *resp,
                                                    size_t resp_max) {
  char name[128];
  size_t name_len = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
  memcpy(name, body, name_len);
  name[name_len] = 0;
  struct class_ptr_result class_res = class_by_name(name);
  PICOMESH_RETURN_IF_ERR(picomesh_size, class_res,
                         "handle_get_class: class_by_name");
  struct get_class_ctx emit_ctx = {resp, 0, resp_max};
  PICOMESH_RETURN_IF_ERR(
      picomesh_size,
      class_for_each_slot(class_res.value, get_class_emit, &emit_ctx),
      "handle_get_class: class_for_each_slot");
  ydebug("get_class('%s') -> %zu entries (%zu bytes)", name, emit_ctx.off / 6,
         emit_ctx.off);
  return PICOMESH_OK(picomesh_size, emit_ctx.off);
}

static struct picomesh_size_result
handle_create(const void *body, size_t body_len, void *resp, size_t resp_max) {
  char name[128];
  size_t name_len = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
  memcpy(name, body, name_len);
  name[name_len] = 0;
  struct class_ptr_result class_res = class_by_name(name);
  PICOMESH_RETURN_IF_ERR(picomesh_size, class_res,
                         "handle_create: class_by_name");
  struct object_ptr_result object_res = object_alloc(class_res.value);
  PICOMESH_RETURN_IF_ERR(picomesh_size, object_res,
                         "handle_create: object_alloc");
  uint64_t handle = rpc_register_object(object_res.value);
  if (handle == 0) {
    /* Object table full: registration failed. Free the just-allocated object
     * (else it leaks, live but unreachable) and surface the failure rather
     * than handing back the invalid handle 0. */
    object_free(object_res.value);
    return PICOMESH_ERR(picomesh_size,
                        "handle_create: object handle table full");
  }
  if (resp_max < sizeof(handle))
    return PICOMESH_ERR(picomesh_size, "handle_create: resp too small");
  memcpy(resp, &handle, sizeof(handle));
  ydebug("create('%s') -> handle=%llu", name, (unsigned long long)handle);
  return PICOMESH_OK(picomesh_size, sizeof(handle));
}

struct picomesh_size_result rpc_dispatch_one(uint32_t header, const void *body,
                                             size_t body_len, void *resp,
                                             size_t resp_max) {
  enum rpc_op op = RPC_HDR_OP(header);
  uint32_t id = RPC_HDR_ID(header);

  switch (op) {
  case RPC_OP_CALL: {
    struct rpc_skel_fn_result skel = rpc_skel_for((method_slot)id);
    PICOMESH_RETURN_IF_ERR(picomesh_size, skel,
                           "rpc_dispatch_one: skel lookup");
    if (!skel.value)
      return PICOMESH_ERR(picomesh_size, "rpc_dispatch_one: no skel for slot");
    ydebug("CALL slot=%u body_len=%zu", id, body_len);
    return skel.value(body, body_len, resp, resp_max);
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
    return PICOMESH_ERR(picomesh_size, "rpc_dispatch_one: unknown op");
  }
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
  char *http_host;      /* Host header, RPC_MODE_HTTP only */
  uint32_t auth_uid;    /* X-Picomesh-Uid for HTTP requests */
  uint32_t auth_sid;    /* X-Picomesh-Sid for HTTP requests */
  struct remote_id_entry *remote_ids;
  struct translated_class *translated;
  struct cached_proxy *proxy_cache; /* per-class remote receiver objects */
  /* Optional async transport (installed by a loop-aware layer). When
   * set, rpc_call delegates here instead of doing blocking fd I/O. */
  void *async_ctx;
  rpc_async_call_fn async_call;
  rpc_async_oneway_fn async_oneway; /* fire-and-forget (telemetry) */
  void (*async_destroy)(void *ctx);
  /* Async loop-aware I/O for a MSGPACK channel (issue #22). When set,
   * peer_channel_msgpack_call routes the framed request/response through this
   * instead of blocking fd I/O. */
  void *mp_async_ctx;
  rpc_msgpack_io_fn mp_async_io;
  void (*mp_async_destroy)(void *ctx);
};

struct peer_channel *peer_channel_create(int fd) {
  struct peer_channel *channel = calloc(1, sizeof(*channel));
  if (channel) {
    channel->fd = fd;
    channel->mode = RPC_MODE_TCP;
  }
  return channel;
}

struct peer_channel *peer_channel_create_http(int fd, const char *host) {
  struct peer_channel *channel = calloc(1, sizeof(*channel));
  if (!channel)
    return NULL;
  channel->fd = fd;
  channel->mode = RPC_MODE_HTTP;
  channel->http_host = strdup(host ? host : "127.0.0.1");
  return channel;
}

/* ---- MessagePack outbound transport ------------------------------- */

#define MSGPACK_CLIENT_RESP_MAX (1u << 20) /* 1 MiB response cap */

static uint32_t msgpack_rd_be32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static void msgpack_wr_be32(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)(value >> 24);
  bytes[1] = (uint8_t)(value >> 16);
  bytes[2] = (uint8_t)(value >> 8);
  bytes[3] = (uint8_t)value;
}

struct peer_channel *peer_channel_create_msgpack(int fd) {
  struct peer_channel *channel = calloc(1, sizeof(*channel));
  if (channel) {
    channel->fd = fd;
    channel->mode = RPC_MODE_MSGPACK;
  }
  return channel;
}

int peer_channel_is_msgpack(const struct peer_channel *channel) {
  return channel && channel->mode == RPC_MODE_MSGPACK;
}

int peer_channel_msgpack_call(struct peer_channel *channel, const char *path,
                              struct yheaders *headers, const void *args,
                              size_t args_len, void *out, size_t out_cap,
                              size_t *out_len, char *err, size_t err_cap) {
  if (out_len)
    *out_len = 0;
  if (!channel || (channel->fd < 0 && !channel->mp_async_io)) {
    snprintf(err, err_cap, "msgpack call: no channel");
    return 0;
  }
  if (!path)
    path = "";

  /* Request envelope: a 6-key map with the caller's pre-encoded args array
   * spliced in verbatim (msgpack is concatenative). */
  uint32_t uid = headers ? yheaders_get_u32(headers, "uid", 0) : 0;
  uint32_t sid = headers ? yheaders_get_u32(headers, "sid", 0) : 0;
  const char *trace = headers ? yheaders_get(headers, "trace_id") : NULL;

  size_t env_cap = args_len + strlen(path) + (trace ? strlen(trace) : 0) + 128;
  uint8_t *env = malloc(env_cap);
  if (!env) {
    snprintf(err, err_cap, "msgpack call: out of memory");
    return 0;
  }
  cmp_ctx_t writer;
  struct picomesh_msgpack_buffer write_buf;
  picomesh_msgpack_writer_init(&writer, &write_buf, env, env_cap);
  cmp_write_map(&writer, 6);
  cmp_write_str(&writer, "v", 1);
  cmp_write_integer(&writer, 1);
  cmp_write_str(&writer, "op", 2);
  cmp_write_str(&writer, "invoke", 6);
  cmp_write_str(&writer, "path", 4);
  cmp_write_str(&writer, path, (uint32_t)strlen(path));
  cmp_write_str(&writer, "args", 4);
  if (write_buf.offset + args_len > write_buf.cap) {
    free(env);
    snprintf(err, err_cap, "msgpack call: args too large");
    return 0;
  }
  memcpy(write_buf.data + write_buf.offset, args, args_len);
  write_buf.offset += args_len;
  cmp_write_str(&writer, "kwargs", 6);
  cmp_write_map(&writer, 0);
  cmp_write_str(&writer, "headers", 7);
  cmp_write_map(&writer, trace ? 3 : 2);
  cmp_write_str(&writer, "uid", 3);
  cmp_write_uinteger(&writer, uid);
  cmp_write_str(&writer, "sid", 3);
  cmp_write_uinteger(&writer, sid);
  if (trace) {
    cmp_write_str(&writer, "trace_id", 8);
    cmp_write_str(&writer, trace, (uint32_t)strlen(trace));
  }
  size_t env_len = write_buf.offset;

  uint8_t *resp = NULL;
  uint32_t resp_len = 0;
  if (channel->mp_async_io) {
    /* loop-aware path (issue #22): the engine's transport writes the framed
     * env and reads one framed response into a malloc'd buffer, all on the
     * event loop without blocking. */
    size_t got = 0;
    int io_ok = channel->mp_async_io(channel->mp_async_ctx, env, env_len, &resp,
                                     &got, err, err_cap);
    free(env);
    if (!io_ok)
      return 0; /* err already filled by the transport */
    resp_len = (uint32_t)got;
    if (resp_len == 0 || resp_len > MSGPACK_CLIENT_RESP_MAX) {
      free(resp);
      snprintf(err, err_cap, "msgpack call: bad response length");
      return 0;
    }
  } else {
    uint8_t lenbuf[4];
    msgpack_wr_be32(lenbuf, (uint32_t)env_len);
    if (write_full(channel->fd, lenbuf, 4) < 0 ||
        write_full(channel->fd, env, env_len) < 0) {
      free(env);
      snprintf(err, err_cap, "msgpack call: write failed");
      return 0;
    }
    free(env);

    uint8_t rlenbuf[4];
    if (read_full(channel->fd, rlenbuf, 4) < 0) {
      snprintf(err, err_cap, "msgpack call: no response");
      return 0;
    }
    resp_len = msgpack_rd_be32(rlenbuf);
    if (resp_len == 0 || resp_len > MSGPACK_CLIENT_RESP_MAX) {
      snprintf(err, err_cap, "msgpack call: bad response length");
      return 0;
    }
    resp = malloc(resp_len);
    if (!resp) {
      snprintf(err, err_cap, "msgpack call: out of memory");
      return 0;
    }
    if (read_full(channel->fd, resp, resp_len) < 0) {
      free(resp);
      snprintf(err, err_cap, "msgpack call: short response");
      return 0;
    }
  }

  cmp_ctx_t reader;
  struct picomesh_msgpack_buffer read_buf;
  picomesh_msgpack_reader_init(&reader, &read_buf, resp, resp_len);
  uint32_t map_count = 0;
  if (!cmp_read_map(&reader, &map_count)) {
    free(resp);
    snprintf(err, err_cap, "msgpack call: response not a map");
    return 0;
  }
  int ok = 0, have_result = 0;
  size_t result_off = 0, result_len = 0;
  char error_msg[8192] = "remote error";
  for (uint32_t i = 0; i < map_count; ++i) {
    char key[32];
    uint32_t klen = (uint32_t)sizeof(key);
    if (!cmp_read_str(&reader, key, &klen))
      break;
    if (strcmp(key, "ok") == 0) {
      bool bool_value = false;
      if (cmp_read_bool(&reader, &bool_value))
        ok = bool_value ? 1 : 0;
    } else if (strcmp(key, "result") == 0) {
      result_off = read_buf.offset;
      if (!cmp_skip_object_no_limit(&reader))
        break;
      result_len = read_buf.offset - result_off;
      have_result = 1;
    } else if (strcmp(key, "error") == 0) {
      uint32_t error_count = 0;
      if (cmp_read_map(&reader, &error_count)) {
        for (uint32_t j = 0; j < error_count; ++j) {
          char error_key[32];
          uint32_t error_key_len = (uint32_t)sizeof(error_key);
          if (!cmp_read_str(&reader, error_key, &error_key_len))
            break;
          if (strcmp(error_key, "message") == 0) {
            uint32_t msg_len = (uint32_t)sizeof(error_msg);
            if (!cmp_read_str(&reader, error_msg, &msg_len))
              break;
          } else if (!cmp_skip_object_no_limit(&reader)) {
            break;
          }
        }
      }
    } else if (!cmp_skip_object_no_limit(&reader)) {
      break;
    }
  }

  if (!ok) {
    snprintf(err, err_cap, "%s", error_msg[0] ? error_msg : "remote error");
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

void peer_channel_set_auth(struct peer_channel *channel, uint32_t uid,
                           uint32_t sid) {
  if (!channel)
    return;
  channel->auth_uid = uid;
  channel->auth_sid = sid;
}

void peer_channel_set_async(struct peer_channel *channel, void *ctx,
                            rpc_async_call_fn call,
                            void (*destroy)(void *ctx)) {
  if (!channel)
    return;
  channel->async_ctx = ctx;
  channel->async_call = call;
  channel->async_destroy = destroy;
}

void peer_channel_set_async_oneway(struct peer_channel *channel,
                                   rpc_async_oneway_fn oneway) {
  if (!channel)
    return;
  channel->async_oneway = oneway;
}

void peer_channel_set_msgpack_async(struct peer_channel *channel, void *ctx,
                                    rpc_msgpack_io_fn io,
                                    void (*destroy)(void *ctx)) {
  if (!channel)
    return;
  channel->mp_async_ctx = ctx;
  channel->mp_async_io = io;
  channel->mp_async_destroy = destroy;
}

/* Drop every cached remote proxy. Called on reconnect (the handles
 * belong to the now-dead backend process) and at channel teardown. */
void peer_channel_flush_proxy_cache(struct peer_channel *channel) {
  if (!channel)
    return;
  struct cached_proxy *current, *next_tmp;
  HASH_ITER(hh, channel->proxy_cache, current, next_tmp) {
    HASH_DEL(channel->proxy_cache, current);
    free(current->qname);
    free(
        current
            ->obj); /* proxy struct; the backend handle is gone with the conn */
    free(current);
  }
}

/* Free a worker's in-process default-instance cache (the no-peer side of
 * rpc_object_acquire). `head` is &worker->local_instances. The cached
 * objects are real local service instances, owned here for the worker's
 * lifetime; release them on worker teardown. */
void rpc_local_cache_destroy(void **head) {
  if (!head)
    return;
  struct cached_proxy **cache_head = (struct cached_proxy **)head;
  struct cached_proxy *current, *next_tmp;
  HASH_ITER(hh, *cache_head, current, next_tmp) {
    HASH_DEL(*cache_head, current);
    free(current->qname);
    object_free(current->obj); /* the local service instance */
    free(current);
  }
  *cache_head = NULL;
}

void peer_channel_destroy(struct peer_channel *channel) {
  if (!channel)
    return;
  if (channel->async_destroy && channel->async_ctx)
    channel->async_destroy(channel->async_ctx);
  if (channel->mp_async_destroy && channel->mp_async_ctx)
    channel->mp_async_destroy(channel->mp_async_ctx);
  peer_channel_flush_proxy_cache(channel);
  struct translated_class *current, *next_tmp;
  HASH_ITER(hh, channel->translated, current, next_tmp) {
    HASH_DEL(channel->translated, current);
    free(current->name);
    free(current);
  }
  struct remote_id_entry *remote_current, *remote_next_tmp;
  HASH_ITER(hh, channel->remote_ids, remote_current, remote_next_tmp) {
    HASH_DEL(channel->remote_ids, remote_current);
    free(remote_current);
  }
  free(channel->http_host);
  if (channel->fd >= 0)
    close(channel->fd);
  free(channel);
}

uint32_t peer_channel_remote_id(struct peer_channel *channel,
                                method_slot slot) {
  if (!channel)
    return RPC_REMOTE_ID_UNRESOLVED;
  struct remote_id_entry *entry = NULL;
  HASH_FIND(hh, channel->remote_ids, &slot, sizeof(slot), entry);
  return entry ? entry->remote_id : RPC_REMOTE_ID_UNRESOLVED;
}

void peer_channel_set_remote_id(struct peer_channel *channel, method_slot slot,
                                uint32_t remote_id) {
  if (!channel)
    return;
  struct remote_id_entry *entry = NULL;
  HASH_FIND(hh, channel->remote_ids, &slot, sizeof(slot), entry);
  if (!entry) {
    entry = calloc(1, sizeof(*entry));
    if (!entry)
      return;
    entry->local_slot = slot;
    HASH_ADD(hh, channel->remote_ids, local_slot, sizeof(method_slot), entry);
  }
  entry->remote_id = remote_id;
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
static size_t rpc_call_http(struct peer_channel *channel, enum rpc_op op,
                            uint32_t id, const void *body, size_t body_len,
                            void *resp, size_t resp_max) {
  char request_hdr[512];
  int hdr_len = snprintf(request_hdr, sizeof(request_hdr),
                         "POST /_rpc?op=%u&id=%u HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Content-Type: application/octet-stream\r\n"
                         "X-Picomesh-Uid: %u\r\n"
                         "X-Picomesh-Sid: %u\r\n"
                         "Connection: keep-alive\r\n"
                         "\r\n",
                         (unsigned)op, id,
                         channel->http_host ? channel->http_host : "127.0.0.1",
                         body_len, channel->auth_uid, channel->auth_sid);
  if (hdr_len <= 0 || (size_t)hdr_len >= sizeof(request_hdr))
    return 0;
  if (write_full(channel->fd, request_hdr, (size_t)hdr_len) < 0)
    return 0;
  if (body_len && write_full(channel->fd, body, body_len) < 0)
    return 0;

  /* Parse the response. We only need the status line + Content-Length;
   * skip everything else. Read byte-by-byte until \r\n\r\n. */
  char line[1024];
  size_t line_pos = 0;
  int got_blank = 0;
  int saw_cr = 0;
  int blank_state = 0; /* 0=line, 1=after \r, 2=after \r\n, 3=after \r\n\r */
  size_t content_length = 0;
  int status = 0;
  int parsed_status = 0;
  for (;;) {
    char ch;
    if (read_full(channel->fd, &ch, 1) < 0)
      return 0;
    if (ch != '\n') {
      if (line_pos + 1 < sizeof(line))
        line[line_pos++] = ch;
    }
    if (saw_cr && ch == '\n') {
      line[line_pos - 1] = 0; /* drop the \r */
      if (line_pos == 1) {
        got_blank = 1;
        break;
      }
      if (!parsed_status) {
        /* "HTTP/1.1 200 OK" — pull the int */
        const char *space = strchr(line, ' ');
        if (space)
          status = atoi(space + 1);
        parsed_status = 1;
      } else {
        /* Looking for Content-Length: */
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
          content_length = (size_t)strtoull(line + 15, NULL, 10);
        }
      }
      line_pos = 0;
      saw_cr = 0;
      blank_state = 2;
      continue;
    }
    saw_cr = (ch == '\r');
    (void)blank_state;
  }
  if (!got_blank)
    return 0;
  if (status < 200 || status >= 300) {
    /* Drain the body so the connection stays usable. */
    size_t remain = content_length;
    uint8_t drain[256];
    while (remain) {
      size_t chunk = remain > sizeof(drain) ? sizeof(drain) : remain;
      if (read_full(channel->fd, drain, chunk) < 0)
        return 0;
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
      if (read_full(channel->fd, drain, chunk) < 0)
        return 0;
      remain -= chunk;
    }
    return 0;
  }
  if (content_length && read_full(channel->fd, resp, content_length) < 0)
    return 0;
  return content_length;
}

void rpc_call_oneway(struct peer_channel *channel, enum rpc_op op, uint32_t id,
                     const void *body, size_t body_len) {
  if (!channel)
    return;
  /* Async (loop) transport: hand off the frame, never wait for a reply. */
  if (channel->async_oneway) {
    channel->async_oneway(channel->async_ctx, op, id, body, body_len);
    return;
  }
  /* Raw-fd fallback: write the frame with the reserved req_id 0 and do
   * NOT read the response (the peer still frames one; it stays buffered
   * until the next read or the socket closes). HTTP mode has no oneway. */
  if (channel->mode != RPC_MODE_TCP || channel->fd < 0)
    return;
  uint32_t header = RPC_HDR_MAKE(op, id);
  uint32_t req_id = 0;
  uint32_t body_len32 = (uint32_t)body_len;
  if (write_full(channel->fd, &header, 4) < 0)
    return;
  if (write_full(channel->fd, &req_id, 4) < 0)
    return;
  if (write_full(channel->fd, &body_len32, 4) < 0)
    return;
  if (body_len)
    write_full(channel->fd, body, body_len);
}

size_t rpc_call(struct peer_channel *channel, enum rpc_op op, uint32_t id,
                const void *body, size_t body_len, void *resp,
                size_t resp_max) {
  if (!channel)
    return 0;
  ydebug("mode=%d op=%u id=%u body_len=%zu", channel->mode, op, id, body_len);

  /* Async transport (loop-aware, non-blocking) takes precedence when
   * installed — it carries the call over a coroutine-yielding
   * connection instead of blocking the loop on the bare fd. */
  if (channel->async_call)
    return channel->async_call(channel->async_ctx, op, id, body, body_len, resp,
                               resp_max);

  if (channel->mode == RPC_MODE_HTTP) {
    return rpc_call_http(channel, op, id, body, body_len, resp, resp_max);
  }

  uint32_t header = RPC_HDR_MAKE(op, id);
  uint32_t req_id = ++channel->next_req_id;
  uint32_t body_len32 = (uint32_t)body_len;
  if (write_full(channel->fd, &header, 4) < 0)
    return 0;
  if (write_full(channel->fd, &req_id, 4) < 0)
    return 0;
  if (write_full(channel->fd, &body_len32, 4) < 0)
    return 0;
  if (body_len && write_full(channel->fd, body, body_len) < 0)
    return 0;

  /* Strictly sequential on this transport: read the echoed req_id and
   * confirm it matches before consuming the payload. */
  uint32_t resp_req_id = 0;
  if (read_full(channel->fd, &resp_req_id, 4) < 0)
    return 0;
  if (resp_req_id != req_id) {
    ywarn("rpc_call: req_id mismatch (sent %u got %u)", req_id, resp_req_id);
    return 0;
  }
  uint32_t resp_len = 0;
  if (read_full(channel->fd, &resp_len, 4) < 0)
    return 0;
  if (resp_len > resp_max) {
    uint8_t drain[256];
    size_t remain = resp_len;
    while (remain) {
      size_t chunk = remain > sizeof(drain) ? sizeof(drain) : remain;
      if (read_full(channel->fd, drain, chunk) < 0)
        return 0;
      remain -= chunk;
    }
    return 0;
  }
  if (resp_len && read_full(channel->fd, resp, resp_len) < 0)
    return 0;
  return resp_len;
}

struct picomesh_uint32_result
peer_channel_ensure_remote_id(struct peer_channel *channel,
                              method_slot local_slot) {
  if (!channel)
    return PICOMESH_OK(picomesh_uint32, RPC_REMOTE_ID_UNRESOLVED);
  uint32_t cached = peer_channel_remote_id(channel, local_slot);
  if (cached != RPC_REMOTE_ID_UNRESOLVED)
    return PICOMESH_OK(picomesh_uint32, cached);

  /* A slot the caller holds must have a name; a lookup failure is a real
   * error to propagate, not a silent "unresolved". */
  struct const_char_ptr_result name_res = method_slot_name(local_slot);
  PICOMESH_RETURN_IF_ERR(
      picomesh_uint32, name_res,
      "peer_channel_ensure_remote_id: method slot name lookup failed");
  const char *name = name_res.value;

  uint32_t remote = RPC_REMOTE_ID_UNRESOLVED;
  size_t resolved_len = rpc_call(channel, RPC_OP_RESOLVE_SLOT, 0, name,
                                 strlen(name), &remote, sizeof(remote));
  /* The remote could not resolve the slot — a legitimate (non-error) sentinel
   * the caller checks; it is NOT a framework failure. */
  if (resolved_len != sizeof(remote) || remote == RPC_REMOTE_ID_UNRESOLVED)
    return PICOMESH_OK(picomesh_uint32, RPC_REMOTE_ID_UNRESOLVED);

  peer_channel_set_remote_id(channel, local_slot, remote);
  ydebug("lazy resolve '%s' local=%u remote=%u", name, local_slot, remote);
  return PICOMESH_OK(picomesh_uint32, remote);
}

struct picomesh_int_result
peer_channel_translate_class(struct peer_channel *channel,
                             const char *class_name) {
  if (!channel || !class_name)
    return PICOMESH_OK(picomesh_int, -1);
  struct translated_class *translated = NULL;
  HASH_FIND_STR(channel->translated, class_name, translated);
  if (translated)
    return PICOMESH_OK(picomesh_int, 0);

  uint8_t buf[BUF_MAX];
  size_t name_len = strlen(class_name);
  size_t resp_len = rpc_call(channel, RPC_OP_GET_CLASS, 0, class_name, name_len,
                             buf, sizeof(buf));
  if (resp_len == 0)
    return PICOMESH_OK(picomesh_int, -1);

  size_t offset = 0;
  while (offset + 2 + 4 <= resp_len) {
    uint16_t name_len16;
    memcpy(&name_len16, buf + offset, 2);
    offset += 2;
    if (offset + name_len16 + 4 > resp_len)
      break;
    char slot_name[128];
    size_t copy =
        name_len16 < sizeof(slot_name) - 1 ? name_len16 : sizeof(slot_name) - 1;
    memcpy(slot_name, buf + offset, copy);
    slot_name[copy] = 0;
    offset += name_len16;
    uint32_t remote_id;
    memcpy(&remote_id, buf + offset, 4);
    offset += 4;

    struct method_slot_result slot_res = method_slot_by_qname(slot_name);
    if (PICOMESH_IS_OK(slot_res)) {
      peer_channel_set_remote_id(channel, slot_res.value, remote_id);
      ydebug("xlat['%s'] local=%u remote=%u", slot_name, slot_res.value,
             remote_id);
    } else {
      /* A remote class's slot that has no local counterpart is the normal
       * case during cross-process translation — skip it, don't treat the
       * "no such local slot" lookup miss as a failure. */
      picomesh_error_destroy(slot_res.error);
    }
  }

  translated = calloc(1, sizeof(*translated));
  if (!translated)
    return PICOMESH_OK(picomesh_int, 0);
  translated->name = strdup(class_name);
  if (!translated->name) {
    free(translated);
    return PICOMESH_OK(picomesh_int, 0);
  }
  HASH_ADD_KEYPTR(hh, channel->translated, translated->name,
                  strlen(translated->name), translated);
  return PICOMESH_OK(picomesh_int, 0);
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
struct object_ptr_result rpc_object_acquire(struct ctx *ctx,
                                            const struct class *klass,
                                            const char *class_qname) {
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
      struct cached_proxy *local_entry = NULL;
      HASH_FIND_STR(*head, class_qname, local_entry);
      if (local_entry)
        return PICOMESH_OK(object_ptr, local_entry->obj);
      struct object_ptr_result local_obj_res = object_alloc(klass);
      if (PICOMESH_IS_ERR(local_obj_res))
        return local_obj_res;
      local_entry = calloc(1, sizeof(*local_entry));
      char *qname_copy = local_entry ? strdup(class_qname) : NULL;
      if (local_entry && qname_copy) {
        local_entry->qname = qname_copy;
        local_entry->obj = local_obj_res.value;
        HASH_ADD_KEYPTR(hh, *head, local_entry->qname,
                        strlen(local_entry->qname), local_entry);
      } else {
        /* couldn't cache (calloc/strdup OOM) → caller still gets a usable
         * instance; never strlen a NULL key. */
        free(qname_copy);
        free(local_entry);
      }
      return local_obj_res;
    }
    return object_alloc(klass);
  }

  struct peer_channel *channel = ctx->peer;
  struct cached_proxy *entry = NULL;
  if (class_qname)
    HASH_FIND_STR(channel->proxy_cache, class_qname, entry);
  if (entry)
    return PICOMESH_OK(object_ptr, entry->obj);
  ydebug("rpc_object_acquire: caching new '%s' proxy on peer=%p",
         class_qname ? class_qname : "?", (void *)channel);

  uint64_t handle = 0;
  if (peer_channel_is_msgpack(channel)) {
    /* The MessagePack wire is STATELESS path-invoke (issue #22): there is no
     * remote object handle and no RPC_OP_CREATE. Hand back a handle-less
     * proxy bound to the class; the generated stub routes each call by its
     * dotted path through peer_channel_msgpack_call. */
    handle = 0;
  } else {
    if (class_qname)
      peer_channel_translate_class(channel, class_qname);
    if (rpc_call(channel, RPC_OP_CREATE, 0, class_qname,
                 class_qname ? strlen(class_qname) : 0, &handle,
                 sizeof(handle)) != sizeof(handle) ||
        !handle)
      return PICOMESH_ERR(object_ptr,
                          "rpc_object_acquire: remote create failed");
  }

  void *proxy_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
  if (!proxy_mem)
    return PICOMESH_ERR(object_ptr, "rpc_object_acquire: calloc(proxy) failed");
  struct object *obj = proxy_mem;
  *(const struct class **)obj = klass;
  *(uint64_t *)((char *)obj + sizeof(*obj)) = handle;

  entry = calloc(1, sizeof(*entry));
  char *qname_copy = (entry && class_qname) ? strdup(class_qname) : NULL;
  if (entry && qname_copy) {
    entry->qname = qname_copy;
    entry->obj = obj;
    HASH_ADD_KEYPTR(hh, channel->proxy_cache, entry->qname,
                    strlen(entry->qname), entry);
  } else {
    /* couldn't cache (calloc/strdup OOM, or no qname) — caller still gets a
     * usable proxy; never strlen a NULL key. */
    free(qname_copy);
    free(entry);
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
void rpc_object_release(struct ctx *ctx, struct object *obj) {
  if (!obj)
    return;
  if (ctx && (ctx->peer || ctx->local_cache))
    return; /* cached: owner frees */
  object_free(obj);
}

/* Name-keyed twin of the codegen `<class>_create` — used by callers
 * (the gateway) that link no typed `*_create` for the target class. */
struct object_ptr_result object_create_in_ctx(struct ctx *ctx,
                                              const char *class_qname) {
  if (!class_qname)
    return PICOMESH_ERR(object_ptr, "object_create_in_ctx: NULL class name");
  struct class_ptr_result class_res = class_by_name(class_qname);
  if (PICOMESH_IS_ERR(class_res))
    return PICOMESH_ERR(object_ptr, "object_create_in_ctx: unknown class",
                        class_res);
  if (!class_res.value)
    return PICOMESH_ERR(object_ptr,
                        "object_create_in_ctx: class not registered");
  return rpc_object_acquire(ctx, class_res.value, class_qname);
}

void object_release_in_ctx(struct ctx *ctx, struct object *obj) {
  rpc_object_release(ctx, obj);
}
