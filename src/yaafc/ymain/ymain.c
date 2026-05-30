/* yaafc — driver binary.
 *
 * Parses argv via yargv (yaapp-style command-into-command CLI),
 * builds the engine (which loads yconfig with full precedence), then
 * dispatches to a subcommand:
 *
 *   yaafc [--config-file PATH] [--config K=V]... [--env K=V]...
 *         [--host H] [--port P] [--verbose]
 *         (serve | client)
 *
 * `serve` starts the yrpc frontend.
 * `client` runs the built-in smoke against a running server.
 * Plugin classes self-register at load via codegen constructor hooks. */

#include <yaafc/plugin/storage/storage.h>
#include <yaafc/plugin/calculator/calculator.h>
#include <yaafc/plugin/time/time.h>
#include <yaafc/plugin/accounts/accounts.h>

#include <yaafc/yengine/engine.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/yloop/yloop.h>
#include <yaafc/yconfig/yconfig.h>
#include <yaafc/yargv/yargv.h>
#include <yaafc/frontends/yrpc/yrpc.h>
#include <yaafc/frontends/yttp/yttp.h>
#include <yaafc/frontends/yhttp/yhttp.h>
#include <yaafc/frontends/cli/cli.h>
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const struct yargv_option_def YAAFC_OPTIONS[] = {
    {"--help",        "-h", "help",        "show help",                       YARGV_BOOL,      0},
    {"--verbose",     "-v", "verbose",     "enable trace output",             YARGV_BOOL,      0},
    {"--config-file", "-f", "config_file", "explicit config file",            YARGV_VALUE,     0},
    {"--config",      NULL, "config",      "config override (dotted=value)",  YARGV_KEY_VALUE, 1},
    {"--env",         "-e", "env",         "environment variable (K=V)",      YARGV_KEY_VALUE, 1},
    {"--host",        NULL, "host",        "bind/connect host",               YARGV_VALUE,     0},
    {"--port",        "-p", "port",        "bind/connect port",               YARGV_VALUE,     0},
    {"--app-name",    NULL, "app_name",    "app name (drives XDG path)",      YARGV_VALUE,     0},
    {"--name",        NULL, "name",        "instance name",                   YARGV_VALUE,     0},
    {"--frontend",    NULL, "frontend",    "frontend: yrpc (default) or yttp",YARGV_VALUE,     0},
    {"--workers",     NULL, "workers",     "in-process worker threads (default 1)", YARGV_VALUE, 0},
    {"--plugins",     NULL, "plugins",     "comma-sep plugin list (yaapp compat)", YARGV_VALUE, 0},
};
#define YAAFC_OPTION_COUNT (sizeof(YAAFC_OPTIONS) / sizeof(YAAFC_OPTIONS[0]))

static int die_err(const char *what, struct yaafc_error e)
{
    yaafc_error_print(stderr, what, e);
    yaafc_error_destroy(e);
    return 1;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage:\n"
            "  %s [--config-file PATH] [--config K=V]... [--env K=V]...\n"
            "      [--host H] [--port P] [--frontend yrpc|yttp]\n"
            "      [--verbose] [--app-name N]\n"
            "      (serve | client | config-dump | invoke <method> [arg...])\n",
            argv0);
}

/* Port / host resolution. Precedence, highest first:
 *
 *   1. --port / --host on the CLI
 *   2. `mesh.services.<--name>.port` / `.host` in the YAML — the
 *      "I'm the X service" path, used by both mesh-spawned children
 *      and standalone single-service invocations like
 *         yaafc --config-file foo.yaml --name accounts serve
 *   3. `yrpc.port` / `yrpc.host` (legacy config keys)
 *   4. defaults (127.0.0.1, 7777)
 */
static const char *resolve_host(struct yaafc_engine *e)
{
    const char *cli_host = yargv_get_string(yaafc_engine_cli(e), "host", NULL);
    if (cli_host && *cli_host) return cli_host;

    const char *name = yargv_get_string(yaafc_engine_cli(e), "name", NULL);
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.host", name);
        struct yconfig_node_ptr_result r = yconfig_get(yaafc_engine_config(e), path);
        if (YAAFC_IS_OK(r) && r.value) {
            const char *s = yconfig_node_as_string(r.value, NULL);
            if (s && *s) return s;
        }
    }
    struct yconfig_node_ptr_result r = yconfig_get(yaafc_engine_config(e), "yrpc.host");
    if (YAAFC_IS_OK(r) && r.value) {
        const char *s = yconfig_node_as_string(r.value, NULL);
        if (s && *s) return s;
    }
    return "127.0.0.1";
}

