/* picomesh — driver binary.
 *
 * Parses argv via yargv (yaapp-style command-into-command CLI),
 * builds the engine (which loads yconfig with full precedence), then
 * dispatches to a subcommand:
 *
 *   picomesh [--config-file PATH] [--config K=V]... [--env K=V]...
 *         [--host H] [--port P] [--verbose]
 *         (serve | client)
 *
 * `serve` starts the yrpc frontend.
 * `client` runs the built-in smoke against a running server.
 * Plugin classes self-register at load via codegen constructor hooks. */

/* Every compiled plugin's public header — for its
 * picomesh_plugin_<name>_register() activation entry point. Being linked
 * in does NOT expose a plugin; the driver calls register() only for the
 * plugins this instance activates via config (registration == activation). */
#include <picomesh/plugin/storage/storage.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>
#include <picomesh/plugin/relational_storage/relational_storage.h>
#include <picomesh/plugin/calculator/calculator.h>
#include <picomesh/plugin/time/time.h>
#include <picomesh/plugin/accounts/accounts.h>
#include <picomesh/plugin/portalloc/portalloc.h>
#include <picomesh/plugin/registry/registry.h>
#include <picomesh/plugin/session/session.h>

#include "autoport.h"
#include <picomesh/plugin/password_authn/password_authn.h>
#include <picomesh/plugin/github_authn/github_authn.h>
#include <picomesh/plugin/token_issuer/token_issuer.h>
#include <picomesh/plugin/issues/issues.h>
#include <picomesh/plugin/git_repo/git_repo.h>
#include <picomesh/plugin/git_pipeline/git_pipeline.h>
#include <picomesh/plugin/personal_access_tokens/personal_access_tokens.h>
#include <picomesh/plugin/runner_agent/runner_agent.h>
#include <picomesh/plugin/trace_collector/trace_collector.h>
#include <picomesh/plugin/mesh/mesh.h>

#include <picomesh/yengine/engine.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yargv/yargv.h>
#include <picomesh/frontends/yrpc/yrpc.h>
#include <picomesh/frontends/yttp/yttp.h>
#include <picomesh/frontends/yhttp/yhttp.h>
#include <picomesh/frontends/alpine/alpine.h>
#include <picomesh/frontends/msgpack/msgpack.h>
#include <picomesh/frontends/cli/cli.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const struct yargv_option_def PICOMESH_OPTIONS[] = {
    {"--help",        "-h", "help",        "show help",                       YARGV_BOOL,      0},
    {"--verbose",     "-v", "verbose",     "enable trace output",             YARGV_BOOL,      0},
    {"--config-file", "-f", "config_file", "explicit config file",            YARGV_VALUE,     0},
    {"--config",      NULL, "config",      "config override (dotted=value)",  YARGV_KEY_VALUE, 1},
    {"--env",         "-e", "env",         "environment variable (K=V)",      YARGV_KEY_VALUE, 1},
    {"--host",        NULL, "host",        "bind/connect host",               YARGV_VALUE,     0},
    {"--port",        "-p", "port",        "bind/connect port",               YARGV_VALUE,     0},
    {"--app-name",    NULL, "app_name",    "app name (drives XDG path)",      YARGV_VALUE,     0},
    {"--name",        NULL, "name",        "instance name",                   YARGV_VALUE,     0},
    {"--frontend",    NULL, "frontend",    "frontend: yrpc (default), yttp, yhttp, alpine or msgpack",YARGV_VALUE, 0},
    {"--transport",   NULL, "transport",   "client transport: yrpc (default) or msgpack", YARGV_VALUE, 0},
    {"--workers",     NULL, "workers",     "in-process worker threads (default 1)", YARGV_VALUE, 0},
    {"--plugins",     NULL, "plugins",     "comma-sep plugin list (yaapp compat)", YARGV_VALUE, 0},
};
#define PICOMESH_OPTION_COUNT (sizeof(PICOMESH_OPTIONS) / sizeof(PICOMESH_OPTIONS[0]))

