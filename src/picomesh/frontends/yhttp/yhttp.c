/* yhttp — HTTP/1.1 + JSON frontend.
 *
 * Per-peer serve coroutine reads one HTTP request via picohttpparser,
 * routes it, writes the HTTP/1.1 response. Body must be Content-Length
 * delimited (no chunked yet); we cap bodies at 256 KB.
 *
 * Body parsing on the way in: simdjson (json_parse).
 * Body serialization on the way out: json_writer.
 * Dispatch backend: jinvoke_for + the rpc handle table — same as yttp,
 * so the same instance handles span both frontends in one process. */

#include <picomesh/config/config.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/engine/engine.h>
#include <picomesh/frontends/yhttp/yhttp.h>
#include <picomesh/json/json.h>
#include <picomesh/loop/loop.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/jinvoke.h>
#include <picomesh/picoclass/rpc.h>

#include <unistd.h>

#include <picohttpparser.h>

#include "frontend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define YHTTP_REQ_BUF (256 * 1024)
#define YHTTP_MAX_HEADERS 64

struct yhttp_frontend {
  struct picomesh_engine *engine;
};

/* ---- helpers ---------------------------------------------------- */

static int header_match(const struct phr_header *hdrs, size_t count,
                        const char *want, char *out, size_t out_cap) {
  size_t want_len = strlen(want);
  for (size_t i = 0; i < count; ++i) {
    if (hdrs[i].name_len != want_len)
      continue;
    int matched = 1;
    for (size_t j = 0; j < want_len; ++j) {
      char name_char = hdrs[i].name[j];
      char want_char = want[j];
      if (name_char >= 'A' && name_char <= 'Z')
        name_char = (char)(name_char - 'A' + 'a');
      if (want_char >= 'A' && want_char <= 'Z')
        want_char = (char)(want_char - 'A' + 'a');
      if (name_char != want_char) {
        matched = 0;
        break;
      }
    }
    if (!matched)
      continue;
    size_t copy =
        hdrs[i].value_len < out_cap - 1 ? hdrs[i].value_len : out_cap - 1;
    memcpy(out, hdrs[i].value, copy);
    out[copy] = 0;
    return 1;
  }
  return 0;
}

static void send_response(struct loop_stream *stream, int status,
                          const char *content_type, const char *body,
                          size_t body_len, int keep_alive) {
  const char *reason = "OK";
  if (status == 400)
    reason = "Bad Request";
  else if (status == 404)
    reason = "Not Found";
  else if (status == 405)
    reason = "Method Not Allowed";
  else if (status == 500)
    reason = "Internal Server Error";

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
                         status, reason, content_type, body_len,
                         keep_alive ? "keep-alive" : "close");
  if (written <= 0)
    return;
  loop_write(stream, header, (size_t)written);
  if (body_len)
    loop_write(stream, body, body_len);
}

static void write_error_detail(struct json_writer *writer,
                               const char *message) {
  const char *msg = message ? message : "";
  size_t first_len = strcspn(msg, "\n");
  char first[512];
  size_t copy = first_len < sizeof(first) - 1 ? first_len : sizeof(first) - 1;
  memcpy(first, msg, copy);
  first[copy] = 0;
  json_writer_key(writer, "message");
  json_writer_string(writer, first[0] ? first : msg);
  json_writer_key(writer, "detail");
  json_writer_string(writer, msg);
  json_writer_key(writer, "trace");
  json_writer_begin_array(writer);
  const char *cursor = msg;
  while (*cursor) {
    const char *newline = strchr(cursor, '\n');
    size_t line_len = newline ? (size_t)(newline - cursor) : strlen(cursor);
    char line[1024];
    size_t line_copy =
        line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
    memcpy(line, cursor, line_copy);
    line[line_copy] = 0;
    json_writer_string(writer, line);
    if (!newline)
      break;
    cursor = newline + 1;
  }
  json_writer_end_array(writer);
}

static void send_json_error(struct loop_stream *stream, int status, int code,
                            const char *message, int keep_alive) {
  if (status >= 500)
    yerror("yhttp request failed: %s", message ? message : "");
  struct json_writer *writer = json_writer_new();
  json_writer_begin_object(writer);
  json_writer_key(writer, "error");
  json_writer_begin_object(writer);
  json_writer_key(writer, "code");
  json_writer_int(writer, code);
  write_error_detail(writer, message);
  json_writer_end_object(writer);
  json_writer_end_object(writer);
  size_t len;
  const char *data = json_writer_data(writer, &len);
  send_response(stream, status, "application/json", data, len, keep_alive);
  json_writer_free(writer);
}

