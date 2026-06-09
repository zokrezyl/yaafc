/* yttp — JSON-RPC 2.0 frontend over TCP.
 *
 * One coroutine per accepted peer. Each coro reads Content-Length
 * frames, parses the JSON-RPC envelope via json, dispatches the
 * method through `jinvoke_for`, and writes back a framed JSON
 * response. The loop integration is the same as yrpc — only the
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
#include <picomesh/engine/engine.h>
#include <picomesh/loop/loop.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/picoclass/jinvoke.h>
#include <picomesh/json/json.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>

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
static int read_frame(struct loop_stream *stream, char **body_out)
{
    char header[256];
    size_t header_len = 0;
    /* Read header byte-by-byte until \r\n\r\n. */
    while (header_len + 1 < sizeof(header)) {
        char ch;
        struct picomesh_size_result read_res = loop_read(stream, &ch, 1);
        if (PICOMESH_IS_ERR(read_res)) { picomesh_error_destroy(read_res.error); return header_len == 0 ? 0 : -1; }
        if (read_res.value != 1) return header_len == 0 ? 0 : -1; /* clean EOF (idle) vs partial frame */
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
    struct picomesh_size_result body_read = loop_read(stream, body, content_len);
    if (PICOMESH_IS_ERR(body_read)) { picomesh_error_destroy(body_read.error); free(body); return -1; }
    if (body_read.value != content_len) {
        free(body);
        return -1;
    }
    body[content_len] = 0;
    *body_out = body;
    return (int)content_len;
}

static int write_frame(struct loop_stream *stream, const char *body, size_t len)
{
    char header[64];
    int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    struct picomesh_size_result header_write = loop_write(stream, header, (size_t)header_len);
    if (PICOMESH_IS_ERR(header_write)) { picomesh_error_destroy(header_write.error); return -1; }
    if (len) {
        struct picomesh_size_result body_write = loop_write(stream, body, len);
        if (PICOMESH_IS_ERR(body_write)) { picomesh_error_destroy(body_write.error); return -1; }
    }
    return 0;
}

/* ---------- JSON-RPC response builders ------------------------- */


static void write_error_detail(struct json_writer *writer, const char *message)
{
    const char *msg = message ? message : "";
    size_t first_len = strcspn(msg, "\n");
    char first[512];
    size_t copy_len = first_len < sizeof(first) - 1 ? first_len : sizeof(first) - 1;
    memcpy(first, msg, copy_len);
    first[copy_len] = 0;
    json_writer_key(writer, "message"); json_writer_string(writer, first[0] ? first : msg);
    json_writer_key(writer, "detail");  json_writer_string(writer, msg);
    json_writer_key(writer, "trace");   json_writer_begin_array(writer);
    const char *scan = msg;
    while (*scan) {
        const char *newline = strchr(scan, '\n');
        size_t line_len = newline ? (size_t)(newline - scan) : strlen(scan);
        char line[1024];
        size_t line_copy_len = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
        memcpy(line, scan, line_copy_len);
        line[line_copy_len] = 0;
        json_writer_string(writer, line);
        if (!newline) break;
        scan = newline + 1;
    }
    json_writer_end_array(writer);
}

static void write_jsonrpc_error(struct loop_stream *stream, const struct json_value *id,
                                int code, const char *message)
{
    if (code <= -32000) yerror("yttp request failed: %s", message ? message : "");
    struct json_writer *writer = json_writer_new();
    json_writer_begin_object(writer);
    json_writer_key(writer, "jsonrpc"); json_writer_string(writer, "2.0");
    json_writer_key(writer, "id");
    if (json_is_int(id))         json_writer_int(writer, json_as_int(id, 0));
    else if (json_is_string(id)) json_writer_string(writer, json_as_string(id, ""));
    else                          json_writer_null(writer);
    json_writer_key(writer, "error");
    json_writer_begin_object(writer);
    json_writer_key(writer, "code"); json_writer_int(writer, code);
    write_error_detail(writer, message);
    json_writer_end_object(writer);
    json_writer_end_object(writer);

    size_t len;
    const char *data = json_writer_data(writer, &len);
    write_frame(stream, data, len);
    json_writer_free(writer);
}

/* ---------- per-method handlers -------------------------------- */

