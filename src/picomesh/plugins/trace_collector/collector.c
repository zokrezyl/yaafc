/* trace_collector — the receiver side of Picomesh tracing (issue #11).
 *
 * A normal yrpc backend plugin: services emit finished spans by calling
 * `trace_collector_ingest` over yrpc (fire-and-forget, from ytelemetry);
 * operators
 * and the webapp read traces back through the query methods via the
 * gateway's /_rpc + /_describe — service-driven, no hand-rolled routes.
 *
 * State is an in-memory, bounded span store (core/ytelemetry_store). No
 * durable storage in v1. This process holds no other plugins and reaches
 * no backends. */

#include <stdio.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/core/ytelemetry_store.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/config/config.h>
#include <picomesh/engine/engine.h>
#include <picomesh/json/json.h>
#include <picomesh/platform/time.h>

#include <stdlib.h>
#include <string.h>

struct PICOMESH_CLASS_ANNOTATE("class@trace_collector:trace_collector") trace_collector_trace_collector_data {
    char placeholder;
};

static uint64_t collector_now_ns(void)
{
    return (uint64_t)picomesh_platform_time_wall_ms() * 1000000ull;
}

/* Size the in-memory store from this process's config the first time the
 * collector is touched: telemetry.max_spans (ring size) + max_age_seconds
 * (retention). Idempotent (ytelemetry_store_init only honours the first
 * call); the `done` flag just skips the per-call config lookup. Safe across
 * the collector's worker threads — same value, idempotent init. */
static struct picomesh_int64_result collector_cfg_int(const struct config *cfg, const char *key)
{
    struct config_node_ptr_result config_res = config_get(cfg, key);
    PICOMESH_RETURN_IF_ERR(picomesh_int64, config_res, "collector_cfg_int: config read failed");
    return PICOMESH_OK(picomesh_int64, config_res.value ? config_node_as_int(config_res.value, 0) : 0);
}

static struct picomesh_void_result collector_ensure_store(void)
{
    static int done = 0;
    if (done) return PICOMESH_OK_VOID();
    struct ytelemetry_store_config store_cfg = {0};
    struct picomesh_engine *engine = picomesh_active_engine();
    if (engine) {
        const struct config *cfg = picomesh_engine_config(engine);
        struct picomesh_int64_result cfg_value;
        cfg_value = collector_cfg_int(cfg, "telemetry.max_spans");
        PICOMESH_RETURN_IF_ERR(picomesh_void, cfg_value, "collector: telemetry.max_spans");
        if (cfg_value.value > 0) store_cfg.max_spans = (size_t)cfg_value.value;
        cfg_value = collector_cfg_int(cfg, "telemetry.max_age_seconds");
        PICOMESH_RETURN_IF_ERR(picomesh_void, cfg_value, "collector: telemetry.max_age_seconds");
        if (cfg_value.value > 0) store_cfg.max_age_seconds = (uint64_t)cfg_value.value;
        cfg_value = collector_cfg_int(cfg, "telemetry.shards");
        PICOMESH_RETURN_IF_ERR(picomesh_void, cfg_value, "collector: telemetry.shards");
        if (cfg_value.value > 0) store_cfg.shards = (unsigned)cfg_value.value;
        cfg_value = collector_cfg_int(cfg, "telemetry.bucket_spans");
        PICOMESH_RETURN_IF_ERR(picomesh_void, cfg_value, "collector: telemetry.bucket_spans");
        if (cfg_value.value > 0) store_cfg.bucket_spans = (size_t)cfg_value.value;
        cfg_value = collector_cfg_int(cfg, "telemetry.flush_ms");
        PICOMESH_RETURN_IF_ERR(picomesh_void, cfg_value, "collector: telemetry.flush_ms");
        if (cfg_value.value > 0) store_cfg.flush_ms = (uint64_t)cfg_value.value;
    }
    ytelemetry_store_init_config(&store_cfg); /* any field left 0 takes its built-in default */
    done = 1;
    return PICOMESH_OK_VOID();
}

/* Render a query-writer's JSON into an owned heap string the caller frees. */
static struct picomesh_string_result render(void (*emit)(struct json_writer *, void *),
                                            void *user_data)
{
    /* config applies on first query too, not only ingest */
    struct picomesh_void_result store_res = collector_ensure_store();
    PICOMESH_RETURN_IF_ERR(picomesh_string, store_res, "trace_collector: store init failed");
    struct json_writer *writer = json_writer_new();
    if (!writer) return PICOMESH_ERR(picomesh_string, "trace_collector: writer alloc failed");
    emit(writer, user_data);
    size_t len = 0;
    const char *data = json_writer_data(writer, &len);
    char *out = malloc(len + 1);
    if (out) { memcpy(out, data, len); out[len] = 0; }
    json_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_string, "trace_collector: out of memory");
    return PICOMESH_OK(picomesh_string, out);
}

/* ---- ingest ---------------------------------------------------------- */