static int die_err(const char *what, struct picomesh_error e)
{
    picomesh_error_print(stderr, what, e);
    picomesh_error_destroy(e);
    return 1;
}

/* ---- plugin activation (registration == activation) -----------------
 *
 * Every plugin is compiled/linked into this binary, but a plugin is only
 * EXPOSED if this instance activates it. Activation = calling the
 * plugin's generated picomesh_plugin_<name>_register(), which is what
 * enters its classes into the shared registry. A plugin we never
 * register is unreachable through every frontend (/_rpc, yttp, yrpc,
 * cli) — "installed != exposed", matching the yaapp PluginFactory
 * allow-list model.
 *
 * The name→register table is the one piece that must enumerate all
 * plugins; it mirrors PICOMESH_PLUGINS in the top-level CMakeLists. */
struct plugin_reg { const char *name; void (*reg)(void); };

static const struct plugin_reg *plugin_registry(size_t *count)
{
    static const struct plugin_reg ROWS[] = {
        {"storage",                picomesh_plugin_storage_register},
        {"sharded_storage",        picomesh_plugin_sharded_storage_register},
        {"relational_storage",     picomesh_plugin_relational_storage_register},
        {"calculator",             picomesh_plugin_calculator_register},
        {"time",                   picomesh_plugin_time_register},
        {"accounts",               picomesh_plugin_accounts_register},
        {"portalloc",              picomesh_plugin_portalloc_register},
        {"registry",               picomesh_plugin_registry_register},
        {"session",                picomesh_plugin_session_register},
        {"password_authn",         picomesh_plugin_password_authn_register},
        {"github_authn",           picomesh_plugin_github_authn_register},
        {"token_issuer",           picomesh_plugin_token_issuer_register},
        {"issues",                 picomesh_plugin_issues_register},
        {"git_repo",               picomesh_plugin_git_repo_register},
        {"git_pipeline",           picomesh_plugin_git_pipeline_register},
        {"personal_access_tokens", picomesh_plugin_personal_access_tokens_register},
        {"runner_agent",           picomesh_plugin_runner_agent_register},
        {"trace_collector",        picomesh_plugin_trace_collector_register},
        {"mesh",                   picomesh_plugin_mesh_register},
    };
    *count = sizeof(ROWS) / sizeof(ROWS[0]);
    return ROWS;
}

/* Activate one plugin by name, ONCE. A name can appear in both the local
 * plugins list and the remotes list (and register() chains lookup hooks
 * that must not be installed twice), so we dedup against an activated set.
 * Unknown name → warn, skip. */
static void activate_one(const char *name)
{
    static const char *done[64];
    static size_t done_n = 0;
    for (size_t i = 0; i < done_n; ++i)
        if (strcmp(done[i], name) == 0) return;  /* already active */

    size_t n = 0;
    const struct plugin_reg *rows = plugin_registry(&n);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(rows[i].name, name) == 0) {
            rows[i].reg();
            if (done_n < sizeof(done) / sizeof(done[0])) done[done_n++] = rows[i].name;
            yinfo("activate: plugin '%s' registered", name);
            return;
        }
    }
    ywarn("activate: unknown plugin '%s' (not compiled in?) — skipped", name);
}

/* for_each cb: copy a remotes-entry's `service` value into `ud` (char[64]). */
static int remotes_service_cb(const char *key, const struct yconfig_node *val, void *ud)
{
    if (strcmp(key, "service") == 0) {
        const char *s = yconfig_node_as_string(val, NULL);
        if (s) { snprintf((char *)ud, 64, "%s", s); return 1; /* stop */ }
    }
    return 0;
}

