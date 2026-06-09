/* alpine — generic service-console frontend (gh#15).
 *
 * A standalone inbound frontend, selected with `frontend: alpine`, that
 * serves the generic `/_alpine` service console (static HTML/JS) and
 * proxies the console's JSON calls (`GET|POST /_describe`,
 * `GET|POST /<path>/_describe`, `POST /_rpc`) to a configured upstream
 * yhttp endpoint — a yrpc->yhttp bridge or the gateway.
 *
 * It is deliberately NEITHER the transport bridge NOR the picoforge
 * webapp: it knows nothing about any plugin, route, or page. It builds
 * its whole UI from `/_describe` and invokes through JSON `/_rpc`, so it
 * works against any yhttp-compatible endpoint.
 *
 * Config (the node's projected `config.alpine`, read at top level after
 * the engine's service projection):
 *
 *   alpine:
 *     upstream:
 *       host: 127.0.0.1   # default 127.0.0.1
 *       port: 9090        # REQUIRED — the yhttp endpoint to proxy to
 *     token: "<secret>"   # optional; when set, every request must carry it
 *                         # (Authorization: Bearer <token> or ?token=<token>)
 *
 * Access control is explicit: the console can reach every service the
 * upstream exposes, so it binds 127.0.0.1 by default and (optionally)
 * gates on a shared token. */

#ifndef PICOMESH_FRONTENDS_ALPINE_ALPINE_H
#define PICOMESH_FRONTENDS_ALPINE_ALPINE_H

#include <picomesh/core/result.h>

struct picomesh_engine;
struct alpine_frontend;

PICOMESH_RESULT_DECLARE(alpine_frontend_ptr, struct alpine_frontend *);

struct alpine_config {
    const char *host; /* bind host, default 127.0.0.1 */
    int port;         /* bind port, default 8231 */
};

struct alpine_frontend_ptr_result alpine_start(struct picomesh_engine *e,
                                               const struct alpine_config *cfg);
void alpine_stop(struct alpine_frontend *f);

#endif /* PICOMESH_FRONTENDS_ALPINE_ALPINE_H */
