/* msgpack frontend — Picomesh MessagePack envelope over TCP.
 *
 * One coroutine per peer, strict serial request/response (no multiplexing in
 * v1). Each request is a big-endian u32 length prefix followed by a msgpack
 * map envelope; dispatch runs through the shared active-service resolver and
 * the codegen-emitted minvoke table. See <picomesh/frontends/msgpack/msgpack.h>
 * for the wire contract. */

#include <picomesh/frontends/msgpack/msgpack.h>

#include <picomesh/yengine/engine.h>
#include <picomesh/yengine/resolve.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yclass/minvoke.h>
#include <picomesh/yclass/jinvoke.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/msgpack/msgpack.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Max bytes in one request frame (the msgpack payload after the length
 * prefix), and the matching cap on the response + result-value scratch. A
 * frame over the request cap is rejected with `frame_too_large`. */
#define MSGPACK_FRAME_MAX (1u << 20) /* 1 MiB */

struct msgpack_frontend {
    struct picomesh_engine *engine;
};

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Pack { "v":1, "ok":false, "error": { "message":msg, "code":code } } into
 * `resp`. Returns the encoded length (0 on overflow — caller drops the
 * connection). */
static size_t pack_error(uint8_t *resp, size_t resp_cap, const char *code, const char *message)
{
    cmp_ctx_t w;
    struct picomesh_msgpack_buffer wb;
    picomesh_msgpack_writer_init(&w, &wb, resp, resp_cap);
    cmp_write_map(&w, 3);
    cmp_write_str(&w, "v", 1);
    cmp_write_integer(&w, 1);
    cmp_write_str(&w, "ok", 2);
    cmp_write_bool(&w, false);
    cmp_write_str(&w, "error", 5);
    cmp_write_map(&w, 2);
    cmp_write_str(&w, "message", 7);
    cmp_write_str(&w, message, (uint32_t)strlen(message));
    cmp_write_str(&w, "code", 4);
    cmp_write_str(&w, code, (uint32_t)strlen(code));
    return wb.offset; /* 0 if the buffer overflowed */
}

/* Map a resolver error message to a stable wire code. */
static const char *resolve_code(const char *msg)
{
    if (!msg)
        return "resolve_error";
    if (strstr(msg, "bad path"))
        return "bad_path";
    if (strstr(msg, "not active"))
        return "service_not_active";
    return "no_such_class";
}

/* Decode `headers` map (already entered: `count` pairs remain) into `hdrs`.
 * Known keys carry typed values; anything else is skipped. */
static void decode_headers(cmp_ctx_t *r, uint32_t count, struct yheaders *hdrs)
{
    for (uint32_t i = 0; i < count; ++i) {
        char key[64];
        uint32_t klen = (uint32_t)sizeof(key);
        if (!cmp_read_str(r, key, &klen))
            return;
        if (strcmp(key, "uid") == 0 || strcmp(key, "caller_uid") == 0) {
            uint64_t v = 0;
            if (!cmp_read_uinteger(r, &v))
                return;
            if (hdrs)
                yheaders_set_u32(hdrs, "uid", (uint32_t)v);
        } else if (strcmp(key, "sid") == 0 || strcmp(key, "caller_sid") == 0) {
            uint64_t v = 0;
            if (!cmp_read_uinteger(r, &v))
                return;
            if (hdrs)
                yheaders_set_u32(hdrs, "sid", (uint32_t)v);
        } else if (strcmp(key, "trace_id") == 0 || strcmp(key, "traceparent") == 0) {
            char val[160];
            uint32_t vlen = (uint32_t)sizeof(val);
            if (!cmp_read_str(r, val, &vlen))
                return;
            if (hdrs)
                yheaders_set(hdrs, key, val);
        } else {
            if (!cmp_skip_object_no_limit(r))
                return;
        }
    }
}

/* Build the success envelope: { "v":1, "ok":true, "result": <value> } where
 * the result value is the already-encoded `value`/`value_len` bytes. Returns
 * the response length, or 0 on overflow. */
