/* socket/bind/inet_pton/nanosleep on glibc want a feature macro before any
 * include pulls in <features.h>. */
#define _POSIX_C_SOURCE 200809L

#include "autoport.h"

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yargv/yargv.h>
#include <picomesh/plugin/registry/registry.h>
#include <picomesh/plugin/portalloc/portalloc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/* ------------------------------------------------------------------ helpers */

static void nap_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* Blocking-connect a TCP socket to host:port. A service that bound 0.0.0.0
 * is reached on loopback. Returns the fd or -1. */
static int autoport_connect(const char *host, int port)
{
    if (!host || !*host) host = "127.0.0.1";
    const char *dial = (strcmp(host, "0.0.0.0") == 0) ? "127.0.0.1" : host;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, dial, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* True iff (host, port) can be bound right now. A PLAIN bind — no
 * SO_REUSEADDR/SO_REUSEPORT — so it cannot join a foreign SO_REUSEPORT
 * listener's group and fails on any port already held (see the matching note
 * in portalloc's portalloc_port_free). This is the consumer's own check before
 * it commits to the port portalloc handed it. */
static int autoport_probe_bind(const char *host, int port)
{
    if (!host || !*host) host = "127.0.0.1";
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return 0;
    }
    int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return rc == 0;
}

/* Read the global `registry: {host, port}` block. Looked up as a map (not a
 * dotted path) so the config inheritance fallback can't peel `registry.host`
 * down to the node's own top-level `host:` key. Returns 1 when present. */
static int autoport_registry_addr(struct picomesh_engine *engine, char *host_out, size_t cap,
                                  int *port_out)
{
    struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(engine), "registry");
    if (PICOMESH_IS_ERR(r) || !r.value || yconfig_node_kind(r.value) != YCONFIG_MAP) return 0;
    const struct yconfig_node *port_node = yconfig_node_get(r.value, "port");
    int64_t port = port_node ? yconfig_node_as_int(port_node, -1) : -1;
    if (port <= 0) return 0;
    const struct yconfig_node *host_node = yconfig_node_get(r.value, "host");
    const char *host = host_node ? yconfig_node_as_string(host_node, NULL) : NULL;
    snprintf(host_out, cap, "%s", (host && *host) ? host : "127.0.0.1");
    *port_out = (int)port;
    return 1;
}

/* True iff `plugin` is in THIS node's activated plugins list. Read from
 * config — NOT by probing a local object create, because `<class>_create`
 * lazily registers the class even for a remote proxy, which would make every
 * node look like it hosts portalloc/registry after the first call. */
static int autoport_plugin_active(struct picomesh_engine *engine, const char *name,
                                  const char *plugin)
{
    const struct yconfig_node *plugins = NULL;
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.plugins", name);
        struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(engine), path);
        if (PICOMESH_IS_OK(r) && r.value && yconfig_node_kind(r.value) == YCONFIG_LIST) plugins = r.value;
    }
    if (!plugins) {
        struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(engine), "plugins");
        if (PICOMESH_IS_OK(r) && r.value && yconfig_node_kind(r.value) == YCONFIG_LIST) plugins = r.value;
    }
    if (!plugins) return 0;
    size_t n = yconfig_node_size(plugins);
    for (size_t i = 0; i < n; ++i) {
        const struct yconfig_node *item = yconfig_node_at(plugins, i);
        const char *s = item ? yconfig_node_as_string(item, NULL) : NULL;
        if (s && strcmp(s, plugin) == 0) return 1;
    }
    return 0;
}

/* A short-lived synchronous RPC session to the registry. */
struct reg_session {
    struct peer_channel *channel;
    struct object *obj;
    struct ctx ctx;
};

static int reg_session_open(struct picomesh_engine *engine, struct reg_session *session)
{
    char host[64];
    int port = 0;
    if (!autoport_registry_addr(engine, host, sizeof(host), &port)) return 0;
    int fd = -1;
    for (int attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        fd = autoport_connect(host, port);
        if (fd < 0) nap_ms(100); /* registry may still be binding */
    }
    if (fd < 0) {
        ywarn("autoport: registry %s:%d unreachable", host, port);
        return 0;
    }
    session->channel = peer_channel_create(fd);
    if (!session->channel) {
        close(fd);
        return 0;
    }
    session->ctx = (struct ctx){.peer = session->channel};
    struct object_ptr_result obj_r = registry_registry_create(&session->ctx);
    if (PICOMESH_IS_ERR(obj_r)) {
        picomesh_error_destroy(obj_r.error);
        peer_channel_destroy(session->channel);
        session->channel = NULL;
        return 0;
    }
    session->obj = obj_r.value;
    return 1;
}

