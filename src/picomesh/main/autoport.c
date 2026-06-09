/* socket/bind/inet_pton/nanosleep on glibc want a feature macro before any
 * include pulls in <features.h>. */
#define _POSIX_C_SOURCE 200809L

#include "autoport.h"

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/config/config.h>
#include <picomesh/argv/argv.h>
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
static struct picomesh_int_result autoport_registry_addr(struct picomesh_engine *engine, char *host_out, size_t cap,
                                  int *port_out)
{
    struct config_node_ptr_result registry_res = config_get(picomesh_engine_config(engine), "registry");
    PICOMESH_RETURN_IF_ERR(picomesh_int, registry_res, "autoport_registry_addr: registry config read failed");
    if (!registry_res.value || config_node_kind(registry_res.value) != CONFIG_MAP) return PICOMESH_OK(picomesh_int, 0);
    const struct config_node *port_node = config_node_get(registry_res.value, "port");
    int64_t port = port_node ? config_node_as_int(port_node, -1) : -1;
    if (port <= 0) return PICOMESH_OK(picomesh_int, 0);
    const struct config_node *host_node = config_node_get(registry_res.value, "host");
    const char *host = host_node ? config_node_as_string(host_node, NULL) : NULL;
    snprintf(host_out, cap, "%s", (host && *host) ? host : "127.0.0.1");
    *port_out = (int)port;
    return PICOMESH_OK(picomesh_int, 1);
}

/* True iff `plugin` is in THIS node's activated plugins list. Read from
 * config — NOT by probing a local object create, because `<class>_create`
 * lazily registers the class even for a remote proxy, which would make every
 * node look like it hosts portalloc/registry after the first call. */
static struct picomesh_int_result autoport_plugin_active(struct picomesh_engine *engine, const char *name,
                                  const char *plugin)
{
    const struct config_node *plugins = NULL;
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.plugins", name);
        struct config_node_ptr_result plugins_res = config_get(picomesh_engine_config(engine), path);
        PICOMESH_RETURN_IF_ERR(picomesh_int, plugins_res, "autoport_plugin_active: service plugins config read failed");
        if (plugins_res.value && config_node_kind(plugins_res.value) == CONFIG_LIST) plugins = plugins_res.value;
    }
    if (!plugins) {
        struct config_node_ptr_result plugins_res = config_get(picomesh_engine_config(engine), "plugins");
        PICOMESH_RETURN_IF_ERR(picomesh_int, plugins_res, "autoport_plugin_active: plugins config read failed");
        if (plugins_res.value && config_node_kind(plugins_res.value) == CONFIG_LIST) plugins = plugins_res.value;
    }
    if (!plugins) return PICOMESH_OK(picomesh_int, 0);
    size_t count = config_node_size(plugins);
    for (size_t i = 0; i < count; ++i) {
        const struct config_node *item = config_node_at(plugins, i);
        const char *str = item ? config_node_as_string(item, NULL) : NULL;
        if (str && strcmp(str, plugin) == 0) return PICOMESH_OK(picomesh_int, 1);
    }
    return PICOMESH_OK(picomesh_int, 0);
}

/* A short-lived synchronous RPC session to the registry. */
struct reg_session {
    struct peer_channel *channel;
    struct object *obj;
    struct ctx ctx;
};