static size_t pack_success(uint8_t *resp, size_t resp_cap, const uint8_t *value, size_t value_len)
{
    cmp_ctx_t w;
    struct picomesh_msgpack_buffer wb;
    picomesh_msgpack_writer_init(&w, &wb, resp, resp_cap);
    if (!cmp_write_map(&w, 3))
        return 0;
    if (!cmp_write_str(&w, "v", 1) || !cmp_write_integer(&w, 1))
        return 0;
    if (!cmp_write_str(&w, "ok", 2) || !cmp_write_bool(&w, true))
        return 0;
    if (!cmp_write_str(&w, "result", 6))
        return 0;
    /* The result value is a complete, pre-encoded msgpack value — append its
     * bytes verbatim as the map's value (msgpack is concatenative). */
    if (wb.offset + value_len > wb.cap)
        return 0;
    memcpy(wb.data + wb.offset, value, value_len);
    wb.offset += value_len;
    return wb.offset;
}

/* op == "invoke". Resolve+gate the path, run the minvoke, build the response
 * envelope into `resp`. `args_present`/`args_offset` locate the args array in
 * the original frame; `kwargs_nonempty` triggers the v1 rejection. */
static size_t handle_invoke(struct picomesh_engine *engine, const char *path, const uint8_t *frame,
                            size_t frame_len, int args_present, size_t args_offset,
                            int kwargs_nonempty, struct yheaders *hdrs, uint8_t *value_buf,
                            uint8_t *resp, size_t resp_cap)
{
    if (kwargs_nonempty)
        return pack_error(resp, resp_cap, "kwargs_unsupported",
                          "msgpack v1 binds positional args only; kwargs must be empty");
    if (!path || !*path)
        return pack_error(resp, resp_cap, "bad_path", "missing 'path'");

    struct picomesh_service_call_result call_r = picomesh_resolve_service_call(engine, path);
    if (PICOMESH_IS_ERR(call_r)) {
        const char *msg = call_r.error.msg ? call_r.error.msg : "resolve failed";
        size_t n = pack_error(resp, resp_cap, resolve_code(msg), msg);
        picomesh_error_destroy(call_r.error);
        return n;
    }
    struct picomesh_service_call call = call_r.value;

    minvoke_fn fn = minvoke_for(call.method_qname);
    if (!fn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "no method '%s'", call.method_qname);
        picomesh_service_call_release(&call);
        return pack_error(resp, resp_cap, "no_such_method", msg);
    }

    /* Re-open a reader on the original frame at the args array and read its
     * length; absent args == zero positional args. */
    cmp_ctx_t ar;
    struct picomesh_msgpack_buffer arb;
    uint32_t argc = 0;
    if (args_present) {
        picomesh_msgpack_reader_init(&ar, &arb, frame, frame_len);
        arb.offset = args_offset;
        if (!cmp_read_array(&ar, &argc)) {
            picomesh_service_call_release(&call);
            return pack_error(resp, resp_cap, "bad_envelope", "'args' is not an array");
        }
    } else {
        picomesh_msgpack_reader_init(&ar, &arb, frame, frame_len);
    }

    cmp_ctx_t vw;
    struct picomesh_msgpack_buffer vb;
    picomesh_msgpack_writer_init(&vw, &vb, value_buf, MSGPACK_FRAME_MAX);

    char err[8192] = {0};
    int rc = fn(&call.ctx, call.obj, hdrs, &ar, argc, &vw, err, sizeof(err));
    picomesh_service_call_release(&call);

    if (rc != 0) {
        yerror("msgpack request failed path=%s: %s", path ? path : "", err[0] ? err : "call failed");
        return pack_error(resp, resp_cap, "call_error", err[0] ? err : "call failed");
    }
    size_t n = pack_success(resp, resp_cap, value_buf, vb.offset);
    if (n == 0)
        return pack_error(resp, resp_cap, "response_too_large", "result exceeds frame cap");
    return n;
}

/* op == "describe". Minimal v1: reflect a method's positional parameter
 * signature (names + C types) for `path`. Resolves through the SAME
 * active-service gate as invoke — a describe for an inactive service is
 * rejected (service_not_active) before any metadata is read. */
