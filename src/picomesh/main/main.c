/* sigaction + execinfo backtrace() (the fatal-signal crash handler below) are
 * GNU/POSIX extensions; define _GNU_SOURCE before any system header. */
#define _GNU_SOURCE 1

/* picomesh — driver binary.
 *
 * Parses argv via argv (yaapp-style command-into-command CLI),
 * builds the engine (which loads config with full precedence), then
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

#include <picomesh/engine/engine.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/loop/loop.h>
#include <picomesh/config/config.h>
#include <picomesh/argv/argv.h>
#include <picomesh/frontends/yrpc/yrpc.h>
#include <picomesh/frontends/yttp/yttp.h>
#include <picomesh/frontends/yhttp/yhttp.h>
#include <picomesh/frontends/alpine/alpine.h>
#include <picomesh/frontends/msgpack/msgpack.h>
#include <picomesh/frontends/cli/cli.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>

static const struct argv_option_def PICOMESH_OPTIONS[] = {
    {"--help",        "-h", "help",        "show help",                       ARGV_BOOL,      0},
    {"--verbose",     "-v", "verbose",     "enable trace output",             ARGV_BOOL,      0},
    {"--config-file", "-f", "config_file", "explicit config file",            ARGV_VALUE,     0},
    {"--config",      NULL, "config",      "config override (dotted=value)",  ARGV_KEY_VALUE, 1},
    {"--env",         "-e", "env",         "environment variable (K=V)",      ARGV_KEY_VALUE, 1},
    {"--host",        NULL, "host",        "bind/connect host",               ARGV_VALUE,     0},
    {"--port",        "-p", "port",        "bind/connect port",               ARGV_VALUE,     0},
    {"--app-name",    NULL, "app_name",    "app name (drives XDG path)",      ARGV_VALUE,     0},
    {"--name",        NULL, "name",        "instance name",                   ARGV_VALUE,     0},
    {"--frontend",    NULL, "frontend",    "frontend: yrpc (default), yttp, yhttp, alpine or msgpack",ARGV_VALUE, 0},
    {"--transport",   NULL, "transport",   "client transport: yrpc (default) or msgpack", ARGV_VALUE, 0},
    {"--workers",     NULL, "workers",     "in-process worker threads (default 1)", ARGV_VALUE, 0},
    {"--plugins",     NULL, "plugins",     "comma-sep plugin list (yaapp compat)", ARGV_VALUE, 0},
};
#define PICOMESH_OPTION_COUNT (sizeof(PICOMESH_OPTIONS) / sizeof(PICOMESH_OPTIONS[0]))

static int die_err(const char *what, struct picomesh_error err)
{
    picomesh_error_print(stderr, what, err);
    picomesh_error_destroy(err);
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
struct plugin_reg { const char *name; struct picomesh_void_result (*reg)(void); };

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
static struct picomesh_void_result activate_one(const char *name)
{
    static const char *done[64];
    static size_t done_n = 0;
    for (size_t i = 0; i < done_n; ++i)
        if (strcmp(done[i], name) == 0) return PICOMESH_OK_VOID();  /* already active */

    size_t row_count = 0;
    const struct plugin_reg *rows = plugin_registry(&row_count);
    for (size_t i = 0; i < row_count; ++i) {
        if (strcmp(rows[i].name, name) == 0) {
            struct picomesh_void_result reg_res = rows[i].reg();
            PICOMESH_RETURN_IF_ERR(picomesh_void, reg_res, "activate: plugin register failed");
            if (done_n < sizeof(done) / sizeof(done[0])) done[done_n++] = rows[i].name;
            yinfo("activate: plugin '%s' registered", name);
            return PICOMESH_OK_VOID();
        }
    }
    ywarn("activate: unknown plugin '%s' (not compiled in?) — skipped", name);
    return PICOMESH_OK_VOID();
}

