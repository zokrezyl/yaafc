/* ytelemetry — span context + fire-and-forget span sender (see
 * ytelemetry.h). No threads, no shared queue, no mutex: each worker ships
 * its own spans on its own connection to the trace_collector plugin. */

#include <picomesh/core/ytelemetry.h>

#include <picomesh/core/yspan.h>
#include <picomesh/argv/argv.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/picoclass/yheaders.h>
#include <picomesh/engine/engine.h>
#include <picomesh/config/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

/* ---- small helpers ---------------------------------------------------- */

static void ytel_copystr(char *dst, size_t cap, const char *src)
{
    if (!cap) return;
    size_t i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static uint64_t ytel_mono_ns(void)
{
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t ytel_wall_ns(void)
{
    struct timespec ts = {0};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int ytel_hex_val(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int ytel_all_hex(const char *str, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (ytel_hex_val(str[i]) < 0) return 0;
    return 1;
}

static int ytel_all_zero(const char *str, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (str[i] != '0') return 0;
    return 1;
}

static int ytel_bytes_all_zero(const uint8_t *bytes, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (bytes[i]) return 0;
    return 1;
}

static void ytel_fill_random(uint8_t *bytes, size_t n)
{
    /* Prefer the kernel CSPRNG; retry partial reads and EINTR. */
    size_t off = 0;
    while (off < n) {
        ssize_t got = getrandom(bytes + off, n - off, 0);
        if (got > 0) { off += (size_t)got; continue; }
        if (got < 0 && errno == EINTR) continue;
        break;
    }
    if (off == n) return;
    /* Fallback for the remainder: trace/span ids are correlation ids, not
     * secrets, so a clock/pid-seeded PRNG is acceptable when getrandom is
     * unavailable. */
    uint64_t seed = ytel_mono_ns() ^ ((uint64_t)getpid() << 32) ^ (uint64_t)(off + 1);
    for (size_t i = off; i < n; ++i) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        bytes[i] = (uint8_t)(seed >> 33);
    }
}

static void ytel_to_hex(char *out, const uint8_t *bytes, size_t n)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = H[bytes[i] >> 4];
        out[2 * i + 1] = H[bytes[i] & 0xf];
    }
    out[2 * n] = 0;
}

void ytelemetry_mint_trace_id(char out[33])
{
    uint8_t bytes[16];
    ytel_fill_random(bytes, sizeof(bytes));
    if (ytel_bytes_all_zero(bytes, sizeof(bytes))) bytes[0] = 1; /* W3C: all-zero trace id invalid */
    ytel_to_hex(out, bytes, sizeof(bytes));
}

void ytelemetry_mint_span_id(char out[17])
{
    uint8_t bytes[8];
    ytel_fill_random(bytes, sizeof(bytes));
    if (ytel_bytes_all_zero(bytes, sizeof(bytes))) bytes[0] = 1; /* W3C: all-zero span id invalid */
    ytel_to_hex(out, bytes, sizeof(bytes));
}

/* ---- W3C traceparent -------------------------------------------------- */

int ytelemetry_traceparent_parse(const char *traceparent, char *trace_id_hex,
                                 char *span_id_hex, int *sampled)
{
    if (!traceparent) return 0;
    if (strlen(traceparent) < 55) return 0; /* "00-<32>-<16>-<2>" */
    if (traceparent[2] != '-' || traceparent[35] != '-' || traceparent[52] != '-') return 0;
    if (!ytel_all_hex(traceparent, 2)) return 0;
    if (!ytel_all_hex(traceparent + 3, 32)) return 0;
    if (!ytel_all_hex(traceparent + 36, 16)) return 0;
    if (!ytel_all_hex(traceparent + 53, 2)) return 0;
    if (ytel_all_zero(traceparent + 3, 32)) return 0;
    if (ytel_all_zero(traceparent + 36, 16)) return 0;

    memcpy(trace_id_hex, traceparent + 3, 32);
    trace_id_hex[32] = 0;
    memcpy(span_id_hex, traceparent + 36, 16);
    span_id_hex[16] = 0;
    if (sampled) {
        int flags = ytel_hex_val(traceparent[53]) * 16 + ytel_hex_val(traceparent[54]);
        *sampled = flags & 0x01;
    }
    return 1;
}

void ytelemetry_traceparent_format(char *buf, size_t cap, const char *trace_id_hex,
                                   const char *span_id_hex, int sampled)
{
    snprintf(buf, cap, "00-%s-%s-%02x", trace_id_hex, span_id_hex, sampled ? 1 : 0);
}

/* ---- trace context on yheaders --------------------------------------- */

/* Telemetry on/off, resolved ONCE per process and cached. Source order:
 *   1. env PICOMESH_TELEMETRY = off/0/false/no  → a global kill-switch (handy
 *      for benchmarking; inherited by every spawned child);
 *   2. config `telemetry.enabled` — the per-service ROOT config knob (after
 *      service projection it sits at the node's config root), default ON.
 * When OFF, every span op below is a single cached `if` and nothing is minted,
 * serialized, or shipped to the collector. */
static int ytel_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        int enabled = 1;
        const char *env = getenv("PICOMESH_TELEMETRY");
        if (env && (!strcasecmp(env, "off") || !strcasecmp(env, "false") ||
                    !strcasecmp(env, "no") || !strcmp(env, "0"))) {
            enabled = 0;
        } else {
            struct picomesh_engine *engine = picomesh_active_engine();
            const struct config *cfg = engine ? picomesh_engine_config(engine) : NULL;
            /* Default ON: absence is not an error, so the default-aware getter
             * returns the value directly. */
            enabled = config_get_bool(cfg, "telemetry.enabled", 1) ? 1 : 0;
        }
        cached = enabled;
    }
    return cached;
}

