/* frontend.h — HTML route handlers (the git-yaapp frontend, ported).
 *
 * Each route handler:
 *   - parses cookies + form-urlencoded bodies
 *   - calls backend services via the engine's `remote()` sessions
 *     (codegen-emitted public stubs route through `ctx.peer`)
 *   - renders HTML inline (no template engine — sprintf into a buffer)
 *
 * The yhttp serve loop calls `yhttp_frontend_try` BEFORE static-file
 * serving so dynamic routes win. Returns 1 if a response was sent. */

#ifndef PICOMESH_FRONTENDS_YHTTP_FRONTEND_H
#define PICOMESH_FRONTENDS_YHTTP_FRONTEND_H

#include <stddef.h>

struct loop_stream;

struct picomesh_int_result yhttp_frontend_try(struct loop_stream *s,
                       const char *method, const char *path,
                       const char *headers_raw, size_t headers_raw_len,
                       const char *body, size_t body_len,
                       int keep_alive);

#endif
