/* yhttp — HTTP/1.1 + JSON frontend.
 *
 * Per-peer serve coroutine reads one HTTP request via picohttpparser,
 * routes it, writes the HTTP/1.1 response. Body must be Content-Length
 * delimited (no chunked yet); we cap bodies at 256 KB.
 *
 * Body parsing on the way in: simdjson (yjson_parse).
 * Body serialization on the way out: yjson_writer.
 * Dispatch backend: jinvoke_for + the rpc handle table — same as yttp,
 * so the same instance handles span both frontends in one process. */

#include <picomesh/frontends/yhttp/yhttp.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yclass/jinvoke.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <unistd.h>

#include <picohttpparser.h>

#include "frontend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YHTTP_REQ_BUF      (256 * 1024)
#define YHTTP_MAX_HEADERS  64

struct yhttp_frontend {
    struct picomesh_engine *engine;
};

/* ---- helpers ---------------------------------------------------- */

static int header_match(const struct phr_header *hdrs, size_t count,
                        const char *want, char *out, size_t out_cap)
{
    size_t want_len = strlen(want);
    for (size_t i = 0; i < count; ++i) {
        if (hdrs[i].name_len != want_len) continue;
        int matched = 1;
        for (size_t j = 0; j < want_len; ++j) {
            char name_char = hdrs[i].name[j];
            char want_char = want[j];
            if (name_char >= 'A' && name_char <= 'Z') name_char = (char)(name_char - 'A' + 'a');
            if (want_char >= 'A' && want_char <= 'Z') want_char = (char)(want_char - 'A' + 'a');
            if (name_char != want_char) { matched = 0; break; }
        }
        if (!matched) continue;
        size_t copy = hdrs[i].value_len < out_cap - 1
                          ? hdrs[i].value_len : out_cap - 1;
        memcpy(out, hdrs[i].value, copy);
        out[copy] = 0;
        return 1;
    }
    return 0;
}

static void send_response(struct yloop_stream *stream, int status,
                          const char *content_type,
                          const char *body, size_t body_len, int keep_alive)
{
    const char *reason = "OK";
    if (status == 400) reason = "Bad Request";
    else if (status == 404) reason = "Not Found";
    else if (status == 405) reason = "Method Not Allowed";
    else if (status == 500) reason = "Internal Server Error";

    /* CORS: pages served by the frontend at :8080 fetch JSON from
     * other services on :8201..:8211 — cross-origin from the browser's
     * perspective. Always-on `*` keeps the local demo simple; a real
     * deploy would gate this on a configured origins list. */
    char header[768];
    int written = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "\r\n",
        status, reason,
        content_type,
        body_len,
        keep_alive ? "keep-alive" : "close");
    if (written <= 0) return;
    yloop_write(stream, header, (size_t)written);
    if (body_len) yloop_write(stream, body, body_len);
}


static void write_error_detail(struct yjson_writer *writer, const char *message)
{
    const char *msg = message ? message : "";
    size_t first_len = strcspn(msg, "\n");
    char first[512];
    size_t copy = first_len < sizeof(first) - 1 ? first_len : sizeof(first) - 1;
    memcpy(first, msg, copy);
    first[copy] = 0;
    yjson_writer_key(writer, "message"); yjson_writer_string(writer, first[0] ? first : msg);
    yjson_writer_key(writer, "detail");  yjson_writer_string(writer, msg);
    yjson_writer_key(writer, "trace");   yjson_writer_begin_array(writer);
    const char *cursor = msg;
    while (*cursor) {
        const char *newline = strchr(cursor, '\n');
        size_t line_len = newline ? (size_t)(newline - cursor) : strlen(cursor);
        char line[1024];
        size_t line_copy = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
        memcpy(line, cursor, line_copy);
        line[line_copy] = 0;
        yjson_writer_string(writer, line);
        if (!newline) break;
        cursor = newline + 1;
    }
    yjson_writer_end_array(writer);
}

static void send_json_error(struct yloop_stream *stream, int status,
                            int code, const char *message, int keep_alive)
{
    if (status >= 500) yerror("yhttp request failed: %s", message ? message : "");
    struct yjson_writer *writer = yjson_writer_new();
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "error");
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "code"); yjson_writer_int(writer, code);
    write_error_detail(writer, message);
    yjson_writer_end_object(writer);
    yjson_writer_end_object(writer);
    size_t len;
    const char *data = yjson_writer_data(writer, &len);
    send_response(stream, status, "application/json", data, len, keep_alive);
    yjson_writer_free(writer);
}

/* ---- route handlers --------------------------------------------- */

