/* yrpc — binary RPC frontend.
 *
 * Wraps loop_listen_tcp + rpc_dispatch_one into a frontend that an
 * engine driver can configure. One coroutine per peer; uses the
 * packed-header wire format documented in <picomesh/picoclass/rpc.h>.
 *
 * Equivalent of yaapp's `yttp` plugin in role (transport-only,
 * delegates dispatch to the engine), but the wire is picomesh's native
 * binary format rather than JSON-RPC over a Content-Length frame.
 * The JSON variant is a follow-up frontend. */

#ifndef PICOMESH_FRONTENDS_YRPC_YRPC_H
#define PICOMESH_FRONTENDS_YRPC_YRPC_H

#include <picomesh/core/result.h>

struct picomesh_engine;
struct yrpc_frontend;

PICOMESH_RESULT_DECLARE(yrpc_frontend_ptr, struct yrpc_frontend *);

struct yrpc_config {
    const char *host; /* default "127.0.0.1" */
    int port;         /* default 7777 */
};

struct yrpc_frontend_ptr_result yrpc_start(struct picomesh_engine *e,
                                           const struct yrpc_config *cfg);
void yrpc_stop(struct yrpc_frontend *f);

#endif /* PICOMESH_FRONTENDS_YRPC_YRPC_H */
