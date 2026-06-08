/* socket/bind/inet_pton on glibc want a feature macro defined before any
 * include pulls in <features.h>; the codegen runs clang without it, so pin
 * the minimum here at file-top. */
#define _POSIX_C_SOURCE 200809L

/* portalloc — port allocator for the mesh.
 *
 * Hands out a free TCP port from a configured range to a named service.
 * Unlike a pure counter, every candidate is BIND-PROBED first: portalloc
 * tries to bind+close a socket on (host, port), so it never leases a port
 * already held by another process — which is exactly what lets several mesh
 * instances coexist on one host. The lease table additionally prevents
 * handing the same port to two services within this instance.
 *
 * A race still exists between "portalloc says port P is free" and "the
 * requesting node actually binds P": some other process can grab P in that
 * window. portalloc cannot close that window alone, so the protocol is:
 * the consumer tries to bind the returned port and, on failure, RELEASEs it
 * and asks again — portalloc's next probe sees P taken and moves on.
 *
 * State is PROCESS-WIDE, not per-object: the RPC server allocates a fresh
 * receiver object for every client's CREATE, so a per-object lease table
 * would be invisible to the next client (two services would both get the
 * first free port). The lease table therefore lives in one mutex-guarded
 * singleton — the same shape the trace collector uses for its span store.
 *
 * Methods:
 *   allocate(service_name, host) → uint32 port (err when the range is full)
 *   release(port)                → 1 ok, 0 unknown
 *   count_used()                 → number of live leases
 *   list(offset, limit)          → JSON [{service, port}, …]
 *   list_all()                   → JSON [{service, port}, …] (unbounded)
 *
 * Idempotent: allocate twice for the same service returns the same port,
 * provided it is still bindable; a lease whose port was stolen is dropped
 * and reassigned. State is in-memory (persistence is a follow-up). */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yjson/yjson.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORTALLOC_DEFAULT_LO 8300
#define PORTALLOC_DEFAULT_HI 8999
#define PORTALLOC_MAX_ENTRIES 1024
#define PORTALLOC_NAME_MAX 64

struct port_entry {
    char service_name[PORTALLOC_NAME_MAX];
    uint32_t port;
    int used;
};

/* The class object carries no state — all of it is process-wide below. */
struct PICOMESH_CLASS_ANNOTATE("class@portalloc:portalloc") portalloc_portalloc_data {
    int placeholder;
};

struct portalloc_state {
    pthread_mutex_t mu;
    struct port_entry entries[PORTALLOC_MAX_ENTRIES];
    size_t count;
};

/* The one process-wide lease table, shared by every receiver object. */
static struct portalloc_state *portalloc_state(void)
{
    static struct portalloc_state state = {.mu = PTHREAD_MUTEX_INITIALIZER};
    return &state;
}

/* Parse the inclusive "LO-HI" range from `portalloc.port_range`; fall back to
 * the built-in default when unset or malformed. */
static void portalloc_range(uint32_t *lo, uint32_t *hi)
{
    *lo = PORTALLOC_DEFAULT_LO;
    *hi = PORTALLOC_DEFAULT_HI;
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return;
    struct yconfig_node_ptr_result config_res =
        yconfig_get(picomesh_engine_config(engine), "portalloc.port_range");
    if (PICOMESH_IS_OK(config_res) && config_res.value) {
        const char *range_str = yconfig_node_as_string(config_res.value, NULL);
        unsigned range_lo = 0, range_hi = 0;
        if (range_str && sscanf(range_str, "%u-%u", &range_lo, &range_hi) == 2 && range_lo > 0 && range_hi >= range_lo) {
            *lo = range_lo;
            *hi = range_hi;
        }
    }
}

/* True iff (host, port) can be bound right now. A PLAIN bind — deliberately no
 * SO_REUSEADDR/SO_REUSEPORT: the real listeners bind with SO_REUSEPORT so a
 * service's N workers can share one port, but a probe that set SO_REUSEPORT (or
 * even SO_REUSEADDR) would be allowed to JOIN another instance's reuseport
 * group and wrongly report the port "free". A bare socket can't join, so its
 * bind fails on any port a foreign listener holds — which is exactly what keeps
 * a second mesh instance off the first's ports. The socket is closed at once. */
static int portalloc_port_free(const char *host, uint32_t port)
{
    if (!host || !*host) host = "127.0.0.1";
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) return 0;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(sock_fd);
        return 0;
    }
    int bind_rc = bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
    close(sock_fd);
    return bind_rc == 0;
}

PICOMESH_CLASS_ANNOTATE("override@portalloc:portalloc:portalloc_allocate")
struct picomesh_uint32_result portalloc_portalloc_allocate_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         const char *service_name, const char *host)
{
    (void)ctx; (void)obj; (void)hdrs;
    if (!service_name || !*service_name)
        return PICOMESH_ERR(picomesh_uint32, "portalloc_allocate: empty service name");
    if (!host || !*host) host = "127.0.0.1";
    struct portalloc_state *state = portalloc_state();
    uint32_t lo, hi;
    portalloc_range(&lo, &hi);

    pthread_mutex_lock(&state->mu);