void ytelemetry_hdrs_seed_root(struct yheaders *hdrs, const char *inbound_traceparent)
{
    if (!hdrs || !ytel_enabled()) return;
    char trace_id[33], span_id[17];
    int sampled = 1;
    if (inbound_traceparent && *inbound_traceparent &&
        ytelemetry_traceparent_parse(inbound_traceparent, trace_id, span_id, &sampled)) {
        yheaders_set(hdrs, "trace_id", trace_id);
        yheaders_set(hdrs, "parent_span_id", span_id);
        yheaders_set_u32(hdrs, "sampled", sampled ? 1 : 0);
    } else {
        ytelemetry_mint_trace_id(trace_id);
        yheaders_set(hdrs, "trace_id", trace_id);
        yheaders_set_u32(hdrs, "sampled", 1);
    }
}

static void ytel_span_begin(struct ytelemetry_span *span, struct yheaders *hdrs,
                            const char *name, uint8_t kind)
{
    memset(span, 0, sizeof(*span));
    span->kind = kind;
    ytel_copystr(span->name, sizeof(span->name), name);

    const char *trace_id = yheaders_get(hdrs, "trace_id");
    if (trace_id && strlen(trace_id) == 32) {
        ytel_copystr(span->trace_id, sizeof(span->trace_id), trace_id);
    } else {
        ytelemetry_mint_trace_id(span->trace_id);
        if (hdrs) yheaders_set(hdrs, "trace_id", span->trace_id);
    }

    const char *parent = yheaders_get(hdrs, "parent_span_id");
    if (parent && *parent) ytel_copystr(span->parent_id, sizeof(span->parent_id), parent);

    span->sampled = yheaders_get_u32(hdrs, "sampled", 1) ? 1 : 0;
    span->uid = yheaders_get_u32(hdrs, "uid", 0);
    ytelemetry_mint_span_id(span->span_id);
    span->start_mono_ns = ytel_mono_ns();
    span->start_unix_ns = ytel_wall_ns();
}

void ytelemetry_server_span_begin(struct ytelemetry_span *span, struct yheaders *hdrs,
                                  const char *name)
{
    if (!ytel_enabled()) { if (span) memset(span, 0, sizeof(*span)); return; }
    ytel_span_begin(span, hdrs, name, YTELEMETRY_KIND_SERVER);
    if (hdrs) {
        yheaders_set(hdrs, "parent_span_id", span->span_id);
        yheaders_set_u32(hdrs, "sampled", span->sampled);
    }
}

