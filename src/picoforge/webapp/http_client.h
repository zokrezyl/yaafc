/* Outgoing HTTP client for the standalone webapp.
 * NOT installed; internal to src/picoforge/webapp/. */
#ifndef PICOFORGE_WEBAPP_HTTP_CLIENT_H
#define PICOFORGE_WEBAPP_HTTP_CLIENT_H

#include <stddef.h>

#include <picomesh/core/result.h>

struct loop;

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

/* Parse "http://host:port" → struct. Returns 0 on success, -1 on error. */
int gateway_url_parse(const char *url, struct gateway_url *out);

/* POST `body` (length `body_len`) to `path` on `gw` with the given
 * Content-Type, plus optional Authorization: Bearer header and
 * picomesh-sid cookie-as-header. The OK value is 0 on success (response
 * fields populated) or -1 on transport failure (the cause chain is also
 * rendered to the log).
 *
 * Must be called from inside a loop coroutine — internally yields
 * the calling coro across connect / read / write. */
struct picomesh_int_result http_post(struct loop *loop, const struct gateway_url *gw,
              const char *path, const char *content_type,
              const char *bearer, const char *sid,
              const char *body, size_t body_len,
              struct http_response *resp);

/* Convenience wrapper: POST with Content-Type: application/json. */
struct picomesh_int_result http_post_json(struct loop *loop, const struct gateway_url *gw,
                   const char *path,
                   const char *bearer, const char *sid,
                   const char *body, size_t body_len,
                   struct http_response *resp);

void http_response_free(struct http_response *r);

#endif
