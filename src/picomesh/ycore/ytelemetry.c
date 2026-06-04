/* ytelemetry — span context + fire-and-forget span sender (see
 * ytelemetry.h). No threads, no shared queue, no mutex: each worker ships
 * its own spans on its own connection to the trace_collector plugin. */

#include <picomesh/ycore/ytelemetry.h>

#include <picomesh/ycore/yspan.h>
#include <picomesh/yargv/yargv.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>

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

static int ytel_hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int ytel_all_hex(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (ytel_hex_val(s[i]) < 0) return 0;
    return 1;
}

static int ytel_all_zero(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (s[i] != '0') return 0;
    return 1;
}

static int ytel_bytes_all_zero(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (b[i]) return 0;
    return 1;
}

static void ytel_fill_random(uint8_t *b, size_t n)
{
    /* Prefer the kernel CSPRNG; retry partial reads and EINTR. */
    size_t off = 0;
    while (off < n) {
        ssize_t r = getrandom(b + off, n - off, 0);
        if (r > 0) { off += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    if (off == n) return;
    /* Fallback for the remainder: trace/span ids are correlation ids, not
     * secrets, so a clock/pid-seeded PRNG is acceptable when getrandom is
     * unavailable. */
    uint64_t seed = ytel_mono_ns() ^ ((uint64_t)getpid() << 32) ^ (uint64_t)(off + 1);
    for (size_t i = off; i < n; ++i) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        b[i] = (uint8_t)(seed >> 33);
    }
}

static void ytel_to_hex(char *out, const uint8_t *b, size_t n)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = H[b[i] >> 4];
        out[2 * i + 1] = H[b[i] & 0xf];
    }
    out[2 * n] = 0;
}

void ytelemetry_mint_trace_id(char out[33])
{
    uint8_t b[16];
    ytel_fill_random(b, sizeof(b));
    if (ytel_bytes_all_zero(b, sizeof(b))) b[0] = 1; /* W3C: all-zero trace id invalid */
    ytel_to_hex(out, b, sizeof(b));
}

void ytelemetry_mint_span_id(char out[17])
{
    uint8_t b[8];
    ytel_fill_random(b, sizeof(b));
    if (ytel_bytes_all_zero(b, sizeof(b))) b[0] = 1; /* W3C: all-zero span id invalid */
    ytel_to_hex(out, b, sizeof(b));
}

/* ---- W3C traceparent -------------------------------------------------- */

int ytelemetry_traceparent_parse(const char *s, char *trace_id_hex,
                                 char *span_id_hex, int *sampled)
{
    if (!s) return 0;
    if (strlen(s) < 55) return 0; /* "00-<32>-<16>-<2>" */
    if (s[2] != '-' || s[35] != '-' || s[52] != '-') return 0;
    if (!ytel_all_hex(s, 2)) return 0;
    if (!ytel_all_hex(s + 3, 32)) return 0;
    if (!ytel_all_hex(s + 36, 16)) return 0;
    if (!ytel_all_hex(s + 53, 2)) return 0;
    if (ytel_all_zero(s + 3, 32)) return 0;
    if (ytel_all_zero(s + 36, 16)) return 0;

    memcpy(trace_id_hex, s + 3, 32);
    trace_id_hex[32] = 0;
    memcpy(span_id_hex, s + 36, 16);
    span_id_hex[16] = 0;
    if (sampled) {
        int flags = ytel_hex_val(s[53]) * 16 + ytel_hex_val(s[54]);
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
            struct picomesh_engine *e = picomesh_active_engine();
            const struct yconfig *cfg = e ? picomesh_engine_config(e) : NULL;
            if (cfg) {
                struct yconfig_node_ptr_result r = yconfig_get(cfg, "telemetry.enabled");
                if (PICOMESH_IS_OK(r) && r.value) enabled = yconfig_node_as_bool(r.value, 1) ? 1 : 0;
                else if (PICOMESH_IS_ERR(r)) picomesh_error_destroy(r.error);
            }
        }
        cached = enabled;
    }
    return cached;
}

void ytelemetry_hdrs_seed_root(struct yheaders *hdrs, const char *inbound_traceparent)
{
    if (!hdrs || !ytel_enabled()) return;
    char tid[33], sid[17];
    int sampled = 1;
    if (inbound_traceparent && *inbound_traceparent &&
        ytelemetry_traceparent_parse(inbound_traceparent, tid, sid, &sampled)) {
        yheaders_set(hdrs, "trace_id", tid);
        yheaders_set(hdrs, "parent_span_id", sid);
        yheaders_set_u32(hdrs, "sampled", sampled ? 1 : 0);
    } else {
        ytelemetry_mint_trace_id(tid);
        yheaders_set(hdrs, "trace_id", tid);
        yheaders_set_u32(hdrs, "sampled", 1);
    }
}