void ytelemetry_client_span_begin(struct ytelemetry_span *span, struct yheaders *hdrs,
                                  const char *name)
{
    if (!ytel_enabled()) { if (span) memset(span, 0, sizeof(*span)); return; }
    ytel_span_begin(span, hdrs, name, YTELEMETRY_KIND_CLIENT);
}

size_t ytelemetry_client_serialize_headers(const struct ytelemetry_span *span,
                                           struct yheaders *hdrs, void *buf, size_t cap)
{
    if (!hdrs || !ytel_enabled()) return yheaders_serialize(hdrs, buf, cap);
    char save[17] = {0};
    const char *current = yheaders_get(hdrs, "parent_span_id");
    if (current) ytel_copystr(save, sizeof(save), current);
    yheaders_set(hdrs, "parent_span_id", span->span_id);
    size_t written = yheaders_serialize(hdrs, buf, cap);
    yheaders_set(hdrs, "parent_span_id", save);
    return written;
}

/* ---- span ship-out: fire-and-forget to the collector plugin ----------- */

/* This process's service name / node id, resolved once from the engine's
 * --name. Filled idempotently (same value on every worker). */
static const char *ytel_service_name(void)
{
    static char name[64];
    static int done = 0;
    if (!done) {
        struct picomesh_engine *engine = picomesh_active_engine();
        const char *name_arg = engine ? argv_get_string(picomesh_engine_cli(engine), "name", NULL)
                                      : NULL;
        ytel_copystr(name, sizeof(name), name_arg && *name_arg ? name_arg : "picomesh");
        done = 1;
    }
    return name;
}

static const char *ytel_node_id(void)
{
    static char node[80];
    static int done = 0;
    if (!done) {
        snprintf(node, sizeof(node), "%s:%d", ytel_service_name(), (int)getpid());
        done = 1;
    }
    return node;
}

static const char *ytel_kind_str(uint8_t kind)
{
    switch (kind) {
    case YTELEMETRY_KIND_SERVER: return "server";
    case YTELEMETRY_KIND_CLIENT: return "client";
    default: return "internal";
    }
}

static size_t ytel_putc(char *buf, size_t cap, size_t off, char ch)
{
    if (off < cap) buf[off] = ch;
    return off + 1;
}

/* Append a JSON-escaped quoted string. */
static size_t ytel_jstr(char *buf, size_t cap, size_t off, const char *str)
{
    off = ytel_putc(buf, cap, off, '"');
    for (; str && *str; ++str) {
        unsigned char ch = (unsigned char)*str;
        if (ch == '"' || ch == '\\') {
            off = ytel_putc(buf, cap, off, '\\');
            off = ytel_putc(buf, cap, off, (char)ch);
        } else if (ch == '\n') {
            off = ytel_putc(buf, cap, off, '\\');
            off = ytel_putc(buf, cap, off, 'n');
        } else if (ch == '\r') {
            off = ytel_putc(buf, cap, off, '\\');
            off = ytel_putc(buf, cap, off, 'r');
        } else if (ch == '\t') {
            off = ytel_putc(buf, cap, off, '\\');
            off = ytel_putc(buf, cap, off, 't');
        } else if (ch < 0x20) {
            char escape[8];
            int escape_len = snprintf(escape, sizeof(escape), "\\u%04x", ch);
            for (int j = 0; j < escape_len; ++j) off = ytel_putc(buf, cap, off, escape[j]);
        } else {
            off = ytel_putc(buf, cap, off, (char)ch);
        }
    }
    off = ytel_putc(buf, cap, off, '"');
    return off;
}

static size_t ytel_lit(char *buf, size_t cap, size_t off, const char *str)
{
    for (; *str; ++str) { if (off < cap) buf[off] = *str; off++; }
    return off;
}