static size_t handle_describe(struct picomesh_engine *engine, const char *path, uint8_t *resp,
                              size_t resp_cap)
{
    if (!path || !*path)
        return pack_error(resp, resp_cap, "bad_path", "describe: missing 'path'");

    struct picomesh_service_call_result call_r = picomesh_resolve_service_call(engine, path);
    if (PICOMESH_IS_ERR(call_r)) {
        const char *msg = call_r.error.msg ? call_r.error.msg : "resolve failed";
        size_t n = pack_error(resp, resp_cap, resolve_code(msg), msg);
        picomesh_error_destroy(call_r.error);
        return n;
    }
    struct picomesh_service_call call = call_r.value;
    const struct jinvoke_params *params = call.params;
    if (!params) {
        picomesh_service_call_release(&call);
        return pack_error(resp, resp_cap, "no_such_method", "describe: unknown method");
    }

    uint8_t value[8192];
    cmp_ctx_t vw;
    struct picomesh_msgpack_buffer vb;
    picomesh_msgpack_writer_init(&vw, &vb, value, sizeof(value));
    cmp_write_map(&vw, 2);
    cmp_write_str(&vw, "path", 4);
    cmp_write_str(&vw, path, (uint32_t)strlen(path));
    cmp_write_str(&vw, "params", 6);
    cmp_write_array(&vw, (uint32_t)params->count);
    for (size_t i = 0; i < params->count; ++i) {
        cmp_write_map(&vw, 2);
        cmp_write_str(&vw, "name", 4);
        cmp_write_str(&vw, params->items[i].name, (uint32_t)strlen(params->items[i].name));
        cmp_write_str(&vw, "type", 4);
        cmp_write_str(&vw, params->items[i].type, (uint32_t)strlen(params->items[i].type));
    }
    picomesh_service_call_release(&call);
    size_t n = pack_success(resp, resp_cap, value, vb.offset);
    if (n == 0)
        return pack_error(resp, resp_cap, "response_too_large", "describe result too large");
    return n;
}

/* Decode one request frame and build its response envelope into `resp`.
 * `value_buf` is MSGPACK_FRAME_MAX scratch for the result value. Returns the
 * response length (always > 0 for a well-formed connection — even errors are
 * a response). */
static size_t dispatch_frame(struct picomesh_engine *engine, const uint8_t *frame, size_t frame_len,
                             uint8_t *value_buf, uint8_t *resp, size_t resp_cap)
{
    cmp_ctx_t r;
    struct picomesh_msgpack_buffer rb;
    picomesh_msgpack_reader_init(&r, &rb, frame, frame_len);

    uint32_t top = 0;
    if (!cmp_read_map(&r, &top))
        return pack_error(resp, resp_cap, "bad_envelope", "envelope is not a msgpack map");

    char op[32] = "invoke";
    char path[256] = {0};
    int args_present = 0, kwargs_nonempty = 0;
    size_t args_offset = 0;
    struct yheaders *hdrs = yheaders_new();

    for (uint32_t i = 0; i < top; ++i) {
        char key[32];
        uint32_t klen = (uint32_t)sizeof(key);
        if (!cmp_read_str(&r, key, &klen)) {
            yheaders_free(hdrs);
            return pack_error(resp, resp_cap, "bad_envelope", "bad envelope key");
        }
        if (strcmp(key, "op") == 0) {
            uint32_t n = (uint32_t)sizeof(op);
            if (!cmp_read_str(&r, op, &n)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "bad 'op'");
            }
        } else if (strcmp(key, "path") == 0) {
            uint32_t n = (uint32_t)sizeof(path);
            if (!cmp_read_str(&r, path, &n)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "bad 'path'");
            }
        } else if (strcmp(key, "args") == 0) {
            args_present = 1;
            args_offset = rb.offset;
            if (!cmp_skip_object_no_limit(&r)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "bad 'args'");
            }
        } else if (strcmp(key, "kwargs") == 0) {
            uint32_t kc = 0;
            if (!cmp_read_map(&r, &kc)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "'kwargs' is not a map");
            }
            kwargs_nonempty = kc > 0;
            for (uint32_t j = 0; j < kc * 2u; ++j) {
                if (!cmp_skip_object_no_limit(&r)) {
                    yheaders_free(hdrs);
                    return pack_error(resp, resp_cap, "bad_envelope", "bad 'kwargs'");
                }
            }
        } else if (strcmp(key, "headers") == 0) {
            uint32_t hc = 0;
            if (!cmp_read_map(&r, &hc)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "'headers' is not a map");
            }
            decode_headers(&r, hc, hdrs);
        } else {
            if (!cmp_skip_object_no_limit(&r)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "bad envelope value");
            }
        }
    }

    size_t n;
    if (strcmp(op, "describe") == 0)
        n = handle_describe(engine, path, resp, resp_cap);
    else
        n = handle_invoke(engine, path, frame, frame_len, args_present, args_offset, kwargs_nonempty,
                          hdrs, value_buf, resp, resp_cap);
    yheaders_free(hdrs);
    return n;
}