/* for_each cb: copy a remotes-entry's `service` value into `ud` (char[64]). */
static int remotes_service_cb(const char *key, const struct config_node *val, void *ud)
{
    if (strcmp(key, "service") == 0) {
        const char *str = config_node_as_string(val, NULL);
        if (str) { snprintf((char *)ud, 64, "%s", str); return 1; /* stop */ }
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
static struct picomesh_void_result activate_plugins(struct picomesh_engine *engine)
{
    /* 1. --plugins CLI: comma-separated names. */
    const char *csv = argv_get_string(picomesh_engine_cli(engine), "plugins", NULL);
    if (csv && *csv) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", csv);
        for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            while (*tok == ' ') ++tok;
            size_t len = strlen(tok);
            while (len && tok[len - 1] == ' ') tok[--len] = 0;
            if (*tok) {
                struct picomesh_void_result ar = activate_one(tok);
                PICOMESH_RETURN_IF_ERR(picomesh_void, ar, "activate_plugins: --plugins");
            }
        }
        return PICOMESH_OK_VOID();
    }

    /* 2/3. A plugins: list in config — under mesh.services.<name> when
     * --name is set, else top-level. Both are YAML lists of strings. */
    char path[256];
    struct config_node_ptr_result list_res = {0};
    const char *name = argv_get_string(picomesh_engine_cli(engine), "name", NULL);
    if (name && *name) {
        snprintf(path, sizeof(path), "mesh.services.%s.plugins", name);
        list_res = config_get(picomesh_engine_config(engine), path);
        /* A read failure here must fail loud, not be overwritten by the
         * top-level fallback below — that would mask the error and select a
         * different plugin set than the operator configured. */
        PICOMESH_RETURN_IF_ERR(picomesh_void, list_res, "activate_plugins: reading service plugins config");
    }
    if (!(PICOMESH_IS_OK(list_res) && list_res.value)) {
        list_res = config_get(picomesh_engine_config(engine), "plugins");
        PICOMESH_RETURN_IF_ERR(picomesh_void, list_res, "activate_plugins: reading plugins config");
    }

    if (PICOMESH_IS_OK(list_res) && list_res.value &&
        config_node_kind(list_res.value) == CONFIG_LIST) {
        size_t count = config_node_size(list_res.value);
        for (size_t i = 0; i < count; ++i) {
            const struct config_node *entry = config_node_at(list_res.value, i);
            if (!entry) continue;
            const char *plugin_name = config_node_as_string(entry, NULL);
            if (plugin_name && *plugin_name) {
                struct picomesh_void_result ar = activate_one(plugin_name);
                PICOMESH_RETURN_IF_ERR(picomesh_void, ar, "activate_plugins: config list");
            }
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
    struct config_node_ptr_result remotes_res = {0};
    if (name && *name) {
        snprintf(rpath, sizeof(rpath), "mesh.services.%s.config.remotes", name);
        remotes_res = config_get(picomesh_engine_config(engine), rpath);
    }
    if (!(PICOMESH_IS_OK(remotes_res) && remotes_res.value))
        remotes_res = config_get(picomesh_engine_config(engine), "remotes");
    if (PICOMESH_IS_OK(remotes_res) && remotes_res.value &&
        config_node_kind(remotes_res.value) == CONFIG_LIST) {
        size_t count = config_node_size(remotes_res.value);
        for (size_t i = 0; i < count; ++i) {
            const struct config_node *entry = config_node_at(remotes_res.value, i);
            if (!entry || config_node_kind(entry) != CONFIG_MAP) continue;
            /* entry is {service: <name>, port?: ...}. No by-key map getter
             * is exposed, so for_each and grab the `service` value. */
            char service_name[64] = {0};
            config_node_for_each(entry, remotes_service_cb, service_name);
            if (service_name[0]) {
                struct picomesh_void_result ar = activate_one(service_name);
                PICOMESH_RETURN_IF_ERR(picomesh_void, ar, "activate_plugins: remote service");
            }
        }
    }
    return PICOMESH_OK_VOID();
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
    argv_print_options(PICOMESH_OPTIONS, PICOMESH_OPTION_COUNT, out);
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
static struct const_char_ptr_result resolve_host(struct picomesh_engine *engine)
{
    const char *cli_host = argv_get_string(picomesh_engine_cli(engine), "host", NULL);
    if (cli_host && *cli_host) return PICOMESH_OK(const_char_ptr, cli_host);

    const char *name = argv_get_string(picomesh_engine_cli(engine), "name", NULL);
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.host", name);
        struct config_node_ptr_result host_res = config_get(picomesh_engine_config(engine), path);
        PICOMESH_RETURN_IF_ERR(const_char_ptr, host_res, "resolve_host: service host config read failed");
        if (host_res.value) {
            const char *str = config_node_as_string(host_res.value, NULL);
            if (str && *str) return PICOMESH_OK(const_char_ptr, str);
        }
    }
    struct config_node_ptr_result host_res = config_get(picomesh_engine_config(engine), "yrpc.host");
    PICOMESH_RETURN_IF_ERR(const_char_ptr, host_res, "resolve_host: yrpc.host config read failed");
    if (host_res.value) {
        const char *str = config_node_as_string(host_res.value, NULL);
        if (str && *str) return PICOMESH_OK(const_char_ptr, str);
    }
    return PICOMESH_OK(const_char_ptr, "127.0.0.1");
}

static struct picomesh_int_result resolve_port(struct picomesh_engine *engine)
{
    int64_t port_cli = argv_get_int(picomesh_engine_cli(engine), "port", -1);
    if (port_cli > 0) return PICOMESH_OK(picomesh_int, (int)port_cli);

    const char *name = argv_get_string(picomesh_engine_cli(engine), "name", NULL);
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.port", name);
        struct config_node_ptr_result port_res = config_get(picomesh_engine_config(engine), path);
        PICOMESH_RETURN_IF_ERR(picomesh_int, port_res, "resolve_port: service port config read failed");
        if (port_res.value) {
            int64_t port_val = config_node_as_int(port_res.value, -1);
            if (port_val > 0) return PICOMESH_OK(picomesh_int, (int)port_val);
        }
    }
    struct config_node_ptr_result port_res = config_get(picomesh_engine_config(engine), "yrpc.port");
    PICOMESH_RETURN_IF_ERR(picomesh_int, port_res, "resolve_port: yrpc.port config read failed");
    if (port_res.value) {
        int64_t port_val = config_node_as_int(port_res.value, -1);
        if (port_val > 0) return PICOMESH_OK(picomesh_int, (int)port_val);
    }
    return PICOMESH_OK(picomesh_int, 7777);
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
static struct picomesh_int_result resolve_workers(struct picomesh_engine *engine)
{
    int64_t worker_count = -1;
    int64_t cli_workers = argv_get_int(picomesh_engine_cli(engine), "workers", -1);
    if (cli_workers > 0) {
        worker_count = cli_workers;
    } else {
        const char *name = argv_get_string(picomesh_engine_cli(engine), "name", NULL);
        if (name && *name) {
            char path[256];
            snprintf(path, sizeof(path), "mesh.services.%s.workers", name);
            struct config_node_ptr_result workers_res = config_get(picomesh_engine_config(engine), path);
            PICOMESH_RETURN_IF_ERR(picomesh_int, workers_res, "resolve_workers: service workers config read failed");
            if (workers_res.value) worker_count = config_node_as_int(workers_res.value, -1);
        }
        if (worker_count <= 0) {
            struct config_node_ptr_result workers_res =
                config_get(picomesh_engine_config(engine), "workers");
            PICOMESH_RETURN_IF_ERR(picomesh_int, workers_res, "resolve_workers: workers config read failed");
            if (workers_res.value) worker_count = config_node_as_int(workers_res.value, -1);
        }
    }
    if (worker_count < 1) worker_count = 1;
    if (worker_count > 256) worker_count = 256;
    return PICOMESH_OK(picomesh_int, (int)worker_count);
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
static struct picomesh_void_result serve_worker_setup(struct picomesh_engine *engine,
                                                   int worker_index, void *ud)
{
    struct serve_setup *setup = ud;

    if (setup->name && *setup->name) {
        struct picomesh_size_result remotes_res = picomesh_engine_open_remotes(engine, setup->name);
        PICOMESH_RETURN_IF_ERR(picomesh_void, remotes_res, "serve_worker_setup: open remotes failed");
        yinfo("serve[%s w%d]: opened %zu remote(s) from mesh.services.%s.config.remotes",
              setup->name, worker_index, remotes_res.value, setup->name);
    }

    /* Config-driven per-node perf profiling (gh#14). Opens this worker's
     * counters on its own thread + loop when `perf.enabled` is set. A
     * permission/config failure is reported loudly but does NOT take the
     * service down — profiling must never be silently dropped, yet a host
     * without perf access shouldn't keep the mesh from coming up. */
    struct picomesh_void_result perf_res = picomesh_engine_perf_start(engine, setup->name);
    if (PICOMESH_IS_ERR(perf_res)) {
        /* Unconditional (ytrace_output, not the gated yerror): a requested-but-
         * refused profile must be loud even when tracing is off. yperf already
         * logged the precise failing event + errno just above. */
        ytrace_output("error", __FILE__, __LINE__, __func__,
                      "serve[%s w%d]: perf profiling could not start (see the preceding perf "
                      "error) — continuing without it",
                      setup->name ? setup->name : "?", worker_index);
        picomesh_error_destroy(perf_res.error);
    }

    if (strcmp(setup->frontend, "yhttp") == 0) {
        struct yhttp_config cfg = {.host = setup->host, .port = setup->port};
        struct yhttp_frontend_ptr_result frontend_res = yhttp_start(engine, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, frontend_res, "serve_worker_setup: yhttp_start failed");
    } else if (strcmp(setup->frontend, "yttp") == 0) {
        struct yttp_config cfg = {.host = setup->host, .port = setup->port};
        struct yttp_frontend_ptr_result frontend_res = yttp_start(engine, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, frontend_res, "serve_worker_setup: yttp_start failed");
    } else if (strcmp(setup->frontend, "alpine") == 0) {
        struct alpine_config cfg = {.host = setup->host, .port = setup->port};
        struct alpine_frontend_ptr_result frontend_res = alpine_start(engine, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, frontend_res, "serve_worker_setup: alpine_start failed");
    } else if (strcmp(setup->frontend, "msgpack") == 0) {
        struct msgpack_config cfg = {.host = setup->host, .port = setup->port};
        struct msgpack_frontend_ptr_result frontend_res = msgpack_start(engine, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, frontend_res, "serve_worker_setup: msgpack_start failed");
    } else {
        struct yrpc_config cfg = {.host = setup->host, .port = setup->port};
        struct yrpc_frontend_ptr_result frontend_res = yrpc_start(engine, &cfg);
        PICOMESH_RETURN_IF_ERR(picomesh_void, frontend_res, "serve_worker_setup: yrpc_start failed");
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result cmd_serve(struct picomesh_engine *engine)
{
    picomesh_active_engine_set(engine);

    const char *name = argv_get_string(picomesh_engine_cli(engine), "name", NULL);
    struct const_char_ptr_result host_res = resolve_host(engine);
    PICOMESH_RETURN_IF_ERR(picomesh_void, host_res, "cmd_serve: resolve host failed");
    const char *host = host_res.value;
    /* `port: auto` — allocate the listen port through portalloc (discovered
     * via the registry) before any worker binds. Otherwise the static port. */
    int port;
    struct picomesh_int_result is_auto = picomesh_serve_port_is_auto(engine, name);
    PICOMESH_RETURN_IF_ERR(picomesh_void, is_auto, "cmd_serve: port-auto check failed");
    if (is_auto.value) {
        struct picomesh_int_result alloc_res = picomesh_autoport_allocate(engine, name, host);
        PICOMESH_RETURN_IF_ERR(picomesh_void, alloc_res, "cmd_serve: port:auto allocation failed");
        port = alloc_res.value;
    } else {
        struct picomesh_int_result port_res = resolve_port(engine);
        PICOMESH_RETURN_IF_ERR(picomesh_void, port_res, "cmd_serve: resolve port failed");
        port = port_res.value;
    }
    /* Registration is an internal feature of every mesh node: announce
     * (name -> host:port) so `port: auto` consumers can discover us, then
     * discover the addresses of our own `port: auto` remotes. Both are no-ops
     * when no registry is configured. Register BEFORE resolving remotes so the
     * registration phase can never deadlock against a peer that is waiting on
     * us. */
    struct picomesh_void_result register_res = picomesh_autoport_register_self(engine, name, host, port);
    PICOMESH_RETURN_IF_ERR(picomesh_void, register_res, "cmd_serve: register self failed");
    struct picomesh_void_result remotes_res = picomesh_autoport_resolve_remotes(engine, name);
    PICOMESH_RETURN_IF_ERR(picomesh_void, remotes_res, "cmd_serve: resolve remotes failed");
    struct picomesh_int_result workers_res = resolve_workers(engine);
    PICOMESH_RETURN_IF_ERR(picomesh_void, workers_res, "cmd_serve: resolve workers failed");
    int workers = workers_res.value;
    const char *frontend = argv_get_string(picomesh_engine_cli(engine), "frontend", "yrpc");
    if (!frontend) frontend = "yrpc";

    /* Reject an unknown frontend up front, before spinning any worker. */
    if (strcmp(frontend, "yrpc") != 0 && strcmp(frontend, "yttp") != 0 &&
        strcmp(frontend, "yhttp") != 0 && strcmp(frontend, "alpine") != 0 &&
        strcmp(frontend, "msgpack") != 0) {
        fprintf(stderr, "unknown --frontend '%s' (try yrpc | yttp | yhttp | alpine | msgpack)\n",
                frontend);
        return PICOMESH_ERR(picomesh_void, "cmd_serve: unknown frontend");
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
    struct serve_setup setup = {.name = name, .host = host, .port = port, .frontend = frontend};
    struct picomesh_void_result run_res =
        picomesh_engine_run_workers(engine, (size_t)workers, serve_worker_setup, &setup);
    PICOMESH_RETURN_IF_ERR(picomesh_void, run_res, "cmd_serve: engine_run_workers failed");
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result cmd_client(struct picomesh_engine *engine)
{
    struct const_char_ptr_result host_res = resolve_host(engine);
    PICOMESH_RETURN_IF_ERR(picomesh_void, host_res, "cmd_client: resolve host failed");
    const char *host = host_res.value;
    struct picomesh_int_result port_res = resolve_port(engine);
    PICOMESH_RETURN_IF_ERR(picomesh_void, port_res, "cmd_client: resolve port failed");
    int port = port_res.value;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return PICOMESH_ERR(picomesh_void, "cmd_client: socket() failed"); }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "bad host: %s\n", host);
        close(fd);
        return PICOMESH_ERR(picomesh_void, "cmd_client: bad host");
    }
    addr.sin_port = htons((uint16_t)port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(fd); return PICOMESH_ERR(picomesh_void, "cmd_client: connect failed");
    }
    yinfo("picomesh client: connected to %s:%d", host, port);

    /* --transport msgpack: drive a FOREIGN msgpack service over the outbound
     * msgpack client path. The receiver object is built locally (no remote
     * CREATE — the msgpack envelope identifies the receiver by path), then the
     * call rides the msgpack peer. Proves picomesh C → foreign msgpack. */
    const char *transport = argv_get_string(picomesh_engine_cli(engine), "transport", NULL);
    if (transport && strcmp(transport, "msgpack") == 0) {
        struct peer_channel *msgpack_peer = peer_channel_create_msgpack(fd);
        if (!msgpack_peer) { close(fd); return PICOMESH_ERR(picomesh_void, "cmd_client: msgpack peer alloc failed"); }
        struct ctx local_ctx = {0};
        struct object_ptr_result create_res = calculator_calc_create(&local_ctx);
        if (PICOMESH_IS_ERR(create_res)) { peer_channel_destroy(msgpack_peer); return PICOMESH_ERR(picomesh_void, "cmd_client: calc_create", create_res); }
        struct object *calc = create_res.value;
        struct ctx msgpack_ctx = {.peer = msgpack_peer};
        struct picomesh_int64_result add_res = calculator_calc_add(&msgpack_ctx, calc, NULL, 6, 7);
        if (PICOMESH_IS_ERR(add_res)) { peer_channel_destroy(msgpack_peer); return PICOMESH_ERR(picomesh_void, "cmd_client: calc_add(msgpack)", add_res); }
        printf("msgpack client: 6 + 7 = %lld\n", (long long)add_res.value);
        struct picomesh_int64_result mul_res = calculator_calc_mul(&msgpack_ctx, calc, NULL, 6, 7);
        if (PICOMESH_IS_ERR(mul_res)) { peer_channel_destroy(msgpack_peer); return PICOMESH_ERR(picomesh_void, "cmd_client: calc_mul(msgpack)", mul_res); }
        printf("msgpack client: 6 * 7 = %lld\n", (long long)mul_res.value);
        object_release_in_ctx(&local_ctx, calc);
        peer_channel_destroy(msgpack_peer);
        return PICOMESH_OK_VOID();
    }

    struct peer_channel *channel = peer_channel_create(fd);
    if (!channel) { close(fd); return PICOMESH_ERR(picomesh_void, "cmd_client: peer channel alloc failed"); }
    struct ctx ctx = {.peer = channel};

    struct object_ptr_result create_res = calculator_calc_create(&ctx);
    if (PICOMESH_IS_ERR(create_res)) { peer_channel_destroy(channel); return PICOMESH_ERR(picomesh_void, "cmd_client: calc_create", create_res); }
    struct object *calc = create_res.value;
    struct picomesh_int64_result add_res = calculator_calc_add(&ctx, calc, NULL, 6, 7);
    if (PICOMESH_IS_ERR(add_res)) { peer_channel_destroy(channel); return PICOMESH_ERR(picomesh_void, "cmd_client: calc_add", add_res); }
    yinfo("client: 6 + 7 = %lld", (long long)add_res.value);
    struct picomesh_int64_result mul_res = calculator_calc_mul(&ctx, calc, NULL, 6, 7);
    if (PICOMESH_IS_ERR(mul_res)) { peer_channel_destroy(channel); return PICOMESH_ERR(picomesh_void, "cmd_client: calc_mul", mul_res); }
    yinfo("client: 6 * 7 = %lld", (long long)mul_res.value);
    object_release_in_ctx(&ctx, calc); /* remote proxy is cached on the channel */

    peer_channel_destroy(channel);
    return PICOMESH_OK_VOID();
}

static int cmd_config_dump(struct picomesh_engine *engine)
{
    char buf[16384];
    size_t len = config_dump(picomesh_engine_config(engine), buf, sizeof(buf));
    (void)len;
    printf("%s\n", buf);
    return 0;
}

/* Fatal-signal crash handler: when the process dies on SIGSEGV/SIGABRT/etc.,
 * dump the signal + a raw backtrace to stderr BEFORE exiting. In the riscv VM
 * the mesh's stderr is wired to /dev/console (runsv → init), so a silent crash
 * — the "gateway :8080 unreachable" the webapp then reports — now leaves a
 * backtrace in the boot log instead of vanishing. backtrace_symbols_fd is the
 * async-signal-safe dumper (it does not malloc); the binary is built
 * unstripped, so frames resolve (else feed the addresses to addr2line). */
PICOMESH_EXTERNAL_CALLBACK
static void main_fatal_signal(int sig)
{
    static const char head[] = "\n*** picomesh FATAL: caught signal ";
    write(STDERR_FILENO, head, sizeof(head) - 1);
    const char *name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" :
                       sig == SIGBUS  ? "SIGBUS"  : sig == SIGILL  ? "SIGILL"  :
                       sig == SIGFPE  ? "SIGFPE"  : "?";
    write(STDERR_FILENO, name, strlen(name));
    write(STDERR_FILENO, " — backtrace follows ***\n", 25);
    void *frames[64];
    int frame_count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, frame_count, STDERR_FILENO);
    /* Restore the default disposition and re-raise so the real exit status /
     * core dump is preserved (and runsv then restarts the service). */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void main_install_crash_handler(void)
{
    /* Warm backtrace()'s unwinder once now (its first call may allocate), so
     * the in-handler call stays allocation-free. */
    void *warm[1];
    (void)backtrace(warm, 1);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = main_fatal_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
}

int main(int argc, char **argv)
{
    ytrace_init();
    main_install_crash_handler();

    struct argv_chain_ptr_result parse_res = argv_parse(PICOMESH_OPTIONS, PICOMESH_OPTION_COUNT, argc, argv);
    if (PICOMESH_IS_ERR(parse_res)) return die_err("argv_parse", parse_res.error);
    struct argv_chain *cli = parse_res.value;

    if (argv_get_bool(cli, "help", 0)) { usage(stdout, argv[0]); argv_chain_destroy(cli); return 0; }
    if (argv_get_bool(cli, "verbose", 0)) {
        ytrace_set_all_enabled(1);
    }

    const char *sub = argv_subcommand(cli);
    if (!sub) { usage(stderr, argv[0]); argv_chain_destroy(cli); return 2; }

    struct picomesh_engine_args engine_args = {
        .cli = cli,                    /* engine takes ownership */
        .app_name = argv_get_string(cli, "app_name", "picomesh"),
    };
    struct picomesh_engine_ptr_result engine_res = picomesh_engine_create(&engine_args);
    if (PICOMESH_IS_ERR(engine_res)) return die_err("engine_create", engine_res.error);
    struct picomesh_engine *engine = engine_res.value;

    /* Registration == activation. Done here on the main thread, before any
     * subcommand (and before serve spawns workers): only the plugins this
     * instance activated via config enter the registry; everything else
     * compiled in stays unreachable. */
    picomesh_active_engine_set(engine);
    struct picomesh_void_result activate_res = activate_plugins(engine);
    if (PICOMESH_IS_ERR(activate_res))
        return die_err("activate_plugins", activate_res.error);

    int rc;
    if (strcmp(sub, "serve") == 0) {
        struct picomesh_void_result serve_res = cmd_serve(engine);
        rc = PICOMESH_IS_ERR(serve_res) ? die_err("serve", serve_res.error) : 0;
    } else if (strcmp(sub, "client") == 0) {
        struct picomesh_void_result client_res = cmd_client(engine);
        rc = PICOMESH_IS_ERR(client_res) ? die_err("client", client_res.error) : 0;
    } else if (strcmp(sub, "config-dump") == 0) {
        rc = cmd_config_dump(engine);
    } else if (strcmp(sub, "invoke") == 0) {
        struct picomesh_int_result cli_res = picomesh_cli_dispatch(engine);
        rc = PICOMESH_IS_ERR(cli_res) ? die_err("invoke", cli_res.error) : cli_res.value;
    } else {
        usage(stderr, argv[0]);
        rc = 2;
    }

    picomesh_engine_destroy(engine);
    return rc;
}