/* Build one span's JSON object into `js`. Returns its length, or 0 on
 * overflow (the span is dropped). */
static size_t ytel_build_span(char *span_json, size_t cap, const struct ytelemetry_span *span,
                              int ok, const char *err, uint64_t dur_ns)
{
    size_t off = 0;
    char uidbuf[16];
    snprintf(uidbuf, sizeof(uidbuf), "%u", span->uid);
    off = ytel_lit(span_json, cap, off, "{\"trace_id\":");        off = ytel_jstr(span_json, cap, off, span->trace_id);
    off = ytel_lit(span_json, cap, off, ",\"span_id\":");          off = ytel_jstr(span_json, cap, off, span->span_id);
    off = ytel_lit(span_json, cap, off, ",\"parent_span_id\":");   off = ytel_jstr(span_json, cap, off, span->parent_id);
    off = ytel_lit(span_json, cap, off, ",\"name\":");             off = ytel_jstr(span_json, cap, off, span->name);
    off = ytel_lit(span_json, cap, off, ",\"kind\":");             off = ytel_jstr(span_json, cap, off, ytel_kind_str(span->kind));
    off = ytel_lit(span_json, cap, off, ",\"service_name\":");     off = ytel_jstr(span_json, cap, off, ytel_service_name());
    off = ytel_lit(span_json, cap, off, ",\"node_id\":");          off = ytel_jstr(span_json, cap, off, ytel_node_id());
    { char num[48]; snprintf(num, sizeof(num), ",\"start_time_ns\":%llu", (unsigned long long)span->start_unix_ns); off = ytel_lit(span_json, cap, off, num); }
    { char num[48]; snprintf(num, sizeof(num), ",\"duration_ns\":%llu", (unsigned long long)dur_ns); off = ytel_lit(span_json, cap, off, num); }
    off = ytel_lit(span_json, cap, off, ",\"status\":");           off = ytel_jstr(span_json, cap, off, ok ? "ok" : "error");
    if (!ok && err && *err) { off = ytel_lit(span_json, cap, off, ",\"error_message\":"); off = ytel_jstr(span_json, cap, off, err); }
    off = ytel_lit(span_json, cap, off, ",\"attributes\":{\"rpc.system\":\"yrpc\",\"picomesh.uid\":");
    off = ytel_jstr(span_json, cap, off, uidbuf);
    off = ytel_lit(span_json, cap, off, "}}");
    return off >= cap ? 0 : off;
}

/* Per-worker span batch (an OTel BatchSpanProcessor): finished spans accumulate
 * thread-locally and ship to the collector as ONE array per RPC — one frame,
 * one parse for N spans, instead of one RPC per span. Bounds keep each frame
 * under the yrpc body cap (BUF_MAX). Thread-confined → no lock on the hot path.
 *
 * The batch ships from two contexts: inline on span_end (a coroutine — its
 * yielding loop_write is fine) and from the per-worker flush timer (which the
 * engine runs INSIDE a spawned coroutine for exactly this reason). The reset
 * happens before the ship so the two never double-ship or discard each other's
 * spans across a yield. */
#define YTEL_BATCH_MAX_SPANS_DEFAULT 128
/* The generated ingest skel unpacks the string arg into a fixed 4096-byte
 * stack buffer (rpc.gen.c) and REJECTS the whole call if the arg is >= 4096.
 * So the shipped array MUST stay under that — not the 64KB yrpc frame cap.
 * 3500 leaves headroom for the brackets + one trailing span (~1KB max). */
#define YTEL_BATCH_MAX_BYTES_DEFAULT 3500
#define YTEL_BATCH_FLUSH_MS_DEFAULT  200     /* scheduled-delay default */

/* Sender batch tunables, resolved ONCE per process and cached (read on the
 * span hot path). Config keys, under each service's `telemetry:` block:
 *   telemetry.batch_max_spans  ship after this many spans      (default 128)
 *   telemetry.batch_max_bytes  ship before the array exceeds this; keep it
 *                              under the yrpc frame cap         (default 48000)
 *   telemetry.batch_flush_ms   ship a partial batch this old    (default 200) */
