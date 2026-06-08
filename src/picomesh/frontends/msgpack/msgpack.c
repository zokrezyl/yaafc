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
    cmp_ctx_t writer;
    struct picomesh_msgpack_buffer writer_buf;
    picomesh_msgpack_writer_init(&writer, &writer_buf, resp, resp_cap);
    cmp_write_map(&writer, 3);
    cmp_write_str(&writer, "v", 1);
    cmp_write_integer(&writer, 1);
    cmp_write_str(&writer, "ok", 2);
    cmp_write_bool(&writer, false);
    cmp_write_str(&writer, "error", 5);
    cmp_write_map(&writer, 2);
    cmp_write_str(&writer, "message", 7);
    cmp_write_str(&writer, message, (uint32_t)strlen(message));
    cmp_write_str(&writer, "code", 4);
    cmp_write_str(&writer, code, (uint32_t)strlen(code));
    return writer_buf.offset; /* 0 if the buffer overflowed */
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
static void decode_headers(cmp_ctx_t *reader, uint32_t count, struct yheaders *hdrs)
{
    for (uint32_t i = 0; i < count; ++i) {
        char key[64];
        uint32_t klen = (uint32_t)sizeof(key);
        if (!cmp_read_str(reader, key, &klen))
            return;
        if (strcmp(key, "uid") == 0 || strcmp(key, "caller_uid") == 0) {
            uint64_t value = 0;
            if (!cmp_read_uinteger(reader, &value))
                return;
            if (hdrs)
                yheaders_set_u32(hdrs, "uid", (uint32_t)value);
        } else if (strcmp(key, "sid") == 0 || strcmp(key, "caller_sid") == 0) {
            uint64_t value = 0;
            if (!cmp_read_uinteger(reader, &value))
                return;
            if (hdrs)
                yheaders_set_u32(hdrs, "sid", (uint32_t)value);
        } else if (strcmp(key, "trace_id") == 0 || strcmp(key, "traceparent") == 0) {
            char val[160];
            uint32_t vlen = (uint32_t)sizeof(val);
            if (!cmp_read_str(reader, val, &vlen))
                return;
            if (hdrs)
                yheaders_set(hdrs, key, val);
        } else {
            if (!cmp_skip_object_no_limit(reader))
                return;
        }
    }
}

/* Build the success envelope: { "v":1, "ok":true, "result": <value> } where
 * the result value is the already-encoded `value`/`value_len` bytes. Returns
 * the response length, or 0 on overflow. */
static size_t pack_success(uint8_t *resp, size_t resp_cap, const uint8_t *value, size_t value_len)
{
    cmp_ctx_t writer;
    struct picomesh_msgpack_buffer writer_buf;
    picomesh_msgpack_writer_init(&writer, &writer_buf, resp, resp_cap);
    if (!cmp_write_map(&writer, 3))
        return 0;
    if (!cmp_write_str(&writer, "v", 1) || !cmp_write_integer(&writer, 1))
        return 0;
    if (!cmp_write_str(&writer, "ok", 2) || !cmp_write_bool(&writer, true))
        return 0;
    if (!cmp_write_str(&writer, "result", 6))
        return 0;
    /* The result value is a complete, pre-encoded msgpack value — append its
     * bytes verbatim as the map's value (msgpack is concatenative). */
    if (writer_buf.offset + value_len > writer_buf.cap)
        return 0;
    memcpy(writer_buf.data + writer_buf.offset, value, value_len);
    writer_buf.offset += value_len;
    return writer_buf.offset;
}

/* op == "invoke". Resolve+gate the path, run the minvoke, build the response
 * envelope into `resp`. `args_present`/`args_offset` locate the args array in
 * the original frame. The caller has already rejected any non-empty kwargs. */