/* Resolve THIS instance's active plugin list and register each, on the
 * main thread before any worker spawns. Precedence, highest first:
 *   1. --plugins CLI (comma-separated)         — explicit override
 *   2. mesh.services.<--name>.plugins (list)   — per-service / collocated app
 *   3. top-level plugins: (list)               — standalone single-service
 *   4. nothing                                 — activate nothing (yaapp default)
 */
static void activate_plugins(struct picomesh_engine *e)
{
    /* 1. --plugins CLI: comma-separated names. */
    const char *csv = yargv_get_string(picomesh_engine_cli(e), "plugins", NULL);
    if (csv && *csv) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", csv);
        for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            while (*tok == ' ') ++tok;
            size_t l = strlen(tok);
            while (l && tok[l - 1] == ' ') tok[--l] = 0;
            if (*tok) activate_one(tok);
        }
        return;
    }

    /* 2/3. A plugins: list in config — under mesh.services.<name> when
     * --name is set, else top-level. Both are YAML lists of strings. */
    char path[256];
    struct yconfig_node_ptr_result lr = {0};
    const char *name = yargv_get_string(picomesh_engine_cli(e), "name", NULL);
    if (name && *name) {
        snprintf(path, sizeof(path), "mesh.services.%s.plugins", name);
        lr = yconfig_get(picomesh_engine_config(e), path);
    }
    if (!(PICOMESH_IS_OK(lr) && lr.value))
        lr = yconfig_get(picomesh_engine_config(e), "plugins");

    if (PICOMESH_IS_OK(lr) && lr.value &&
        yconfig_node_kind(lr.value) == YCONFIG_LIST) {
        size_t n = yconfig_node_size(lr.value);
        for (size_t i = 0; i < n; ++i) {
            const struct yconfig_node *entry = yconfig_node_at(lr.value, i);
            if (!entry) continue;
            const char *pn = yconfig_node_as_string(entry, NULL);
            if (pn && *pn) activate_one(pn);
        }
    } else {
        yinfo("activate: no plugins list in config — nothing activated locally");
    }

    /* Active exposure = active LOCAL plugins (above) + active REMOTES.
     * A process that proxies a service (e.g. the gateway: plugins:[] but
     * remotes:[git_repo,...]) must ALSO register that service's plugin so
     * its jinvoke shims + class metadata exist locally to ENCODE the call;
     * `service_ctx` then returns the remote peer, so the call forwards.
     * Registration here is what makes a remote service reachable/encodable
     * — without it `/_rpc` can't pack args ("no method"). The plugin name
     * is the remote's `service:` name (1:1 in this scenario). */
    char rpath[256];
    struct yconfig_node_ptr_result rr = {0};
    if (name && *name) {
        snprintf(rpath, sizeof(rpath), "mesh.services.%s.config.remotes", name);
        rr = yconfig_get(picomesh_engine_config(e), rpath);
    }
    if (!(PICOMESH_IS_OK(rr) && rr.value))
        rr = yconfig_get(picomesh_engine_config(e), "remotes");
    if (PICOMESH_IS_OK(rr) && rr.value &&
        yconfig_node_kind(rr.value) == YCONFIG_LIST) {
        size_t n = yconfig_node_size(rr.value);
        for (size_t i = 0; i < n; ++i) {
            const struct yconfig_node *entry = yconfig_node_at(rr.value, i);
            if (!entry || yconfig_node_kind(entry) != YCONFIG_MAP) continue;
            /* entry is {service: <name>, port?: ...}. No by-key map getter
             * is exposed, so for_each and grab the `service` value. */
            char rn[64] = {0};
            yconfig_node_for_each(entry, remotes_service_cb, rn);
            if (rn[0]) activate_one(rn);
        }
    }
}

/* Single source of CLI docs: the synopsis + subcommands here, the per-flag
 * lines generated straight from PICOMESH_OPTIONS so they can never drift. Goes
 * to stdout when --help is asked for, stderr when shown on a usage error. */