PICOMESH_CLASS_ANNOTATE("override@trace_collector:trace_collector:trace_collector_ingest")
struct picomesh_void_result trace_collector_trace_collector_ingest_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs, const char *span_json)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct picomesh_void_result store_res = collector_ensure_store();
    PICOMESH_RETURN_IF_ERR(picomesh_void, store_res, "trace_collector_ingest: store init failed");
    /* The store tallies malformed/evicted spans (queryable via store_stats),
     * so bad input is not invisible. Ingest is fire-and-forget at the wire,
     * so we still return OK regardless — the caller never waits on this.
     * The payload is a JSON array (the batched sender) or one span object
     * (back-compat); the batch entry point handles both and parses once. */
    if (span_json && *span_json)
        ytelemetry_store_ingest_batch_json(span_json, strlen(span_json));
    return PICOMESH_OK_VOID();
}

/* ---- queries --------------------------------------------------------- */

struct trace_arg { const char *a; const char *b; uint64_t n; };

static void emit_trace(struct json_writer *writer, void *user_data)
{
    ytelemetry_store_write_trace(writer, ((struct trace_arg *)user_data)->a);
}

PICOMESH_CLASS_ANNOTATE("override@trace_collector:trace_collector:trace_collector_get_trace")
struct picomesh_string_result trace_collector_trace_collector_get_trace_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs, const char *trace_id)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct trace_arg arg = {.a = trace_id};
    return render(emit_trace, &arg);
}

static void emit_traces(struct json_writer *writer, void *user_data)
{
    struct trace_arg *arg = user_data;
    ytelemetry_store_write_traces(writer, (arg->a && *arg->a) ? arg->a : NULL,
                                  arg->n,
                                  (arg->b && *arg->b) ? arg->b : NULL);
}

PICOMESH_CLASS_ANNOTATE("override@trace_collector:trace_collector:trace_collector_traces")
struct picomesh_string_result trace_collector_trace_collector_traces_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    const char *service, const char *status, uint32_t since_secs)
{
    (void)ctx; (void)obj; (void)hdrs;
    uint64_t floor = 0;
    if (since_secs) {
        uint64_t window = (uint64_t)since_secs * 1000000000ull;
        uint64_t now = collector_now_ns();
        floor = now > window ? now - window : 0;
    }
    struct trace_arg arg = {.a = service, .b = status, .n = floor};
    return render(emit_traces, &arg);
}

static void emit_services(struct json_writer *writer, void *user_data)
{
    (void)user_data;
    ytelemetry_store_write_services(writer);
}

PICOMESH_CLASS_ANNOTATE("override@trace_collector:trace_collector:trace_collector_services")
struct picomesh_string_result trace_collector_trace_collector_services_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj; (void)hdrs;
    return render(emit_services, NULL);
}

static void emit_operations(struct json_writer *writer, void *user_data)
{
    const char *service = ((struct trace_arg *)user_data)->a;
    ytelemetry_store_write_operations(writer, (service && *service) ? service : NULL);
}

PICOMESH_CLASS_ANNOTATE("override@trace_collector:trace_collector:trace_collector_operations")
struct picomesh_string_result trace_collector_trace_collector_operations_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs, const char *service)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct trace_arg arg = {.a = service};
    return render(emit_operations, &arg);
}

static void emit_latency(struct json_writer *writer, void *user_data)
{
    struct trace_arg *arg = user_data;
    ytelemetry_store_write_latency(writer, (arg->a && *arg->a) ? arg->a : NULL,
                                   (arg->b && *arg->b) ? arg->b : NULL, arg->n);
}

PICOMESH_CLASS_ANNOTATE("override@trace_collector:trace_collector:trace_collector_latency")
struct picomesh_string_result trace_collector_trace_collector_latency_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    const char *service, const char *operation, uint32_t window_secs)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct trace_arg arg = {.a = service, .b = operation,
                          .n = (uint64_t)window_secs * 1000000000ull};
    return render(emit_latency, &arg);
}

static void emit_stats(struct json_writer *writer, void *user_data)
{
    (void)user_data;
    ytelemetry_store_write_stats(writer);
}

PICOMESH_CLASS_ANNOTATE("override@trace_collector:trace_collector:trace_collector_stats")
struct picomesh_string_result trace_collector_trace_collector_stats_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct picomesh_void_result store_res = collector_ensure_store();
    PICOMESH_RETURN_IF_ERR(picomesh_string, store_res, "trace_collector_stats: store init failed");
    return render(emit_stats, NULL);
}

static void emit_errors(struct json_writer *writer, void *user_data)
{
    ytelemetry_store_write_errors(writer, ((struct trace_arg *)user_data)->n);
}

PICOMESH_CLASS_ANNOTATE("override@trace_collector:trace_collector:trace_collector_errors")
struct picomesh_string_result trace_collector_trace_collector_errors_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs, uint32_t since_secs)
{
    (void)ctx; (void)obj; (void)hdrs;
    uint64_t floor = 0;
    if (since_secs) {
        uint64_t window = (uint64_t)since_secs * 1000000000ull;
        uint64_t now = collector_now_ns();
        floor = now > window ? now - window : 0;
    }
    struct trace_arg arg = {.n = floor};
    return render(emit_errors, &arg);
}

#include "collector.gen.c"
