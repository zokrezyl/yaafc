/* msgpack — MessagePack RPC frontend.
 *
 * A sibling transport to yrpc that speaks the Picomesh MessagePack envelope
 * so clients in any language can call services without binding to the C
 * wire. Strict serial request/response (no multiplexing in v1). Each request
 * is a length-prefixed msgpack map:
 *
 *   u32 frame_len (big-endian) | msgpack({
 *       "v": 1, "op": "invoke"|"describe", "path": "service.class.method",
 *       "args": [...], "kwargs": {}, "headers": { "uid":N, "sid":N, ... } })
 *
 * The response is the same framing wrapping:
 *
 *   { "v":1, "ok":true,  "result": <value> }
 *   { "v":1, "ok":false, "error": { "message": "...", "code": "..." } }
 *
 * Dispatch goes through the shared active-service resolver (the gate) and the
 * codegen-emitted minvoke table — never a global method table directly. The
 * native binary yrpc path is untouched and remains the default. */

#ifndef PICOMESH_FRONTENDS_MSGPACK_MSGPACK_H
#define PICOMESH_FRONTENDS_MSGPACK_MSGPACK_H

#include <picomesh/core/result.h>

struct picomesh_engine;
struct msgpack_frontend;

PICOMESH_RESULT_DECLARE(msgpack_frontend_ptr, struct msgpack_frontend *);

struct msgpack_config {
    const char *host; /* default "127.0.0.1" */
    int port;         /* default 7900 */
};

struct msgpack_frontend_ptr_result msgpack_start(struct picomesh_engine *e,
                                                 const struct msgpack_config *cfg);
void msgpack_stop(struct msgpack_frontend *f);

#endif /* PICOMESH_FRONTENDS_MSGPACK_MSGPACK_H */
