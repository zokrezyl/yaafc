/* minvoke — MessagePack-aware method invocation.
 *
 * The msgpack twin of `jinvoke_fn`. Each annotated method gets a msgpack
 * invoker emitted by codegen alongside its binary skel and JSON jinvoke: the
 * invoker reads positional args from a cmp reader and writes the result into
 * a cmp writer. Like jinvoke, it forwards the caller's `struct ctx` straight
 * into the public stub, so the same entry point dispatches locally (NULL/
 * zeroed ctx) or forwards to the owning backend (live ctx->peer).
 *
 * The binary RPC layer keeps using `rpc_skel_fn`; the JSON frontends keep
 * using `jinvoke_fn`; the msgpack frontend uses this. */

#ifndef PICOMESH_PICOCLASS_MINVOKE_H
#define PICOMESH_PICOCLASS_MINVOKE_H

#include <picomesh/core/result.h>
#include <picomesh/msgpack/msgpack.h> /* cmp_ctx_t */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ctx;
struct object;
struct yheaders;

/* Invoker signature. The msgpack frontend decodes the request envelope,
 * resolves the path through the active-service gate, then calls the matching
 * invoker with:
 *   - ctx/obj/hdrs: framework dispatch context, receiver, header bag (same
 *     as the binary skel / jinvoke).
 *   - args:        a cmp reader positioned AT THE FIRST element of the
 *     decoded `args` array; the invoker reads exactly `args_count` positional
 *     values from it (with per-type width/range validation).
 *   - args_count:  the array length the frontend already read.
 *   - result:      a cmp writer; on success the invoker writes EXACTLY ONE
 *     msgpack value (the method's return), which the frontend wraps as the
 *     envelope `result`.
 * On failure the invoker writes a diagnostic into `err_msg` (NUL-terminated,
 * of `err_cap` bytes), writes nothing to `result`, and returns the error
 * Result carrying the upstream cause chain. */
typedef struct picomesh_void_result (*minvoke_fn)(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *args, uint32_t args_count,
                          cmp_ctx_t *result, char *err_msg, size_t err_cap);

typedef minvoke_fn (*minvoke_lookup_fn)(const char *qname);
void minvoke_add_lookup(minvoke_lookup_fn fn);
minvoke_fn minvoke_for(const char *qname);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_PICOCLASS_MINVOKE_H */
