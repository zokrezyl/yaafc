/* Minimal HTTP/1.1 client for the standalone webapp.
 *
 * One synchronous-shaped call per RPC: connect → write request → read
 * response → close. Synchronous in API only — internally yields the
 * calling coroutine via yloop_connect_tcp / yloop_read / yloop_write.
 *
 * Scope: enough to call POST /_rpc on a gateway. We do NOT handle:
 *   - chunked transfer-encoding (gateway always sends Content-Length)
 *   - keep-alive across calls (one connection per call; cheap enough
 *     on loopback for the demo, can be pooled later)
 *   - TLS (the gateway is in the local mesh) */

#include "http_client.h"

#include <picomesh/yloop/yloop.h>
#include <picomesh/ycore/ytrace.h>

#include <picohttpparser.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HC_MAX_HEADERS  64
#define HC_RESP_BUF     (256 * 1024)

/* Reject any string that would let a caller smuggle headers into the
 * outgoing request via CR/LF/NUL or other control bytes. The HTTP
 * header value grammar (RFC 9110) is "field-content = VCHAR / SP / HTAB
 * / obs-text"; we use a strict subset: printable + space + horizontal
 * tab. Returns 1 if safe to interpolate, 0 if it must be rejected.
 *
 * Defensive at the SINK: callers (cookies, opaque bearer tokens, URL
 * paths, hostnames) reach this function from a mix of trusted (config)
 * and untrusted (browser) sources. Validating here means a future
 * caller threading attacker-controlled bytes can't smuggle headers
 * (CRLF injection / request splitting).
 *
 * NULL is treated as safe (callers that pass NULL skip the header
 * write entirely). */
static int header_value_safe(const char *value)
{
    if (!value) return 1;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '\r' || *p == '\n') return 0;
        if (*p < 0x20 && *p != '\t')  return 0;
        if (*p == 0x7F)               return 0;
    }
    return 1;
}

int gateway_url_parse(const char *url, struct gateway_url *out)
{
    if (!url || !out) return -1;
    memset(out, 0, sizeof(*out));
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) return -1; /* TLS not yet */

    /* p now points at "host[:port][/path]" */
    const char *slash = strchr(p, '/');
    size_t hostport_len = slash ? (size_t)(slash - p) : strlen(p);
    if (hostport_len == 0 || hostport_len >= sizeof(out->host) + 8)
        return -1;

    char hp[256];
    if (hostport_len >= sizeof(hp)) return -1;
    memcpy(hp, p, hostport_len);
    hp[hostport_len] = 0;

    const char *colon = strchr(hp, ':');
    if (colon) {
        size_t hl = (size_t)(colon - hp);
        if (hl >= sizeof(out->host)) return -1;
        memcpy(out->host, hp, hl);
        out->host[hl] = 0;
        out->port = atoi(colon + 1);
    } else {
        size_t hl = strlen(hp);
        if (hl >= sizeof(out->host)) return -1;
        memcpy(out->host, hp, hl);
        out->host[hl] = 0;
        out->port = 80;
    }
    if (out->port <= 0) return -1;
    return 0;
}

/* Read the HTTP response off `stream` until headers parse + Content-Length
 * bytes of body are in. Caller frees out_body. */
static int read_full_response(struct yloop_stream *stream,
                              char **out_body, size_t *out_body_len,
                              int *out_status,
                              char *out_set_cookie, size_t out_set_cookie_cap)
{
    char *buf = malloc(HC_RESP_BUF);
    if (!buf) return -1;
    size_t total = 0, last = 0;
    int minor = 0;
    int status_code = 0;
    const char *msg = NULL;
    size_t msg_len = 0;
    struct phr_header headers[HC_MAX_HEADERS];
    size_t num_headers;
    int header_end = -1;

    while (total < HC_RESP_BUF) {
        size_t got = yloop_read_some(stream, buf + total, HC_RESP_BUF - total);
        if (got == 0) { free(buf); return -1; }
        total += got;
        num_headers = HC_MAX_HEADERS;
        int parse_result = phr_parse_response(buf, total, &minor, &status_code,
                                   &msg, &msg_len,
                                   headers, &num_headers, last);
        if (parse_result > 0) { header_end = parse_result; break; }
        if (parse_result == -1) { free(buf); return -1; }
        last = total;
    }
    if (header_end < 0) { free(buf); return -1; }

    /* Content-Length & first Set-Cookie. Multiple Set-Cookie support is
     * a TODO; only login.start needs it today and the gateway emits one. */
    long content_length = -1;
    if (out_set_cookie && out_set_cookie_cap) out_set_cookie[0] = 0;
    for (size_t i = 0; i < num_headers; ++i) {
        if (headers[i].name_len == 14) {
            int eq = 1;
            for (size_t j = 0; j < 14; ++j) {
                char a = headers[i].name[j];
                char b = "Content-Length"[j];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) { eq = 0; break; }
            }
            if (eq) {
                char tmp[32];
                size_t cl = headers[i].value_len < sizeof(tmp) - 1
                                ? headers[i].value_len : sizeof(tmp) - 1;
                memcpy(tmp, headers[i].value, cl);
                tmp[cl] = 0;
                content_length = atol(tmp);
            }
        } else if (headers[i].name_len == 10 && out_set_cookie && out_set_cookie_cap) {
            int eq = 1;
            for (size_t j = 0; j < 10; ++j) {
                char a = headers[i].name[j];
                char b = "Set-Cookie"[j];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) { eq = 0; break; }
            }
            if (eq && !out_set_cookie[0]) {
                size_t cl = headers[i].value_len < out_set_cookie_cap - 1
                                ? headers[i].value_len : out_set_cookie_cap - 1;
                memcpy(out_set_cookie, headers[i].value, cl);
                out_set_cookie[cl] = 0;
            }
        }
    }
    if (content_length < 0) { free(buf); return -1; }

    /* Cap content_length to our response buffer before reading. If the
     * gateway promises more bytes than we can stage, fail closed —
     * silently truncating would let the caller treat the short body as
     * authoritative. */
    if ((size_t)content_length > HC_RESP_BUF - (size_t)header_end) {
        free(buf);
        return -1;
    }

    /* Read remaining body bytes if not already buffered. */
    size_t body_have = total - (size_t)header_end;
    size_t body_need = (size_t)content_length - body_have;
    while (body_need > 0) {
        size_t got = yloop_read_some(stream, buf + total, body_need);
        if (got == 0) break;
        total += got;
        body_need -= got;
    }

    /* If the peer closed before delivering the full body, fail closed.
     * The previous version memcpy'd `content_length` bytes from a buffer
     * holding fewer, exposing uninitialized heap to the caller (info
     * disclosure + downstream UB). */
    if (body_need > 0) {
        free(buf);
        return -1;
    }

    *out_body_len = (size_t)content_length;
    *out_body = malloc((size_t)content_length + 1);
    if (!*out_body) { free(buf); return -1; }
    memcpy(*out_body, buf + header_end, (size_t)content_length);
    (*out_body)[content_length] = 0;
    *out_status = status_code;
    free(buf);
    return 0;
}

