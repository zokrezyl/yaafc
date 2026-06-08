/* yttp — JSON-RPC 2.0 frontend over TCP.
 *
 * One coroutine per accepted peer. Each coro reads Content-Length
 * frames, parses the JSON-RPC envelope via yjson, dispatches the
 * method through `jinvoke_for`, and writes back a framed JSON
 * response. The yloop integration is the same as yrpc — only the
 * payload shape changes.
 *
 * Method routing:
 *
 *   "create"   →  yaapp's "instantiate this class". We allocate via
 *                 `class_by_name` + `object_alloc`, register the
 *                 instance on the binary-RPC handle table so the same
 *                 u64 ↔ ptr mapping works across frontends, and reply
 *                 with `{"handle": u64}`.
 *   "invoke"   →  look up the method's jinvoke_fn, resolve handle to
 *                 a `struct object *`, call it. The args list is
 *                 forwarded as-is into the codegen invoker, which
 *                 handles type-checking the positionals.
 *   "describe" →  list every slot known on a class. Walks the
 *                 dispatch table via `class_for_each_slot`.
 *
 * No partial-frame parser yet — we read Content-Length, then the
 * exact body length in one shot. Sufficient for one round-trip per
 * peer; pipelining would need streaming parse. */

#include <picomesh/frontends/yttp/yttp.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yclass/jinvoke.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct yttp_frontend {
    struct picomesh_engine *engine;
};

/* ---------- framing -------------------------------------------- */

/* Read up to one Content-Length-framed message. Returns 0 on EOF,
 * -1 on error, length on success (body is malloc'd, caller frees). */
static int read_frame(struct yloop_stream *stream, char **body_out)
{
    char header[256];
    size_t header_len = 0;
    /* Read header byte-by-byte until \r\n\r\n. */
    while (header_len + 1 < sizeof(header)) {
        char ch;
        if (yloop_read(stream, &ch, 1) != 1) return header_len == 0 ? 0 : -1;
        header[header_len++] = ch;
        if (header_len >= 4 &&
            header[header_len - 4] == '\r' && header[header_len - 3] == '\n' &&
            header[header_len - 2] == '\r' && header[header_len - 1] == '\n') {
            break;
        }
    }
    header[header_len] = 0;

    /* Find Content-Length: <N> */
    const char *scan = header;
    size_t content_len = 0;
    int found = 0;
    while ((scan = strstr(scan, "Content-Length:")) != NULL) {
        scan += strlen("Content-Length:");
        while (*scan == ' ' || *scan == '\t') ++scan;
        content_len = (size_t)strtoull(scan, NULL, 10);
        found = 1;
        break;
    }
    if (!found) return -1;
    if (content_len == 0 || content_len > (1u << 20)) return -1;

    char *body = malloc(content_len + 1);
    if (!body) return -1;
    if (yloop_read(stream, body, content_len) != content_len) {
        free(body);
        return -1;
    }
    body[content_len] = 0;
    *body_out = body;
    return (int)content_len;
}

static int write_frame(struct yloop_stream *stream, const char *body, size_t len)
{
    char header[64];
    int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    if (yloop_write(stream, header, (size_t)header_len) != (size_t)header_len) return -1;
    if (len && yloop_write(stream, body, len) != len) return -1;
    return 0;
}

/* ---------- JSON-RPC response builders ------------------------- */


static void write_error_detail(struct yjson_writer *writer, const char *message)
{
    const char *msg = message ? message : "";
    size_t first_len = strcspn(msg, "\n");
    char first[512];
    size_t copy_len = first_len < sizeof(first) - 1 ? first_len : sizeof(first) - 1;
    memcpy(first, msg, copy_len);
    first[copy_len] = 0;
    yjson_writer_key(writer, "message"); yjson_writer_string(writer, first[0] ? first : msg);
    yjson_writer_key(writer, "detail");  yjson_writer_string(writer, msg);
    yjson_writer_key(writer, "trace");   yjson_writer_begin_array(writer);
    const char *scan = msg;
    while (*scan) {
        const char *newline = strchr(scan, '\n');
        size_t line_len = newline ? (size_t)(newline - scan) : strlen(scan);
        char line[1024];
        size_t line_copy_len = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
        memcpy(line, scan, line_copy_len);
        line[line_copy_len] = 0;
        yjson_writer_string(writer, line);
        if (!newline) break;
        scan = newline + 1;
    }
    yjson_writer_end_array(writer);
}