static void usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "usage: %s [options] <subcommand> [args...]\n"
            "\n"
            "subcommands:\n"
            "  serve                          run the engine's frontend listener\n"
            "  client                         one-shot RPC smoke client\n"
            "  config-dump                    print the resolved config and exit\n"
            "  invoke <method> [arg...]       call one method from the CLI\n"
            "\n"
            "options:\n",
            argv0);
    yargv_print_options(PICOMESH_OPTIONS, PICOMESH_OPTION_COUNT, out);
}

/* Port / host resolution. Precedence, highest first:
 *
 *   1. --port / --host on the CLI
 *   2. `mesh.services.<--name>.port` / `.host` in the YAML — the
 *      "I'm the X service" path, used by both mesh-spawned children
 *      and standalone single-service invocations like
 *         picomesh --config-file foo.yaml --name accounts serve
 *   3. `yrpc.port` / `yrpc.host` (legacy config keys)
 *   4. defaults (127.0.0.1, 7777)
 */
static const char *resolve_host(struct picomesh_engine *e)
{
    const char *cli_host = yargv_get_string(picomesh_engine_cli(e), "host", NULL);
    if (cli_host && *cli_host) return cli_host;

    const char *name = yargv_get_string(picomesh_engine_cli(e), "name", NULL);
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.host", name);
        struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(e), path);
        if (PICOMESH_IS_OK(r) && r.value) {
            const char *s = yconfig_node_as_string(r.value, NULL);
            if (s && *s) return s;
        }
    }
    struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(e), "yrpc.host");
    if (PICOMESH_IS_OK(r) && r.value) {
        const char *s = yconfig_node_as_string(r.value, NULL);
        if (s && *s) return s;
    }
    return "127.0.0.1";
}

static int resolve_port(struct picomesh_engine *e)
{
    int64_t port_cli = yargv_get_int(picomesh_engine_cli(e), "port", -1);
    if (port_cli > 0) return (int)port_cli;

    const char *name = yargv_get_string(picomesh_engine_cli(e), "name", NULL);
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.port", name);
        struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(e), path);
        if (PICOMESH_IS_OK(r) && r.value) {
            int64_t p = yconfig_node_as_int(r.value, -1);
            if (p > 0) return (int)p;
        }
    }
    struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(e), "yrpc.port");
    if (PICOMESH_IS_OK(r) && r.value) {
        int64_t p = yconfig_node_as_int(r.value, -1);
        if (p > 0) return (int)p;
    }
    return 7777;
}

/* Number of in-process worker threads (gh#6). Precedence, highest first:
 *
 *   1. --workers on the CLI
 *   2. `mesh.services.<--name>.workers` in the YAML (a per-service run
 *      knob, sibling of `port`/`frontend`)
 *   3. top-level `workers` (standalone single-service / promoted config)
 *   4. default 1 (single-threaded — identical to the pre-gh#6 behaviour)
 *
 * Clamped to [1, 256].
 */
static int resolve_workers(struct picomesh_engine *e)
{
    int64_t n = -1;
    int64_t cli = yargv_get_int(picomesh_engine_cli(e), "workers", -1);
    if (cli > 0) {
        n = cli;
    } else {
        const char *name = yargv_get_string(picomesh_engine_cli(e), "name", NULL);
        if (name && *name) {
            char path[256];
            snprintf(path, sizeof(path), "mesh.services.%s.workers", name);
            struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(e), path);
            if (PICOMESH_IS_OK(r) && r.value) n = yconfig_node_as_int(r.value, -1);
        }
        if (n <= 0) {
            struct yconfig_node_ptr_result r =
                yconfig_get(picomesh_engine_config(e), "workers");
            if (PICOMESH_IS_OK(r) && r.value) n = yconfig_node_as_int(r.value, -1);
        }
    }
    if (n < 1) n = 1;
    if (n > 256) n = 256;
    return (int)n;
}