int http_post(struct yloop *loop, const struct gateway_url *gw,
              const char *path, const char *content_type,
              const char *bearer,    /* opaque token; may be NULL */
              const char *sid,       /* picomesh-sid cookie value; may be NULL */
              const char *body, size_t body_len,
              struct http_response *resp)
{
    if (!loop || !gw || !path || !body || !resp) return -1;
    if (!content_type) content_type = "application/octet-stream";
    memset(resp, 0, sizeof(*resp));
    /* Reject CRLF / control chars BEFORE writing them into the request
     * stream. `path` and `gw->host` are usually compile-time / config-
     * derived (trusted), but `bearer` and `sid` come from browser-
     * supplied headers/cookies as we wire more routes through, and a
     * `\r\n` in either of those would smuggle an arbitrary header (or
     * a fake body) into the gateway-bound request. Validate at the
     * sink so no future caller can regress. */
    if (!header_value_safe(path) || !header_value_safe(gw->host) ||
        !header_value_safe(content_type) ||
        !header_value_safe(bearer) || !header_value_safe(sid)) {
        ywarn("http_post: unsafe header value rejected");
        return -1;
    }

    struct yloop_stream_ptr_result connect_res =
        yloop_connect_tcp(loop, gw->host, gw->port);
    if (PICOMESH_IS_ERR(connect_res)) {
        picomesh_error_destroy(connect_res.error);
        return -1;
    }
    struct yloop_stream *stream = connect_res.value;

    /* Build request headers. */
    char hdr[1024];
    int header_len = snprintf(hdr, sizeof(hdr),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n",
        path, gw->host, gw->port, content_type, body_len);
    if (header_len <= 0 || (size_t)header_len >= sizeof(hdr)) { yloop_close(stream); return -1; }
    if (bearer && *bearer) {
        int written = snprintf(hdr + header_len, sizeof(hdr) - header_len,
                         "Authorization: Bearer %s\r\n", bearer);
        if (written <= 0 || header_len + written >= (int)sizeof(hdr)) { yloop_close(stream); return -1; }
        header_len += written;
    }
    if (sid && *sid) {
        int written = snprintf(hdr + header_len, sizeof(hdr) - header_len,
                         "picomesh-sid: %s\r\n", sid);
        if (written <= 0 || header_len + written >= (int)sizeof(hdr)) { yloop_close(stream); return -1; }
        header_len += written;
    }
    int written = snprintf(hdr + header_len, sizeof(hdr) - header_len, "\r\n");
    if (written <= 0) { yloop_close(stream); return -1; }
    header_len += written;

    if (yloop_write(stream, hdr, (size_t)header_len) != (size_t)header_len) {
        yloop_close(stream);
        return -1;
    }
    if (yloop_write(stream, body, body_len) != body_len) {
        yloop_close(stream);
        return -1;
    }

    int read_res = read_full_response(stream, &resp->body, &resp->body_len,
                                &resp->status,
                                resp->set_cookie, sizeof(resp->set_cookie));
    yloop_close(stream);
    return read_res;
}

int http_post_json(struct yloop *loop, const struct gateway_url *gw,
                   const char *path,
                   const char *bearer, const char *sid,
                   const char *body, size_t body_len,
                   struct http_response *resp)
{
    return http_post(loop, gw, path, "application/json",
                     bearer, sid, body, body_len, resp);
}

void http_response_free(struct http_response *resp)
{
    if (!resp) return;
    free(resp->body);
    resp->body = NULL;
    resp->body_len = 0;
}