static void write_jsonrpc_error(struct yloop_stream *stream, const struct yjson_value *id,
                                int code, const char *message)
{
    if (code <= -32000) yerror("yttp request failed: %s", message ? message : "");
    struct yjson_writer *writer = yjson_writer_new();
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "jsonrpc"); yjson_writer_string(writer, "2.0");
    yjson_writer_key(writer, "id");
    if (yjson_is_int(id))         yjson_writer_int(writer, yjson_as_int(id, 0));
    else if (yjson_is_string(id)) yjson_writer_string(writer, yjson_as_string(id, ""));
    else                          yjson_writer_null(writer);
    yjson_writer_key(writer, "error");
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "code"); yjson_writer_int(writer, code);
    write_error_detail(writer, message);
    yjson_writer_end_object(writer);
    yjson_writer_end_object(writer);

    size_t len;
    const char *data = yjson_writer_data(writer, &len);
    write_frame(stream, data, len);
    yjson_writer_free(writer);
}

/* ---------- per-method handlers -------------------------------- */

static void handle_create(struct yloop_stream *stream, const struct yjson_value *id,
                          const struct yjson_value *params)
{
    const char *class_name = yjson_as_string(yjson_object_get(params, "class"), NULL);
    if (!class_name) {
        write_jsonrpc_error(stream, id, -32602, "create: missing 'class'");
        return;
    }
    struct class_ptr_result class_res = class_by_name(class_name);
    if (PICOMESH_IS_ERR(class_res)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "create: unknown class '%s'", class_name);
        picomesh_error_destroy(class_res.error);
        write_jsonrpc_error(stream, id, -32601, msg);
        return;
    }
    struct object_ptr_result object_res = object_alloc(class_res.value);
    if (PICOMESH_IS_ERR(object_res)) {
        picomesh_error_destroy(object_res.error);
        write_jsonrpc_error(stream, id, -32603, "create: object_alloc failed");
        return;
    }
    uint64_t handle = rpc_register_object(object_res.value);

    struct yjson_writer *writer = yjson_writer_new();
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "jsonrpc"); yjson_writer_string(writer, "2.0");
    yjson_writer_key(writer, "id");
    if (yjson_is_int(id))         yjson_writer_int(writer, yjson_as_int(id, 0));
    else if (yjson_is_string(id)) yjson_writer_string(writer, yjson_as_string(id, ""));
    else                          yjson_writer_null(writer);
    yjson_writer_key(writer, "result");
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "handle"); yjson_writer_int(writer, (int64_t)handle);
    yjson_writer_end_object(writer);
    yjson_writer_end_object(writer);

    size_t len;
    const char *data = yjson_writer_data(writer, &len);
    write_frame(stream, data, len);
    yjson_writer_free(writer);
}

static void handle_invoke(struct yloop_stream *stream, const struct yjson_value *id,
                          const struct yjson_value *params)
{
    const char *method = yjson_as_string(yjson_object_get(params, "method"), NULL);
    int64_t handle = yjson_as_int(yjson_object_get(params, "handle"), 0);
    const struct yjson_value *args = yjson_object_get(params, "args");
    if (!method) {
        write_jsonrpc_error(stream, id, -32602, "invoke: missing 'method'");
        return;
    }
    if (!handle) {
        write_jsonrpc_error(stream, id, -32602, "invoke: missing or zero 'handle'");
        return;
    }
    void *obj = rpc_handle_resolve((uint64_t)handle);
    if (!obj) {
        write_jsonrpc_error(stream, id, -32602, "invoke: unknown handle");
        return;
    }
    jinvoke_fn invoke_fn = jinvoke_for(method);
    if (!invoke_fn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "invoke: no jinvoke for '%s'", method);
        write_jsonrpc_error(stream, id, -32601, msg);
        return;
    }

    struct yjson_writer *writer = yjson_writer_new();
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "jsonrpc"); yjson_writer_string(writer, "2.0");
    yjson_writer_key(writer, "id");
    if (yjson_is_int(id))         yjson_writer_int(writer, yjson_as_int(id, 0));
    else if (yjson_is_string(id)) yjson_writer_string(writer, yjson_as_string(id, ""));
    else                          yjson_writer_null(writer);
    yjson_writer_key(writer, "result");

    char err[8192] = {0};
    /* Local dispatch: yttp owns the object in-process — NULL ctx (call
     * the impl directly) and NULL headers (no request metadata here). */
    int rc = invoke_fn(NULL, (struct object *)obj, NULL, args, writer, err, sizeof(err));
    if (rc != 0) {
        /* Roll back the partial response by discarding the writer
         * and emitting an error envelope instead. */
        yjson_writer_free(writer);
        write_jsonrpc_error(stream, id, -32000, err[0] ? err : "invoke: impl failed");
        return;
    }
    yjson_writer_end_object(writer);

    size_t len;
    const char *data = yjson_writer_data(writer, &len);
    write_frame(stream, data, len);
    yjson_writer_free(writer);
}