/* Inputs the per-worker setup callback needs. Lives on cmd_serve's stack
 * for the duration of picomesh_engine_run_workers (which blocks until every
 * worker loop exits), so plain borrowed pointers are safe. */
struct serve_setup {
    const char *name;
    const char *host;
    int port;
    const char *frontend;
};

/* Runs ON each worker thread (worker 0 = main thread). Opens the
 * service's remotes on this worker's own loop, then installs the chosen
 * frontend listener on it. All workers bind the same port via
 * SO_REUSEPORT. The frontend handle is owned for the process lifetime —
 * the loop owns the listener and the process is torn down by signal, so
 * there is nothing to stop here. */
static struct picomesh_void_result serve_worker_setup(struct picomesh_engine *e,
                                                   int worker_index, void *ud)
{
    struct serve_setup *ss = ud;

    if (ss->name && *ss->name) {
        size_t n = picomesh_engine_open_remotes(e, ss->name);
        yinfo("serve[%s w%d]: opened %zu remote(s) from mesh.services.%s.config.remotes",
              ss->name, worker_index, n, ss->name);
    }

    /* Config-driven per-node perf profiling (gh#14). Opens this worker's
     * counters on its own thread + loop when `perf.enabled` is set. A
     * permission/config failure is reported loudly but does NOT take the
     * service down — profiling must never be silently dropped, yet a host
     * without perf access shouldn't keep the mesh from coming up. */
    struct picomesh_void_result perf_r = picomesh_engine_perf_start(e, ss->name);
    if (PICOMESH_IS_ERR(perf_r)) {
        /* Unconditional (ytrace_output, not the gated yerror): a requested-but-
         * refused profile must be loud even when tracing is off. yperf already
         * logged the precise failing event + errno just above. */
        ytrace_output("error", __FILE__, __LINE__, __func__,
                      "serve[%s w%d]: perf profiling could not start (see the preceding perf "
                      "error) — continuing without it",
                      ss->name ? ss->name : "?", worker_index);
        picomesh_error_destroy(perf_r.error);
    }

    if (strcmp(ss->frontend, "yhttp") == 0) {
        struct yhttp_config cfg = {.host = ss->host, .port = ss->port};
        struct yhttp_frontend_ptr_result fr = yhttp_start(e, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, fr, "serve_worker_setup: yhttp_start failed");
    } else if (strcmp(ss->frontend, "yttp") == 0) {
        struct yttp_config cfg = {.host = ss->host, .port = ss->port};
        struct yttp_frontend_ptr_result fr = yttp_start(e, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, fr, "serve_worker_setup: yttp_start failed");
    } else if (strcmp(ss->frontend, "alpine") == 0) {
        struct alpine_config cfg = {.host = ss->host, .port = ss->port};
        struct alpine_frontend_ptr_result fr = alpine_start(e, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, fr, "serve_worker_setup: alpine_start failed");
    } else if (strcmp(ss->frontend, "msgpack") == 0) {
        struct msgpack_config cfg = {.host = ss->host, .port = ss->port};
        struct msgpack_frontend_ptr_result fr = msgpack_start(e, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, fr, "serve_worker_setup: msgpack_start failed");
    } else {
        struct yrpc_config cfg = {.host = ss->host, .port = ss->port};
        struct yrpc_frontend_ptr_result fr = yrpc_start(e, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, fr, "serve_worker_setup: yrpc_start failed");
    }
    return PICOMESH_OK_VOID();
}

