/* jinvoke — JSON-aware method invocation.
 *
 * Parallel to `rpc_skel_fn` (binary wire dispatch). Each annotated
 * method gets a JSON invoker emitted by codegen alongside its binary
 * skel: the invoker reads positional args from a `json_value` array
 * and writes the result into a `json_writer`.
 *
 * The invoker forwards the caller's `struct ctx` straight into the
 * public stub, so the SAME entry point serves two callers:
 *   - JSON frontends that own the object locally (yttp / cli) pass a
 *     zeroed/NULL ctx → the stub dispatches in-process.
 *   - the gateway, which has no local plugin objects, passes a ctx
 *     whose `session` points at the owning backend → the stub packs
 *     the args to the binary wire and forwards over yrpc.
 *
 * The binary RPC layer keeps using `rpc_skel_fn`. */

#ifndef PICOMESH_PICOCLASS_JINVOKE_H
#define PICOMESH_PICOCLASS_JINVOKE_H

#include <picomesh/core/result.h>
#include <picomesh/json/json.h>

#ifdef __cplusplus
extern "C" {
#endif

struct object;
struct ctx;
struct yheaders;

/* Invoker signature. `ctx` is the framework dispatch context (NULL/
 * zeroed → local; live `session` → forward to the owning backend).
 * `hdrs` is the request-header bag passed straight through to the stub
 * (the dispatch layer populates it from the envelope). `args` is the
 * positional JSON args array; `obj` is the target instance (local or a
 * remote proxy). On success the invoker pushes ONE value into `result`
 * and returns OK; on failure it writes a diagnostic into `err_msg`
 * (NUL-terminated, of `err_cap` bytes) and returns the error Result,
 * carrying the upstream cause chain. */
typedef struct picomesh_void_result (*jinvoke_fn)(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    const struct json_value *args, struct json_writer *result, char *err_msg,
    size_t err_cap);

typedef jinvoke_fn (*jinvoke_lookup_fn)(const char *qname);
void jinvoke_add_lookup(jinvoke_lookup_fn fn);
jinvoke_fn jinvoke_for(const char *qname);

/* ---- runtime call-signature reflection -------------------------------
 *
 * Codegen bakes each method's USER parameters (the args after the
 * framework ctx/obj/hdrs) into the binary, so `/_describe` can expose the
 * call signature at RUNTIME — no build-time model.yaml in the deployment
 * image. The args stay positional on the wire; this just names/types them
 * in declared order so a generic console can render one field per param. */
struct jinvoke_param {
  const char *name; /* parameter name, e.g. "key" */
  const char *type; /* C type spelling, e.g. "const char *" */
};
struct jinvoke_params {
  const struct jinvoke_param *items;
  size_t count;
};
typedef const struct jinvoke_params *(*jinvoke_params_lookup_fn)(
    const char *qname);
void jinvoke_params_add_lookup(jinvoke_params_lookup_fn fn);
/* NULL when the method qname is unknown; a row with count==0 means the
 * method takes no user parameters (codegen always emits a row). */
const struct jinvoke_params *jinvoke_params_for(const char *qname);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_PICOCLASS_JINVOKE_H */
