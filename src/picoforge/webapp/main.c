/* picoforge-webapp — standalone HTML rendering process.
 *
 * Mirrors the yaapp git-yaapp page server (its scenario web tier):
 *   - own HTTP listener for browsers
 *   - every backend call leaves this process as `POST /_rpc` against
 *     `--gateway-url` (picomesh gateway, yaapp gateway — same contract)
 *   - no picomesh plugins linked in, no mesh, no picoclass dispatch — pure
 *     HTTP client + template renderer
 *
 * Auth: the `picomesh-sid` cookie value is forwarded to the gateway as a
 * `picomesh-sid` header on every outbound /_rpc call. Opaque token, no
 * JWT ever passes through.
 *
 * Usage:
 *   picoforge-webapp --gateway-url http://127.0.0.1:8090 \
 *                  --host 0.0.0.0 --port 8080 \
 *                  [--templates assets/picoforge/templates] \
 *                  [--static assets/picoforge/static]
 */

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/argv/argv.h>
#include <picomesh/loop/loop.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webapp.h"

static const struct argv_option_def WEBAPP_OPTIONS[] = {
    {"--gateway-url", NULL, "gateway_url", "Gateway base URL (required)",
     ARGV_VALUE, 0},
    {"--host",        NULL, "host",         "Bind address (default 0.0.0.0)",
     ARGV_VALUE, 0},
    {"--port",        NULL, "port",         "Bind port (default 8080)",
     ARGV_VALUE, 0},
    {"--templates",   NULL, "templates",    "Template directory (default ./templates)",
     ARGV_VALUE, 0},
    {"--static",      NULL, "static_dir",   "Static directory (default ./static)",
     ARGV_VALUE, 0},
    {"--console-url", NULL, "console_url",  "Service console URL linked on /admin/services "
                                            "(default http://127.0.0.1:8231/_alpine; \"\" to hide)",
     ARGV_VALUE, 0},
    {"--github-client", NULL, "github_client", "GitHub OAuth App client id (or $PICOFORGE_GITHUB_CLIENT)",
     ARGV_VALUE, 0},
    {"--github-url", NULL, "github_url", "GitHub web URL (default https://github.com)",
     ARGV_VALUE, 0},
    {"--public-url", NULL, "public_url", "Externally visible webapp URL for OAuth callbacks",
     ARGV_VALUE, 0},
    {"--verbose",     "-v", "verbose",      "Enable debug logging",
     ARGV_BOOL,  0},
    {"--help",        "-h", "help",         "Show this message",
     ARGV_BOOL,  0},
};
#define WEBAPP_OPTION_COUNT (sizeof(WEBAPP_OPTIONS) / sizeof(WEBAPP_OPTIONS[0]))

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s --gateway-url <url> [--host <h>] [--port <p>]\n"
        "          [--templates <dir>] [--static <dir>] [--console-url <url>] [-v]\n"
        "\n"
        "Standalone HTML web app. Talks to the gateway via POST /_rpc.\n"
        "Works against either the picomesh C gateway or the yaapp Python gateway —\n"
        "the wire shape is identical.\n",
        prog);
}

static int die(const char *what, const char *why)
{
    fprintf(stderr, "picoforge-webapp: %s: %s\n", what, why);
    return 1;
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    struct argv_chain_ptr_result pr =
        argv_parse(WEBAPP_OPTIONS, WEBAPP_OPTION_COUNT, argc, argv);
    if (PICOMESH_IS_ERR(pr)) {
        fprintf(stderr, "picoforge-webapp: argv parse: %s\n",
                pr.error.msg ? pr.error.msg : "?");
        picomesh_error_destroy(pr.error);
        return 2;
    }
    struct argv_chain *cli = pr.value;

    if (argv_get_bool(cli, "help", 0)) {
        usage(argv[0]);
        argv_chain_destroy(cli);
        return 0;
    }
    if (argv_get_bool(cli, "verbose", 0)) {
        ytrace_set_all_enabled(true);
    }

    const char *gateway_url = argv_get_string(cli, "gateway_url", NULL);
    if (!gateway_url || !*gateway_url) {
        usage(argv[0]);
        argv_chain_destroy(cli);
        return die("config", "--gateway-url is required");
    }
    const char *host = argv_get_string(cli, "host", "0.0.0.0");
    int port = (int)argv_get_int(cli, "port", 8080);
    const char *templates_dir = argv_get_string(cli, "templates", "templates");
    const char *static_dir    = argv_get_string(cli, "static_dir", "static");
    /* The generic /_alpine service console is a SEPARATE node the webapp does
     * not discover from the gateway; default to its conventional address so
     * the /admin/services link works out of the box. */
    const char *console_url   = argv_get_string(cli, "console_url",
                                                 "http://127.0.0.1:8231/_alpine");
    const char *github_client = argv_get_string(cli, "github_client", getenv("PICOFORGE_GITHUB_CLIENT"));
    const char *github_url    = argv_get_string(cli, "github_url", "https://github.com");
    const char *public_url    = argv_get_string(cli, "public_url", getenv("PICOFORGE_PUBLIC_URL"));

    struct loop_ptr_result lr = loop_create();
    if (PICOMESH_IS_ERR(lr)) {
        argv_chain_destroy(cli);
        return die("loop_create", lr.error.msg ? lr.error.msg : "?");
    }
    struct loop *loop = lr.value;

    struct webapp_config fc = {
        .gateway_url = gateway_url,
        .templates_dir = templates_dir,
        .static_dir = static_dir,
        .console_url = console_url,
        .github_client_id = github_client,
        .github_url = github_url,
        .public_url = public_url,
    };
    struct picomesh_void_result sr = webapp_start(loop, host, port, &fc);
    if (PICOMESH_IS_ERR(sr)) {
        fprintf(stderr, "picoforge-webapp: start: %s\n",
                sr.error.msg ? sr.error.msg : "?");
        picomesh_error_destroy(sr.error);
        loop_destroy(loop);
        argv_chain_destroy(cli);
        return 1;
    }

    yinfo("picoforge-webapp: listening on %s:%d (gateway=%s)",
          host, port, gateway_url);
    struct picomesh_void_result run_res = loop_run(loop);
    int run_failed = PICOMESH_IS_ERR(run_res);
    if (run_failed) {
        /* Process root: a failed event loop must be logged with its full cause
         * chain and surfaced as a non-zero exit, never mistaken for a clean
         * shutdown. */
        picomesh_error_print(stderr, "picoforge-webapp: loop_run", run_res.error);
        picomesh_error_destroy(run_res.error);
    }

    loop_destroy(loop);
    argv_chain_destroy(cli);
    return run_failed ? 1 : 0;
}