    /* Idempotent: a service keeps the port it already holds — but only while
     * that port is still bindable. A lease stolen by a foreign process is
     * dropped here and reassigned below. */
    for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES; ++i) {
        if (state->entries[i].used && strcmp(state->entries[i].service_name, service_name) == 0) {
            if (portalloc_port_free(host, state->entries[i].port)) {
                uint32_t held = state->entries[i].port;
                pthread_mutex_unlock(&state->mu);
                return PICOMESH_OK(picomesh_uint32, held);
            }
            state->entries[i].used = 0;
            state->count--;
            break;
        }
    }

    /* First-fit: skip ports leased here OR currently held on the host. */
    for (uint32_t candidate_port = lo; candidate_port <= hi; ++candidate_port) {
        int taken = 0;
        for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES; ++i) {
            if (state->entries[i].used && state->entries[i].port == candidate_port) { taken = 1; break; }
        }
        if (taken) continue;
        if (!portalloc_port_free(host, candidate_port)) continue;
        for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES; ++i) {
            if (!state->entries[i].used) {
                snprintf(state->entries[i].service_name, sizeof(state->entries[i].service_name), "%s", service_name);
                state->entries[i].port = candidate_port;
                state->entries[i].used = 1;
                state->count++;
                pthread_mutex_unlock(&state->mu);
                yinfo("portalloc: '%s' → port %u (host %s)", service_name, candidate_port, host);
                return PICOMESH_OK(picomesh_uint32, candidate_port);
            }
        }
        pthread_mutex_unlock(&state->mu);
        return PICOMESH_ERR(picomesh_uint32, "portalloc_allocate: lease table full");
    }
    pthread_mutex_unlock(&state->mu);
    return PICOMESH_ERR(picomesh_uint32, "portalloc_allocate: no free port in range");
}

PICOMESH_CLASS_ANNOTATE("override@portalloc:portalloc:portalloc_release")
struct picomesh_int_result portalloc_portalloc_release_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                     uint32_t port)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct portalloc_state *state = portalloc_state();
    pthread_mutex_lock(&state->mu);
    for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES; ++i) {
        if (state->entries[i].used && state->entries[i].port == port) {
            state->entries[i].used = 0;
            state->count--;
            pthread_mutex_unlock(&state->mu);
            yinfo("portalloc: released port %u", port);
            return PICOMESH_OK(picomesh_int, 1);
        }
    }
    pthread_mutex_unlock(&state->mu);
    return PICOMESH_OK(picomesh_int, 0);
}

PICOMESH_CLASS_ANNOTATE("override@portalloc:portalloc:portalloc_count_used")
struct picomesh_size_result portalloc_portalloc_count_used_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct portalloc_state *state = portalloc_state();
    pthread_mutex_lock(&state->mu);
    size_t count = state->count;
    pthread_mutex_unlock(&state->mu);
    return PICOMESH_OK(picomesh_size, count);
}

/* Build the allocation list as JSON `[{service,port}, …]`, honoring
 * offset/limit (limit < 0 == all). Caller holds the lock. */
static struct picomesh_json_result portalloc_list_window(struct portalloc_state *state,
                                                         int64_t offset, int64_t limit)
{
    struct yjson_writer *writer = yjson_writer_new();
    if (!writer) return PICOMESH_ERR(picomesh_json, "portalloc_list: writer alloc failed");
    yjson_writer_begin_array(writer);
    int64_t skip = offset > 0 ? offset : 0, emitted = 0;
    for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES && (limit < 0 || emitted < limit); ++i) {
        if (!state->entries[i].used) continue;
        if (skip > 0) { --skip; continue; }
        yjson_writer_begin_object(writer);
        yjson_writer_key(writer, "service"); yjson_writer_string(writer, state->entries[i].service_name);
        yjson_writer_key(writer, "port");    yjson_writer_int(writer, (int64_t)state->entries[i].port);
        yjson_writer_end_object(writer);
        ++emitted;
    }
    yjson_writer_end_array(writer);
    size_t len = 0;
    const char *data = yjson_writer_data(writer, &len);
    char *out = strdup(data ? data : "[]");
    yjson_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_json, "portalloc_list: strdup failed");
    return PICOMESH_OK(picomesh_json, out);
}

/* List port allocations as a JSON array `[{service,port}]`, paginated. */
PICOMESH_CLASS_ANNOTATE("override@portalloc:portalloc:portalloc_list")
struct picomesh_json_result portalloc_portalloc_list_impl(struct ctx *ctx, struct object *obj,
                                                      struct yheaders *hdrs,
                                                      int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj; (void)hdrs;
    if (limit <= 0) limit = 100;
    struct portalloc_state *state = portalloc_state();
    pthread_mutex_lock(&state->mu);
    struct picomesh_json_result list_res = portalloc_list_window(state, offset, limit);
    pthread_mutex_unlock(&state->mu);
    return list_res;
}

/* Unbounded variant — every allocation. */
PICOMESH_CLASS_ANNOTATE("override@portalloc:portalloc:portalloc_list_all")
struct picomesh_json_result portalloc_portalloc_list_all_impl(struct ctx *ctx, struct object *obj,
                                                              struct yheaders *hdrs)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct portalloc_state *state = portalloc_state();
    pthread_mutex_lock(&state->mu);
    struct picomesh_json_result list_res = portalloc_list_window(state, 0, -1);
    pthread_mutex_unlock(&state->mu);
    return list_res;
}

#include "portalloc.gen.c"
