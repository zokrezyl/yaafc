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
static int read_frame(struct yloop_stream *s, char **body_out)
{
    char header[256];
    size_t hl = 0;
    /* Read header byte-by-byte until \r\n\r\n. */
    while (hl + 1 < sizeof(header)) {
        char c;
        if (yloop_read(s, &c, 1) != 1) return hl == 0 ? 0 : -1;
        header[hl++] = c;
        if (hl >= 4 &&
            header[hl - 4] == '\r' && header[hl - 3] == '\n' &&
            header[hl - 2] == '\r' && header[hl - 1] == '\n') {
            break;
        }
    }
    header[hl] = 0;

    /* Find Content-Length: <N> */
    const char *p = header;
    size_t content_len = 0;
    int found = 0;
    while ((p = strstr(p, "Content-Length:")) != NULL) {
        p += strlen("Content-Length:");
        while (*p == ' ' || *p == '\t') ++p;
        content_len = (size_t)strtoull(p, NULL, 10);
        found = 1;
        break;
    }
    if (!found) return -1;
    if (content_len == 0 || content_len > (1u << 20)) return -1;

    char *body = malloc(content_len + 1);
    if (!body) return -1;
    if (yloop_read(s, body, content_len) != content_len) {
        free(body);
        return -1;
    }
    body[content_len] = 0;
    *body_out = body;
    return (int)content_len;
}

static int write_frame(struct yloop_stream *s, const char *body, size_t len)
{
    char hdr[64];
    int n = snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", len);
    if (yloop_write(s, hdr, (size_t)n) != (size_t)n) return -1;
    if (len && yloop_write(s, body, len) != len) return -1;
    return 0;
}

/* ---------- JSON-RPC response builders ------------------------- */


static void write_error_detail(struct yjson_writer *w, const char *message)
{
    const char *msg = message ? message : "";
    size_t first_len = strcspn(msg, "\n");
    char first[512];
    size_t copy = first_len < sizeof(first) - 1 ? first_len : sizeof(first) - 1;
    memcpy(first, msg, copy);
    first[copy] = 0;
    yjson_writer_key(w, "message"); yjson_writer_string(w, first[0] ? first : msg);
    yjson_writer_key(w, "detail");  yjson_writer_string(w, msg);
    yjson_writer_key(w, "trace");   yjson_writer_begin_array(w);
    const char *p = msg;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char line[1024];
        size_t lc = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        memcpy(line, p, lc);
        line[lc] = 0;
        yjson_writer_string(w, line);
        if (!nl) break;
        p = nl + 1;
    }
    yjson_writer_end_array(w);
}

static void write_jsonrpc_error(struct yloop_stream *s, const struct yjson_value *id,
                                int code, const char *message)
{
    if (code <= -32000) yerror("yttp request failed: %s", message ? message : "");
    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "jsonrpc"); yjson_writer_string(w, "2.0");
    yjson_writer_key(w, "id");
    if (yjson_is_int(id))         yjson_writer_int(w, yjson_as_int(id, 0));
    else if (yjson_is_string(id)) yjson_writer_string(w, yjson_as_string(id, ""));
    else                          yjson_writer_null(w);
    yjson_writer_key(w, "error");
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "code"); yjson_writer_int(w, code);
    write_error_detail(w, message);
    yjson_writer_end_object(w);
    yjson_writer_end_object(w);

    size_t len;
    const char *data = yjson_writer_data(w, &len);
    write_frame(s, data, len);
    yjson_writer_free(w);
}

/* ---------- per-method handlers -------------------------------- */

static void handle_create(struct yloop_stream *s, const struct yjson_value *id,
                          const struct yjson_value *params)
{
    const char *class_name = yjson_as_string(yjson_object_get(params, "class"), NULL);
    if (!class_name) {
        write_jsonrpc_error(s, id, -32602, "create: missing 'class'");
        return;
    }
    struct class_ptr_result cr = class_by_name(class_name);
    if (PICOMESH_IS_ERR(cr)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "create: unknown class '%s'", class_name);
        picomesh_error_destroy(cr.error);
        write_jsonrpc_error(s, id, -32601, msg);
        return;
    }
    struct object_ptr_result orr = object_alloc(cr.value);
    if (PICOMESH_IS_ERR(orr)) {
        picomesh_error_destroy(orr.error);
        write_jsonrpc_error(s, id, -32603, "create: object_alloc failed");
        return;
    }
    uint64_t handle = rpc_register_object(orr.value);

    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "jsonrpc"); yjson_writer_string(w, "2.0");
    yjson_writer_key(w, "id");
    if (yjson_is_int(id))         yjson_writer_int(w, yjson_as_int(id, 0));
    else if (yjson_is_string(id)) yjson_writer_string(w, yjson_as_string(id, ""));
    else                          yjson_writer_null(w);
    yjson_writer_key(w, "result");
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "handle"); yjson_writer_int(w, (int64_t)handle);
    yjson_writer_end_object(w);
    yjson_writer_end_object(w);

    size_t len;
    const char *data = yjson_writer_data(w, &len);
    write_frame(s, data, len);
    yjson_writer_free(w);
}

