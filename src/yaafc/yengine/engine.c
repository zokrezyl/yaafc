/* yaafc engine — lifecycle owner.
 *
 * Now also owns the yconfig tree and the CLI chain. The driver hands
 * us a parsed `yargv_chain`; we read its --config-file / --config
 * K=V / --env K=V options, feed them to `yconfig_create`, and stash
 * everything on the engine. */

#include <yaafc/yengine/engine.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/yloop/yloop.h>
#include <yaafc/yconfig/yconfig.h>
#include <yaafc/yargv/yargv.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/result.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linked list of registered remote RPC sessions, keyed by name. The
 * engine owns every session; lookups are O(N names) which is fine
 * for the handful of remotes a typical plugin holds. */
struct remote_entry {
    char *name;
    struct rpc_session *session;
    struct remote_entry *next;
};

struct yaafc_engine {
    struct yloop *loop;
    struct yconfig *config;
    struct yargv_chain *cli; /* may be NULL */
    struct remote_entry *remotes;
};

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

struct yaafc_engine_ptr_result yaafc_engine_create(const struct yaafc_engine_args *args)
{
    ytrace_init();
    rpc_init();

    struct yaafc_engine *e = calloc(1, sizeof(*e));
    if (!e) return YAAFC_ERR(yaafc_engine_ptr, "yaafc_engine_create: calloc failed");

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
        .app_name = args && args->app_name ? args->app_name : "yaafc",
        .no_filesystem_search = args && args->no_filesystem_search,
    };
    struct yconfig_ptr_result cr = yconfig_create(&cfg_args);
    if (YAAFC_IS_ERR(cr)) {
        free(e);
        return YAAFC_ERR(yaafc_engine_ptr, "yaafc_engine_create: yconfig_create failed", cr);
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
        struct yaafc_void_result pr = yconfig_promote_subtree(e->config, path);
        if (YAAFC_IS_ERR(pr)) {
            ywarn("engine: projecting %s onto root failed (continuing)", path);
            yaafc_error_destroy(pr.error);
        } else {
            ydebug("engine: projected %s onto root", path);
        }
    }

    struct yloop_ptr_result lr = yloop_create();
    if (YAAFC_IS_ERR(lr)) {
        yconfig_destroy(e->config);
        free(e);
        return YAAFC_ERR(yaafc_engine_ptr, "yaafc_engine_create: yloop_create failed", lr);
    }
    e->loop = lr.value;
    yinfo("yaafc_engine: ready (config=%s)",
          config_file ? config_file : "(defaults+search)");
    return YAAFC_OK(yaafc_engine_ptr, e);
}

void yaafc_engine_destroy(struct yaafc_engine *e)
{
    if (!e) return;
    struct remote_entry *r = e->remotes;
    while (r) {
        struct remote_entry *next = r->next;
        rpc_session_destroy(r->session);
        free(r->name);
        free(r);
        r = next;
    }
    yloop_destroy(e->loop);
    yconfig_destroy(e->config);
    yargv_chain_destroy(e->cli);
    free(e);
}

/* ---- remote sessions ---------------------------------------------- */