static int resolve_port(struct yaafc_engine *e)
{
    int64_t port_cli = yargv_get_int(yaafc_engine_cli(e), "port", -1);
    if (port_cli > 0) return (int)port_cli;

    const char *name = yargv_get_string(yaafc_engine_cli(e), "name", NULL);
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.port", name);
        struct yconfig_node_ptr_result r = yconfig_get(yaafc_engine_config(e), path);
        if (YAAFC_IS_OK(r) && r.value) {
            int64_t p = yconfig_node_as_int(r.value, -1);
            if (p > 0) return (int)p;
        }
    }
    struct yconfig_node_ptr_result r = yconfig_get(yaafc_engine_config(e), "yrpc.port");
    if (YAAFC_IS_OK(r) && r.value) {
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
static int resolve_workers(struct yaafc_engine *e)
{
    int64_t n = -1;
    int64_t cli = yargv_get_int(yaafc_engine_cli(e), "workers", -1);
    if (cli > 0) {
        n = cli;
    } else {
        const char *name = yargv_get_string(yaafc_engine_cli(e), "name", NULL);
        if (name && *name) {
            char path[256];
            snprintf(path, sizeof(path), "mesh.services.%s.workers", name);
            struct yconfig_node_ptr_result r = yconfig_get(yaafc_engine_config(e), path);
            if (YAAFC_IS_OK(r) && r.value) n = yconfig_node_as_int(r.value, -1);
        }
        if (n <= 0) {
            struct yconfig_node_ptr_result r =
                yconfig_get(yaafc_engine_config(e), "workers");
            if (YAAFC_IS_OK(r) && r.value) n = yconfig_node_as_int(r.value, -1);
        }
    }
    if (n < 1) n = 1;
    if (n > 256) n = 256;
    return (int)n;
}

/* Inputs the per-worker setup callback needs. Lives on cmd_serve's stack
 * for the duration of yaafc_engine_run_workers (which blocks until every
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
static struct yaafc_void_result serve_worker_setup(struct yaafc_engine *e,
                                                   int worker_index, void *ud)
{
    struct serve_setup *ss = ud;

    if (ss->name && *ss->name) {
        size_t n = yaafc_engine_open_remotes(e, ss->name);
        yinfo("serve[%s w%d]: opened %zu remote(s) from mesh.services.%s.config.remotes",
              ss->name, worker_index, n, ss->name);
    }

    if (strcmp(ss->frontend, "yhttp") == 0) {
        struct yhttp_config cfg = {.host = ss->host, .port = ss->port};
        struct yhttp_frontend_ptr_result fr = yhttp_start(e, &cfg);
        YAAFC_RETURN_IF_ERR(yaafc_void, fr, "serve_worker_setup: yhttp_start failed");
    } else if (strcmp(ss->frontend, "yttp") == 0) {
        struct yttp_config cfg = {.host = ss->host, .port = ss->port};
        struct yttp_frontend_ptr_result fr = yttp_start(e, &cfg);
        YAAFC_RETURN_IF_ERR(yaafc_void, fr, "serve_worker_setup: yttp_start failed");
    } else {
        struct yrpc_config cfg = {.host = ss->host, .port = ss->port};
        struct yrpc_frontend_ptr_result fr = yrpc_start(e, &cfg);
        YAAFC_RETURN_IF_ERR(yaafc_void, fr, "serve_worker_setup: yrpc_start failed");
    }
    return YAAFC_OK_VOID();
}

static int cmd_serve(struct yaafc_engine *e)
{
    yaafc_active_engine_set(e);

    const char *name = yargv_get_string(yaafc_engine_cli(e), "name", NULL);
    const char *host = resolve_host(e);
    int port = resolve_port(e);
    int workers = resolve_workers(e);
    const char *frontend = yargv_get_string(yaafc_engine_cli(e), "frontend", "yrpc");
    if (!frontend) frontend = "yrpc";

    /* Reject an unknown frontend up front, before spinning any worker. */
    if (strcmp(frontend, "yrpc") != 0 && strcmp(frontend, "yttp") != 0 &&
        strcmp(frontend, "yhttp") != 0) {
        fprintf(stderr, "unknown --frontend '%s' (try yrpc | yttp | yhttp)\n", frontend);
        return 2;
    }

    yinfo("yaafc serve [%s]: %s:%d (%d worker%s)",
          frontend, host, port, workers, workers == 1 ? "" : "s");

    /* One process, N worker threads: each runs serve_worker_setup on its
     * own loop (opening this service's remotes + installing the frontend
     * listener), then runs that loop. With workers == 1 this is exactly
     * the old single-threaded path (worker 0 on the main thread). The
     * `--name` service resolution drives every per-service config lookup
     * the workers do, exactly as before — only now it happens per worker
     * so each holds its own backend connection set. */
    struct serve_setup ss = {.name = name, .host = host, .port = port, .frontend = frontend};
    struct yaafc_void_result rr =
        yaafc_engine_run_workers(e, (size_t)workers, serve_worker_setup, &ss);
    if (YAAFC_IS_ERR(rr)) return die_err("engine_run_workers", rr.error);
    return 0;
}

