/* Outgoing HTTP client for picotrace.
 * NOT installed; internal to src/picomesh/core-apps/picotrace/. */
#ifndef PICOMESH_PICOTRACE_HTTP_CLIENT_H
#define PICOMESH_PICOTRACE_HTTP_CLIENT_H

#include <stddef.h>

struct yloop;

struct gateway_url {
    char host[128];
    int  port;
};

struct http_response {
    int    status;
    char  *body;
    size_t body_len;
    char   set_cookie[512];  /* first Set-Cookie value (multi support TBD) */
};

/* Parse "http://host:port" -> struct. Returns 0 on success, -1 on error. */
int gateway_url_parse(const char *url, struct gateway_url *out);

/* POST `body` (length `body_len`) to `path` on `gw` with the given
 * Content-Type, plus optional Authorization: Bearer header and
 * optional picomesh-sid header. Returns 0 on success (response fields
 * populated), -1 on transport failure.
 *
 * Must be called from inside a yloop coroutine — internally yields
 * the calling coro across connect / read / write. */
int http_post(struct yloop *loop, const struct gateway_url *gw,
              const char *path, const char *content_type,
              const char *bearer, const char *sid,
              const char *body, size_t body_len,
              struct http_response *resp);

/* Convenience wrapper: POST with Content-Type: application/json. */
int http_post_json(struct yloop *loop, const struct gateway_url *gw,
                   const char *path,
                   const char *bearer, const char *sid,
                   const char *body, size_t body_len,
                   struct http_response *resp);

void http_response_free(struct http_response *r);

#endif