static struct picomesh_int_result reg_session_open(struct picomesh_engine *engine, struct reg_session *session)
{
    char host[64];
    int port = 0;
    struct picomesh_int_result addr_res = autoport_registry_addr(engine, host, sizeof(host), &port);
    PICOMESH_RETURN_IF_ERR(picomesh_int, addr_res, "reg_session_open: registry addr lookup failed");
    if (!addr_res.value) return PICOMESH_OK(picomesh_int, 0);
    int fd = -1;
    for (int attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        fd = autoport_connect(host, port);
        if (fd < 0) nap_ms(100); /* registry may still be binding */
    }
    if (fd < 0) {
        ywarn("autoport: registry %s:%d unreachable", host, port);
        return PICOMESH_OK(picomesh_int, 0);
    }
    session->channel = peer_channel_create(fd);
    if (!session->channel) {
        close(fd);
        return PICOMESH_OK(picomesh_int, 0);
    }
    session->ctx = (struct ctx){.peer = session->channel};
    struct object_ptr_result obj_res = registry_registry_create(&session->ctx);
    if (PICOMESH_IS_ERR(obj_res)) {
        peer_channel_destroy(session->channel);
        session->channel = NULL;
        return PICOMESH_ERR(picomesh_int, "reg_session_open: registry create failed", obj_res);
    }
    session->obj = obj_res.value;
    return PICOMESH_OK(picomesh_int, 1);
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
static struct picomesh_int_result reg_resolve(struct reg_session *session, const char *service, int wait,
                       char *host_out, size_t cap, int *port_out)
{
    int tries = wait ? 150 : 1; /* ~15s of 100ms polls */
    for (int attempt = 0; attempt < tries; ++attempt) {
        struct picomesh_string_result resolve_res =
            registry_registry_resolve(&session->ctx, session->obj, NULL, service);
        if (PICOMESH_IS_ERR(resolve_res)) {
            /* Transient — the registry may still be starting; retry. */
            picomesh_error_destroy(resolve_res.error);
        } else if (resolve_res.value && *resolve_res.value) {
            const char *colon = strrchr(resolve_res.value, ':');
            int ok = 0;
            if (colon && colon != resolve_res.value) {
                size_t host_len = (size_t)(colon - resolve_res.value);
                if (host_len < cap) {
                    memcpy(host_out, resolve_res.value, host_len);
                    host_out[host_len] = 0;
                    *port_out = atoi(colon + 1);
                    ok = (*port_out > 0);
                }
            }
            free(resolve_res.value);
            if (ok) return PICOMESH_OK(picomesh_int, 1);
        } else {
            free(resolve_res.value); /* empty == not registered yet */
        }
        if (wait) nap_ms(100);
    }
    return PICOMESH_OK(picomesh_int, 0);
}

/* ------------------------------------------------------------------ public */

struct picomesh_int_result picomesh_serve_port_is_auto(struct picomesh_engine *engine, const char *name)
{
    if (!engine) return PICOMESH_OK(picomesh_int, 0);
    /* An explicit numeric --port always wins and is never "auto". */
    int64_t cli = argv_get_int(picomesh_engine_cli(engine), "port", -1);
    if (cli > 0) return PICOMESH_OK(picomesh_int, 0);

    const struct config_node *node = NULL;
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.port", name);
        struct config_node_ptr_result port_res = config_get(picomesh_engine_config(engine), path);
        PICOMESH_RETURN_IF_ERR(picomesh_int, port_res, "picomesh_serve_port_is_auto: service port config read failed");
        if (port_res.value) node = port_res.value;
    }
    if (!node) {
        struct config_node_ptr_result port_res = config_get(picomesh_engine_config(engine), "port");
        PICOMESH_RETURN_IF_ERR(picomesh_int, port_res, "picomesh_serve_port_is_auto: port config read failed");
        if (port_res.value) node = port_res.value;
    }
    if (!node || config_node_kind(node) != CONFIG_STRING) return PICOMESH_OK(picomesh_int, 0);
    const char *str = config_node_as_string(node, NULL);
    return PICOMESH_OK(picomesh_int, str && strcmp(str, "auto") == 0 ? 1 : 0);
}

struct picomesh_int_result picomesh_autoport_allocate(struct picomesh_engine *engine, const char *name, const char *host)
{
    if (!engine || !name || !*name) return PICOMESH_ERR(picomesh_int, "autoport_allocate: engine/name required");
    if (!host || !*host) host = "127.0.0.1";

    struct ctx pctx = {0};
    struct object *pobj = NULL;
    struct peer_channel *portalloc_channel = NULL;

    /* Bootstrap case: THIS node provides portalloc. It cannot discover itself
     * through a registry it hasn't registered with yet, so allocate from its
     * local instance — every receiver shares the one process-wide lease table,
     * so the port portalloc takes for itself is never handed to a caller. */
    struct picomesh_int_result portalloc_active = autoport_plugin_active(engine, name, "portalloc");
    PICOMESH_RETURN_IF_ERR(picomesh_int, portalloc_active, "autoport_allocate: plugin active check failed");
    if (portalloc_active.value) {
        pctx = picomesh_engine_service_ctx(engine, "portalloc");
        struct object_ptr_result obj_res = portalloc_portalloc_create(&pctx);
        if (PICOMESH_IS_ERR(obj_res)) {
            ywarn("autoport: portalloc node failed to create its local allocator");
            return PICOMESH_ERR(picomesh_int, "autoport_allocate: local portalloc create failed", obj_res);
        }
        pobj = obj_res.value;
    }