static struct picomesh_void_result handle_create(struct loop_stream *stream, const struct json_value *id,
                          const struct json_value *params)
{
    const char *class_name = json_as_string(json_object_get(params, "class"), NULL);
    if (!class_name) {
        write_jsonrpc_error(stream, id, -32602, "create: missing 'class'");
        return PICOMESH_OK_VOID();
    }
    struct class_ptr_result class_res = class_by_name(class_name);
    if (PICOMESH_IS_ERR(class_res)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "create: unknown class '%s'", class_name);
        picomesh_error_print(stderr, "yttp handle_create: class_by_name", class_res.error);
        picomesh_error_destroy(class_res.error);
        write_jsonrpc_error(stream, id, -32601, msg);
        return PICOMESH_OK_VOID();
    }
    struct object_ptr_result object_res = object_alloc(class_res.value);
    if (PICOMESH_IS_ERR(object_res)) {
        char msg[8192];
        picomesh_error_snprint(msg, sizeof(msg), object_res.error);
        picomesh_error_print(stderr, "yttp handle_create: object_alloc", object_res.error);
        picomesh_error_destroy(object_res.error);
        write_jsonrpc_error(stream, id, -32603, msg);
        return PICOMESH_OK_VOID();
    }
    uint64_t handle = rpc_register_object(object_res.value);
    if (handle == 0) {
        /* Object table full — free the leaked object, don't hand back handle 0. */
        object_free(object_res.value);
        write_jsonrpc_error(stream, id, -32603, "create: object handle table full");
        return PICOMESH_OK_VOID();
    }

    struct json_writer *writer = json_writer_new();
    json_writer_begin_object(writer);
    json_writer_key(writer, "jsonrpc"); json_writer_string(writer, "2.0");
    json_writer_key(writer, "id");
    if (json_is_int(id))         json_writer_int(writer, json_as_int(id, 0));
    else if (json_is_string(id)) json_writer_string(writer, json_as_string(id, ""));
    else                          json_writer_null(writer);
    json_writer_key(writer, "result");
    json_writer_begin_object(writer);
    json_writer_key(writer, "handle"); json_writer_int(writer, (int64_t)handle);
    json_writer_end_object(writer);
    json_writer_end_object(writer);

    size_t len;
    const char *data = json_writer_data(writer, &len);
    write_frame(stream, data, len);
    json_writer_free(writer);
    return PICOMESH_OK_VOID();
}

static void handle_invoke(struct loop_stream *stream, const struct json_value *id,
                          const struct json_value *params)
{
    const char *method = json_as_string(json_object_get(params, "method"), NULL);
    int64_t handle = json_as_int(json_object_get(params, "handle"), 0);
    const struct json_value *args = json_object_get(params, "args");
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

    struct json_writer *writer = json_writer_new();
    json_writer_begin_object(writer);
    json_writer_key(writer, "jsonrpc"); json_writer_string(writer, "2.0");
    json_writer_key(writer, "id");
    if (json_is_int(id))         json_writer_int(writer, json_as_int(id, 0));
    else if (json_is_string(id)) json_writer_string(writer, json_as_string(id, ""));
    else                          json_writer_null(writer);
    json_writer_key(writer, "result");

    char err[8192] = {0};
    /* Local dispatch: yttp owns the object in-process — NULL ctx (call
     * the impl directly) and NULL headers (no request metadata here). */
    struct picomesh_void_result invoke_res =
        invoke_fn(NULL, (struct object *)obj, NULL, args, writer, err, sizeof(err));
    if (PICOMESH_IS_ERR(invoke_res)) {
        /* Roll back the partial response by discarding the writer
         * and emitting an error envelope instead. */
        picomesh_error_destroy(invoke_res.error);
        json_writer_free(writer);
        write_jsonrpc_error(stream, id, -32000, err[0] ? err : "invoke: impl failed");
        return;
    }
    json_writer_end_object(writer);

    size_t len;
    const char *data = json_writer_data(writer, &len);
    write_frame(stream, data, len);
    json_writer_free(writer);
}

struct describe_ctx {
    struct json_writer *w;
};

static void describe_emit(const char *name, method_slot slot, void *ud)
{
    (void)slot;
    struct describe_ctx *describe_ctx = ud;
    json_writer_string(describe_ctx->w, name);
}