static int cmd_serve(struct picomesh_engine *e)
{
    picomesh_active_engine_set(e);

    const char *name = yargv_get_string(picomesh_engine_cli(e), "name", NULL);
    const char *host = resolve_host(e);
    /* `port: auto` — allocate the listen port through portalloc (discovered
     * via the registry) before any worker binds. Otherwise the static port. */
    int port;
    if (picomesh_serve_port_is_auto(e, name)) {
        port = picomesh_autoport_allocate(e, name, host);
        if (port <= 0) {
            fprintf(stderr, "serve[%s]: port:auto allocation failed\n", name ? name : "?");
            return 1;
        }
    } else {
        port = resolve_port(e);
    }
    /* Registration is an internal feature of every mesh node: announce
     * (name -> host:port) so `port: auto` consumers can discover us, then
     * discover the addresses of our own `port: auto` remotes. Both are no-ops
     * when no registry is configured. Register BEFORE resolving remotes so the
     * registration phase can never deadlock against a peer that is waiting on
     * us. */
    picomesh_autoport_register_self(e, name, host, port);
    picomesh_autoport_resolve_remotes(e, name);
    int workers = resolve_workers(e);
    const char *frontend = yargv_get_string(picomesh_engine_cli(e), "frontend", "yrpc");
    if (!frontend) frontend = "yrpc";

    /* Reject an unknown frontend up front, before spinning any worker. */
    if (strcmp(frontend, "yrpc") != 0 && strcmp(frontend, "yttp") != 0 &&
        strcmp(frontend, "yhttp") != 0 && strcmp(frontend, "alpine") != 0 &&
        strcmp(frontend, "msgpack") != 0) {
        fprintf(stderr, "unknown --frontend '%s' (try yrpc | yttp | yhttp | alpine | msgpack)\n",
                frontend);
        return 2;
    }

    yinfo("picomesh serve [%s]: %s:%d (%d worker%s)",
          frontend, host, port, workers, workers == 1 ? "" : "s");

    /* One process, N worker threads: each runs serve_worker_setup on its
     * own loop (opening this service's remotes + installing the frontend
     * listener), then runs that loop. With workers == 1 this is exactly
     * the old single-threaded path (worker 0 on the main thread). The
     * `--name` service resolution drives every per-service config lookup
     * the workers do, exactly as before — only now it happens per worker
     * so each holds its own backend connection set. */
    struct serve_setup ss = {.name = name, .host = host, .port = port, .frontend = frontend};
    struct picomesh_void_result rr =
        picomesh_engine_run_workers(e, (size_t)workers, serve_worker_setup, &ss);
    if (PICOMESH_IS_ERR(rr)) return die_err("engine_run_workers", rr.error);
    return 0;
}