static void reg_session_close(struct reg_session *session)
{
    if (!session || !session->channel) return;
    object_release_in_ctx(&session->ctx, session->obj);
    peer_channel_destroy(session->channel);
    session->channel = NULL;
    session->obj = NULL;
}

/* Resolve `service` to host/port via the registry. When `wait`, retry until
 * the producer has registered (bounded). Returns 1 on success. */
static int reg_resolve(struct reg_session *session, const char *service, int wait,
                       char *host_out, size_t cap, int *port_out)
{
    int tries = wait ? 150 : 1; /* ~15s of 100ms polls */
    for (int attempt = 0; attempt < tries; ++attempt) {
        struct picomesh_string_result r =
            registry_registry_resolve(&session->ctx, session->obj, NULL, service);
        if (PICOMESH_IS_ERR(r)) {
            picomesh_error_destroy(r.error);
        } else if (r.value && *r.value) {
            const char *colon = strrchr(r.value, ':');
            int ok = 0;
            if (colon && colon != r.value) {
                size_t hlen = (size_t)(colon - r.value);
                if (hlen < cap) {
                    memcpy(host_out, r.value, hlen);
                    host_out[hlen] = 0;
                    *port_out = atoi(colon + 1);
                    ok = (*port_out > 0);
                }
            }
            free(r.value);
            if (ok) return 1;
        } else {
            free(r.value); /* empty == not registered yet */
        }
        if (wait) nap_ms(100);
    }
    return 0;
}

/* ------------------------------------------------------------------ public */

int picomesh_serve_port_is_auto(struct picomesh_engine *engine, const char *name)
{
    if (!engine) return 0;
    /* An explicit numeric --port always wins and is never "auto". */
    int64_t cli = yargv_get_int(picomesh_engine_cli(engine), "port", -1);
    if (cli > 0) return 0;

    const struct yconfig_node *node = NULL;
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.port", name);
        struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(engine), path);
        if (PICOMESH_IS_OK(r) && r.value) node = r.value;
    }
    if (!node) {
        struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(engine), "port");
        if (PICOMESH_IS_OK(r) && r.value) node = r.value;
    }
    if (!node || yconfig_node_kind(node) != YCONFIG_STRING) return 0;
    const char *s = yconfig_node_as_string(node, NULL);
    return s && strcmp(s, "auto") == 0;
}

int picomesh_autoport_allocate(struct picomesh_engine *engine, const char *name, const char *host)
{
    if (!engine || !name || !*name) return -1;
    if (!host || !*host) host = "127.0.0.1";

    struct ctx pctx = {0};
    struct object *pobj = NULL;
    struct peer_channel *pa_channel = NULL;

    /* Bootstrap case: THIS node provides portalloc. It cannot discover itself
     * through a registry it hasn't registered with yet, so allocate from its
     * local instance — every receiver shares the one process-wide lease table,
     * so the port portalloc takes for itself is never handed to a caller. */
    if (autoport_plugin_active(engine, name, "portalloc")) {
        pctx = picomesh_engine_service_ctx(engine, "portalloc");
        struct object_ptr_result obj_r = portalloc_portalloc_create(&pctx);
        if (PICOMESH_IS_ERR(obj_r)) {
            picomesh_error_destroy(obj_r.error);
            ywarn("autoport: portalloc node failed to create its local allocator");
            return -1;
        }
        pobj = obj_r.value;
    }

    /* Normal case: discover the remote portalloc through the registry. */
    if (!pobj) {
        struct reg_session session = {0};
        if (!reg_session_open(engine, &session)) {
            ywarn("autoport: '%s' wants port:auto but the registry is unreachable", name);
            return -1;
        }
        char ph[64];
        int pp = 0;
        int ok = reg_resolve(&session, "portalloc", 1, ph, sizeof(ph), &pp);
        reg_session_close(&session);
        if (!ok) {
            ywarn("autoport: portalloc not discoverable through registry for '%s'", name);
            return -1;
        }
        int fd = -1;
        for (int attempt = 0; attempt < 100 && fd < 0; ++attempt) {
            fd = autoport_connect(ph, pp);
            if (fd < 0) nap_ms(100);
        }
        if (fd < 0) {
            ywarn("autoport: portalloc %s:%d unreachable for '%s'", ph, pp, name);
            return -1;
        }
        pa_channel = peer_channel_create(fd);
        if (!pa_channel) {
            close(fd);
            return -1;
        }
        pctx = (struct ctx){.peer = pa_channel};
        struct object_ptr_result obj_r = portalloc_portalloc_create(&pctx);
        if (PICOMESH_IS_ERR(obj_r)) {
            picomesh_error_destroy(obj_r.error);
            peer_channel_destroy(pa_channel);
            return -1;
        }
        pobj = obj_r.value;
    }

    /* Allocate + bind-probe + retry: portalloc may hand out a port that a
     * foreign process grabs before we bind it. On a probe miss, release it
     * (portalloc's next probe skips it) and ask again. */
    int port = -1;
    for (int attempt = 0; attempt < 64; ++attempt) {
        struct picomesh_uint32_result r =
            portalloc_portalloc_allocate(&pctx, pobj, NULL, name, host);
        if (PICOMESH_IS_ERR(r)) {
            ywarn("autoport: portalloc.allocate('%s') failed: %s", name,
                  r.error.msg ? r.error.msg : "?");
            picomesh_error_destroy(r.error);
            break;
        }
        uint32_t candidate = r.value;
        if (autoport_probe_bind(host, (int)candidate)) {
            port = (int)candidate;
            break;
        }
        struct picomesh_int_result rel =
            portalloc_portalloc_release(&pctx, pobj, NULL, candidate);
        if (PICOMESH_IS_ERR(rel)) picomesh_error_destroy(rel.error);
        nap_ms(20);
    }

    object_release_in_ctx(&pctx, pobj);
    if (pa_channel) peer_channel_destroy(pa_channel);
    if (port > 0) yinfo("autoport: '%s' allocated port %d", name, port);
    return port;
}