static void route_root(struct yloop_stream *stream, int keep_alive)
{
    static const char body[] =
        "picomesh/yhttp frontend\n"
        "\n"
        "POST /create   {\"class\": \"<plugin>_<class>\"}                            -> {\"handle\": u64}\n"
        "POST /invoke   {\"method\": \"<qname>\", \"handle\": u64, \"args\": [...]}    -> {\"result\": ...}\n"
        "GET  /describe?class=NAME                                              -> {\"class\":..., \"methods\":[...]}\n";
    send_response(stream, 200, "text/plain", body, sizeof(body) - 1, keep_alive);
}


static void route_create(struct yloop_stream *stream, const char *body, size_t blen,
                         int keep_alive)
{
    struct yjson_doc *doc = yjson_parse(body, blen);
    if (!doc) { send_json_error(stream, 400, -32700, "invalid JSON", keep_alive); return; }
    const struct yjson_value *root = yjson_doc_root(doc);
    const char *cls = yjson_as_string(yjson_object_get(root, "class"), NULL);
    if (!cls) {
        yjson_doc_free(doc);
        send_json_error(stream, 400, -32602, "create: missing 'class'", keep_alive);
        return;
    }
    struct class_ptr_result class_res = class_by_name(cls);
    if (PICOMESH_IS_ERR(class_res)) {
        picomesh_error_destroy(class_res.error);
        char msg[256];
        snprintf(msg, sizeof(msg), "create: unknown class '%s'", cls);
        yjson_doc_free(doc);
        send_json_error(stream, 404, -32601, msg, keep_alive);
        return;
    }
    struct object_ptr_result alloc_res = object_alloc(class_res.value);
    if (PICOMESH_IS_ERR(alloc_res)) {
        picomesh_error_destroy(alloc_res.error);
        yjson_doc_free(doc);
        send_json_error(stream, 500, -32603, "create: object_alloc failed", keep_alive);
        return;
    }
    uint64_t handle = rpc_register_object(alloc_res.value);
    yjson_doc_free(doc);

    struct yjson_writer *writer = yjson_writer_new();
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "handle"); yjson_writer_int(writer, (int64_t)handle);
    yjson_writer_end_object(writer);
    size_t len; const char *data = yjson_writer_data(writer, &len);
    send_response(stream, 200, "application/json", data, len, keep_alive);
    yjson_writer_free(writer);
}

static void route_invoke(struct yloop_stream *stream, const char *body, size_t blen,
                         int keep_alive)
{
    struct yjson_doc *doc = yjson_parse(body, blen);
    if (!doc) { send_json_error(stream, 400, -32700, "invalid JSON", keep_alive); return; }
    const struct yjson_value *root = yjson_doc_root(doc);
    const char *method = yjson_as_string(yjson_object_get(root, "method"), NULL);
    int64_t handle = yjson_as_int(yjson_object_get(root, "handle"), 0);
    const struct yjson_value *args = yjson_object_get(root, "args");

    if (!method) { yjson_doc_free(doc); send_json_error(stream, 400, -32602, "invoke: missing 'method'", keep_alive); return; }
    if (!handle) { yjson_doc_free(doc); send_json_error(stream, 400, -32602, "invoke: missing/zero 'handle'", keep_alive); return; }

    void *obj = rpc_handle_resolve((uint64_t)handle);
    if (!obj) { yjson_doc_free(doc); send_json_error(stream, 404, -32602, "invoke: unknown handle", keep_alive); return; }

    jinvoke_fn invoke_fn = jinvoke_for(method);
    if (!invoke_fn) {
        char msg[256]; snprintf(msg, sizeof(msg), "invoke: no jinvoke for '%s'", method);
        yjson_doc_free(doc);
        send_json_error(stream, 404, -32601, msg, keep_alive);
        return;
    }

    struct yjson_writer *writer = yjson_writer_new();
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "result");
    char err[8192] = {0};
    /* Legacy /invoke is the bootstrap control plane (mesh parent):
     * the object is local to this process — NULL ctx, NULL headers. */
    int invoke_rc = invoke_fn(NULL, (struct object *)obj, NULL, args, writer, err, sizeof(err));
    if (invoke_rc != 0) {
        yjson_writer_free(writer);
        yjson_doc_free(doc);
        send_json_error(stream, 500, -32000, err[0] ? err : "invoke: impl failed", keep_alive);
        return;
    }
    yjson_writer_end_object(writer);
    size_t len; const char *data = yjson_writer_data(writer, &len);
    send_response(stream, 200, "application/json", data, len, keep_alive);
    yjson_writer_free(writer);
    yjson_doc_free(doc);
}