static size_t handle_invoke(struct picomesh_engine *engine, const char *path, const uint8_t *frame,
                            size_t frame_len, int args_present, size_t args_offset,
                            struct yheaders *hdrs, uint8_t *value_buf, uint8_t *resp,
                            size_t resp_cap)
{
    if (!path || !*path)
        return pack_error(resp, resp_cap, "bad_path", "missing 'path'");

    struct picomesh_service_call_result call_res = picomesh_resolve_service_call(engine, path);
    if (PICOMESH_IS_ERR(call_res)) {
        const char *msg = call_res.error.msg ? call_res.error.msg : "resolve failed";
        size_t response_len = pack_error(resp, resp_cap, resolve_code(msg), msg);
        picomesh_error_destroy(call_res.error);
        return response_len;
    }
    struct picomesh_service_call call = call_res.value;

    minvoke_fn method_fn = minvoke_for(call.method_qname);
    if (!method_fn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "no method '%s'", call.method_qname);
        picomesh_service_call_release(&call);
        return pack_error(resp, resp_cap, "no_such_method", msg);
    }

    /* Re-open a reader on the original frame at the args array and read its
     * length; absent args == zero positional args. */
    cmp_ctx_t args_reader;
    struct picomesh_msgpack_buffer args_reader_buf;
    uint32_t argc = 0;
    if (args_present) {
        picomesh_msgpack_reader_init(&args_reader, &args_reader_buf, frame, frame_len);
        args_reader_buf.offset = args_offset;
        if (!cmp_read_array(&args_reader, &argc)) {
            picomesh_service_call_release(&call);
            return pack_error(resp, resp_cap, "bad_envelope", "'args' is not an array");
        }
    } else {
        picomesh_msgpack_reader_init(&args_reader, &args_reader_buf, frame, frame_len);
    }

    cmp_ctx_t value_writer;
    struct picomesh_msgpack_buffer value_buf_state;
    picomesh_msgpack_writer_init(&value_writer, &value_buf_state, value_buf, MSGPACK_FRAME_MAX);

    char err[8192] = {0};
    int call_rc = method_fn(&call.ctx, call.obj, hdrs, &args_reader, argc, &value_writer, err,
                            sizeof(err));
    picomesh_service_call_release(&call);

    if (call_rc != 0) {
        yerror("msgpack request failed path=%s: %s", path ? path : "", err[0] ? err : "call failed");
        return pack_error(resp, resp_cap, "call_error", err[0] ? err : "call failed");
    }
    size_t response_len = pack_success(resp, resp_cap, value_buf, value_buf_state.offset);
    if (response_len == 0)
        return pack_error(resp, resp_cap, "response_too_large", "result exceeds frame cap");
    return response_len;
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

    struct picomesh_service_call_result call_res = picomesh_resolve_service_call(engine, path);
    if (PICOMESH_IS_ERR(call_res)) {
        const char *msg = call_res.error.msg ? call_res.error.msg : "resolve failed";
        size_t response_len = pack_error(resp, resp_cap, resolve_code(msg), msg);
        picomesh_error_destroy(call_res.error);
        return response_len;
    }
    struct picomesh_service_call call = call_res.value;
    const struct jinvoke_params *params = call.params;
    if (!params) {
        picomesh_service_call_release(&call);
        return pack_error(resp, resp_cap, "no_such_method", "describe: unknown method");
    }

    uint8_t value[8192];
    cmp_ctx_t value_writer;
    struct picomesh_msgpack_buffer value_buf_state;
    picomesh_msgpack_writer_init(&value_writer, &value_buf_state, value, sizeof(value));
    cmp_write_map(&value_writer, 2);
    cmp_write_str(&value_writer, "path", 4);
    cmp_write_str(&value_writer, path, (uint32_t)strlen(path));
    cmp_write_str(&value_writer, "params", 6);
    cmp_write_array(&value_writer, (uint32_t)params->count);
    for (size_t i = 0; i < params->count; ++i) {
        cmp_write_map(&value_writer, 2);
        cmp_write_str(&value_writer, "name", 4);
        cmp_write_str(&value_writer, params->items[i].name, (uint32_t)strlen(params->items[i].name));
        cmp_write_str(&value_writer, "type", 4);
        cmp_write_str(&value_writer, params->items[i].type, (uint32_t)strlen(params->items[i].type));
    }
    picomesh_service_call_release(&call);
    size_t response_len = pack_success(resp, resp_cap, value, value_buf_state.offset);
    if (response_len == 0)
        return pack_error(resp, resp_cap, "response_too_large", "describe result too large");
    return response_len;
}

/* Decode one request frame and build its response envelope into `resp`.
 * `value_buf` is MSGPACK_FRAME_MAX scratch for the result value. Returns the
 * response length (always > 0 for a well-formed connection — even errors are
 * a response). */