/* Absorb a route handler Result at the serve-loop boundary: render the full
 * cause chain to stderr AND back to the client as a 500 body. The serve
 * coroutine's void signature is fixed by the accept-handler API, so this is
 * where the dispatch chain terminates. */
static void absorb_route_result(struct loop_stream *stream, const char *label,
                                struct picomesh_void_result res,
                                int keep_alive) {
  if (PICOMESH_IS_OK(res))
    return;
  char eb[8192];
  picomesh_error_snprint(eb, sizeof(eb), res.error);
  picomesh_error_print(stderr, label, res.error);
  picomesh_error_destroy(res.error);
  send_response(stream, 500, "application/json", eb, strlen(eb), keep_alive);
}

/* ---- route handlers --------------------------------------------- */

static void route_root(struct loop_stream *stream, int keep_alive) {
  static const char body[] =
      "picomesh/yhttp frontend\n"
      "\n"
      "POST /create   {\"class\": \"<plugin>_<class>\"}                        "
      "    -> {\"handle\": u64}\n"
      "POST /invoke   {\"method\": \"<qname>\", \"handle\": u64, \"args\": "
      "[...]}    -> {\"result\": ...}\n"
      "GET  /describe?class=NAME                                              "
      "-> {\"class\":..., \"methods\":[...]}\n";
  send_response(stream, 200, "text/plain", body, sizeof(body) - 1, keep_alive);
}

static struct picomesh_void_result route_create(struct loop_stream *stream,
                                                const char *body, size_t blen,
                                                int keep_alive) {
  struct json_doc *doc = json_parse(body, blen);
  if (!doc) {
    send_json_error(stream, 400, -32700, "invalid JSON", keep_alive);
    return PICOMESH_OK_VOID();
  }
  const struct json_value *root = json_doc_root(doc);
  const char *cls = json_as_string(json_object_get(root, "class"), NULL);
  if (!cls) {
    json_doc_free(doc);
    send_json_error(stream, 400, -32602, "create: missing 'class'", keep_alive);
    return PICOMESH_OK_VOID();
  }
  struct class_ptr_result class_res = class_by_name(cls);
  if (PICOMESH_IS_ERR(class_res)) {
    picomesh_error_print(stderr, "route_create: class_by_name",
                         class_res.error);
    picomesh_error_destroy(class_res.error);
    char msg[256];
    snprintf(msg, sizeof(msg), "create: unknown class '%s'", cls);
    json_doc_free(doc);
    send_json_error(stream, 404, -32601, msg, keep_alive);
    return PICOMESH_OK_VOID();
  }
  struct object_ptr_result alloc_res = object_alloc(class_res.value);
  if (PICOMESH_IS_ERR(alloc_res)) {
    char eb[8192];
    picomesh_error_snprint(eb, sizeof(eb), alloc_res.error);
    picomesh_error_print(stderr, "route_create: object_alloc", alloc_res.error);
    picomesh_error_destroy(alloc_res.error);
    json_doc_free(doc);
    send_json_error(stream, 500, -32603, eb, keep_alive);
    return PICOMESH_OK_VOID();
  }
  uint64_t handle = rpc_register_object(alloc_res.value);
  if (handle == 0) {
    /* Object table full — free the leaked object, don't hand back handle 0. */
    object_free(alloc_res.value);
    json_doc_free(doc);
    send_json_error(stream, 500, -32603, "create: object handle table full",
                    keep_alive);
    return PICOMESH_OK_VOID();
  }
  json_doc_free(doc);

  struct json_writer *writer = json_writer_new();
  json_writer_begin_object(writer);
  json_writer_key(writer, "handle");
  json_writer_int(writer, (int64_t)handle);
  json_writer_end_object(writer);
  size_t len;
  const char *data = json_writer_data(writer, &len);
  send_response(stream, 200, "application/json", data, len, keep_alive);
  json_writer_free(writer);
  return PICOMESH_OK_VOID();
}

