/* Shared active-service resolver.
 *
 * Every inbound service-path transport (the gateway's /_rpc, the msgpack
 * frontend) turns a dotted "service.class.method" string into a concrete
 * call through exactly this function, so the active-service gate is written
 * once. The gate binds the path's service segment to a service that is
 * actually active on this node before any class/method is resolved — a
 * transport never reaches the global method tables on its own. */

#include <picomesh/yengine/resolve.h>

#include <picomesh/yengine/engine.h>
#include <picomesh/yclass/jinvoke.h> /* jinvoke_params_for, jinvoke_for, jinvoke_fn */
#include <picomesh/yclass/rpc.h>     /* object_create_in_ctx / object_release_in_ctx */
#include <picomesh/yjson/yjson.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Split "service.class.method" (needs at least two dots) into the service
 * segment, the class qname (path up to the LAST dot, '.'→'_') and the method
 * qname (the whole path, '.'→'_' — the codegen's qualified slot / leaf-table
 * key). Returns false on a malformed or over-long path. Mirrors the gateway's
 * historical path parsing so existing dotted paths resolve identically. */
static bool split_path(const char *path,
                       char *service, size_t service_cap,
                       char *class_qname, size_t class_cap,
                       char *method_qname, size_t method_cap)
{
    if (!path)
        return false;
    const char *first = strchr(path, '.');
    if (!first)
        return false;
    const char *last = strrchr(path, '.');
    if (last == first)
        return false; /* need two dots */

    size_t service_len = (size_t)(first - path);
    size_t class_len = (size_t)(last - path);
    size_t method_len = strlen(path);
    if (service_len == 0 || service_len >= service_cap)
        return false;
    if (class_len >= class_cap)
        return false;
    if (method_len >= method_cap)
        return false;

    for (size_t i = 0; i < method_len; ++i) {
        char ch = path[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '.'))
            return false; /* keep junk out of class_by_name / the leaf tables */
    }

    memcpy(service, path, service_len);
    service[service_len] = 0;
    memcpy(class_qname, path, class_len);
    class_qname[class_len] = 0;
    memcpy(method_qname, path, method_len);
    method_qname[method_len] = 0;
    for (char *p = class_qname; *p; ++p)
        if (*p == '.')
            *p = '_';
    for (char *p = method_qname; *p; ++p)
        if (*p == '.')
            *p = '_';
    return true;
}

/* class_for_each cb: flips `found` when a registered class carries the
 * "<service>_" prefix. Registration == activation in this runtime, so a hit
 * proves the service is active (locally activated or proxied) in THIS
 * process. */
struct prefix_scan {
    const char *prefix;
    size_t prefix_len;
    bool found;
};

static void prefix_scan_cb(const struct class *cls, const char *qname, void *userdata)
{
    (void)cls;
    struct prefix_scan *scan = userdata;
    if (!scan->found && strncmp(qname, scan->prefix, scan->prefix_len) == 0)
        scan->found = true;
}

static bool service_is_active(struct picomesh_engine *engine, const char *service)
{
    /* A configured remote ⇒ active (reachable over a peer channel). */
    if (picomesh_engine_service_ctx(engine, service).peer != NULL)
        return true;
    /* Otherwise active iff this process registered at least one class for it. */
    char prefix[80];
    int prefix_len = snprintf(prefix, sizeof(prefix), "%s_", service);
    if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix))
        return false;
    struct prefix_scan scan = {.prefix = prefix, .prefix_len = (size_t)prefix_len, .found = false};
    class_for_each(prefix_scan_cb, &scan);
    return scan.found;
}

struct picomesh_service_call_result
picomesh_resolve_service_call(struct picomesh_engine *engine, const char *path)
{
    if (!engine)
        return PICOMESH_ERR(picomesh_service_call, "resolve: NULL engine");

    char service[64], class_qname[160], method_qname[192];
    if (!split_path(path, service, sizeof(service), class_qname, sizeof(class_qname),
                    method_qname, sizeof(method_qname)))
        return PICOMESH_ERR(picomesh_service_call,
                            "resolve: bad path (want service.class.method)");

    if (!service_is_active(engine, service))
        return PICOMESH_ERR(picomesh_service_call, "resolve: service not active on this node");

    struct ctx ctx = picomesh_engine_service_ctx(engine, service);
    struct object_ptr_result obj_r = object_create_in_ctx(&ctx, class_qname);
    if (PICOMESH_IS_ERR(obj_r))
        return PICOMESH_ERR(picomesh_service_call,
                            "resolve: no such class (or backend create failed)", obj_r);

    struct picomesh_service_call call = {.ctx = ctx, .obj = obj_r.value};
    snprintf(call.class_qname, sizeof(call.class_qname), "%s", class_qname);
    snprintf(call.method_qname, sizeof(call.method_qname), "%s", method_qname);
    call.params = jinvoke_params_for(method_qname);
    return PICOMESH_OK(picomesh_service_call, call);
}

void picomesh_service_call_release(struct picomesh_service_call *call)
{
    if (!call || !call->obj)
        return;
    object_release_in_ctx(&call->ctx, call->obj);
    call->obj = NULL;
}

struct picomesh_string_result
picomesh_engine_invoke_json(struct picomesh_engine *engine, const char *path,
                            const char *args_json, struct yheaders *hdrs)
{
    struct picomesh_service_call_result call_res = picomesh_resolve_service_call(engine, path);
    if (PICOMESH_IS_ERR(call_res))
        return PICOMESH_ERR(picomesh_string, "engine_invoke: resolve failed", call_res);
    struct picomesh_service_call call = call_res.value;

    jinvoke_fn invoke_fn = jinvoke_for(call.method_qname);
    if (!invoke_fn) {
        picomesh_service_call_release(&call);
        return PICOMESH_ERR(picomesh_string, "engine_invoke: no such method");
    }

    struct yjson_doc *args_doc = NULL;
    const struct yjson_value *args = NULL;
    if (args_json && *args_json) {
        args_doc = yjson_parse(args_json, strlen(args_json));
        if (!args_doc) {
            /* Non-empty but unparseable args must NOT silently become a
             * no-argument call — for a mutating method that would invoke the
             * wrong operation. Reject it as a client error. */
            picomesh_service_call_release(&call);
            return PICOMESH_ERR(picomesh_string, "engine_invoke: malformed args JSON");
        }
        args = yjson_doc_root(args_doc);
    }

    struct yjson_writer *writer = yjson_writer_new();
    if (!writer) {
        if (args_doc) yjson_doc_free(args_doc);
        picomesh_service_call_release(&call);
        return PICOMESH_ERR(picomesh_string, "engine_invoke: writer alloc failed");
    }
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "result");
    char err[8192] = {0};
    int rc = invoke_fn(&call.ctx, call.obj, hdrs, args, writer, err, sizeof(err));
    picomesh_service_call_release(&call);
    if (args_doc) yjson_doc_free(args_doc);
    if (rc != 0) {
        yjson_writer_free(writer);
        return PICOMESH_ERR(picomesh_string, err[0] ? strdup(err) : "engine_invoke: call failed");
    }
    yjson_writer_end_object(writer);
    size_t len = 0;
    const char *data = yjson_writer_data(writer, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_string, "engine_invoke: out of memory");
    return PICOMESH_OK(picomesh_string, out);
}