void picomesh_autoport_register_self(struct picomesh_engine *engine, const char *name,
                                     const char *host, int port)
{
    if (!engine || !name || !*name || port <= 0) return;
    char rhost[64];
    int rport = 0;
    if (!autoport_registry_addr(engine, rhost, sizeof(rhost), &rport)) return; /* no registry */

    /* The registry node does not register itself: it can't connect to its own
     * listener before that listener is up, and nothing discovers the registry
     * through the registry. */
    if (autoport_plugin_active(engine, name, "registry")) return;

    struct reg_session session = {0};
    if (!reg_session_open(engine, &session)) return;
    const char *reg_host = (strcmp(host, "0.0.0.0") == 0) ? "127.0.0.1" : host;
    struct picomesh_int_result r =
        registry_registry_register_service(&session.ctx, session.obj, NULL, name, name, reg_host,
                                           (uint32_t)port);
    if (PICOMESH_IS_OK(r)) {
        yinfo("autoport: registered '%s' -> %s:%d", name, reg_host, port);
    } else {
        ywarn("autoport: register '%s' failed: %s", name, r.error.msg ? r.error.msg : "?");
        picomesh_error_destroy(r.error);
    }
    reg_session_close(&session);
}

void picomesh_autoport_resolve_remotes(struct picomesh_engine *engine, const char *name)
{
    if (!engine || !name || !*name) return;
    char rhost[64];
    int rport = 0;
    if (!autoport_registry_addr(engine, rhost, sizeof(rhost), &rport)) return; /* no registry */

    char path[256];
    snprintf(path, sizeof(path), "mesh.services.%s.config.remotes", name);
    struct yconfig_node_ptr_result lr = yconfig_get(picomesh_engine_config(engine), path);
    if (PICOMESH_IS_ERR(lr) || !lr.value || yconfig_node_kind(lr.value) != YCONFIG_LIST) return;
    const struct yconfig_node *list = lr.value;
    size_t n = yconfig_node_size(list);

    struct reg_session session = {0};
    int opened = 0;
    for (size_t i = 0; i < n; ++i) {
        const struct yconfig_node *entry = yconfig_node_at(list, i);
        if (!entry || yconfig_node_kind(entry) != YCONFIG_MAP) continue;
        const struct yconfig_node *svc_node = yconfig_node_get(entry, "service");
        const char *svc = svc_node ? yconfig_node_as_string(svc_node, NULL) : NULL;
        if (!svc || !*svc) continue;
        /* An explicit positive port is a fixed remote — nothing to discover. */
        const struct yconfig_node *port_node = yconfig_node_get(entry, "port");
        int64_t pv = port_node ? yconfig_node_as_int(port_node, -1) : -1;
        if (pv > 0) continue;
        if (!opened) {
            if (!reg_session_open(engine, &session)) return;
            opened = 1;
        }
        char h[64];
        int p = 0;
        if (reg_resolve(&session, svc, 1, h, sizeof(h), &p)) {
            picomesh_engine_set_resolved_remote(engine, svc, h, p);
            yinfo("autoport: remote '%s' -> %s:%d (registry)", svc, h, p);
        } else {
            ywarn("autoport: remote '%s' (consumed by '%s') not resolvable via registry", svc, name);
        }
    }
    if (opened) reg_session_close(&session);
}
