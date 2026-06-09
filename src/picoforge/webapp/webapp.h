/* Private internal header for the standalone picoforge-webapp binary.
 * NOT installed; nothing outside src/picoforge/webapp/ includes this. */
#ifndef PICOFORGE_WEBAPP_INTERNAL_H
#define PICOFORGE_WEBAPP_INTERNAL_H

#include <picomesh/core/result.h>

struct loop;

struct webapp_config {
    const char *gateway_url;    /* e.g. "http://127.0.0.1:8090" */
    const char *templates_dir;  /* may be NULL → no templates */
    const char *static_dir;     /* may be NULL → no static files */
    const char *console_url;    /* generic /_alpine service console URL, shown
                                 * on /admin/services; NULL/"" → no link */
    const char *github_client_id; /* GitHub OAuth App client id; NULL/"" hides button */
    const char *github_url;       /* GitHub web URL, default https://github.com */
    const char *public_url;       /* externally visible webapp URL for OAuth callbacks */
};

/* Start the HTTP listener on host:port. Spawns serve coroutines via
 * loop_listen_tcp. Does NOT block — returns after the listener is
 * bound; the caller runs loop_run() to actually serve. */
struct picomesh_void_result webapp_start(struct loop *loop,
                                         const char *host, int port,
                                         const struct webapp_config *cfg);

#endif