static struct picomesh_void_result route_invoke(struct loop_stream *stream,
                                                const char *body, size_t blen,
                                                int keep_alive) {
  struct json_doc *doc = json_parse(body, blen);
  if (!doc) {
    send_json_error(stream, 400, -32700, "invalid JSON", keep_alive);
    return PICOMESH_OK_VOID();
  }
  const struct json_value *root = json_doc_root(doc);
  const char *method = json_as_string(json_object_get(root, "method"), NULL);
  int64_t handle = json_as_int(json_object_get(root, "handle"), 0);
  const struct json_value *args = json_object_get(root, "args");

  if (!method) {
    json_doc_free(doc);
    send_json_error(stream, 400, -32602, "invoke: missing 'method'",
                    keep_alive);
    return PICOMESH_OK_VOID();
  }
  if (!handle) {
    json_doc_free(doc);
    send_json_error(stream, 400, -32602, "invoke: missing/zero 'handle'",
                    keep_alive);
    return PICOMESH_OK_VOID();
  }

  void *obj = rpc_handle_resolve((uint64_t)handle);
  if (!obj) {
    json_doc_free(doc);
    send_json_error(stream, 404, -32602, "invoke: unknown handle", keep_alive);
    return PICOMESH_OK_VOID();
  }

  jinvoke_fn invoke_fn = jinvoke_for(method);
  if (!invoke_fn) {
    char msg[256];
    snprintf(msg, sizeof(msg), "invoke: no jinvoke for '%s'", method);
    json_doc_free(doc);
    send_json_error(stream, 404, -32601, msg, keep_alive);
    return PICOMESH_OK_VOID();
  }

  struct json_writer *writer = json_writer_new();
  json_writer_begin_object(writer);
  json_writer_key(writer, "result");
  char err[8192] = {0};
  /* Legacy /invoke is the bootstrap control plane (mesh parent):
   * the object is local to this process — NULL ctx, NULL headers. */
  struct picomesh_void_result invoke_res = invoke_fn(
      NULL, (struct object *)obj, NULL, args, writer, err, sizeof(err));
  if (PICOMESH_IS_ERR(invoke_res)) {
    /* Render the full cause chain to both stderr and the 500 body so the
     * caller sees why the impl failed, not just a generic message. */
    char eb[8192];
    picomesh_error_snprint(eb, sizeof(eb), invoke_res.error);
    picomesh_error_print(stderr, "route_invoke: impl", invoke_res.error);
    picomesh_error_destroy(invoke_res.error);
    json_writer_free(writer);
    json_doc_free(doc);
    send_json_error(stream, 500, -32000,
                    eb[0] ? eb : (err[0] ? err : "invoke: impl failed"),
                    keep_alive);
    return PICOMESH_OK_VOID();
  }
  json_writer_end_object(writer);
  size_t len;
  const char *data = json_writer_data(writer, &len);
  send_response(stream, 200, "application/json", data, len, keep_alive);
  json_writer_free(writer);
  json_doc_free(doc);
  return PICOMESH_OK_VOID();
}

/* Forward-decl for the URL ?key=val extractor; the real definition
 * sits a few hundred lines below alongside other URL helpers. */
static const char *query_get(const char *path, const char *key, char *out,
                             size_t out_cap);

/* Parse one HTTP header value out of the raw header block into `out`.
 * Returns 1 on hit. Used by the /_rpc handler to read auth context
 * out of `X-Picomesh-Uid` / `X-Picomesh-Sid`. */