static int dial_tcp(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    /* Disable Nagle: yrpc traffic is request/response with small
     * (header+body_len+body) frames; the default 40 ms delayed-ACK +
     * Nagle interaction kills loopback latency. */
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    addr.sin_port = htons((uint16_t)port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

struct yaafc_void_result yaafc_engine_add_remote(struct yaafc_engine *e,
                                                  const char *name,
                                                  const char *host, int port)
{
    if (!e || !name) return YAAFC_ERR(yaafc_void, "add_remote: bad args");
    if (!host) host = "127.0.0.1";

    /* Replace any previous registration with the same name. */
    for (struct remote_entry *r = e->remotes; r; r = r->next) {
        if (strcmp(r->name, name) == 0) {
            rpc_session_destroy(r->session);
            r->session = NULL;
            int fd = dial_tcp(host, port);
            if (fd < 0) return YAAFC_ERR(yaafc_void, "add_remote: dial failed");
            r->session = rpc_session_create(fd);
            if (!r->session) { close(fd); return YAAFC_ERR(yaafc_void, "add_remote: session_create"); }
            yinfo("engine: reopened remote '%s' → %s:%d", name, host, port);
            return YAAFC_OK_VOID();
        }
    }

    int fd = dial_tcp(host, port);
    if (fd < 0) return YAAFC_ERR(yaafc_void, "add_remote: dial failed");
    struct rpc_session *s = rpc_session_create(fd);
    if (!s) { close(fd); return YAAFC_ERR(yaafc_void, "add_remote: session_create"); }

    struct remote_entry *node = calloc(1, sizeof(*node));
    if (!node) { rpc_session_destroy(s); return YAAFC_ERR(yaafc_void, "add_remote: calloc"); }
    node->name = strdup(name);
    if (!node->name) { rpc_session_destroy(s); free(node); return YAAFC_ERR(yaafc_void, "add_remote: strdup"); }
    node->session = s;
    node->next = e->remotes;
    e->remotes = node;
    yinfo("engine: opened remote '%s' → %s:%d", name, host, port);
    return YAAFC_OK_VOID();
}

struct rpc_session *yaafc_engine_remote(struct yaafc_engine *e, const char *name)
{
    if (!e || !name) return NULL;
    for (struct remote_entry *r = e->remotes; r; r = r->next) {
        if (strcmp(r->name, name) == 0) return r->session;
    }
    return NULL;
}

struct ctx yaafc_engine_service_ctx(struct yaafc_engine *e, const char *service)
{
    struct ctx c = {.session = NULL};
    if (!e || !service) return c;
    c.session = yaafc_engine_remote(e, service);
    return c;
}

/* Look up the bind host/port for a named service by scanning
 * `mesh.services.<svc>.host/port` (scenario shape). Returns 1 on
 * success. */
static int resolve_service_bind(struct yaafc_engine *e, const char *service,
                                char *host_out, size_t host_cap, int *port_out)
{
    char path[256];
    snprintf(path, sizeof(path), "mesh.services.%s.host", service);
    struct yconfig_node_ptr_result hr = yconfig_get(yaafc_engine_config(e), path);
    const char *h = NULL;
    if (YAAFC_IS_OK(hr) && hr.value) {
        h = yconfig_node_as_string(hr.value, NULL);
    }
    snprintf(path, sizeof(path), "mesh.services.%s.port", service);
    struct yconfig_node_ptr_result pr = yconfig_get(yaafc_engine_config(e), path);
    int64_t p = -1;
    if (YAAFC_IS_OK(pr) && pr.value) {
        p = yconfig_node_as_int(pr.value, -1);
    }
    if (p <= 0) return 0;
    snprintf(host_out, host_cap, "%s", h && *h ? h : "127.0.0.1");
    *port_out = (int)p;
    return 1;
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

size_t yaafc_engine_open_remotes(struct yaafc_engine *e, const char *plugin)
{
    if (!e || !plugin) return 0;

    char path[256];
    snprintf(path, sizeof(path), "mesh.services.%s.config.remotes", plugin);
    struct yconfig_node_ptr_result lr =
        yconfig_get(yaafc_engine_config(e), path);
    if (YAAFC_IS_ERR(lr) || !lr.value) return 0;
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
        struct yaafc_void_result ar =
            yaafc_engine_add_remote(e, fc.svc, host, port);
        if (YAAFC_IS_OK(ar)) {
            opened++;
        } else {
            ywarn("engine: failed to open remote '%s' for plugin '%s'",
                  fc.svc, plugin);
            yaafc_error_destroy(ar.error);
        }
    }
    return opened;
}

struct yaafc_void_result yaafc_engine_run(struct yaafc_engine *e)
{
    if (!e) return YAAFC_ERR(yaafc_void, "yaafc_engine_run: NULL engine");
    struct yaafc_void_result r = yloop_run(e->loop);
    YAAFC_RETURN_IF_ERR(yaafc_void, r, "yaafc_engine_run: yloop_run failed");
    return YAAFC_OK_VOID();
}

void yaafc_engine_stop(struct yaafc_engine *e)
{
    if (!e) return;
    yloop_stop(e->loop);
}

struct yloop *yaafc_engine_loop(struct yaafc_engine *e)
{
    return e ? e->loop : NULL;
}

const struct yconfig *yaafc_engine_config(struct yaafc_engine *e)
{
    return e ? e->config : NULL;
}

const struct yconfig_node *yaafc_engine_plugin_config(struct yaafc_engine *e, const char *plugin)
{
    return yconfig_section(e ? e->config : NULL, plugin);
}

struct yargv_chain *yaafc_engine_cli(struct yaafc_engine *e)
{
    return e ? e->cli : NULL;
}

struct yaafc_void_result yaafc_engine_for_each_plugin(struct yaafc_engine *e,
                                                     void (*cb)(const char *qname, void *ud),
                                                     void *ud)
{
    (void)e; (void)cb; (void)ud;
    return YAAFC_OK_VOID();
}

static struct yaafc_engine **active_engine_slot(void)
{
    static struct yaafc_engine *p = NULL;
    return &p;
}

void yaafc_active_engine_set(struct yaafc_engine *e)
{
    *active_engine_slot() = e;
}

struct yaafc_engine *yaafc_active_engine(void)
{
    return *active_engine_slot();
}