/* Forward-decl for the URL ?key=val extractor; the real definition
 * sits a few hundred lines below alongside other URL helpers. */
static const char *query_get(const char *path, const char *key,
                             char *out, size_t out_cap);

/* Parse one HTTP header value out of the raw header block into `out`.
 * Returns 1 on hit. Used by the /_rpc handler to read auth context
 * out of `X-Picomesh-Uid` / `X-Picomesh-Sid`. */
static int header_get(const char *raw, size_t raw_len, const char *name,
                      char *out, size_t out_cap)
{
    size_t name_len = strlen(name);
    const char *cursor = raw;
    const char *end = raw + raw_len;
    while (cursor < end) {
        const char *eol = memchr(cursor, '\n', (size_t)(end - cursor));
        if (!eol) break;
        size_t line_len = (size_t)(eol - cursor);
        if (line_len && cursor[line_len - 1] == '\r') line_len--;
        if (line_len > name_len + 1 && cursor[name_len] == ':' &&
            strncasecmp(cursor, name, name_len) == 0) {
            const char *value_start = cursor + name_len + 1;
            while (value_start < cursor + line_len && (*value_start == ' ' || *value_start == '\t')) ++value_start;
            size_t value_len = (size_t)(cursor + line_len - value_start);
            if (value_len >= out_cap) value_len = out_cap - 1;
            memcpy(out, value_start, value_len);
            out[value_len] = 0;
            return 1;
        }
        cursor = eol + 1;
    }
    return 0;
}

/* POST /_rpc?op=<op>&id=<id> — binary RPC over HTTP. The body is the
 * same packed-args payload the legacy yrpc protocol carried; we just
 * wrap it in HTTP so auth and (later) tracing context can travel as
 * headers. Mirrors yaapp's `POST /_rpc` boundary at the gateway. */
static void route_rpc_binary(struct yloop_stream *stream,
                             const char *path,
                             const char *headers_raw, size_t headers_raw_len,
                             const char *body, size_t body_len,
                             int keep_alive)
{
    char op_s[16] = {0}, id_s[16] = {0};
    query_get(path, "op",  op_s, sizeof(op_s));
    query_get(path, "id",  id_s, sizeof(id_s));
    if (!*op_s) { send_json_error(stream, 400, -32602, "_rpc: missing op", keep_alive); return; }
    enum rpc_op op = (enum rpc_op)atoi(op_s);
    uint32_t id  = (uint32_t)strtoul(id_s, NULL, 10);

    /* Auth context flows in HTTP headers. Backend trusts the caller
     * (frontend) — same trust model as yaapp's gateway. */
    char uid_s[16] = {0}, sid_s[16] = {0};
    header_get(headers_raw, headers_raw_len, "X-Picomesh-Uid", uid_s, sizeof(uid_s));
    header_get(headers_raw, headers_raw_len, "X-Picomesh-Sid", sid_s, sizeof(sid_s));
    uint32_t caller_uid = (uint32_t)strtoul(uid_s, NULL, 10);
    uint32_t caller_sid = (uint32_t)strtoul(sid_s, NULL, 10);

    /* Stash the caller's ctx where the skel can pick it up.
     * Coroutines are cooperative and skel runs to completion (yields
     * to libuv only on its own outbound rpc_calls); ctx is read AT
     * the top of the skel before any yield can clobber it. */
    extern void rpc_set_current_caller(uint32_t uid, uint32_t sid);
    rpc_set_current_caller(caller_uid, caller_sid);

    uint8_t resp[8192];
    size_t resp_len = 0;
    switch (op) {
    case RPC_OP_CALL: {
        rpc_skel_fn skel_fn = rpc_skel_for((method_slot)id);
        if (skel_fn) resp_len = skel_fn(body, body_len, resp, sizeof(resp));
        break;
    }
    case RPC_OP_RESOLVE_SLOT:
    case RPC_OP_GET_CLASS:
    case RPC_OP_CREATE:
    case RPC_OP_DESTROY:
        /* Dispatch the admin ops via the existing inline handlers.
         * They take (body, body_len, resp, resp_max) → bytes_written. */
        {
            extern size_t rpc_handle_admin_op(enum rpc_op op,
                const void *body, size_t body_len, void *resp, size_t resp_max);
            resp_len = rpc_handle_admin_op(op, body, body_len, resp, sizeof(resp));
        }
        break;
    }
    rpc_set_current_caller(0, 0);

    send_response(stream, 200, "application/octet-stream", (const char *)resp, resp_len, keep_alive);
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

/* Extract the value of ?class=… from a URL like /describe?class=foo. */
static const char *query_get(const char *path, const char *key,
                             char *out, size_t out_cap)
{
    const char *query = strchr(path, '?');
    if (!query) return NULL;
    size_t key_len = strlen(key);
    const char *cursor = query + 1;
    while (*cursor) {
        const char *eq = strchr(cursor, '=');
        if (!eq) return NULL;
        if ((size_t)(eq - cursor) == key_len && strncmp(cursor, key, key_len) == 0) {
            const char *end = strchr(eq + 1, '&');
            size_t value_len = end ? (size_t)(end - eq - 1) : strlen(eq + 1);
            if (value_len >= out_cap) value_len = out_cap - 1;
            memcpy(out, eq + 1, value_len);
            out[value_len] = 0;
            return out;
        }
        const char *next = strchr(cursor, '&');
        if (!next) break;
        cursor = next + 1;
    }
    return NULL;
}

static void route_describe(struct yloop_stream *stream, const char *path,
                           int keep_alive)
{
    char cls[128];
    if (!query_get(path, "class", cls, sizeof(cls))) {
        send_json_error(stream, 400, -32602, "describe: missing ?class=", keep_alive);
        return;
    }
    struct class_ptr_result class_res = class_by_name(cls);
    if (PICOMESH_IS_ERR(class_res)) {
        picomesh_error_destroy(class_res.error);
        char msg[256]; snprintf(msg, sizeof(msg), "describe: unknown class '%s'", cls);
        send_json_error(stream, 404, -32601, msg, keep_alive);
        return;
    }
    struct yjson_writer *writer = yjson_writer_new();
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "class");   yjson_writer_string(writer, cls);
    yjson_writer_key(writer, "methods"); yjson_writer_begin_array(writer);
    struct describe_ctx describe_ctx = {.w = writer};
    class_for_each_slot(class_res.value, describe_emit, &describe_ctx);
    yjson_writer_end_array(writer);
    yjson_writer_end_object(writer);
    size_t len; const char *data = yjson_writer_data(writer, &len);
    send_response(stream, 200, "application/json", data, len, keep_alive);
    yjson_writer_free(writer);
}