static int header_get(const char *raw, size_t raw_len, const char *name,
                      char *out, size_t out_cap) {
  size_t name_len = strlen(name);
  const char *cursor = raw;
  const char *end = raw + raw_len;
  while (cursor < end) {
    const char *eol = memchr(cursor, '\n', (size_t)(end - cursor));
    if (!eol)
      break;
    size_t line_len = (size_t)(eol - cursor);
    if (line_len && cursor[line_len - 1] == '\r')
      line_len--;
    if (line_len > name_len + 1 && cursor[name_len] == ':' &&
        strncasecmp(cursor, name, name_len) == 0) {
      const char *value_start = cursor + name_len + 1;
      while (value_start < cursor + line_len &&
             (*value_start == ' ' || *value_start == '\t'))
        ++value_start;
      size_t value_len = (size_t)(cursor + line_len - value_start);
      if (value_len >= out_cap)
        value_len = out_cap - 1;
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
static struct picomesh_void_result
route_rpc_binary(struct loop_stream *stream, const char *path,
                 const char *headers_raw, size_t headers_raw_len,
                 const char *body, size_t body_len, int keep_alive) {
  char op_s[16] = {0}, id_s[16] = {0};
  query_get(path, "op", op_s, sizeof(op_s));
  query_get(path, "id", id_s, sizeof(id_s));
  if (!*op_s) {
    send_json_error(stream, 400, -32602, "_rpc: missing op", keep_alive);
    return PICOMESH_OK_VOID();
  }
  enum rpc_op op = (enum rpc_op)atoi(op_s);
  uint32_t id = (uint32_t)strtoul(id_s, NULL, 10);

  /* Auth context flows in HTTP headers. Backend trusts the caller
   * (frontend) — same trust model as yaapp's gateway. */
  char uid_s[16] = {0}, sid_s[16] = {0};
  header_get(headers_raw, headers_raw_len, "X-Picomesh-Uid", uid_s,
             sizeof(uid_s));
  header_get(headers_raw, headers_raw_len, "X-Picomesh-Sid", sid_s,
             sizeof(sid_s));
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
    struct rpc_skel_fn_result skel = rpc_skel_for((method_slot)id);
    if (PICOMESH_IS_ERR(skel)) {
      picomesh_error_print(stderr, "_rpc: skel lookup failed", skel.error);
      picomesh_error_destroy(skel.error);
    } else if (skel.value) {
      struct picomesh_size_result sr =
          skel.value(body, body_len, resp, sizeof(resp));
      if (PICOMESH_IS_ERR(sr)) {
        picomesh_error_print(stderr, "_rpc: skel failed", sr.error);
        picomesh_error_destroy(sr.error);
      } else {
        resp_len = sr.value;
      }
    }
    break;
  }
  case RPC_OP_RESOLVE_SLOT:
  case RPC_OP_GET_CLASS:
  case RPC_OP_CREATE:
  case RPC_OP_DESTROY: {
    struct picomesh_size_result ar =
        rpc_handle_admin_op(op, body, body_len, resp, sizeof(resp));
    if (PICOMESH_IS_ERR(ar)) {
      picomesh_error_print(stderr, "_rpc: admin op failed", ar.error);
      picomesh_error_destroy(ar.error);
    } else {
      resp_len = ar.value;
    }
    break;
  }
  }
  rpc_set_current_caller(0, 0);

  send_response(stream, 200, "application/octet-stream", (const char *)resp,
                resp_len, keep_alive);
  return PICOMESH_OK_VOID();
}

struct describe_ctx {
  struct json_writer *w;
};

static void describe_emit(const char *name, method_slot slot, void *ud) {
  (void)slot;
  struct describe_ctx *describe_ctx = ud;
  json_writer_string(describe_ctx->w, name);
}

/* Extract the value of ?class=… from a URL like /describe?class=foo. */
static const char *query_get(const char *path, const char *key, char *out,
                             size_t out_cap) {
  const char *query = strchr(path, '?');
  if (!query)
    return NULL;
  size_t key_len = strlen(key);
  const char *cursor = query + 1;
  while (*cursor) {
    const char *eq = strchr(cursor, '=');
    if (!eq)
      return NULL;
    if ((size_t)(eq - cursor) == key_len &&
        strncmp(cursor, key, key_len) == 0) {
      const char *end = strchr(eq + 1, '&');
      size_t value_len = end ? (size_t)(end - eq - 1) : strlen(eq + 1);
      if (value_len >= out_cap)
        value_len = out_cap - 1;
      memcpy(out, eq + 1, value_len);
      out[value_len] = 0;
      return out;
    }
    const char *next = strchr(cursor, '&');
    if (!next)
      break;
    cursor = next + 1;
  }
  return NULL;
}

static struct picomesh_void_result
route_describe(struct loop_stream *stream, const char *path, int keep_alive) {
  char cls[128];
  if (!query_get(path, "class", cls, sizeof(cls))) {
    send_json_error(stream, 400, -32602,
                    "describe: missing ?class=", keep_alive);
    return PICOMESH_OK_VOID();
  }
  struct class_ptr_result class_res = class_by_name(cls);
  if (PICOMESH_IS_ERR(class_res)) {
    picomesh_error_destroy(class_res.error);
    char msg[256];
    snprintf(msg, sizeof(msg), "describe: unknown class '%s'", cls);
    send_json_error(stream, 404, -32601, msg, keep_alive);
    return PICOMESH_OK_VOID();
  }
  struct json_writer *writer = json_writer_new();
  json_writer_begin_object(writer);
  json_writer_key(writer, "class");
  json_writer_string(writer, cls);
  json_writer_key(writer, "methods");
  json_writer_begin_array(writer);
  struct describe_ctx describe_ctx = {.w = writer};
  struct picomesh_void_result slots_res =
      class_for_each_slot(class_res.value, describe_emit, &describe_ctx);
  if (PICOMESH_IS_ERR(slots_res)) {
    picomesh_error_print(stderr, "route_describe: class_for_each_slot",
                         slots_res.error);
    picomesh_error_destroy(slots_res.error);
  }
  json_writer_end_array(writer);
  json_writer_end_object(writer);
  size_t len;
  const char *data = json_writer_data(writer, &len);
  send_response(stream, 200, "application/json", data, len, keep_alive);
  json_writer_free(writer);
  return PICOMESH_OK_VOID();
}

/* ---- request loop ---------------------------------------------- */

static int starts_with(const char *str, size_t str_len, const char *prefix) {
  size_t prefix_len = strlen(prefix);
  return str_len >= prefix_len && memcmp(str, prefix, prefix_len) == 0;
}

/* Per-connection serve coroutine — its void signature is fixed by the loop's
 * accept-handler API and there is no caller to propagate a Result to, so route
 * dispatch failures are absorbed here (their chain rendered to stderr and into
 * the response). This is the transport boundary the route handlers propagate up
 * to. */
PICOMESH_EXTERNAL_CALLBACK
static void serve_one(struct loop *loop, struct loop_stream *stream, void *ud) {
  (void)loop;
  (void)ud; /* gh#5: no static_root to read anymore */
  yinfo("yhttp: peer connected");
  char *buf = malloc(YHTTP_REQ_BUF);
  if (!buf) {
    loop_close(stream);
    return;
  }

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
      if (buf_len >= YHTTP_REQ_BUF)
        goto close_peer;
      size_t chunk = YHTTP_REQ_BUF - buf_len;
      if (chunk > 4096)
        chunk = 4096;
      /* read_some: returns whatever bytes are available, not
       * exactly `chunk` — critical for HTTP request lines that
       * arrive in single packets shorter than our buffer. */
      struct picomesh_size_result read_res =
          loop_read_some(stream, buf + buf_len, chunk);
      if (PICOMESH_IS_ERR(read_res)) {
        picomesh_error_destroy(read_res.error);
        goto close_peer;
      }
      if (read_res.value == 0)
        goto close_peer; /* clean EOF */
      buf_len += read_res.value;
      num_headers = YHTTP_MAX_HEADERS;
      parsed = phr_parse_request(buf, buf_len, &phr_method, &phr_method_len,
                                 &phr_path, &phr_path_len, &phr_minor_version,
                                 headers, &num_headers, 0);
    }
    if (parsed < 0)
      goto close_peer;
    size_t header_end = (size_t)parsed;

    /* Pull headers we care about. */
    char clen_buf[32] = {0};
    char conn_buf[32] = {0};
    long content_length = 0;
    if (header_match(headers, num_headers, "Content-Length", clen_buf,
                     sizeof(clen_buf))) {
      content_length = strtol(clen_buf, NULL, 10);
    }
    header_match(headers, num_headers, "Connection", conn_buf,
                 sizeof(conn_buf));
    int keep_alive = strcasecmp(conn_buf, "keep-alive") == 0;

    /* Read the body if Content-Length > already-read tail. */
    if (content_length > 0) {
      size_t body_present = buf_len - header_end;
      while ((long)body_present < content_length) {
        if (header_end + content_length > YHTTP_REQ_BUF)
          goto close_peer;
        size_t need = (size_t)content_length - body_present;
        struct picomesh_size_result read_res =
            loop_read_some(stream, buf + buf_len, need);
        if (PICOMESH_IS_ERR(read_res)) {
          picomesh_error_destroy(read_res.error);
          goto close_peer;
        }
        if (read_res.value == 0)
          goto close_peer; /* clean EOF */
        buf_len += read_res.value;
        body_present += read_res.value;
      }
    }
    const char *body = buf + header_end;

    /* NUL-terminate the path so query parsing can use C-strings.
     * The path slice ends at phr_path + phr_path_len; the byte
     * right after is `HTTP/1.1\r\n` so overwriting it is safe
     * because we don't re-parse this request. */
    ((char *)phr_path)[phr_path_len] = 0;

    char method[16] = {0};
    size_t method_copy_len = phr_method_len < sizeof(method) - 1
                                 ? phr_method_len
                                 : sizeof(method) - 1;
    memcpy(method, phr_method, method_copy_len);

    ydebug("yhttp: %s %s (body=%ld)", method, phr_path, content_length);

    /* Frontend page handlers get first crack at every request —
     * they pattern-match on URL, render HTML, and short-circuit if
     * they take ownership. Returns 0 → fall through to static /
     * JSON-API routes below. */
    struct picomesh_int_result try_res =
        yhttp_frontend_try(stream, method, phr_path, buf, header_end, body,
                           (size_t)content_length, keep_alive);
    if (PICOMESH_IS_ERR(try_res)) {
      /* Transport boundary: render the full cause chain to BOTH the log
       * and the 500 response body so the operator sees exactly what
       * failed, chain intact. */
      char eb[8192];
      picomesh_error_snprint(eb, sizeof(eb), try_res.error);
      picomesh_error_print(stderr, "yhttp_frontend_try", try_res.error);
      picomesh_error_destroy(try_res.error);
      send_response(stream, 500, "application/json", eb, strlen(eb),
                    keep_alive);
      if (!keep_alive)
        goto close_peer;
      continue;
    }
    if (try_res.value) {
      if (!keep_alive)
        goto close_peer;
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
        absorb_route_result(stream, "route_describe",
                            route_describe(stream, phr_path, keep_alive),
                            keep_alive);
      } else if (strcmp(phr_path, "/") == 0) {
        route_root(stream, keep_alive);
      } else {
        send_json_error(stream, 404, -32601, "no such GET route", keep_alive);
      }
    } else if (strcmp(method, "POST") == 0) {
      if (strcmp(phr_path, "/create") == 0) {
        absorb_route_result(
            stream, "route_create",
            route_create(stream, body, (size_t)content_length, keep_alive),
            keep_alive);
      } else if (strcmp(phr_path, "/invoke") == 0) {
        absorb_route_result(
            stream, "route_invoke",
            route_invoke(stream, body, (size_t)content_length, keep_alive),
            keep_alive);
      } else if (strncmp(phr_path, "/_rpc", 5) == 0 &&
                 (phr_path[5] == 0 || phr_path[5] == '?')) {
        absorb_route_result(stream, "route_rpc_binary",
                            route_rpc_binary(stream, phr_path, buf, header_end,
                                             body, (size_t)content_length,
                                             keep_alive),
                            keep_alive);
      } else {
        send_json_error(stream, 404, -32601, "no such POST route", keep_alive);
      }
    } else {
      send_json_error(stream, 405, -32601, "method not allowed", keep_alive);
    }

    if (!keep_alive)
      goto close_peer;
  }