struct ytel_batch_cfg {
    int max_spans;
    size_t max_bytes;
    uint64_t flush_ns;
};

static const struct ytel_batch_cfg *ytel_batch_config(void)
{
    static struct ytel_batch_cfg cfg;
    static int loaded = 0;
    if (!loaded) {
        cfg.max_spans = YTEL_BATCH_MAX_SPANS_DEFAULT;
        cfg.max_bytes = YTEL_BATCH_MAX_BYTES_DEFAULT;
        cfg.flush_ns = (uint64_t)YTEL_BATCH_FLUSH_MS_DEFAULT * 1000000ull;
        struct picomesh_engine *engine = picomesh_active_engine();
        const struct config *config = engine ? picomesh_engine_config(engine) : NULL;
        /* Each knob has a documented default, so absence is not an error — the
         * default-aware getters return the configured value or that default. */
        int64_t value;
        value = config_get_int(config, "telemetry.batch_max_spans", 0);
        if (value > 0) cfg.max_spans = (int)value;
        value = config_get_int(config, "telemetry.batch_max_bytes", 0);
        if (value > 0) cfg.max_bytes = (size_t)value;
        value = config_get_int(config, "telemetry.batch_flush_ms", 0);
        if (value > 0) cfg.flush_ns = (uint64_t)value * 1000000ull;
        loaded = 1;
    }
    return &cfg;
}

struct ytel_batch_state {
    char *json;   /* accumulated "{..},{..}" without the enclosing brackets */
    size_t len, cap;
    int count;
    uint64_t last_flush_ns;
    uint8_t *body; /* wire-frame scratch, grown as needed */
    size_t body_cap;
};

static __thread struct ytel_batch_state ytel_batch;

/* Ship the accumulated batch as one ingest call, fire-and-forget. Resets the
 * buffer whether or not the ship-out succeeds (best-effort telemetry).
 *
 * This is a deliberate absorb boundary: telemetry shipping MUST NOT propagate a
 * failure into the request being traced (tracing is transparent to the traced
 * code). Its void signature is structural, like an external callback's — so a
 * ship-out failure is rendered to the log here, never surfaced upward. */
PICOMESH_EXTERNAL_CALLBACK
static void ytel_flush_batch(void)
{
    struct ytel_batch_state *batch = &ytel_batch;
    if (batch->count == 0 || batch->len == 0) { batch->count = 0; batch->len = 0; return; }
    batch->last_flush_ns = ytel_mono_ns();

    struct picomesh_engine *engine = picomesh_active_engine();
    struct ctx ctx = engine ? picomesh_engine_service_ctx(engine, "trace_collector") : (struct ctx){0};
    if (!ctx.peer) { batch->count = 0; batch->len = 0; return; } /* collector not wired → drop */

    static method_slot ingest_slot = METHOD_SLOT_UNDEFINED;
    if (ingest_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result slot_res =
            method_slot_by_name("trace_collector", "trace_collector_ingest");
        if (PICOMESH_IS_ERR(slot_res)) {
            picomesh_error_print(stderr, "ytelemetry flush: method_slot_by_name", slot_res.error);
            picomesh_error_destroy(slot_res.error); batch->count = 0; batch->len = 0; return;
        }
        ingest_slot = slot_res.value;
    }
    struct object_ptr_result object_res = object_create_in_ctx(&ctx, "trace_collector_trace_collector");
    if (PICOMESH_IS_ERR(object_res)) {
        picomesh_error_print(stderr, "ytelemetry flush: object_create_in_ctx", object_res.error);
        picomesh_error_destroy(object_res.error); batch->count = 0; batch->len = 0; return;
    }
    struct object *object = object_res.value;
    struct picomesh_uint32_result remote_id_res = peer_channel_ensure_remote_id(ctx.peer, ingest_slot);
    if (PICOMESH_IS_ERR(remote_id_res)) {
        picomesh_error_print(stderr, "ytelemetry flush: ensure remote id", remote_id_res.error);
        picomesh_error_destroy(remote_id_res.error);
        batch->count = 0; batch->len = 0; return;
    }
    uint32_t remote_id = remote_id_res.value;
    if (remote_id == RPC_REMOTE_ID_UNRESOLVED) { batch->count = 0; batch->len = 0; return; }

    size_t arg_len = batch->len + 2; /* the enclosing [ ] */
    size_t need = 256 + 8 + 4 + arg_len;
    if (batch->body_cap < need) {
        uint8_t *new_body = realloc(batch->body, need + 1024);
        if (!new_body) { batch->count = 0; batch->len = 0; return; }
        batch->body = new_body; batch->body_cap = need + 1024;
    }
    size_t body_off = yheaders_serialize(NULL, batch->body, batch->body_cap);
    if (body_off == 0) { batch->count = 0; batch->len = 0; return; }
    uint64_t handle = *(uint64_t *)((char *)object + sizeof(struct object));
    memcpy(batch->body + body_off, &handle, 8); body_off += 8;
    uint32_t arg_len32 = (uint32_t)arg_len;
    memcpy(batch->body + body_off, &arg_len32, 4); body_off += 4;
    batch->body[body_off++] = '[';
    memcpy(batch->body + body_off, batch->json, batch->len); body_off += batch->len;
    batch->body[body_off++] = ']';

    /* Reset BEFORE the (possibly yielding) ship. The wire snapshot is already
     * in batch->body, and rpc_call_oneway copies it out synchronously before any
     * yield — so spans produced while this ship is suspended start a fresh
     * batch instead of being re-shipped or discarded by this call's reset. */
    batch->count = 0;
    batch->len = 0;
    rpc_call_oneway(ctx.peer, RPC_OP_CALL, remote_id, batch->body, body_off);
}