/* ---- request loop ---------------------------------------------- */

static int starts_with(const char *str, size_t str_len, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    return str_len >= prefix_len && memcmp(str, prefix, prefix_len) == 0;
}

static void serve_one(struct yloop *loop, struct yloop_stream *stream, void *ud)
{
    (void)loop;
    (void)ud; /* gh#5: no static_root to read anymore */
    yinfo("yhttp: peer connected");
    char *buf = malloc(YHTTP_REQ_BUF);
    if (!buf) { yloop_close(stream); return; }

    for (;;) {
        size_t buf_len = 0;
        int phr_minor_version = 0;
        const char *phr_method = NULL;
        size_t phr_method_len = 0;
        const char *phr_path = NULL;
        size_t phr_path_len = 0;
        struct phr_header headers[YHTTP_MAX_HEADERS];
        size_t num_headers;
        int parsed = -2;

        /* phr_parse_request says "incomplete" with -2 — keep reading.
         * For simplicity we read up to YHTTP_REQ_BUF in 4-KB chunks. */
        while (parsed == -2) {
            if (buf_len >= YHTTP_REQ_BUF) goto close_peer;
            size_t chunk = YHTTP_REQ_BUF - buf_len;
            if (chunk > 4096) chunk = 4096;
            /* read_some: returns whatever bytes are available, not
             * exactly `chunk` — critical for HTTP request lines that
             * arrive in single packets shorter than our buffer. */
            size_t got = yloop_read_some(stream, buf + buf_len, chunk);
            if (got == 0) goto close_peer;
            buf_len += got;
            num_headers = YHTTP_MAX_HEADERS;
            parsed = phr_parse_request(buf, buf_len,
                                       &phr_method, &phr_method_len,
                                       &phr_path, &phr_path_len,
                                       &phr_minor_version,
                                       headers, &num_headers, 0);
        }
        if (parsed < 0) goto close_peer;
        size_t header_end = (size_t)parsed;

        /* Pull headers we care about. */
        char clen_buf[32] = {0};
        char conn_buf[32] = {0};
        long content_length = 0;
        if (header_match(headers, num_headers, "Content-Length", clen_buf, sizeof(clen_buf))) {
            content_length = strtol(clen_buf, NULL, 10);
        }
        header_match(headers, num_headers, "Connection", conn_buf, sizeof(conn_buf));
        int keep_alive = strcasecmp(conn_buf, "keep-alive") == 0;

        /* Read the body if Content-Length > already-read tail. */
        if (content_length > 0) {
            size_t body_present = buf_len - header_end;
            while ((long)body_present < content_length) {
                if (header_end + content_length > YHTTP_REQ_BUF) goto close_peer;
                size_t need = (size_t)content_length - body_present;
                size_t got = yloop_read_some(stream, buf + buf_len, need);
                if (got == 0) goto close_peer;
                buf_len += got;
                body_present += got;
            }
        }
        const char *body = buf + header_end;

        /* NUL-terminate the path so query parsing can use C-strings.
         * The path slice ends at phr_path + phr_path_len; the byte
         * right after is `HTTP/1.1\r\n` so overwriting it is safe
         * because we don't re-parse this request. */
        ((char *)phr_path)[phr_path_len] = 0;

        char method[16] = {0};
        size_t method_copy_len = phr_method_len < sizeof(method) - 1 ? phr_method_len : sizeof(method) - 1;
        memcpy(method, phr_method, method_copy_len);

        ydebug("yhttp: %s %s (body=%ld)", method, phr_path, content_length);

        /* Frontend page handlers get first crack at every request —
         * they pattern-match on URL, render HTML, and short-circuit if
         * they take ownership. Returns 0 → fall through to static /
         * JSON-API routes below. */
        if (yhttp_frontend_try(stream, method, phr_path, buf, header_end,
                               body, (size_t)content_length, keep_alive)) {
            if (!keep_alive) goto close_peer;
            continue;
        }

        if (strcmp(method, "OPTIONS") == 0) {
            /* CORS preflight: browser sends OPTIONS before any
             * cross-origin POST with a JSON body. send_response()
             * already adds the Access-Control headers; an empty 200
             * is the right answer. */
            send_response(stream, 200, "text/plain", "", 0, keep_alive);
        } else if (strcmp(method, "GET") == 0) {
            if (starts_with(phr_path, phr_path_len, "/describe")) {
                route_describe(stream, phr_path, keep_alive);
            } else if (strcmp(phr_path, "/") == 0) {
                route_root(stream, keep_alive);
            } else {
                send_json_error(stream, 404, -32601, "no such GET route", keep_alive);
            }
        } else if (strcmp(method, "POST") == 0) {
            if (strcmp(phr_path, "/create") == 0) {
                route_create(stream, body, (size_t)content_length, keep_alive);
            } else if (strcmp(phr_path, "/invoke") == 0) {
                route_invoke(stream, body, (size_t)content_length, keep_alive);
            } else if (strncmp(phr_path, "/_rpc", 5) == 0 &&
                       (phr_path[5] == 0 || phr_path[5] == '?')) {
                route_rpc_binary(stream, phr_path, buf, header_end,
                                 body, (size_t)content_length, keep_alive);
            } else {
                send_json_error(stream, 404, -32601, "no such POST route", keep_alive);
            }
        } else {
            send_json_error(stream, 405, -32601, "method not allowed", keep_alive);
        }

        if (!keep_alive) goto close_peer;
    }
close_peer:
    free(buf);
    yinfo("yhttp: peer disconnected");
    yloop_close(stream);
}