static void handle_invoke(struct yloop_stream *s, const struct yjson_value *id,
                          const struct yjson_value *params)
{
    const char *method = yjson_as_string(yjson_object_get(params, "method"), NULL);
    int64_t handle = yjson_as_int(yjson_object_get(params, "handle"), 0);
    const struct yjson_value *args = yjson_object_get(params, "args");
    if (!method) {
        write_jsonrpc_error(s, id, -32602, "invoke: missing 'method'");
        return;
    }
    if (!handle) {
        write_jsonrpc_error(s, id, -32602, "invoke: missing or zero 'handle'");
        return;
    }
    void *obj = rpc_handle_resolve((uint64_t)handle);
    if (!obj) {
        write_jsonrpc_error(s, id, -32602, "invoke: unknown handle");
        return;
    }
    jinvoke_fn fn = jinvoke_for(method);
    if (!fn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "invoke: no jinvoke for '%s'", method);
        write_jsonrpc_error(s, id, -32601, msg);
        return;
    }

    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "jsonrpc"); yjson_writer_string(w, "2.0");
    yjson_writer_key(w, "id");
    if (yjson_is_int(id))         yjson_writer_int(w, yjson_as_int(id, 0));
    else if (yjson_is_string(id)) yjson_writer_string(w, yjson_as_string(id, ""));
    else                          yjson_writer_null(w);
    yjson_writer_key(w, "result");

    char err[8192] = {0};
    /* Local dispatch: yttp owns the object in-process — NULL ctx (call
     * the impl directly) and NULL headers (no request metadata here). */
    int rc = fn(NULL, (struct object *)obj, NULL, args, w, err, sizeof(err));
    if (rc != 0) {
        /* Roll back the partial response by discarding the writer
         * and emitting an error envelope instead. */
        yjson_writer_free(w);
        write_jsonrpc_error(s, id, -32000, err[0] ? err : "invoke: impl failed");
        return;
    }
    yjson_writer_end_object(w);

    size_t len;
    const char *data = yjson_writer_data(w, &len);
    write_frame(s, data, len);
    yjson_writer_free(w);
}

struct describe_ctx {
    struct yjson_writer *w;
};

static void describe_emit(const char *name, method_slot slot, void *ud)
{
    (void)slot;
    struct describe_ctx *dc = ud;
    yjson_writer_string(dc->w, name);
}

static void handle_describe(struct yloop_stream *s, const struct yjson_value *id,
                            const struct yjson_value *params)
{
    const char *class_name = yjson_as_string(yjson_object_get(params, "class"), NULL);
    if (!class_name) {
        write_jsonrpc_error(s, id, -32602, "describe: missing 'class'");
        return;
    }
    struct class_ptr_result cr = class_by_name(class_name);
    if (PICOMESH_IS_ERR(cr)) {
        picomesh_error_destroy(cr.error);
        char msg[256];
        snprintf(msg, sizeof(msg), "describe: unknown class '%s'", class_name);
        write_jsonrpc_error(s, id, -32601, msg);
        return;
    }
    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "jsonrpc"); yjson_writer_string(w, "2.0");
    yjson_writer_key(w, "id");
    if (yjson_is_int(id))         yjson_writer_int(w, yjson_as_int(id, 0));
    else if (yjson_is_string(id)) yjson_writer_string(w, yjson_as_string(id, ""));
    else                          yjson_writer_null(w);
    yjson_writer_key(w, "result");
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "class");   yjson_writer_string(w, class_name);
    yjson_writer_key(w, "methods");
    yjson_writer_begin_array(w);
    struct describe_ctx dc = {.w = w};
    class_for_each_slot(cr.value, describe_emit, &dc);
    yjson_writer_end_array(w);
    yjson_writer_end_object(w);
    yjson_writer_end_object(w);

    size_t len;
    const char *data = yjson_writer_data(w, &len);
    write_frame(s, data, len);
    yjson_writer_free(w);
}

/* ---------- per-peer serve loop -------------------------------- */

static void serve_one(struct yloop *l, struct yloop_stream *s, void *ud)
{
    (void)l; (void)ud;
    yinfo("yttp: peer connected");
    for (;;) {
        char *body = NULL;
        int n = read_frame(s, &body);
        if (n <= 0) { free(body); break; }

        struct yjson_doc *doc = yjson_parse(body, (size_t)n);
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
            write_jsonrpc_error(s, id, -32600, "missing 'method'");
        } else if (strcmp(method, "create") == 0) {
            handle_create(s, id, params);
        } else if (strcmp(method, "invoke") == 0) {
            handle_invoke(s, id, params);
        } else if (strcmp(method, "describe") == 0) {
            handle_describe(s, id, params);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unknown method '%s'", method);
            write_jsonrpc_error(s, id, -32601, msg);
        }

        yjson_doc_free(doc);
        free(body);
    }
    yinfo("yttp: peer disconnected");
}

struct yttp_frontend_ptr_result yttp_start(struct picomesh_engine *e,
                                           const struct yttp_config *cfg)
{
    if (!e) return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: NULL engine");
    const char *host = (cfg && cfg->host) ? cfg->host : "127.0.0.1";
    int port = (cfg && cfg->port > 0) ? cfg->port : 8800;

    struct yloop *l = picomesh_engine_loop(e);
    if (!l) return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: engine has no loop");

    struct picomesh_void_result lr = yloop_listen_tcp(l, host, port, serve_one, NULL);
    if (PICOMESH_IS_ERR(lr)) {
        return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: yloop_listen_tcp failed", lr);
    }
    struct yttp_frontend *f = calloc(1, sizeof(*f));
    if (!f) return PICOMESH_ERR(yttp_frontend_ptr, "yttp_start: calloc failed");
    f->engine = e;
    yinfo("yttp: listening on %s:%d", host, port);
    return PICOMESH_OK(yttp_frontend_ptr, f);
}

void yttp_stop(struct yttp_frontend *f)
{
    free(f);
}