int ytelemetry_pending_local(void)
{
    return ytel_batch.count;
}

/* Append one finished span to this worker's batch; flush on size/bytes/time. */
static void ytel_buffer_span(const struct ytelemetry_span *span, int ok, const char *err,
                             uint64_t dur_ns)
{
    const struct ytel_batch_cfg *batch_cfg = ytel_batch_config();
    struct ytel_batch_state *batch = &ytel_batch;
    if (!batch->json) {
        batch->cap = batch_cfg->max_bytes + 2048;
        batch->json = malloc(batch->cap);
        if (!batch->json) { batch->cap = 0; return; } /* can't buffer → drop (best-effort) */
        batch->last_flush_ns = ytel_mono_ns();
    }

    char span_json[1024];
    size_t off = ytel_build_span(span_json, sizeof(span_json), span, ok, err, dur_ns);
    if (off == 0) return; /* overflow → drop this span */

    /* Flush first if appending would exceed the byte budget for one frame. */
    if (batch->count && batch->len + 1 + off > batch_cfg->max_bytes) ytel_flush_batch();

    if (batch->len + (batch->count ? 1u : 0u) + off <= batch->cap) {
        if (batch->count) batch->json[batch->len++] = ',';
        memcpy(batch->json + batch->len, span_json, off);
        batch->len += off;
        batch->count++;
    }

    if (batch->count >= batch_cfg->max_spans) {
        ytel_flush_batch();
    } else if (batch->count && ytel_mono_ns() - batch->last_flush_ns > batch_cfg->flush_ns) {
        ytel_flush_batch();
    }
}

void ytelemetry_span_end(struct ytelemetry_span *span, int ok, const char *err)
{
    /* Telemetry off (or a span that begin() zeroed): nothing to record or ship. */
    if (!ytel_enabled() || !span || span->start_mono_ns == 0) return;
    uint64_t duration_ns = ytel_mono_ns() - span->start_mono_ns;
    /* Local /_perf aggregate, regardless of collector availability. */
    yspan_record(span->name, (double)duration_ns / 1000.0);
    ytel_buffer_span(span, ok, err, duration_ns);
}

void ytelemetry_flush_local(void)
{
    if (ytel_batch.count) ytel_flush_batch();
}