struct yhttp_frontend_ptr_result yhttp_start(struct picomesh_engine *engine,
                                             const struct yhttp_config *cfg)
{
    if (!engine) return PICOMESH_ERR(yhttp_frontend_ptr, "yhttp_start: NULL engine");
    const char *host = (cfg && cfg->host) ? cfg->host : "127.0.0.1";
    int port = (cfg && cfg->port > 0) ? cfg->port : 8080;

    struct yloop *loop = picomesh_engine_loop(engine);
    if (!loop) return PICOMESH_ERR(yhttp_frontend_ptr, "yhttp_start: engine has no loop");

    struct yhttp_frontend *frontend = calloc(1, sizeof(*frontend));
    if (!frontend) return PICOMESH_ERR(yhttp_frontend_ptr, "yhttp_start: calloc failed");
    frontend->engine = engine;

    struct picomesh_void_result listen_res = yloop_listen_tcp(loop, host, port, serve_one, frontend);
    if (PICOMESH_IS_ERR(listen_res)) {
        free(frontend);
        return PICOMESH_ERR(yhttp_frontend_ptr, "yhttp_start: yloop_listen_tcp failed", listen_res);
    }
    yinfo("yhttp: listening on %s:%d", host, port);
    return PICOMESH_OK(yhttp_frontend_ptr, frontend);
}

void yhttp_stop(struct yhttp_frontend *frontend)
{
    if (!frontend) return;
    free(frontend);
}
