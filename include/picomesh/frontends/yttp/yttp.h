/* yttp — JSON-RPC 2.0 frontend, Content-Length-framed.
 *
 * Port of yaapp's `frontends/yttp/plugin.py`. Two transports:
 *
 *   - TCP listener (this initial cut). One coroutine per peer.
 *   - stdio (planned).
 *
 * Wire framing (LSP / MCP style):
 *
 *     Content-Length: <N>\r\n
 *     \r\n
 *     <body>
 *
 * Body shape (JSON-RPC 2.0):
 *
 *     { "jsonrpc":"2.0", "id":<scalar>, "method":"<name>",
 *       "params":{...} }
 *
 * Methods (same set as the binary `rpc.gen.c` emits, but JSON-named):
 *
 *     create    {"class": "<plugin>_<class>"}            -> {"handle": u64}
 *     invoke    {"method": "<plugin>_<class>_<verb>",
 *                "handle": <u64>, "args": [...]}        -> {"result": <value>}
 *     describe  {"class": "<plugin>_<class>"}            -> {"methods": [...]}
 *
 * Lookup of `invoke` goes through `jinvoke_for(method)`, which
 * codegen wires up via per-module hooks. */

#ifndef PICOMESH_FRONTENDS_YTTP_YTTP_H
#define PICOMESH_FRONTENDS_YTTP_YTTP_H

#include <picomesh/core/result.h>

struct picomesh_engine;
struct yttp_frontend;

PICOMESH_RESULT_DECLARE(yttp_frontend_ptr, struct yttp_frontend *);

struct yttp_config {
    const char *host; /* default "127.0.0.1" */
    int port;         /* default 8800 */
};

struct yttp_frontend_ptr_result yttp_start(struct picomesh_engine *e,
                                           const struct yttp_config *cfg);
void yttp_stop(struct yttp_frontend *f);

#endif /* PICOMESH_FRONTENDS_YTTP_YTTP_H */