static void ytel_span_begin(struct ytelemetry_span *sp, struct yheaders *hdrs,
                            const char *name, uint8_t kind)
{
    memset(sp, 0, sizeof(*sp));
    sp->kind = kind;
    ytel_copystr(sp->name, sizeof(sp->name), name);

    const char *tid = yheaders_get(hdrs, "trace_id");
    if (tid && strlen(tid) == 32) {
        ytel_copystr(sp->trace_id, sizeof(sp->trace_id), tid);
    } else {
        ytelemetry_mint_trace_id(sp->trace_id);
        if (hdrs) yheaders_set(hdrs, "trace_id", sp->trace_id);
    }

    const char *par = yheaders_get(hdrs, "parent_span_id");
    if (par && *par) ytel_copystr(sp->parent_id, sizeof(sp->parent_id), par);

    sp->sampled = yheaders_get_u32(hdrs, "sampled", 1) ? 1 : 0;
    sp->uid = yheaders_get_u32(hdrs, "uid", 0);
    ytelemetry_mint_span_id(sp->span_id);
    sp->start_mono_ns = ytel_mono_ns();
    sp->start_unix_ns = ytel_wall_ns();
}

void ytelemetry_server_span_begin(struct ytelemetry_span *sp, struct yheaders *hdrs,
                                  const char *name)
{
    if (!ytel_enabled()) { if (sp) memset(sp, 0, sizeof(*sp)); return; }
    ytel_span_begin(sp, hdrs, name, YTELEMETRY_KIND_SERVER);
    if (hdrs) {
        yheaders_set(hdrs, "parent_span_id", sp->span_id);
        yheaders_set_u32(hdrs, "sampled", sp->sampled);
    }
}

void ytelemetry_client_span_begin(struct ytelemetry_span *sp, struct yheaders *hdrs,
                                  const char *name)
{
    if (!ytel_enabled()) { if (sp) memset(sp, 0, sizeof(*sp)); return; }
    ytel_span_begin(sp, hdrs, name, YTELEMETRY_KIND_CLIENT);
}

size_t ytelemetry_client_serialize_headers(const struct ytelemetry_span *sp,
                                           struct yheaders *hdrs, void *buf, size_t cap)
{
    if (!hdrs || !ytel_enabled()) return yheaders_serialize(hdrs, buf, cap);
    char save[17] = {0};
    const char *cur = yheaders_get(hdrs, "parent_span_id");
    if (cur) ytel_copystr(save, sizeof(save), cur);
    yheaders_set(hdrs, "parent_span_id", sp->span_id);
    size_t n = yheaders_serialize(hdrs, buf, cap);
    yheaders_set(hdrs, "parent_span_id", save);
    return n;
}

/* ---- span ship-out: fire-and-forget to the collector plugin ----------- */

/* This process's service name / node id, resolved once from the engine's
 * --name. Filled idempotently (same value on every worker). */