static int cmd_client(struct picomesh_engine *e)
{
    const char *host = resolve_host(e);
    int port = resolve_port(e);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "bad host: %s\n", host);
        close(fd);
        return 1;
    }
    addr.sin_port = htons((uint16_t)port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(fd); return 1;
    }
    yinfo("picomesh client: connected to %s:%d", host, port);

    /* --transport msgpack: drive a FOREIGN msgpack service over the outbound
     * msgpack client path. The receiver object is built locally (no remote
     * CREATE — the msgpack envelope identifies the receiver by path), then the
     * call rides the msgpack peer. Proves picomesh C → foreign msgpack. */
    const char *transport = yargv_get_string(picomesh_engine_cli(e), "transport", NULL);
    if (transport && strcmp(transport, "msgpack") == 0) {
        struct peer_channel *mp = peer_channel_create_msgpack(fd);
        if (!mp) { close(fd); return 1; }
        struct ctx local_ctx = {0};
        struct object_ptr_result cr = calculator_calc_create(&local_ctx);
        if (PICOMESH_IS_ERR(cr)) { peer_channel_destroy(mp); return die_err("calc_create", cr.error); }
        struct object *calc = cr.value;
        struct ctx mp_ctx = {.peer = mp};
        struct picomesh_int64_result ar = calculator_calc_add(&mp_ctx, calc, NULL, 6, 7);
        if (PICOMESH_IS_ERR(ar)) { peer_channel_destroy(mp); return die_err("calc_add(msgpack)", ar.error); }
        printf("msgpack client: 6 + 7 = %lld\n", (long long)ar.value);
        struct picomesh_int64_result mr = calculator_calc_mul(&mp_ctx, calc, NULL, 6, 7);
        if (PICOMESH_IS_ERR(mr)) { peer_channel_destroy(mp); return die_err("calc_mul(msgpack)", mr.error); }
        printf("msgpack client: 6 * 7 = %lld\n", (long long)mr.value);
        object_release_in_ctx(&local_ctx, calc);
        peer_channel_destroy(mp);
        return 0;
    }

    struct peer_channel *s = peer_channel_create(fd);
    if (!s) { close(fd); return 1; }
    struct ctx ctx = {.peer = s};

    struct object_ptr_result cr = calculator_calc_create(&ctx);
    if (PICOMESH_IS_ERR(cr)) { peer_channel_destroy(s); return die_err("calc_create", cr.error); }
    struct object *calc = cr.value;
    struct picomesh_int64_result ar = calculator_calc_add(&ctx, calc, NULL, 6, 7);
    if (PICOMESH_IS_ERR(ar)) { peer_channel_destroy(s); return die_err("calc_add", ar.error); }
    yinfo("client: 6 + 7 = %lld", (long long)ar.value);
    struct picomesh_int64_result mr = calculator_calc_mul(&ctx, calc, NULL, 6, 7);
    if (PICOMESH_IS_ERR(mr)) { peer_channel_destroy(s); return die_err("calc_mul", mr.error); }
    yinfo("client: 6 * 7 = %lld", (long long)mr.value);
    object_release_in_ctx(&ctx, calc); /* remote proxy is cached on the channel */

    peer_channel_destroy(s);
    return 0;
}

static int cmd_config_dump(struct picomesh_engine *e)
{
    char buf[16384];
    size_t n = yconfig_dump(picomesh_engine_config(e), buf, sizeof(buf));
    (void)n;
    printf("%s\n", buf);
    return 0;
}

int main(int argc, char **argv)
{
    ytrace_init();

    struct yargv_chain_ptr_result pr = yargv_parse(PICOMESH_OPTIONS, PICOMESH_OPTION_COUNT, argc, argv);
    if (PICOMESH_IS_ERR(pr)) return die_err("yargv_parse", pr.error);
    struct yargv_chain *cli = pr.value;

    if (yargv_get_bool(cli, "help", 0)) { usage(stdout, argv[0]); yargv_chain_destroy(cli); return 0; }
    if (yargv_get_bool(cli, "verbose", 0)) {
        ytrace_set_all_enabled(1);
    }

    const char *sub = yargv_subcommand(cli);
    if (!sub) { usage(stderr, argv[0]); yargv_chain_destroy(cli); return 2; }

    struct picomesh_engine_args ea = {
        .cli = cli,                    /* engine takes ownership */
        .app_name = yargv_get_string(cli, "app_name", "picomesh"),
    };
    struct picomesh_engine_ptr_result er = picomesh_engine_create(&ea);
    if (PICOMESH_IS_ERR(er)) return die_err("engine_create", er.error);
    struct picomesh_engine *e = er.value;

    /* Registration == activation. Done here on the main thread, before any
     * subcommand (and before serve spawns workers): only the plugins this
     * instance activated via config enter the registry; everything else
     * compiled in stays unreachable. */
    picomesh_active_engine_set(e);
    activate_plugins(e);

    int rc;
    if (strcmp(sub, "serve") == 0) {
        rc = cmd_serve(e);
    } else if (strcmp(sub, "client") == 0) {
        rc = cmd_client(e);
    } else if (strcmp(sub, "config-dump") == 0) {
        rc = cmd_config_dump(e);
    } else if (strcmp(sub, "invoke") == 0) {
        rc = picomesh_cli_dispatch(e);
    } else {
        usage(stderr, argv[0]);
        rc = 2;
    }

    picomesh_engine_destroy(e);
    return rc;
}