struct describe_ctx {
    struct yjson_writer *w;
};

static void describe_emit(const char *name, method_slot slot, void *ud)
{
    (void)slot;
    struct describe_ctx *describe_ctx = ud;
    yjson_writer_string(describe_ctx->w, name);
}

static void handle_describe(struct yloop_stream *stream, const struct yjson_value *id,
                            const struct yjson_value *params)
{
    const char *class_name = yjson_as_string(yjson_object_get(params, "class"), NULL);
    if (!class_name) {
        write_jsonrpc_error(stream, id, -32602, "describe: missing 'class'");
        return;
    }
    struct class_ptr_result class_res = class_by_name(class_name);
    if (PICOMESH_IS_ERR(class_res)) {
        picomesh_error_destroy(class_res.error);
        char msg[256];
        snprintf(msg, sizeof(msg), "describe: unknown class '%s'", class_name);
        write_jsonrpc_error(stream, id, -32601, msg);
        return;
    }
    struct yjson_writer *writer = yjson_writer_new();
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "jsonrpc"); yjson_writer_string(writer, "2.0");
    yjson_writer_key(writer, "id");
    if (yjson_is_int(id))         yjson_writer_int(writer, yjson_as_int(id, 0));
    else if (yjson_is_string(id)) yjson_writer_string(writer, yjson_as_string(id, ""));
    else                          yjson_writer_null(writer);
    yjson_writer_key(writer, "result");
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "class");   yjson_writer_string(writer, class_name);
    yjson_writer_key(writer, "methods");
    yjson_writer_begin_array(writer);
    struct describe_ctx describe_ctx = {.w = writer};
    class_for_each_slot(class_res.value, describe_emit, &describe_ctx);
    yjson_writer_end_array(writer);
    yjson_writer_end_object(writer);
    yjson_writer_end_object(writer);

    size_t len;
    const char *data = yjson_writer_data(writer, &len);
    write_frame(stream, data, len);
    yjson_writer_free(writer);
}

/* ---------- per-peer serve loop -------------------------------- */

static void serve_one(struct yloop *loop, struct yloop_stream *stream, void *ud)
{
    (void)loop; (void)ud;
    yinfo("yttp: peer connected");
    for (;;) {
        char *body = NULL;
        int frame_len = read_frame(stream, &body);
        if (frame_len <= 0) { free(body); break; }

        struct yjson_doc *doc = yjson_parse(body, (size_t)frame_len);
        if (!doc) {
            ywarn("yttp: parse error: %s", yjson_last_error());
            free(body);
            continue;
        }
        const struct yjson_value *root = yjson_doc_root(doc);
        const struct yjson_value *id = yjson_object_get(root, "id");
        const char *method = yjson_as_string(yjson_object_get(root, "method"), NULL);
        const struct yjson_value *params = yjson_object_get(root, "params");

        if (!method) {
            write_jsonrpc_error(stream, id, -32600, "missing 'method'");
        } else if (strcmp(method, "create") == 0) {
            handle_create(stream, id, params);
        } else if (strcmp(method, "invoke") == 0) {
            handle_invoke(stream, id, params);
        } else if (strcmp(method, "describe") == 0) {
            handle_describe(stream, id, params);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unknown method '%s'", method);
            write_jsonrpc_error(stream, id, -32601, msg);
        }

        yjson_doc_free(doc);
        free(body);
    }
    yinfo("yttp: peer disconnected");
}

struct yttp_frontend_ptr_result yttp_start(struct picomesh_engine *engine,
                                           const struct yttp_config *cfg)
{
    if (!engine) return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: NULL engine");
    const char *host = (cfg && cfg->host) ? cfg->host : "127.0.0.1";
    int port = (cfg && cfg->port > 0) ? cfg->port : 8800;

    struct yloop *loop = picomesh_engine_loop(engine);
    if (!loop) return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: engine has no loop");

    struct picomesh_void_result listen_res = yloop_listen_tcp(loop, host, port, serve_one, NULL);
    if (PICOMESH_IS_ERR(listen_res)) {
        return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: yloop_listen_tcp failed", listen_res);
    }
    struct yttp_frontend *frontend = calloc(1, sizeof(*frontend));
    if (!frontend) return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: calloc failed");
    frontend->engine = engine;
    yinfo("yttp: listening on %s:%d", host, port);
    return PICOMESH_OK(yttp_frontend_ptr, frontend);
}

void yttp_stop(struct yttp_frontend *frontend)
{
    free(frontend);
}