static const char *ytel_service_name(void)
{
    static char name[64];
    static int done = 0;
    if (!done) {
        struct picomesh_engine *e = picomesh_active_engine();
        const char *n = e ? yargv_get_string(picomesh_engine_cli(e), "name", NULL) : NULL;
        ytel_copystr(name, sizeof(name), n && *n ? n : "picomesh");
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

static const char *ytel_kind_str(uint8_t k)
{
    switch (k) {
    case YTELEMETRY_KIND_SERVER: return "server";
    case YTELEMETRY_KIND_CLIENT: return "client";
    default: return "internal";
    }
}

static size_t ytel_putc(char *buf, size_t cap, size_t off, char c)
{
    if (off < cap) buf[off] = c;
    return off + 1;
}

/* Append a JSON-escaped quoted string. */
static size_t ytel_jstr(char *buf, size_t cap, size_t off, const char *s)
{
    off = ytel_putc(buf, cap, off, '"');
    for (; s && *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            off = ytel_putc(buf, cap, off, '\\');
            off = ytel_putc(buf, cap, off, (char)c);
        } else if (c == '\n') {
            off = ytel_putc(buf, cap, off, '\\');
            off = ytel_putc(buf, cap, off, 'n');
        } else if (c == '\r') {
            off = ytel_putc(buf, cap, off, '\\');
            off = ytel_putc(buf, cap, off, 'r');
        } else if (c == '\t') {
            off = ytel_putc(buf, cap, off, '\\');
            off = ytel_putc(buf, cap, off, 't');
        } else if (c < 0x20) {
            char u[8];
            int k = snprintf(u, sizeof(u), "\\u%04x", c);
            for (int j = 0; j < k; ++j) off = ytel_putc(buf, cap, off, u[j]);
        } else {
            off = ytel_putc(buf, cap, off, (char)c);
        }
    }
    off = ytel_putc(buf, cap, off, '"');
    return off;
}

static size_t ytel_lit(char *buf, size_t cap, size_t off, const char *s)
{
    for (; *s; ++s) { if (off < cap) buf[off] = *s; off++; }
    return off;
}

/* Send one finished span to the collector plugin's ingest method over the
 * current worker's own connection, fire-and-forget. No-op if the collector
 * is not a remote of this process (or this IS the collector). Bypasses the
 * codegen stub on purpose so the ship-out itself emits no span. */
static void ytel_ship(const struct ytelemetry_span *sp, int ok, const char *err,
                      uint64_t dur_ns)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return;
    struct ctx c = picomesh_engine_service_ctx(e, "trace_collector");
    if (!c.peer) return; /* collector not wired here → drop */

    static method_slot ingest_slot = METHOD_SLOT_UNDEFINED;
    if (ingest_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result sr =
            method_slot_by_name("trace_collector", "trace_collector_ingest");
        if (PICOMESH_IS_ERR(sr)) { picomesh_error_destroy(sr.error); return; }
        ingest_slot = sr.value;
    }

    struct object_ptr_result obr = object_create_in_ctx(&c, "trace_collector_trace_collector");
    if (PICOMESH_IS_ERR(obr)) { picomesh_error_destroy(obr.error); return; }
    struct object *obj = obr.value;

    uint32_t rid = peer_channel_ensure_remote_id(c.peer, ingest_slot);
    if (rid == RPC_REMOTE_ID_UNRESOLVED) return;

    /* Build the span JSON (the single string arg to ingest). */
    char js[1024];
    size_t off = 0;
    char uidbuf[16];
    snprintf(uidbuf, sizeof(uidbuf), "%u", sp->uid);
    off = ytel_lit(js, sizeof(js), off, "{\"trace_id\":");        off = ytel_jstr(js, sizeof(js), off, sp->trace_id);
    off = ytel_lit(js, sizeof(js), off, ",\"span_id\":");          off = ytel_jstr(js, sizeof(js), off, sp->span_id);
    off = ytel_lit(js, sizeof(js), off, ",\"parent_span_id\":");   off = ytel_jstr(js, sizeof(js), off, sp->parent_id);
    off = ytel_lit(js, sizeof(js), off, ",\"name\":");             off = ytel_jstr(js, sizeof(js), off, sp->name);
    off = ytel_lit(js, sizeof(js), off, ",\"kind\":");             off = ytel_jstr(js, sizeof(js), off, ytel_kind_str(sp->kind));
    off = ytel_lit(js, sizeof(js), off, ",\"service_name\":");     off = ytel_jstr(js, sizeof(js), off, ytel_service_name());
    off = ytel_lit(js, sizeof(js), off, ",\"node_id\":");          off = ytel_jstr(js, sizeof(js), off, ytel_node_id());
    { char n[48]; snprintf(n, sizeof(n), ",\"start_time_ns\":%llu", (unsigned long long)sp->start_unix_ns); off = ytel_lit(js, sizeof(js), off, n); }
    { char n[48]; snprintf(n, sizeof(n), ",\"duration_ns\":%llu", (unsigned long long)dur_ns); off = ytel_lit(js, sizeof(js), off, n); }
    off = ytel_lit(js, sizeof(js), off, ",\"status\":");           off = ytel_jstr(js, sizeof(js), off, ok ? "ok" : "error");
    if (!ok && err && *err) { off = ytel_lit(js, sizeof(js), off, ",\"error_message\":"); off = ytel_jstr(js, sizeof(js), off, err); }
    off = ytel_lit(js, sizeof(js), off, ",\"attributes\":{\"rpc.system\":\"yrpc\",\"picomesh.uid\":");
    off = ytel_jstr(js, sizeof(js), off, uidbuf);
    off = ytel_lit(js, sizeof(js), off, "}}");
    if (off >= sizeof(js)) return; /* overflow → drop (bounded) */

    /* Pack the wire body: empty header bag + object handle + string arg. */
    uint8_t body[1280];
    size_t bo = yheaders_serialize(NULL, body, sizeof(body));
    if (bo == 0) return;
    uint64_t h = *(uint64_t *)((char *)obj + sizeof(struct object));
    if (bo + 8 + 4 + off > sizeof(body)) return;
    memcpy(body + bo, &h, 8); bo += 8;
    uint32_t slen = (uint32_t)off;
    memcpy(body + bo, &slen, 4); bo += 4;
    memcpy(body + bo, js, off); bo += off;

    rpc_call_oneway(c.peer, RPC_OP_CALL, rid, body, bo);
}

void ytelemetry_span_end(struct ytelemetry_span *sp, int ok, const char *err)
{
    /* Telemetry off (or a span that begin() zeroed): nothing to record or ship. */
    if (!ytel_enabled() || !sp || sp->start_mono_ns == 0) return;
    uint64_t dur = ytel_mono_ns() - sp->start_mono_ns;
    /* Local /_perf aggregate, regardless of collector availability. */
    yspan_record(sp->name, (double)dur / 1000.0);
    ytel_ship(sp, ok, err, dur);
}