static void serve_one(struct yloop *l, struct yloop_stream *s, void *ud)
{
    (void)l;
    struct msgpack_frontend *f = ud;
    struct picomesh_engine *engine = f ? f->engine : NULL;

    uint8_t *frame = malloc(MSGPACK_FRAME_MAX);
    uint8_t *value_buf = malloc(MSGPACK_FRAME_MAX);
    uint8_t *resp = malloc(MSGPACK_FRAME_MAX);
    if (!frame || !value_buf || !resp) {
        free(frame);
        free(value_buf);
        free(resp);
        return;
    }

    for (;;) {
        uint8_t lenbuf[4];
        if (yloop_read(s, lenbuf, 4) != 4)
            break; /* peer hung up */
        uint32_t frame_len = read_be32(lenbuf);
        if (frame_len == 0 || frame_len > MSGPACK_FRAME_MAX) {
            size_t en = pack_error(resp, MSGPACK_FRAME_MAX, "frame_too_large",
                                   "frame length zero or over the 1 MiB cap");
            uint8_t outlen[4];
            write_be32(outlen, (uint32_t)en);
            yloop_write(s, outlen, 4);
            if (en)
                yloop_write(s, resp, en);
            break; /* framing is unrecoverable — close */
        }
        if (yloop_read(s, frame, frame_len) != frame_len)
            break;

        size_t resp_len = dispatch_frame(engine, frame, frame_len, value_buf, resp, MSGPACK_FRAME_MAX);
        if (resp_len == 0)
            break; /* response overflow — drop the connection */
        uint8_t outlen[4];
        write_be32(outlen, (uint32_t)resp_len);
        yloop_write(s, outlen, 4);
        yloop_write(s, resp, resp_len);
    }

    free(frame);
    free(value_buf);
    free(resp);
}

struct msgpack_frontend_ptr_result msgpack_start(struct picomesh_engine *e,
                                                 const struct msgpack_config *cfg)
{
    if (!e)
        return PICOMESH_ERR(msgpack_frontend_ptr, "msgpack_start: NULL engine");
    const char *host = (cfg && cfg->host) ? cfg->host : "127.0.0.1";
    int port = (cfg && cfg->port > 0) ? cfg->port : 7900;

    struct yloop *l = picomesh_engine_loop(e);
    if (!l)
        return PICOMESH_ERR(msgpack_frontend_ptr, "msgpack_start: engine has no loop");

    struct msgpack_frontend *f = calloc(1, sizeof(*f));
    if (!f)
        return PICOMESH_ERR(msgpack_frontend_ptr, "msgpack_start: calloc failed");
    f->engine = e;

    struct picomesh_void_result lr = yloop_listen_tcp(l, host, port, serve_one, f);
    if (PICOMESH_IS_ERR(lr)) {
        free(f);
        return PICOMESH_ERR(msgpack_frontend_ptr, "msgpack_start: yloop_listen_tcp failed", lr);
    }
    yinfo("msgpack: listening on %s:%d", host, port);
    return PICOMESH_OK(msgpack_frontend_ptr, f);
}

void msgpack_stop(struct msgpack_frontend *f)
{
    /* The listener is owned by the yloop and torn down when the loop closes. */
    if (!f)
        return;
    free(f);
}