close_peer:
  free(buf);
  yinfo("yhttp: peer disconnected");
  loop_close(stream);
}

struct yhttp_frontend_ptr_result yhttp_start(struct picomesh_engine *engine,
                                             const struct yhttp_config *cfg) {
  if (!engine)
    return PICOMESH_ERR(yhttp_frontend_ptr, "yhttp_start: NULL engine");
  const char *host = (cfg && cfg->host) ? cfg->host : "127.0.0.1";
  int port = (cfg && cfg->port > 0) ? cfg->port : 8080;

  struct loop *loop = picomesh_engine_loop(engine);
  if (!loop)
    return PICOMESH_ERR(yhttp_frontend_ptr, "yhttp_start: engine has no loop");

  struct yhttp_frontend *frontend = calloc(1, sizeof(*frontend));
  if (!frontend)
    return PICOMESH_ERR(yhttp_frontend_ptr, "yhttp_start: calloc failed");
  frontend->engine = engine;

  struct picomesh_void_result listen_res =
      loop_listen_tcp(loop, host, port, serve_one, frontend);
  if (PICOMESH_IS_ERR(listen_res)) {
    free(frontend);
    return PICOMESH_ERR(yhttp_frontend_ptr,
                        "yhttp_start: loop_listen_tcp failed", listen_res);
  }
  yinfo("yhttp: listening on %s:%d", host, port);
  return PICOMESH_OK(yhttp_frontend_ptr, frontend);
}

void yhttp_stop(struct yhttp_frontend *frontend) {
  if (!frontend)
    return;
  free(frontend);
}