static int cmd_client(struct yaafc_engine *e)
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
    yinfo("yaafc client: connected to %s:%d", host, port);
    struct peer_channel *s = peer_channel_create(fd);
    if (!s) { close(fd); return 1; }
    struct ctx ctx = {.peer = s};

    struct object_ptr_result kv_r = storage_kv_create(&ctx);
    if (YAAFC_IS_ERR(kv_r)) { peer_channel_destroy(s); return die_err("kv_create", kv_r.error); }
    struct object *kv = kv_r.value;
    for (uint32_t k = 1; k <= 3; ++k) {
        char key[16], val[16];
        snprintf(key, sizeof(key), "k%u", k);
        snprintf(val, sizeof(val), "%u", k * 10);
        struct yaafc_int_result sr = storage_kv_set(&ctx, kv, NULL, key, val);
        if (YAAFC_IS_ERR(sr)) { peer_channel_destroy(s); return die_err("kv_set", sr.error); }
    }
    for (uint32_t k = 1; k <= 3; ++k) {
        char key[16];
        snprintf(key, sizeof(key), "k%u", k);
        struct yaafc_string_result gr = storage_kv_get(&ctx, kv, NULL, key);
        if (YAAFC_IS_ERR(gr)) { peer_channel_destroy(s); return die_err("kv_get", gr.error); }
        yinfo("client: kv[%s] = %s", key, gr.value ? gr.value : "(null)");
        free(gr.value);
    }
    object_release_in_ctx(&ctx, kv); /* remote proxy is cached on the channel */

    struct object_ptr_result cr = calculator_calc_create(&ctx);
    if (YAAFC_IS_ERR(cr)) { peer_channel_destroy(s); return die_err("calc_create", cr.error); }
    struct object *calc = cr.value;
    struct yaafc_int64_result ar = calculator_calc_add(&ctx, calc, NULL, 6, 7);
    if (YAAFC_IS_ERR(ar)) { peer_channel_destroy(s); return die_err("calc_add", ar.error); }
    yinfo("client: 6 + 7 = %lld", (long long)ar.value);
    struct yaafc_int64_result mr = calculator_calc_mul(&ctx, calc, NULL, 6, 7);
    if (YAAFC_IS_ERR(mr)) { peer_channel_destroy(s); return die_err("calc_mul", mr.error); }
    yinfo("client: 6 * 7 = %lld", (long long)mr.value);
    object_release_in_ctx(&ctx, calc); /* remote proxy is cached on the channel */

    peer_channel_destroy(s);
    return 0;
}

static int cmd_config_dump(struct yaafc_engine *e)
{
    char buf[16384];
    size_t n = yconfig_dump(yaafc_engine_config(e), buf, sizeof(buf));
    (void)n;
    printf("%s\n", buf);
    return 0;
}

int main(int argc, char **argv)
{
    ytrace_init();

    struct yargv_chain_ptr_result pr = yargv_parse(YAAFC_OPTIONS, YAAFC_OPTION_COUNT, argc, argv);
    if (YAAFC_IS_ERR(pr)) return die_err("yargv_parse", pr.error);
    struct yargv_chain *cli = pr.value;

    if (yargv_get_bool(cli, "help", 0)) { usage(argv[0]); yargv_chain_destroy(cli); return 0; }
    if (yargv_get_bool(cli, "verbose", 0)) {
        ytrace_set_all_enabled(1);
    }

    const char *sub = yargv_subcommand(cli);
    if (!sub) { usage(argv[0]); yargv_chain_destroy(cli); return 2; }

    struct yaafc_engine_args ea = {
        .cli = cli,                    /* engine takes ownership */
        .app_name = yargv_get_string(cli, "app_name", "yaafc"),
    };
    struct yaafc_engine_ptr_result er = yaafc_engine_create(&ea);
    if (YAAFC_IS_ERR(er)) return die_err("engine_create", er.error);
    struct yaafc_engine *e = er.value;

    int rc;
    if (strcmp(sub, "serve") == 0) {
        rc = cmd_serve(e);
    } else if (strcmp(sub, "client") == 0) {
        rc = cmd_client(e);
    } else if (strcmp(sub, "config-dump") == 0) {
        rc = cmd_config_dump(e);
    } else if (strcmp(sub, "invoke") == 0) {
        rc = yaafc_cli_dispatch(e);
    } else {
        usage(argv[0]);
        rc = 2;
    }

    yaafc_engine_destroy(e);
    return rc;
}