static size_t dispatch_frame(struct picomesh_engine *engine, const uint8_t *frame, size_t frame_len,
                             uint8_t *value_buf, uint8_t *resp, size_t resp_cap)
{
    cmp_ctx_t reader;
    struct picomesh_msgpack_buffer reader_buf;
    picomesh_msgpack_reader_init(&reader, &reader_buf, frame, frame_len);

    uint32_t top = 0;
    if (!cmp_read_map(&reader, &top))
        return pack_error(resp, resp_cap, "bad_envelope", "envelope is not a msgpack map");

    char op[32] = "invoke";
    char path[256] = {0};
    int args_present = 0, kwargs_nonempty = 0;
    size_t args_offset = 0;
    struct yheaders *hdrs = yheaders_new();

    for (uint32_t i = 0; i < top; ++i) {
        char key[32];
        uint32_t klen = (uint32_t)sizeof(key);
        if (!cmp_read_str(&reader, key, &klen)) {
            yheaders_free(hdrs);
            return pack_error(resp, resp_cap, "bad_envelope", "bad envelope key");
        }
        if (strcmp(key, "op") == 0) {
            uint32_t read_len = (uint32_t)sizeof(op);
            if (!cmp_read_str(&reader, op, &read_len)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "bad 'op'");
            }
        } else if (strcmp(key, "path") == 0) {
            uint32_t read_len = (uint32_t)sizeof(path);
            if (!cmp_read_str(&reader, path, &read_len)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "bad 'path'");
            }
        } else if (strcmp(key, "args") == 0) {
            args_present = 1;
            args_offset = reader_buf.offset;
            if (!cmp_skip_object_no_limit(&reader)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "bad 'args'");
            }
        } else if (strcmp(key, "kwargs") == 0) {
            uint32_t kwargs_count = 0;
            if (!cmp_read_map(&reader, &kwargs_count)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "'kwargs' is not a map");
            }
            kwargs_nonempty = kwargs_count > 0;
            for (uint32_t j = 0; j < kwargs_count * 2u; ++j) {
                if (!cmp_skip_object_no_limit(&reader)) {
                    yheaders_free(hdrs);
                    return pack_error(resp, resp_cap, "bad_envelope", "bad 'kwargs'");
                }
            }
        } else if (strcmp(key, "headers") == 0) {
            uint32_t headers_count = 0;
            if (!cmp_read_map(&reader, &headers_count)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "'headers' is not a map");
            }
            decode_headers(&reader, headers_count, hdrs);
        } else {
            if (!cmp_skip_object_no_limit(&reader)) {
                yheaders_free(hdrs);
                return pack_error(resp, resp_cap, "bad_envelope", "bad envelope value");
            }
        }
    }

    /* v1 is positional-only: a non-empty `kwargs` is rejected for every op
     * (invoke AND describe) before dispatch, never silently ignored. */
    if (kwargs_nonempty) {
        yheaders_free(hdrs);
        return pack_error(resp, resp_cap, "kwargs_unsupported",
                          "msgpack v1 binds positional args only; kwargs must be empty");
    }

    size_t response_len;
    if (strcmp(op, "describe") == 0)
        response_len = handle_describe(engine, path, resp, resp_cap);
    else
        response_len = handle_invoke(engine, path, frame, frame_len, args_present, args_offset, hdrs,
                                     value_buf, resp, resp_cap);
    yheaders_free(hdrs);
    return response_len;
}

static void serve_one(struct yloop *l, struct yloop_stream *s, void *ud)
{
    (void)l;
    struct msgpack_frontend *frontend = ud;
    struct picomesh_engine *engine = frontend ? frontend->engine : NULL;

    uint8_t *frame = malloc(MSGPACK_FRAME_MAX);
    uint8_t *value_buf = malloc(MSGPACK_FRAME_MAX);
    uint8_t *resp = malloc(MSGPACK_FRAME_MAX);
    if (!frame || !value_buf || !resp) {
        /* Root coroutine: nowhere to propagate, but local resource exhaustion
         * must be visible rather than a silently dropped connection. */
        yerror("msgpack: per-connection scratch alloc failed (%u bytes ×3)", MSGPACK_FRAME_MAX);
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
        if (resp_len == 0) {
            /* Response could not be framed (overflow) — log before dropping the
             * connection so the failure isn't silent at the root boundary. */
            yerror("msgpack: response framing overflow, dropping connection");
            break;
        }
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