    /* Normal case: discover the remote portalloc through the registry. */
    if (!pobj) {
        struct reg_session session = {0};
        struct picomesh_int_result session_res = reg_session_open(engine, &session);
        PICOMESH_RETURN_IF_ERR(picomesh_int, session_res, "autoport_allocate: registry session failed");
        if (!session_res.value) {
            ywarn("autoport: '%s' wants port:auto but the registry is unreachable", name);
            return PICOMESH_ERR(picomesh_int, "autoport_allocate: registry unreachable");
        }
        char portalloc_host[64];
        int portalloc_port = 0;
        struct picomesh_int_result resolve_res =
            reg_resolve(&session, "portalloc", 1, portalloc_host, sizeof(portalloc_host), &portalloc_port);
        reg_session_close(&session);
        PICOMESH_RETURN_IF_ERR(picomesh_int, resolve_res, "autoport_allocate: portalloc resolve failed");
        if (!resolve_res.value) {
            ywarn("autoport: portalloc not discoverable through registry for '%s'", name);
            return PICOMESH_ERR(picomesh_int, "autoport_allocate: portalloc not discoverable");
        }
        int fd = -1;
        for (int attempt = 0; attempt < 100 && fd < 0; ++attempt) {
            fd = autoport_connect(portalloc_host, portalloc_port);
            if (fd < 0) nap_ms(100);
        }
        if (fd < 0) {
            ywarn("autoport: portalloc %s:%d unreachable for '%s'", portalloc_host, portalloc_port, name);
            return PICOMESH_ERR(picomesh_int, "autoport_allocate: portalloc unreachable");
        }
        portalloc_channel = peer_channel_create(fd);
        if (!portalloc_channel) {
            close(fd);
            return PICOMESH_ERR(picomesh_int, "autoport_allocate: peer channel alloc failed");
        }
        pctx = (struct ctx){.peer = portalloc_channel};
        struct object_ptr_result obj_res = portalloc_portalloc_create(&pctx);
        if (PICOMESH_IS_ERR(obj_res)) {
            peer_channel_destroy(portalloc_channel);
            return PICOMESH_ERR(picomesh_int, "autoport_allocate: remote portalloc create failed", obj_res);
        }
        pobj = obj_res.value;
    }

    /* Allocate + bind-probe + retry: portalloc may hand out a port that a
     * foreign process grabs before we bind it. On a probe miss, release it
     * (portalloc's next probe skips it) and ask again. */
    int port = -1;
    for (int attempt = 0; attempt < 64; ++attempt) {
        struct picomesh_uint32_result alloc_res =
            portalloc_portalloc_allocate(&pctx, pobj, NULL, name, host);
        if (PICOMESH_IS_ERR(alloc_res)) {
            ywarn("autoport: portalloc.allocate('%s') failed: %s", name,
                  alloc_res.error.msg ? alloc_res.error.msg : "?");
            picomesh_error_destroy(alloc_res.error);
            break;
        }
        uint32_t candidate = alloc_res.value;
        if (autoport_probe_bind(host, (int)candidate)) {
            port = (int)candidate;
            break;
        }
        struct picomesh_int_result release_res =
            portalloc_portalloc_release(&pctx, pobj, NULL, candidate);
        if (PICOMESH_IS_ERR(release_res)) picomesh_error_destroy(release_res.error);
        nap_ms(20);
    }

    object_release_in_ctx(&pctx, pobj);
    if (portalloc_channel) peer_channel_destroy(portalloc_channel);
    if (port > 0) {
        yinfo("autoport: '%s' allocated port %d", name, port);
        return PICOMESH_OK(picomesh_int, port);
    }
    return PICOMESH_ERR(picomesh_int, "autoport_allocate: no bindable port from portalloc");
}

