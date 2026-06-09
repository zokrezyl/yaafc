/* GENERATED — do not edit. */
/* Public interface for plugin `trace_collector` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/trace_collector/. */
#ifndef PICOMESH_PLUGIN_TRACE_COLLECTOR_H
#define PICOMESH_PLUGIN_TRACE_COLLECTOR_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_string_result;
struct picomesh_void_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result trace_collector_trace_collector_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result
trace_collector_trace_collector_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_void_result
trace_collector_trace_collector_ingest(struct ctx *ctx, struct object *obj,
                                       struct yheaders *hdrs,
                                       const char *span_json);
struct picomesh_string_result
trace_collector_trace_collector_get_trace(struct ctx *ctx, struct object *obj,
                                          struct yheaders *hdrs,
                                          const char *trace_id);
struct picomesh_string_result trace_collector_trace_collector_traces(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    const char *service, const char *status, uint32_t since_secs);
struct picomesh_string_result
trace_collector_trace_collector_services(struct ctx *ctx, struct object *obj,
                                         struct yheaders *hdrs);
struct picomesh_string_result
trace_collector_trace_collector_operations(struct ctx *ctx, struct object *obj,
                                           struct yheaders *hdrs,
                                           const char *service);
struct picomesh_string_result trace_collector_trace_collector_latency(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    const char *service, const char *operation, uint32_t window_secs);
struct picomesh_string_result
trace_collector_trace_collector_stats(struct ctx *ctx, struct object *obj,
                                      struct yheaders *hdrs);
struct picomesh_string_result
trace_collector_trace_collector_errors(struct ctx *ctx, struct object *obj,
                                       struct yheaders *hdrs,
                                       uint32_t since_secs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_trace_collector_register(void);

#endif