static struct picomesh_void_result handle_describe(struct loop_stream *stream, const struct json_value *id,
                            const struct json_value *params)
{
    const char *class_name = json_as_string(json_object_get(params, "class"), NULL);
    if (!class_name) {
        write_jsonrpc_error(stream, id, -32602, "describe: missing 'class'");
        return PICOMESH_OK_VOID();
    }
    struct class_ptr_result class_res = class_by_name(class_name);
    if (PICOMESH_IS_ERR(class_res)) {
        picomesh_error_print(stderr, "yttp handle_describe: class_by_name", class_res.error);
        picomesh_error_destroy(class_res.error);
        char msg[256];
        snprintf(msg, sizeof(msg), "describe: unknown class '%s'", class_name);
        write_jsonrpc_error(stream, id, -32601, msg);
        return PICOMESH_OK_VOID();
    }
    struct json_writer *writer = json_writer_new();
    json_writer_begin_object(writer);
    json_writer_key(writer, "jsonrpc"); json_writer_string(writer, "2.0");
    json_writer_key(writer, "id");
    if (json_is_int(id))         json_writer_int(writer, json_as_int(id, 0));
    else if (json_is_string(id)) json_writer_string(writer, json_as_string(id, ""));
    else                          json_writer_null(writer);
    json_writer_key(writer, "result");
    json_writer_begin_object(writer);
    json_writer_key(writer, "class");   json_writer_string(writer, class_name);
    json_writer_key(writer, "methods");
    json_writer_begin_array(writer);
    struct describe_ctx describe_ctx = {.w = writer};
    struct picomesh_void_result slots_res = class_for_each_slot(class_res.value, describe_emit, &describe_ctx);
    if (PICOMESH_IS_ERR(slots_res)) {
        picomesh_error_print(stderr, "yttp handle_describe: class_for_each_slot", slots_res.error);
        picomesh_error_destroy(slots_res.error);
    }
    json_writer_end_array(writer);
    json_writer_end_object(writer);
    json_writer_end_object(writer);

    size_t len;
    const char *data = json_writer_data(writer, &len);
    write_frame(stream, data, len);
    json_writer_free(writer);
    return PICOMESH_OK_VOID();
}

/* ---------- per-peer serve loop -------------------------------- */

/* Per-peer reader coroutine — its void signature is fixed by the loop's
 * accept-handler API. The per-method handlers already write their own JSON-RPC
 * errors; a framework-level Result failure is absorbed here (chain rendered). */
PICOMESH_EXTERNAL_CALLBACK
static void serve_one(struct loop *loop, struct loop_stream *stream, void *ud)
{
    (void)loop; (void)ud;
    yinfo("yttp: peer connected");
    for (;;) {
        char *body = NULL;
        int frame_len = read_frame(stream, &body);
        if (frame_len <= 0) { free(body); break; }

        struct json_doc *doc = json_parse(body, (size_t)frame_len);
        if (!doc) {
            ywarn("yttp: parse error: %s", json_last_error());
            free(body);
            continue;
        }
        const struct json_value *root = json_doc_root(doc);
        const struct json_value *id = json_object_get(root, "id");
        const char *method = json_as_string(json_object_get(root, "method"), NULL);
        const struct json_value *params = json_object_get(root, "params");

        if (!method) {
            write_jsonrpc_error(stream, id, -32600, "missing 'method'");
        } else if (strcmp(method, "create") == 0) {
            struct picomesh_void_result handler_res = handle_create(stream, id, params);
            if (PICOMESH_IS_ERR(handler_res)) {
                picomesh_error_print(stderr, "yttp: handle_create", handler_res.error);
                picomesh_error_destroy(handler_res.error);
            }
        } else if (strcmp(method, "invoke") == 0) {
            handle_invoke(stream, id, params);
        } else if (strcmp(method, "describe") == 0) {
            struct picomesh_void_result handler_res = handle_describe(stream, id, params);
            if (PICOMESH_IS_ERR(handler_res)) {
                picomesh_error_print(stderr, "yttp: handle_describe", handler_res.error);
                picomesh_error_destroy(handler_res.error);
            }
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unknown method '%s'", method);
            write_jsonrpc_error(stream, id, -32601, msg);
        }

        json_doc_free(doc);
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

    struct loop *loop = picomesh_engine_loop(engine);
    if (!loop) return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: engine has no loop");

    struct picomesh_void_result listen_res = loop_listen_tcp(loop, host, port, serve_one, NULL);
    if (PICOMESH_IS_ERR(listen_res)) {
        return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: loop_listen_tcp failed", listen_res);
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