struct picomesh_void_result picomesh_autoport_register_self(struct picomesh_engine *engine, const char *name,
                                     const char *host, int port)
{
    if (!engine || !name || !*name || port <= 0) return PICOMESH_OK_VOID();
    char rhost[64];
    int rport = 0;
    struct picomesh_int_result addr_res = autoport_registry_addr(engine, rhost, sizeof(rhost), &rport);
    PICOMESH_RETURN_IF_ERR(picomesh_void, addr_res, "register_self: registry addr lookup failed");
    if (!addr_res.value) return PICOMESH_OK_VOID(); /* no registry */

    /* The registry node does not register itself: it can't connect to its own
     * listener before that listener is up, and nothing discovers the registry
     * through the registry. */
    struct picomesh_int_result reg_active = autoport_plugin_active(engine, name, "registry");
    PICOMESH_RETURN_IF_ERR(picomesh_void, reg_active, "register_self: plugin active check failed");
    if (reg_active.value) return PICOMESH_OK_VOID();

    struct reg_session session = {0};
    struct picomesh_int_result session_res = reg_session_open(engine, &session);
    PICOMESH_RETURN_IF_ERR(picomesh_void, session_res, "register_self: registry session failed");
    if (!session_res.value) return PICOMESH_OK_VOID();
    const char *reg_host = (strcmp(host, "0.0.0.0") == 0) ? "127.0.0.1" : host;
    struct picomesh_int_result register_res =
        registry_registry_register_service(&session.ctx, session.obj, NULL, name, name, reg_host,
                                           (uint32_t)port);
    if (PICOMESH_IS_OK(register_res)) {
        yinfo("autoport: registered '%s' -> %s:%d", name, reg_host, port);
    } else {
        ywarn("autoport: register '%s' failed: %s", name, register_res.error.msg ? register_res.error.msg : "?");
        picomesh_error_destroy(register_res.error);
    }
    reg_session_close(&session);
    return PICOMESH_OK_VOID();
}

struct picomesh_void_result picomesh_autoport_resolve_remotes(struct picomesh_engine *engine, const char *name)
{
    if (!engine || !name || !*name) return PICOMESH_OK_VOID();
    char rhost[64];
    int rport = 0;
    struct picomesh_int_result addr_res = autoport_registry_addr(engine, rhost, sizeof(rhost), &rport);
    PICOMESH_RETURN_IF_ERR(picomesh_void, addr_res, "resolve_remotes: registry addr lookup failed");
    if (!addr_res.value) return PICOMESH_OK_VOID(); /* no registry */

    char path[256];
    snprintf(path, sizeof(path), "mesh.services.%s.config.remotes", name);
    struct config_node_ptr_result list_res = config_get(picomesh_engine_config(engine), path);
    PICOMESH_RETURN_IF_ERR(picomesh_void, list_res, "resolve_remotes: remotes config read failed");
    if (!list_res.value || config_node_kind(list_res.value) != CONFIG_LIST) return PICOMESH_OK_VOID();
    const struct config_node *list = list_res.value;
    size_t count = config_node_size(list);

    struct reg_session session = {0};
    int opened = 0;
    for (size_t i = 0; i < count; ++i) {
        const struct config_node *entry = config_node_at(list, i);
        if (!entry || config_node_kind(entry) != CONFIG_MAP) continue;
        const struct config_node *svc_node = config_node_get(entry, "service");
        const char *svc = svc_node ? config_node_as_string(svc_node, NULL) : NULL;
        if (!svc || !*svc) continue;
        /* An explicit positive port is a fixed remote — nothing to discover. */
        const struct config_node *port_node = config_node_get(entry, "port");
        int64_t port_val = port_node ? config_node_as_int(port_node, -1) : -1;
        if (port_val > 0) continue;
        if (!opened) {
            struct picomesh_int_result session_res = reg_session_open(engine, &session);
            PICOMESH_RETURN_IF_ERR(picomesh_void, session_res, "resolve_remotes: registry session failed");
            if (!session_res.value) return PICOMESH_OK_VOID();
            opened = 1;
        }
        char host[64];
        int port = 0;
        struct picomesh_int_result resolve_res = reg_resolve(&session, svc, 1, host, sizeof(host), &port);
        if (PICOMESH_IS_ERR(resolve_res)) {
            reg_session_close(&session);
            return PICOMESH_ERR(picomesh_void, "resolve_remotes: registry resolve failed", resolve_res);
        }
        if (resolve_res.value) {
            picomesh_engine_set_resolved_remote(engine, svc, host, port);
            yinfo("autoport: remote '%s' -> %s:%d (registry)", svc, host, port);
        } else {
            ywarn("autoport: remote '%s' (consumed by '%s') not resolvable via registry", svc, name);
        }
    }
    if (opened) reg_session_close(&session);
    return PICOMESH_OK_VOID();
}
