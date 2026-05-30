/* frontend.c — server-side HTML rendering for the git-yaafc scenario.
 *
 * Mirrors `scenarios/git-yaapp/frontend/frontend.py` from yaapp:
 * Jinja templates → hand-emitted HTML, FastAPI routes → an inline URL
 * router, FastAPI's session cookie → a plain `yaafc-sid` cookie that
 * resolves to a uint32 sid via `session_store_lookup`.
 *
 * Pages currently rendered (URL shape matches git-yaapp where it
 * makes sense; the simpler scalar-only wire keeps some places
 * skinnier than yaapp's account/repo path):
 *
 *   GET  /                      landing (logged in → /repos; else login)
 *   GET  /login                 form
 *   POST /login                 accounts.register + password_authn +
 *                               token_issuer + session, sets cookie
 *   POST /logout                drops the session, clears cookie
 *
 *   GET  /repos                 git_repo list (count) + create form
 *   POST /repos/new             git_repo_store_make
 *   GET  /repo/<id>             repo show (issues + runs panels)
 *   GET  /repo/<id>/issues      issues list (with status filter)
 *   POST /repo/<id>/issues/new  issues_store_open
 *   POST /repo/<id>/issues/<iid>/close
 *   GET  /repo/<id>/runs        pipeline list
 *   POST /repo/<id>/runs/new    git_pipeline_store_enqueue
 *
 *   GET  /admin/users           accounts roster (site-owner only)
 *
 * All backend calls go through the engine's `remote()` sessions —
 * loaded from `mesh.services.gateway.config.remotes[]` at startup.
 *
 * NOTE: this file is the LEGACY gateway-side HTML renderer; the
 * planned split moves these routes into a separate `yaafc-frontend`
 * binary that talks to the gateway via `POST /_rpc`, the same way
 * yaapp's `frontend.py` does. Until that work lands, this file lives
 * inside the gateway process. */

#define _POSIX_C_SOURCE 200809L

#include "frontend.h"

#include <yaafc/yengine/engine.h>
#include <yaafc/yloop/yloop.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/jinvoke.h>
#include <yaafc/yclass/yheaders.h>
#include <yaafc/yjson/yjson.h>
#include <yaafc/yconfig/yconfig.h>
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/yspan.h>

/* Each service header brings in its create() + method stubs. */
#include <yaafc/plugin/sharded_storage/sharded_storage.h>
#include <yaafc/plugin/accounts/accounts.h>
#include <yaafc/plugin/password_authn/password_authn.h>
#include <yaafc/plugin/token_issuer/token_issuer.h>
#include <yaafc/plugin/session/session.h>
#include <yaafc/plugin/issues/issues.h>
#include <yaafc/plugin/git_repo/git_repo.h>
#include <yaafc/plugin/git_pipeline/git_pipeline.h>
#include <yaafc/plugin/personal_access_tokens/personal_access_tokens.h>

#include <ctype.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ---------- tiny growable buffer for response bodies ---------- */

struct buf {
    char *data;
    size_t len;
    size_t cap;
};

static void buf_init(struct buf *b) { b->data = NULL; b->len = b->cap = 0; }
static void buf_free(struct buf *b) { free(b->data); }

static void buf_reserve(struct buf *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 1024;
    while (nc < b->len + extra + 1) nc *= 2;
    char *nd = realloc(b->data, nc);
    if (!nd) return;
    b->data = nd;
    b->cap = nc;
}

static void buf_append(struct buf *b, const char *s, size_t n)
{
    buf_reserve(b, n);
    if (b->cap > b->len + n) {
        memcpy(b->data + b->len, s, n);
        b->len += n;
        b->data[b->len] = 0;
    }
}

static void buf_puts(struct buf *b, const char *s)
{
    buf_append(b, s, strlen(s));
}

static void buf_printf(struct buf *b, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void buf_printf(struct buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    buf_reserve(b, (size_t)n);
    if (b->cap > b->len + (size_t)n) {
        vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap2);
        b->len += (size_t)n;
    }
    va_end(ap2);
}

/* HTML-escape into the buffer. */
static void buf_esc(struct buf *b, const char *s)
{
    if (!s) return;
    for (const char *p = s; *p; ++p) {
        switch (*p) {
        case '&':  buf_puts(b, "&amp;");  break;
        case '<':  buf_puts(b, "&lt;");   break;
        case '>':  buf_puts(b, "&gt;");   break;
        case '"':  buf_puts(b, "&quot;"); break;
        case '\'': buf_puts(b, "&#39;");  break;
        default:   buf_append(b, p, 1);   break;
        }
    }
}

/* ---------- shared rendering ---------- */

/* Deterministic hashes so we can keep typing real usernames/passwords
 * while the underlying plugins still index on uint32/int64 (their wire
 * shape today). FNV-1a, no salt — fine for a demo where the goal is
 * "same name + same password always reach the same account". */
static uint32_t hash_username(const char *s)
{
    uint32_t h = 2166136261u;
    if (s) for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        h ^= *p;
        h *= 16777619u;
    }
    if (!h) h = 1; /* uid==0 means anonymous */
    return h;
}

static int64_t hash_password(const char *s)
{
    uint64_t h = 14695981039346656037ull;
    if (s) for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    return (int64_t)h;
}

/* Forward decls for helpers defined later in the file but referenced
 * from render_head (which renders the global nav and needs to know
 * whether the signed-in user is a site admin) and from
 * route_register_post (bootstrap). */
static int is_site_admin(uint32_t uid);
static void promote_to_site_admin(uint32_t uid);
static const char *landing_url(uint32_t uid, const char *uname,
                               char *out, size_t cap);

static void render_head(struct buf *b, const char *title,
                        uint32_t uid, const char *uname)
{
    buf_printf(b,
        "<!doctype html><html lang=\"en\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>%s · git-yaafc</title>"
        "<script src=\"https://unpkg.com/htmx.org@1.9.10\"></script>"
        "<link rel=\"stylesheet\" href=\"/style.css\">"
        "</head><body>"
        "<nav class=\"topnav\">"
        "<a class=\"brand\" href=\"/\">git-yaafc</a>", title);

    /* Service nav links — only when signed in (mirrors yaapp's
     * base.html: anonymous visitors only see the brand + sign-in).
     * Per-repo links (Issues, Pipelines) live on the repo pages
     * themselves now that URLs follow /<account>/<repo>/... — there's
     * no single "repo #1" to point at from the global header.
     *
     * /admin/users is the only admin surface in the UI now, and it's
     * gated on the site-owner role. Storage internals are NOT a UI
     * concern: there's no /admin/storage page in the nav (or anywhere
     * else) — backend kv inspection happens via sqlite3(1) directly. */
    if (uid) {
        buf_puts(b, "<ul class=\"nav-links\">"
                    "<li><a href=\"/repos\">Repos</a></li>");
        if (uname && *uname) {
            buf_puts(b, "<li><a href=\"/");
            buf_esc(b, uname);
            buf_puts(b, "\">My account</a></li>");
        }
        if (is_site_admin(uid)) {
            buf_puts(b, "<li><a href=\"/admin/users\">Users</a></li>");
        }
        buf_puts(b, "</ul>");
    }

    buf_puts(b, "<div class=\"nav-right\">");
    if (uid) {
        buf_puts(b, "<a class=\"user\" href=\"/\">");
        if (uname && *uname) {
            buf_esc(b, uname);
        } else {
            buf_printf(b, "uid #%u", uid);
        }
        buf_puts(b,
            "</a>"
            "<form method=\"post\" action=\"/logout\" style=\"display:inline\">"
            "<button class=\"link\" type=\"submit\">sign out</button></form>");
    } else {
        buf_puts(b, "<a href=\"/login\">sign in</a>");
    }
    buf_puts(b, "</div></nav><main class=\"container\">");
}

static void render_foot(struct buf *b)
{
    buf_puts(b,
        "</main><footer><span>git-yaafc · ported from yaapp's "
        "git-yaapp scenario · libuv + libco + simdjson + libyaml + libsqlite3"
        "</span></footer></body></html>");
}

/* ---------- HTTP response helpers (mirror of yhttp.c::send_response) -- */
extern size_t yloop_write(struct yloop_stream *s, const void *buf, size_t n);

static void send_html(struct yloop_stream *s, int status,
                      const char *body, size_t body_len, int keep_alive,
                      const char *extra_headers)
{
    const char *reason = "OK";
    if (status == 303) reason = "See Other";
    else if (status == 400) reason = "Bad Request";
    else if (status == 404) reason = "Not Found";
    else if (status == 500) reason = "Internal Server Error";

    char hdr[1024];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "%s"
        "\r\n",
        status, reason, body_len,
        keep_alive ? "keep-alive" : "close",
        extra_headers ? extra_headers : "");
    if (n <= 0) return;
    yloop_write(s, hdr, (size_t)n);
    if (body_len) yloop_write(s, body, body_len);
}

static void send_redirect(struct yloop_stream *s, const char *where,
                          int keep_alive, const char *extra_set_cookie)
{
    char hdr[1024];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 303 See Other\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: %s\r\n"
        "%s"
        "\r\n",
        where, keep_alive ? "keep-alive" : "close",
        extra_set_cookie ? extra_set_cookie : "");
    if (n > 0) yloop_write(s, hdr, (size_t)n);
}

/* ---------- url-decode / form parsing ---------- */

static int hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void urldecode_into(char *out, size_t cap, const char *in, size_t len)
{
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < cap; ++i) {
        if (in[i] == '+') { out[o++] = ' '; continue; }
        if (in[i] == '%' && i + 2 < len) {
            int a = hex((unsigned char)in[i+1]);
            int b = hex((unsigned char)in[i+2]);
            if (a >= 0 && b >= 0) { out[o++] = (char)((a << 4) | b); i += 2; continue; }
        }
        out[o++] = in[i];
    }
    out[o] = 0;
}

/* Pull a value out of `body` (a form-urlencoded payload). Writes the
 * decoded value into `out`. Returns 1 if found, 0 otherwise. */
static int form_get(const char *body, size_t body_len, const char *key,
                    char *out, size_t out_cap)
{
    size_t kl = strlen(key);
    const char *p = body;
    const char *end = body + body_len;
    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        const char *eq = memchr(p, '=', (size_t)((amp ? amp : end) - p));
        if (eq && (size_t)(eq - p) == kl && memcmp(p, key, kl) == 0) {
            const char *vstart = eq + 1;
            size_t vlen = (size_t)((amp ? amp : end) - vstart);
            urldecode_into(out, out_cap, vstart, vlen);
            return 1;
        }
        if (!amp) break;
        p = amp + 1;
    }
    out[0] = 0;
    return 0;
}

/* Pull the value of cookie `name` from a raw HTTP headers block.
 * Looks for `Cookie:` header(s) and splits on `; `. Returns 1 if
 * found. */
static int cookie_get(const char *headers_raw, size_t headers_raw_len,
                      const char *name, char *out, size_t out_cap)
{
    out[0] = 0;
    size_t nl = strlen(name);
    const char *p = headers_raw;
    const char *end = headers_raw + headers_raw_len;
    while (p < end) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        if (!line_end) line_end = end;
        if (line_end - p > 7 && strncasecmp(p, "Cookie:", 7) == 0) {
            const char *c = p + 7;
            while (c < line_end && (*c == ' ' || *c == '\t')) ++c;
            /* Walk `name=value; name=value` */
            while (c < line_end) {
                const char *eq = memchr(c, '=', (size_t)(line_end - c));
                if (!eq) break;
                if ((size_t)(eq - c) == nl && memcmp(c, name, nl) == 0) {
                    const char *vstart = eq + 1;
                    const char *vend = vstart;
                    while (vend < line_end && *vend != ';' && *vend != '\r' && *vend != '\n')
                        ++vend;
                    size_t vlen = (size_t)(vend - vstart);
                    if (vlen >= out_cap) vlen = out_cap - 1;
                    memcpy(out, vstart, vlen);
                    out[vlen] = 0;
                    return 1;
                }
                const char *semi = memchr(c, ';', (size_t)(line_end - c));
                if (!semi) break;
                c = semi + 1;
                while (c < line_end && *c == ' ') ++c;
            }
        }
        if (line_end == end) break;
        p = line_end + 1;
    }
    return 0;
}

/* Resolve the session cookie → user id, or 0 if missing/invalid. */
static uint32_t resolve_uid(const char *headers_raw, size_t headers_raw_len)
{
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return 0;
    char cookie[64];
    if (!cookie_get(headers_raw, headers_raw_len, "yaafc-sid", cookie, sizeof(cookie)))
        return 0;
    uint32_t sid = (uint32_t)strtoul(cookie, NULL, 10);
    if (!sid) return 0;

    struct ctx c = yaafc_engine_service_ctx(e, "session");
    if (!c.peer) return 0;
    struct object_ptr_result o = session_store_create(&c);
    if (YAAFC_IS_ERR(o)) { yaafc_error_destroy(o.error); return 0; }
    struct yaafc_uint32_result lr = session_store_lookup(&c, o.value, NULL, sid);
    object_release_in_ctx(&c, o.value); /* cached, service-lifetime — no-op */
    if (YAAFC_IS_ERR(lr)) { yaafc_error_destroy(lr.error); return 0; }
    return lr.value;
}

/* ---------- per-route page helpers ---------- */

/* Convenience: build a ctx for a named remote, create a transient
 * object, return both — and free the obj when the caller is done. */
struct svc_ctx {
    struct ctx c;
    struct object *obj;
    int ok;
};

#define SVC_OPEN(VAR, SVC, CREATE_FN)                                              \
    struct svc_ctx VAR = {0};                                                      \
    do {                                                                           \
        struct yaafc_engine *_e = yaafc_active_engine();                           \
        if (!_e) break;                                                            \
        VAR.c = yaafc_engine_service_ctx(_e, SVC);                                 \
        if (!VAR.c.peer) break;                                                 \
        struct object_ptr_result _o = CREATE_FN(&VAR.c);                           \
        if (YAAFC_IS_ERR(_o)) { yaafc_error_destroy(_o.error); break; }            \
        VAR.obj = _o.value;                                                        \
        VAR.ok = 1;                                                                \
    } while (0)
/* The backend receiver object is a cached, connection-lifetime dependency
 * (SVC_OPEN routes through the codegen `*_create` → rpc_object_acquire,
 * which caches it on the peer channel). It is NOT created per request, so
 * there is nothing to release here — destroying it would defeat the cache
 * and bring back the per-request CREATE/DESTROY round-trips. */
static inline void svc_close_impl(struct svc_ctx *v)
{
    (void)v;
}
#define SVC_CLOSE(VAR) svc_close_impl(&(VAR))

/* ---------- route: GET / ---------- */

static void route_root(struct yloop_stream *s, uint32_t uid,
                       const char *uname, int keep_alive)
{
    char to[128];
    send_redirect(s, landing_url(uid, uname, to, sizeof(to)),
                  keep_alive, NULL);
}

/* ---------- route: GET /login ---------- */

/* Restrict the username to a charset that's safe inside a cookie value
 * and inside our nav display. Lower-case alpha, digits, "-_." — the
 * intersection of typical handle rules and "no quoting needed". CR/LF
 * are obviously rejected: that's the header-injection vector.
 * Returns 1 if accepted, 0 otherwise. Side-effect: mutates `s` to
 * lower-case in place when accepted. */
static int username_ok(char *s)
{
    if (!s || !*s) return 0;
    size_t n = 0;
    for (char *p = s; *p; ++p, ++n) {
        if (n >= 32) return 0;
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.')) return 0;
        *p = c;
    }
    if (n < 1) return 0;
    return 1;
}

/* Shared form renderer for /login and /register. `is_register` flips
 * heading, button copy, alt-link target. */
static void render_auth_form(struct yloop_stream *s, int is_register,
                             const char *error, int keep_alive)
{
    struct buf b; buf_init(&b);
    render_head(&b, is_register ? "Create account" : "Sign in", 0, NULL);
    buf_puts(&b, "<div class=\"card narrow\"><h1>");
    buf_puts(&b, is_register ? "Create account" : "Sign in");
    buf_puts(&b, "</h1>");
    if (error && *error) {
        buf_puts(&b, "<div class=\"error\">");
        buf_esc(&b, error);
        buf_puts(&b, "</div>");
    }
    buf_printf(&b,
        "<form method=\"post\" action=\"/%s\">"
        "<label>Username"
        "<input type=\"text\" name=\"username\" autofocus required "
        "pattern=\"[a-zA-Z0-9._-]{1,32}\" maxlength=\"32\">"
        "</label>"
        "<label>Password"
        "<input type=\"password\" name=\"password\" required>"
        "</label>"
        "<button type=\"submit\" class=\"primary\">%s</button>"
        "</form>"
        "<p class=\"muted small\">",
        is_register ? "register" : "login",
        is_register ? "Create account" : "Sign in");
    if (is_register) {
        buf_puts(&b, "Already have an account? <a href=\"/login\">Sign in</a>.");
    } else {
        buf_puts(&b, "No account yet? <a href=\"/register\">Create one</a>.");
    }
    buf_puts(&b, "</p></div>");
    render_foot(&b);
    send_html(s, 200, b.data, b.len, keep_alive, NULL);
    buf_free(&b);
}

static void render_login(struct yloop_stream *s, const char *error, int keep_alive)
{
    render_auth_form(s, /*is_register=*/0, error, keep_alive);
}
static void render_register(struct yloop_stream *s, const char *error, int keep_alive)
{
    render_auth_form(s, /*is_register=*/1, error, keep_alive);
}

static void route_login_get(struct yloop_stream *s, int keep_alive)
{
    render_login(s, NULL, keep_alive);
}
static void route_register_get(struct yloop_stream *s, int keep_alive)
{
    render_register(s, NULL, keep_alive);
}

/* Build the Set-Cookie header(s) after a successful auth. Returns
 * non-zero on success; caller has already validated `uname` via
 * username_ok() so it's safe to splice into the cookie value. */
static int build_session_cookies(char *out, size_t cap,
                                 uint32_t sid, const char *uname)
{
    int n = snprintf(out, cap,
        "Set-Cookie: yaafc-sid=%u; Path=/; HttpOnly; SameSite=Lax\r\n"
        "Set-Cookie: yaafc-uname=%s; Path=/; SameSite=Lax\r\n",
        sid, uname);
    return n > 0 && (size_t)n < cap;
}

/* Authenticate + mint a session. Used by both /login and /register
 * (after register has put the credential record in place). On error
 * the function does NOT render — it returns 0 and fills *err with a
 * static error string. On success returns the new sid. */
static uint32_t auth_and_start_session(uint32_t uid, int64_t pw,
                                       const char **err)
{
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) { *err = "no engine"; return 0; }

    SVC_OPEN(pw_sv, "password_authn", password_authn_store_create);
    if (!pw_sv.ok) { *err = "password_authn unreachable"; return 0; }
    struct yaafc_int_result auth =
        password_authn_store_authenticate(&pw_sv.c, pw_sv.obj, NULL, uid, pw);
    SVC_CLOSE(pw_sv);
    if (YAAFC_IS_ERR(auth) || auth.value != 1) {
        if (YAAFC_IS_ERR(auth)) yaafc_error_destroy(auth.error);
        *err = "invalid username or password";
        return 0;
    }

    SVC_OPEN(ti, "token_issuer", token_issuer_store_create);
    if (!ti.ok) { *err = "token_issuer unreachable"; return 0; }
    token_issuer_store_login(&ti.c, ti.obj, NULL, uid, /*provider*/1);
    SVC_CLOSE(ti);

    SVC_OPEN(ses, "session", session_store_create);
    if (!ses.ok) { *err = "session unreachable"; return 0; }
    struct yaafc_uint32_result sid_r =
        session_store_start(&ses.c, ses.obj, NULL, uid, /*provider*/1);
    SVC_CLOSE(ses);
    if (YAAFC_IS_ERR(sid_r)) {
        yaafc_error_destroy(sid_r.error);
        *err = "session create failed";
        return 0;
    }
    return sid_r.value;
}

/* POST /login — yaapp shape: authenticate an EXISTING account; do NOT
 * auto-register. Caller must visit /register first if they don't have
 * a credential yet. */
static void route_login_post(struct yloop_stream *s, const char *body,
                             size_t body_len, int keep_alive)
{
    char uname[64] = {0}, pwtext[128] = {0};
    if (!form_get(body, body_len, "username", uname,  sizeof(uname)) ||
        !form_get(body, body_len, "password", pwtext, sizeof(pwtext)) ||
        !username_ok(uname) || !*pwtext) {
        render_login(s, "username and password are required", keep_alive);
        return;
    }
    uint32_t uid = hash_username(uname);
    int64_t  pw  = hash_password(pwtext);

    /* Refuse to authenticate if the user hasn't registered yet — that's
     * what differentiates /login from /register. */
    SVC_OPEN(acc, "accounts", accounts_store_create);
    if (!acc.ok) { render_login(s, "accounts service unreachable", keep_alive); return; }
    struct yaafc_int_result ex = accounts_store_exists(&acc.c, acc.obj, NULL, uid);
    SVC_CLOSE(acc);
    int exists = YAAFC_IS_OK(ex) && ex.value;
    if (YAAFC_IS_ERR(ex)) yaafc_error_destroy(ex.error);
    if (!exists) {
        render_login(s, "no such user — register first", keep_alive);
        return;
    }

    const char *err = NULL;
    uint32_t sid = auth_and_start_session(uid, pw, &err);
    if (!sid) { render_login(s, err ? err : "login failed", keep_alive); return; }

    char cookie_hdr[256];
    if (!build_session_cookies(cookie_hdr, sizeof(cookie_hdr), sid, uname)) {
        render_login(s, "cookie build failed", keep_alive);
        return;
    }
    char to[128];
    send_redirect(s, landing_url(uid, uname, to, sizeof(to)),
                  keep_alive, cookie_hdr);
}

/* POST /register — create the account + credential, then start a
 * session and redirect to the user's namespace page. Fails if the
 * username is already taken (so the flow forks cleanly from /login). */
static void route_register_post(struct yloop_stream *s, const char *body,
                                size_t body_len, int keep_alive)
{
    char uname[64] = {0}, pwtext[128] = {0};
    if (!form_get(body, body_len, "username", uname,  sizeof(uname)) ||
        !form_get(body, body_len, "password", pwtext, sizeof(pwtext)) ||
        !username_ok(uname) || !*pwtext) {
        render_register(s, "username (a-z, 0-9, ._-) and password required", keep_alive);
        return;
    }
    uint32_t uid = hash_username(uname);
    int64_t  pw  = hash_password(pwtext);

    /* Store the credential FIRST, and CHECK it. If this is skipped or its
     * result ignored (as it was), a failed password write — e.g. a backend
     * still coming up during the slow in-browser VM boot — leaves a
     * passwordless account in `accounts`, and every later login then fails
     * with the misleading "invalid username or password". Doing the
     * password write before the accounts entry also means a failure leaves
     * nothing half-created, so the user can just retry once the mesh is up
     * (the retry re-uses the now-stored hash: register returns 0 = already
     * present, which is fine here). */
    SVC_OPEN(pw_sv, "password_authn", password_authn_store_create);
    if (!pw_sv.ok) { render_register(s, "password_authn unreachable", keep_alive); return; }
    struct yaafc_int_result pwreg =
        password_authn_store_register(&pw_sv.c, pw_sv.obj, NULL, uid, pw);
    SVC_CLOSE(pw_sv);
    if (YAAFC_IS_ERR(pwreg)) {
        yaafc_error_destroy(pwreg.error);
        render_register(s, "could not store credentials (backend not ready?) — try again", keep_alive);
        return;
    }

    SVC_OPEN(acc, "accounts", accounts_store_create);
    if (!acc.ok) { render_register(s, "accounts service unreachable", keep_alive); return; }
    struct yaafc_int_result reg = accounts_store_register(&acc.c, acc.obj, NULL, uid);
    SVC_CLOSE(acc);
    int was_new = YAAFC_IS_OK(reg) && reg.value == 1;
    if (YAAFC_IS_ERR(reg)) yaafc_error_destroy(reg.error);
    if (!was_new) {
        render_register(s, "username already taken", keep_alive);
        return;
    }

    /* Bootstrap: the very first user becomes site-owner. After
     * `accounts.register` succeeds and the running total is 1, this
     * uid is the only registered account → promote it. */
    SVC_OPEN(acc2, "accounts", accounts_store_create);
    if (acc2.ok) {
        struct yaafc_size_result cr = accounts_store_count(&acc2.c, acc2.obj, NULL);
        if (YAAFC_IS_OK(cr) && cr.value == 1) {
            promote_to_site_admin(uid);
        } else if (YAAFC_IS_ERR(cr)) {
            yaafc_error_destroy(cr.error);
        }
        SVC_CLOSE(acc2);
    }

    const char *err = NULL;
    uint32_t sid = auth_and_start_session(uid, pw, &err);
    if (!sid) { render_register(s, err ? err : "register failed", keep_alive); return; }

    char cookie_hdr[256];
    if (!build_session_cookies(cookie_hdr, sizeof(cookie_hdr), sid, uname)) {
        render_register(s, "cookie build failed", keep_alive);
        return;
    }
    char to[128];
    send_redirect(s, landing_url(uid, uname, to, sizeof(to)),
                  keep_alive, cookie_hdr);
}

/* POST /logout — destroy the server-side session record AND wipe the
 * cookies. The earlier version only cleared the browser cookie, so a
 * copied/stale sid stayed valid server-side (gh#2). */
static void route_logout(struct yloop_stream *s,
                         const char *headers_raw, size_t headers_raw_len,
                         int keep_alive)
{
    char sid_s[64];
    if (cookie_get(headers_raw, headers_raw_len, "yaafc-sid", sid_s, sizeof(sid_s))) {
        uint32_t sid = (uint32_t)strtoul(sid_s, NULL, 10);
        if (sid) {
            SVC_OPEN(ses, "session", session_store_create);
            if (ses.ok) {
                struct yaafc_int_result r = session_store_destroy(&ses.c, ses.obj, NULL, sid);
                if (YAAFC_IS_ERR(r)) yaafc_error_destroy(r.error);
                SVC_CLOSE(ses);
            }
        }
    }
    send_redirect(s, "/login", keep_alive,
        "Set-Cookie: yaafc-sid=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0\r\n"
        "Set-Cookie: yaafc-uname=; Path=/; SameSite=Lax; Max-Age=0\r\n");
}

/* ---------- repo URL helpers ---------- */

/* Hash an `<account>/<name>` pair to the 32-bit repo_id the backend
 * services (git_repo, issues, git_pipeline) all key on. Same FNV-1a
 * 32 as username/password; with `account` already hashed to a uid
 * elsewhere this gives a stable per-(account,name) identifier. */
static uint32_t hash_repo(const char *account, const char *name)
{
    uint32_t h = 2166136261u;
    if (account) for (const unsigned char *p = (const unsigned char *)account; *p; ++p) {
        h ^= *p; h *= 16777619u;
    }
    h ^= '/'; h *= 16777619u;
    if (name) for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        h ^= *p; h *= 16777619u;
    }
    if (!h) h = 1;
    return h;
}

/* The set of single-segment paths that are NOT user accounts —
 * mirrors yaapp's _RESERVED_TOP. The dispatcher checks this so
 * `/login` doesn't get parsed as user-account "login". */
static int is_reserved_top(const char *seg, size_t seg_len)
{
    static const char *const RESERVED[] = {
        "login", "logout", "register", "auth", "repos", "issues",
        "runs", "runners", "orgs", "admin", "favicon.ico", "robots.txt",
        "style.css", "app.js", "static", NULL,
    };
    for (size_t i = 0; RESERVED[i]; ++i) {
        size_t n = strlen(RESERVED[i]);
        if (n == seg_len && memcmp(seg, RESERVED[i], n) == 0) return 1;
    }
    return 0;
}

/* Pick where to send a freshly-authenticated user (mirrors yaapp's
 * `_landing_url` in frontend.py).
 *
 *   1. `/<username>` is the canonical GitHub-style landing.
 *   2. If the username happens to collide with a reserved top-level
 *      path (e.g. a bootstrap account named `admin`), no namespace
 *      page can be rendered for them — site-owners fall back to
 *      `/admin/users`, regular users fall back to `/login` (the only
 *      page guaranteed to render for them).
 *
 * `uname` may be NULL/empty (e.g. when called from route_root and the
 * cookie wasn't sent). `uid` of 0 is the anonymous signal — used by
 * route_root for the never-signed-in case. Writes a NUL-terminated
 * path into `out` and returns a pointer to it. */
static const char *landing_url(uint32_t uid, const char *uname,
                               char *out, size_t cap)
{
    if (uname && *uname && !is_reserved_top(uname, strlen(uname))) {
        snprintf(out, cap, "/%s", uname);
        return out;
    }
    if (uid && is_site_admin(uid)) {
        snprintf(out, cap, "/admin/users");
        return out;
    }
    snprintf(out, cap, "/login");
    return out;
}

/* Repo-name charset: same as username so the URL stays well-formed. */
static int reponame_ok(const char *s, size_t n)
{
    if (n < 1 || n > 32) return 0;
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return 0;
    }
    return 1;
}

/* Open the frontend's `storage` remote for repo-registry reads/writes.
 * The frontend already declares storage in its remotes block. */
static int repo_storage_open(struct svc_ctx *out)
{
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return 0;
    out->c = yaafc_engine_service_ctx(e, "sharded_storage");
    if (!out->c.peer) return 0;
    struct object_ptr_result o = sharded_storage_db_create(&out->c);
    if (YAAFC_IS_ERR(o)) { yaafc_error_destroy(o.error); return 0; }
    out->obj = o.value;
    out->ok = 1;
    return 1;
}

/* ---------- authz: site-owner / regular-user role check ----------
 *
 * Mirrors yaapp's `'site:owner' in user.groups` check. We store the
 * role as an int64 at key `role:<uid>` in the `accounts` storage
 * context (0 = regular user, 1 = site-owner). The first user to
 * /register gets role=1 automatically (bootstrap); subsequent users
 * get 0.
 *
 * Without this gate any signed-in user could hit /admin/users — the
 * frontend has to enforce policy explicitly because the underlying
 * accounts plugin doesn't carry authz state itself. */

/* Read the role bit. Returns 0 (regular) on any error / absent key. */
static int is_site_admin(uint32_t uid)
{
    if (!uid) return 0;
    struct svc_ctx st = {0};
    if (!repo_storage_open(&st)) return 0;
    char k[64];
    snprintf(k, sizeof(k), "role:%u", uid);
    struct yaafc_string_result r = sharded_storage_db_get(&st.c, st.obj, NULL, "accounts", k);
    int admin = YAAFC_IS_OK(r) && r.value && strtoll(r.value, NULL, 10) >= 1;
    if (YAAFC_IS_OK(r)) free(r.value); else yaafc_error_destroy(r.error);
    SVC_CLOSE(st);
    return admin;
}

/* Promote `uid` to site-owner. Called from /register when accounts
 * count is exactly 1 right after a successful register — i.e. this
 * was the first user to ever join. */
static void promote_to_site_admin(uint32_t uid)
{
    if (!uid) return;
    struct svc_ctx st = {0};
    if (!repo_storage_open(&st)) return;
    char k[64];
    snprintf(k, sizeof(k), "role:%u", uid);
    sharded_storage_db_set(&st.c, st.obj, NULL, "accounts", k, "1");
    SVC_CLOSE(st);
    yinfo("authz: uid=%u bootstrapped as site-owner", uid);
}

/* Mark <account>/<name> as a registered repo and return its repo_id.
 * Storage layout in the `repos` context:
 *   by_id:<repo_id>            = 1
 *   owner:<account_uid>:count  = <N>
 */
static uint32_t repo_register(const char *account, const char *name, uint32_t owner_uid)
{
    uint32_t rid = hash_repo(account, name);
    struct svc_ctx st = {0};
    if (!repo_storage_open(&st)) return 0;
    char k[96];
    snprintf(k, sizeof(k), "by_id:%u", rid);
    sharded_storage_db_set(&st.c, st.obj, NULL, "repos", k, "1");
    snprintf(k, sizeof(k), "owner:%u:count", owner_uid);
    struct yaafc_string_result cur = sharded_storage_db_get(&st.c, st.obj, NULL, "repos", k);
    int64_t count = (YAAFC_IS_OK(cur) && cur.value ? strtoll(cur.value, NULL, 10) : 0) + 1;
    if (YAAFC_IS_OK(cur)) free(cur.value); else yaafc_error_destroy(cur.error);
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "%lld", (long long)count);
    sharded_storage_db_set(&st.c, st.obj, NULL, "repos", k, cbuf);
    SVC_CLOSE(st);
    return rid;
}

static int repo_exists(uint32_t repo_id)
{
    struct svc_ctx st = {0};
    if (!repo_storage_open(&st)) return 0;
    char k[64];
    snprintf(k, sizeof(k), "by_id:%u", repo_id);
    struct yaafc_int_result r = sharded_storage_db_exists(&st.c, st.obj, NULL, "repos", k);
    int present = YAAFC_IS_OK(r) && r.value;
    if (YAAFC_IS_ERR(r)) yaafc_error_destroy(r.error);
    SVC_CLOSE(st);
    return present;
}

static int64_t repo_count_for_owner(uint32_t owner_uid)
{
    struct svc_ctx st = {0};
    if (!repo_storage_open(&st)) return -1;
    char k[64];
    snprintf(k, sizeof(k), "owner:%u:count", owner_uid);
    struct yaafc_string_result r = sharded_storage_db_get(&st.c, st.obj, NULL, "repos", k);
    int64_t v = (YAAFC_IS_OK(r) && r.value) ? strtoll(r.value, NULL, 10) : 0;
    if (YAAFC_IS_OK(r)) free(r.value); else yaafc_error_destroy(r.error);
    SVC_CLOSE(st);
    return v;
}

/* ---------- route: GET /repos + POST /repos/new ---------- */

/* Pull the `yaafc-uname` cookie value into a stack buffer. Returns 1
 * if found AND syntactically valid. Used by the per-request handlers
 * to decide which account namespace to write into. */
static int resolve_uname(const char *headers_raw, size_t headers_raw_len,
                         char *out, size_t cap)
{
    if (!cookie_get(headers_raw, headers_raw_len, "yaafc-uname", out, cap))
        return 0;
    return username_ok(out);
}

static void render_repos(struct yloop_stream *s, uint32_t uid,
                         const char *uname, int keep_alive)
{
    struct buf b; buf_init(&b);
    render_head(&b, "Repositories", uid, uname);
    buf_puts(&b,
        "<header class=\"page-header\">"
        "<h1>Repositories</h1>"
        "<a class=\"btn primary\" href=\"#new\">New repository</a>"
        "</header>");

    /* Repo count from git_repo. */
    int64_t total = -1;
    SVC_OPEN(repo, "git_repo", git_repo_store_create);
    if (repo.ok) {
        struct yaafc_size_result r = git_repo_store_count_total(&repo.c, repo.obj, NULL);
        if (YAAFC_IS_OK(r)) total = (int64_t)r.value;
        else yaafc_error_destroy(r.error);
        SVC_CLOSE(repo);
    }

    int64_t mine = (uname && *uname) ? repo_count_for_owner(uid) : -1;

    buf_puts(&b, "<section class=\"card\"><header class=\"card-header\">"
                 "<h2>All repositories</h2><span class=\"muted small\">");
    if (total >= 0) buf_printf(&b, "%lld total", (long long)total);
    else buf_puts(&b, "git_repo unreachable");
    buf_puts(&b, "</span></header>");
    if (mine >= 0 && uname) {
        buf_printf(&b, "<p class=\"muted small\">You own %lld repo%s:</p>",
                   (long long)mine, mine == 1 ? "" : "s");
        /* List the signed-in user's repos by name as links. */
        char *names = NULL;
        SVC_OPEN(rlist, "git_repo", git_repo_store_create);
        if (rlist.ok) {
            struct yaafc_string_result lr =
                git_repo_store_list_for_owner(&rlist.c, rlist.obj, NULL, uid);
            SVC_CLOSE(rlist);
            if (YAAFC_IS_OK(lr)) names = lr.value;
            else yaafc_error_destroy(lr.error);
        }
        if (names && *names) {
            buf_puts(&b, "<ul class=\"repo-list\">");
            char *p = names;
            while (*p) {
                char *nl = strchr(p, '\n');
                size_t seg = nl ? (size_t)(nl - p) : strlen(p);
                if (seg) {
                    char nm[80];
                    size_t cn = seg < sizeof(nm) - 1 ? seg : sizeof(nm) - 1;
                    memcpy(nm, p, cn);
                    nm[cn] = 0;
                    buf_printf(&b, "<li><a href=\"/%s/%s\">%s/%s</a></li>",
                               uname, nm, uname, nm);
                }
                if (!nl) break;
                p = nl + 1;
            }
            buf_puts(&b, "</ul>");
        }
        free(names);
    }
    if (total <= 0) {
        buf_puts(&b, "<p class=\"muted\">No repos yet. Use the form below.</p>");
    }
    buf_puts(&b, "</section>");

    /* New form — owner is implicit (the signed-in user). */
    buf_printf(&b,
        "<section class=\"card\" id=\"new\"><header class=\"card-header\">"
        "<h2>Create a new repository</h2></header>"
        "<form method=\"post\" action=\"/repos/new\">"
        "<label>Name <input type=\"text\" name=\"name\" "
        "pattern=\"[a-zA-Z0-9._-]{1,32}\" maxlength=\"32\" required></label>"
        "<button type=\"submit\" class=\"primary\">Create as %s/&hellip;</button>"
        "</form></section>",
        uname ? uname : "you");

    render_foot(&b);
    send_html(s, 200, b.data, b.len, keep_alive, NULL);
    buf_free(&b);
}

static void route_repos_new_post(struct yloop_stream *s, uint32_t uid,
                                 const char *uname,
                                 const char *body, size_t body_len, int keep_alive)
{
    if (!uname || !*uname) { send_redirect(s, "/login", keep_alive, NULL); return; }

    char name[64];
    if (!form_get(body, body_len, "name", name, sizeof(name)) ||
        !reponame_ok(name, strlen(name))) {
        render_repos(s, uid, uname, keep_alive);
        return;
    }

    uint32_t rid = repo_register(uname, name, uid);
    if (!rid) { send_redirect(s, "/repos", keep_alive, NULL); return; }

    /* Mirror the binding into git_repo so its count_total reflects
     * reality AND a bare repo lands on disk under the per-user
     * parent dir (<repos_dir>/<uname>/<name>.git). */
    SVC_OPEN(repo, "git_repo", git_repo_store_create);
    if (repo.ok) {
        git_repo_store_make(&repo.c, repo.obj, NULL, uid, uname, name);
        SVC_CLOSE(repo);
    }

    char where[128];
    snprintf(where, sizeof(where), "/%s/%s", uname, name);
    send_redirect(s, where, keep_alive, NULL);
}

/* Parse `/<account>/<name>[/<rest>]` into its three pieces. Returns 1
 * on success. `account` and `name` are zero-terminated copies; `rest`
 * is a pointer into the original path (NULL if the URL is just
 * /<account>/<name>). On failure all outputs are left untouched. */
static int parse_account_repo(const char *path, char *acct, size_t acct_cap,
                              char *name, size_t name_cap,
                              const char **rest_out)
{
    if (!path || path[0] != '/') return 0;
    const char *p = path + 1;
    const char *slash = strchr(p, '/');
    if (!slash) return 0;
    size_t alen = (size_t)(slash - p);
    if (is_reserved_top(p, alen)) return 0;
    if (alen < 1 || alen >= acct_cap) return 0;
    memcpy(acct, p, alen); acct[alen] = 0;
    if (!username_ok(acct)) return 0;

    const char *q = slash + 1;
    const char *end = q;
    while (*end && *end != '/' && *end != '?') ++end;
    size_t nlen = (size_t)(end - q);
    if (nlen < 1 || nlen >= name_cap) return 0;
    memcpy(name, q, nlen); name[nlen] = 0;
    if (!reponame_ok(name, nlen)) return 0;

    *rest_out = (*end == '/' || *end == '?') ? end : NULL;
    return 1;
}

/* All per-repo handlers now key on the URL pair `(account, name)`.
 * The wire-level repo_id is derived from `hash_repo(account, name)`
 * — same value at any call site, so the backend services (issues,
 * git_pipeline) continue to use their existing uint32 repo_id keys.
 *
 * Inside the renderers we build internal URLs as /<acct>/<name>/...
 * to keep the page graph closed under the yaapp shape. */

/* ---------- route: GET /<account> ---------- */

static void render_account_landing(struct yloop_stream *s, uint32_t uid,
                                   const char *uname,
                                   const char *account, int keep_alive)
{
    struct buf b; buf_init(&b);
    char title[80];
    snprintf(title, sizeof(title), "@%s", account);
    render_head(&b, title, uid, uname);

    uint32_t acct_uid = hash_username(account);
    int64_t  count    = repo_count_for_owner(acct_uid);

    buf_printf(&b,
        "<header class=\"page-header\">"
        "<h1>@%s</h1>"
        "<a class=\"btn primary\" href=\"/repos#new\">New repository</a>"
        "</header>"
        "<section class=\"card\"><header class=\"card-header\">"
        "<h2>Repositories</h2><span class=\"muted small\">",
        account);
    if (count < 0) buf_puts(&b, "storage unreachable");
    else           buf_printf(&b, "%lld owned", (long long)count);
    buf_puts(&b, "</span></header>");
    if (count <= 0) {
        buf_puts(&b, "<p class=\"muted\">No repos yet for this user. "
                     "Create one from <a href=\"/repos#new\">/repos</a>.</p>");
    } else {
        /* Enumerate the owner's repos by NAME and render each as a link.
         * (Previously the page only showed a count and told you to guess
         * the URL — so a created repo never appeared anywhere.) */
        char *names = NULL;
        SVC_OPEN(rlist, "git_repo", git_repo_store_create);
        if (rlist.ok) {
            struct yaafc_string_result lr =
                git_repo_store_list_for_owner(&rlist.c, rlist.obj, NULL, acct_uid);
            SVC_CLOSE(rlist);
            if (YAAFC_IS_OK(lr)) names = lr.value;
            else yaafc_error_destroy(lr.error);
        }
        if (names && *names) {
            buf_puts(&b, "<ul class=\"repo-list\">");
            char *p = names;
            while (*p) {
                char *nl = strchr(p, '\n');
                size_t seg = nl ? (size_t)(nl - p) : strlen(p);
                if (seg) {
                    char nm[80];
                    size_t cn = seg < sizeof(nm) - 1 ? seg : sizeof(nm) - 1;
                    memcpy(nm, p, cn);
                    nm[cn] = 0;
                    /* name is reponame_ok-validated ([a-zA-Z0-9._-]) → safe. */
                    buf_printf(&b, "<li><a href=\"/%s/%s\">%s/%s</a></li>",
                               account, nm, account, nm);
                }
                if (!nl) break;
                p = nl + 1;
            }
            buf_puts(&b, "</ul>");
        } else {
            buf_puts(&b, "<p class=\"muted\">No repos yet.</p>");
        }
        free(names);
    }
    buf_puts(&b, "</section>");

    render_foot(&b);
    send_html(s, 200, b.data, b.len, keep_alive, NULL);
    buf_free(&b);
}

/* ---------- per-repo renderers ---------- */

static void render_repo_show(struct yloop_stream *s, uint32_t uid,
                             const char *uname,
                             const char *account, const char *name,
                             int keep_alive)
{
    uint32_t repo_id = hash_repo(account, name);
    if (!repo_exists(repo_id)) {
        send_html(s, 404, "<p>repo not found</p>", 21, keep_alive, NULL);
        return;
    }

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "%s/%s", account, name);
    render_head(&b, title, uid, uname);

    /* Resolve owner. */
    uint32_t owner = 0;
    SVC_OPEN(rep, "git_repo", git_repo_store_create);
    if (rep.ok) {
        struct yaafc_uint32_result r = git_repo_store_owner_of(&rep.c, rep.obj, NULL, repo_id);
        if (YAAFC_IS_OK(r)) owner = r.value;
        else yaafc_error_destroy(r.error);
        SVC_CLOSE(rep);
    }

    int64_t open_issues = -1, queued = -1, running = -1, done = -1;
    SVC_OPEN(iss, "issues", issues_store_create);
    if (iss.ok) {
        struct yaafc_size_result o = issues_store_count_open_in_repo(&iss.c, iss.obj, NULL, repo_id);
        if (YAAFC_IS_OK(o)) open_issues = (int64_t)o.value; else yaafc_error_destroy(o.error);
        SVC_CLOSE(iss);
    }
    SVC_OPEN(gp, "git_pipeline", git_pipeline_store_create);
    if (gp.ok) {
        struct yaafc_size_result p = git_pipeline_store_count_pending(&gp.c, gp.obj, NULL);
        struct yaafc_size_result r = git_pipeline_store_count_running(&gp.c, gp.obj, NULL);
        struct yaafc_size_result d = git_pipeline_store_count_done(&gp.c, gp.obj, NULL);
        if (YAAFC_IS_OK(p)) queued = (int64_t)p.value; else yaafc_error_destroy(p.error);
        if (YAAFC_IS_OK(r)) running = (int64_t)r.value; else yaafc_error_destroy(r.error);
        if (YAAFC_IS_OK(d)) done = (int64_t)d.value; else yaafc_error_destroy(d.error);
        SVC_CLOSE(gp);
    }

    buf_printf(&b,
        "<header class=\"page-header\">"
        "<div><h1><a href=\"/%s\">%s</a>/%s</h1>"
        "<p class=\"muted small\">owner uid: <code>%u</code> · "
        "repo_id: <code>%u</code></p></div>"
        "<a class=\"btn\" href=\"/repos\">All repos ▸</a>"
        "</header>"
        "<section class=\"card\"><header class=\"card-header\">"
        "<h2>Issues</h2>"
        "<a class=\"btn\" href=\"/%s/%s/issues\">browse ▸</a>"
        "</header>",
        account, account, name, owner, repo_id, account, name);
    if (open_issues >= 0) {
        buf_printf(&b, "<p>%lld open issue%s.</p>",
                   (long long)open_issues, open_issues == 1 ? "" : "s");
    } else {
        buf_puts(&b, "<p class=\"muted\">issues service unreachable.</p>");
    }
    buf_puts(&b, "</section>");

    buf_printf(&b,
        "<section class=\"card\"><header class=\"card-header\">"
        "<h2>Pipeline runs</h2>"
        "<a class=\"btn\" href=\"/%s/%s/runs\">browse ▸</a></header>",
        account, name);
    if (queued >= 0) {
        buf_printf(&b,
            "<p>"
            "<span class=\"badge queued\">%lld queued</span> · "
            "<span class=\"badge running\">%lld running</span> · "
            "<span class=\"badge succeeded\">%lld done</span></p>",
            (long long)queued, (long long)running, (long long)done);
    } else {
        buf_puts(&b, "<p class=\"muted\">git_pipeline unreachable.</p>");
    }
    buf_puts(&b, "</section>");

    buf_printf(&b,
        "<section class=\"card\"><h2>Clone</h2>"
        "<pre class=\"clone-url\">git clone http://127.0.0.1:8209/git/%s/%s.git</pre>"
        "<p class=\"muted small\">(stub URL — git transport isn't wired in yet.)</p>"
        "</section>", account, name);

    render_foot(&b);
    send_html(s, 200, b.data, b.len, keep_alive, NULL);
    buf_free(&b);
}

static void render_repo_issues(struct yloop_stream *s, uint32_t uid,
                               const char *uname,
                               const char *account, const char *name,
                               const char *filter, int keep_alive)
{
    uint32_t repo_id = hash_repo(account, name);
    if (!repo_exists(repo_id)) {
        send_html(s, 404, "<p>repo not found</p>", 21, keep_alive, NULL);
        return;
    }

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "Issues — %s/%s", account, name);
    render_head(&b, title, uid, uname);

    int show_closed = filter && strcmp(filter, "closed") == 0;
    int show_open   = !filter || strcmp(filter, "open") == 0;

    int64_t open_in_repo = -1;
    SVC_OPEN(iss, "issues", issues_store_create);
    if (iss.ok) {
        struct yaafc_size_result o = issues_store_count_open_in_repo(&iss.c, iss.obj, NULL, repo_id);
        if (YAAFC_IS_OK(o)) open_in_repo = (int64_t)o.value; else yaafc_error_destroy(o.error);
        SVC_CLOSE(iss);
    }

    buf_printf(&b,
        "<header class=\"page-header\">"
        "<div><h1>Issues</h1>"
        "<p class=\"muted small\"><a href=\"/%s/%s\">← %s/%s</a></p></div>"
        "<a class=\"btn primary\" href=\"#new\">New issue</a>"
        "</header>"
        "<nav class=\"filters\">"
        "<a class=\"%s\" href=\"/%s/%s/issues?status=open\">Open</a>"
        "<a class=\"%s\" href=\"/%s/%s/issues?status=closed\">Closed</a>"
        "<a href=\"/%s/%s/issues\">All</a>"
        "</nav>",
        account, name, account, name,
        show_open   ? "active" : "", account, name,
        show_closed ? "active" : "", account, name,
        account, name);

    buf_puts(&b, "<section class=\"card\">");
    if (open_in_repo >= 0) {
        buf_printf(&b, "<p>%lld open issue%s in this repo.</p>",
                   (long long)open_in_repo, open_in_repo == 1 ? "" : "s");
    } else {
        buf_puts(&b, "<p class=\"muted\">issues service unreachable.</p>");
    }
    buf_puts(&b, "</section>");

    buf_printf(&b,
        "<section class=\"card\" id=\"new\">"
        "<header class=\"card-header\"><h2>File a new issue</h2></header>"
        "<form method=\"post\" action=\"/%s/%s/issues/new\">"
        "<button type=\"submit\" class=\"primary\">Open issue as you</button>"
        "</form></section>"
        "<section class=\"card\">"
        "<header class=\"card-header\"><h2>Close an issue</h2></header>"
        "<form method=\"post\" action=\"/%s/%s/issues/close\">"
        "<label>Issue id"
        "<input type=\"number\" name=\"issue_id\" required>"
        "</label>"
        "<button type=\"submit\">Close</button>"
        "</form></section>",
        account, name, account, name);

    render_foot(&b);
    send_html(s, 200, b.data, b.len, keep_alive, NULL);
    buf_free(&b);
}

static void route_repo_issues_new_post(struct yloop_stream *s, uint32_t uid,
                                       const char *account, const char *name,
                                       const char *body, size_t body_len,
                                       int keep_alive)
{
    (void)body; (void)body_len;
    uint32_t repo_id = hash_repo(account, name);
    if (!repo_exists(repo_id)) { send_html(s, 404, "<p>no such repo</p>", 19, keep_alive, NULL); return; }
    SVC_OPEN(iss, "issues", issues_store_create);
    if (iss.ok) {
        issues_store_open(&iss.c, iss.obj, NULL, repo_id, uid ? uid : 1);
        SVC_CLOSE(iss);
    }
    char where[160];
    snprintf(where, sizeof(where), "/%s/%s/issues", account, name);
    send_redirect(s, where, keep_alive, NULL);
}

static void route_repo_issues_close_post(struct yloop_stream *s, uint32_t uid,
                                         const char *account, const char *name,
                                         const char *body, size_t body_len,
                                         int keep_alive)
{
    (void)uid;
    uint32_t repo_id = hash_repo(account, name);
    if (!repo_exists(repo_id)) { send_html(s, 404, "<p>no such repo</p>", 19, keep_alive, NULL); return; }
    char id_s[32];
    if (form_get(body, body_len, "issue_id", id_s, sizeof(id_s))) {
        uint32_t iid = (uint32_t)strtoul(id_s, NULL, 10);
        if (iid) {
            SVC_OPEN(iss, "issues", issues_store_create);
            if (iss.ok) {
                issues_store_close(&iss.c, iss.obj, NULL, iid);
                SVC_CLOSE(iss);
            }
        }
    }
    char where[160];
    snprintf(where, sizeof(where), "/%s/%s/issues", account, name);
    send_redirect(s, where, keep_alive, NULL);
}

static void render_repo_runs(struct yloop_stream *s, uint32_t uid,
                             const char *uname,
                             const char *account, const char *name, int keep_alive)
{
    uint32_t repo_id = hash_repo(account, name);
    if (!repo_exists(repo_id)) {
        send_html(s, 404, "<p>repo not found</p>", 21, keep_alive, NULL);
        return;
    }
    (void)repo_id; /* git_pipeline counts are global today */

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "Pipelines — %s/%s", account, name);
    render_head(&b, title, uid, uname);

    int64_t q = -1, r = -1, d = -1;
    SVC_OPEN(gp, "git_pipeline", git_pipeline_store_create);
    if (gp.ok) {
        struct yaafc_size_result a = git_pipeline_store_count_pending(&gp.c, gp.obj, NULL);
        struct yaafc_size_result b2 = git_pipeline_store_count_running(&gp.c, gp.obj, NULL);
        struct yaafc_size_result c = git_pipeline_store_count_done(&gp.c, gp.obj, NULL);
        if (YAAFC_IS_OK(a)) q = (int64_t)a.value;  else yaafc_error_destroy(a.error);
        if (YAAFC_IS_OK(b2)) r = (int64_t)b2.value; else yaafc_error_destroy(b2.error);
        if (YAAFC_IS_OK(c)) d = (int64_t)c.value;  else yaafc_error_destroy(c.error);
        SVC_CLOSE(gp);
    }

    buf_printf(&b,
        "<header class=\"page-header\">"
        "<div><h1>Pipeline runs</h1>"
        "<p class=\"muted small\"><a href=\"/%s/%s\">← %s/%s</a></p></div>"
        "<a class=\"btn primary\" href=\"#new\">Enqueue job</a></header>"
        "<section class=\"card\">"
        "<table class=\"grid\"><thead><tr><th>State</th><th>Count</th></tr></thead>"
        "<tbody>"
        "<tr><td>queued</td>  <td><span class=\"badge queued\">%lld</span></td></tr>"
        "<tr><td>running</td> <td><span class=\"badge running\">%lld</span></td></tr>"
        "<tr><td>finished</td><td><span class=\"badge succeeded\">%lld</span></td></tr>"
        "</tbody></table></section>"
        "<section class=\"card\" id=\"new\">"
        "<header class=\"card-header\"><h2>Enqueue a job for this repo</h2></header>"
        "<form method=\"post\" action=\"/%s/%s/runs/new\">"
        "<button type=\"submit\" class=\"primary\">git_pipeline_store_enqueue</button>"
        "</form></section>"
        "<section class=\"card\">"
        "<header class=\"card-header\"><h2>Lease the next job</h2></header>"
        "<form method=\"post\" action=\"/%s/%s/runs/lease\">"
        "<label>Runner uid <input type=\"number\" name=\"runner\" value=\"1\"></label>"
        "<button type=\"submit\">lease</button>"
        "</form></section>",
        account, name, account, name,
        (long long)(q < 0 ? 0 : q),
        (long long)(r < 0 ? 0 : r),
        (long long)(d < 0 ? 0 : d),
        account, name, account, name);

    render_foot(&b);
    send_html(s, 200, b.data, b.len, keep_alive, NULL);
    buf_free(&b);
}

static void route_repo_runs_new_post(struct yloop_stream *s, uint32_t uid,
                                     const char *account, const char *name,
                                     int keep_alive)
{
    (void)uid;
    uint32_t repo_id = hash_repo(account, name);
    if (!repo_exists(repo_id)) { send_html(s, 404, "<p>no such repo</p>", 19, keep_alive, NULL); return; }
    SVC_OPEN(gp, "git_pipeline", git_pipeline_store_create);
    if (gp.ok) {
        git_pipeline_store_enqueue(&gp.c, gp.obj, NULL, repo_id);
        SVC_CLOSE(gp);
    }
    char where[160];
    snprintf(where, sizeof(where), "/%s/%s/runs", account, name);
    send_redirect(s, where, keep_alive, NULL);
}

static void route_repo_runs_lease_post(struct yloop_stream *s, uint32_t uid,
                                       const char *account, const char *name,
                                       const char *body, size_t body_len, int keep_alive)
{
    (void)uid;
    uint32_t repo_id = hash_repo(account, name);
    if (!repo_exists(repo_id)) { send_html(s, 404, "<p>no such repo</p>", 19, keep_alive, NULL); return; }
    char runner_s[32];
    if (form_get(body, body_len, "runner", runner_s, sizeof(runner_s))) {
        uint32_t runner = (uint32_t)strtoul(runner_s, NULL, 10);
        if (!runner) runner = 1;
        SVC_OPEN(gp, "git_pipeline", git_pipeline_store_create);
        if (gp.ok) {
            git_pipeline_store_lease(&gp.c, gp.obj, NULL, runner);
            SVC_CLOSE(gp);
        }
    }
    char where[160];
    snprintf(where, sizeof(where), "/%s/%s/runs", account, name);
    send_redirect(s, where, keep_alive, NULL);
}

/* ---------- admin pages ---------- */

static void render_admin_users(struct yloop_stream *s, uint32_t uid,
                               const char *uname, int keep_alive)
{
    struct buf b; buf_init(&b);
    render_head(&b, "Users", uid, uname);

    int64_t total = -1;
    SVC_OPEN(acc, "accounts", accounts_store_create);
    if (acc.ok) {
        struct yaafc_size_result r = accounts_store_count(&acc.c, acc.obj, NULL);
        if (YAAFC_IS_OK(r)) total = (int64_t)r.value;
        else yaafc_error_destroy(r.error);
        SVC_CLOSE(acc);
    }

    int64_t patc = -1;
    SVC_OPEN(pat, "personal_access_tokens", personal_access_tokens_store_create);
    if (pat.ok) {
        struct yaafc_size_result r = personal_access_tokens_store_count_active(&pat.c, pat.obj, NULL);
        if (YAAFC_IS_OK(r)) patc = (int64_t)r.value;
        else yaafc_error_destroy(r.error);
        SVC_CLOSE(pat);
    }

    buf_printf(&b,
        "<header class=\"page-header\"><h1>Users</h1></header>"
        "<section class=\"card\"><header class=\"card-header\">"
        "<h2>accounts</h2></header>"
        "<p>Total registered users: <strong>%s</strong></p>"
        "<form method=\"post\" action=\"/admin/users/register\">"
        "<label>uid <input type=\"number\" name=\"uid\" value=\"100\"></label>"
        "<button type=\"submit\" class=\"primary\">register</button>"
        "</form></section>"
        "<section class=\"card\"><header class=\"card-header\">"
        "<h2>personal access tokens</h2></header>"
        "<p>Active PATs: <strong>%s</strong></p>"
        "<form method=\"post\" action=\"/admin/users/mint_pat\">"
        "<label>uid <input type=\"number\" name=\"uid\" value=\"100\"></label>"
        "<button type=\"submit\">mint</button>"
        "</form></section>",
        total < 0 ? "—" : ({static char x[32]; snprintf(x, sizeof(x), "%lld", (long long)total); x;}),
        patc  < 0 ? "—" : ({static char x[32]; snprintf(x, sizeof(x), "%lld", (long long)patc);  x;}));
    render_foot(&b);
    send_html(s, 200, b.data, b.len, keep_alive, NULL);
    buf_free(&b);
}

static void route_admin_users_register(struct yloop_stream *s,
                                       const char *body, size_t body_len, int keep_alive)
{
    char uid_s[32];
    if (form_get(body, body_len, "uid", uid_s, sizeof(uid_s))) {
        uint32_t uid = (uint32_t)strtoul(uid_s, NULL, 10);
        SVC_OPEN(acc, "accounts", accounts_store_create);
        if (acc.ok && uid) {
            accounts_store_register(&acc.c, acc.obj, NULL, uid);
            SVC_CLOSE(acc);
        }
    }
    send_redirect(s, "/admin/users", keep_alive, NULL);
}

static void route_admin_users_mint_pat(struct yloop_stream *s,
                                       const char *body, size_t body_len, int keep_alive)
{
    char uid_s[32];
    if (form_get(body, body_len, "uid", uid_s, sizeof(uid_s))) {
        uint32_t uid = (uint32_t)strtoul(uid_s, NULL, 10);
        SVC_OPEN(pat, "personal_access_tokens", personal_access_tokens_store_create);
        if (pat.ok && uid) {
            personal_access_tokens_store_mint(&pat.c, pat.obj, NULL, uid);
            SVC_CLOSE(pat);
        }
    }
    send_redirect(s, "/admin/users", keep_alive, NULL);
}

/* `/admin/storage` was removed: backend kv state isn't a UI concern,
 * even for the site owner. Operators inspect /tmp/git-yaafc/central.db
 * with sqlite3(1) directly. */

/* ---------- top-level dispatcher ---------- */

static int path_eq(const char *p, const char *target)
{
    /* `/repos?...` should match `/repos`. */
    const char *q = strchr(p, '?');
    size_t n = q ? (size_t)(q - p) : strlen(p);
    return n == strlen(target) && memcmp(p, target, n) == 0;
}

static const char *query_get(const char *path, const char *key,
                             char *out, size_t cap)
{
    const char *q = strchr(path, '?');
    if (!q) { out[0] = 0; return NULL; }
    const char *p = q + 1;
    size_t kl = strlen(key);
    while (*p) {
        const char *eq = strchr(p, '=');
        if (!eq) break;
        if ((size_t)(eq - p) == kl && strncmp(p, key, kl) == 0) {
            const char *vstart = eq + 1;
            const char *end = strchr(vstart, '&');
            size_t vlen = end ? (size_t)(end - vstart) : strlen(vstart);
            if (vlen >= cap) vlen = cap - 1;
            memcpy(out, vstart, vlen);
            out[vlen] = 0;
            return out;
        }
        const char *nxt = strchr(p, '&');
        if (!nxt) break;
        p = nxt + 1;
    }
    out[0] = 0;
    return NULL;
}

/* ---------- yaapp-compatible gateway API (/_rpc, /_describe) ---------- */

static void send_json(struct yloop_stream *s, int status,
                      const char *body, size_t body_len, int keep_alive)
{
    const char *reason = "OK";
    if (status == 400) reason = "Bad Request";
    else if (status == 404) reason = "Not Found";
    else if (status == 410) reason = "Gone";
    else if (status == 500) reason = "Internal Server Error";
    else if (status == 502) reason = "Bad Gateway";

    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, reason, body_len,
        keep_alive ? "keep-alive" : "close");
    if (n <= 0) return;
    yloop_write(s, hdr, (size_t)n);
    if (body_len) yloop_write(s, body, body_len);
}

static void send_json_error(struct yloop_stream *s, int status,
                            const char *message, int keep_alive)
{
    struct yjson_writer *w = yjson_writer_new();
    yjson_w_begin_object(w);
    yjson_w_key(w, "error"); yjson_w_string(w, message);
    yjson_w_end_object(w);
    size_t len;
    const char *data = yjson_w_data(w, &len);
    send_json(s, status, data, len, keep_alive);
    yjson_writer_free(w);
}

/* Pull one HTTP header value (case-insensitive name) out of the raw
 * header block into `out`. Mirrors yhttp.c::header_get. Returns 1 on
 * hit. The block holds the request line + headers up to CRLFCRLF. */
static int header_value(const char *raw, size_t raw_len, const char *name,
                        char *out, size_t out_cap)
{
    size_t nlen = strlen(name);
    const char *p = raw;
    const char *end = raw + raw_len;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol) break;
        size_t llen = (size_t)(eol - p);
        if (llen && p[llen - 1] == '\r') llen--;
        if (llen > nlen + 1 && p[nlen] == ':' &&
            strncasecmp(p, name, nlen) == 0) {
            const char *v = p + nlen + 1;
            while (v < p + llen && (*v == ' ' || *v == '\t')) ++v;
            size_t vl = (size_t)(p + llen - v);
            if (vl >= out_cap) vl = out_cap - 1;
            memcpy(out, v, vl);
            out[vl] = 0;
            return 1;
        }
        p = eol + 1;
    }
    return 0;
}

/* The opaque session token a programmatic client presents: the
 * yaafc-sid cookie, a `yaafc-sid:` header, or `Authorization: Bearer
 * <token>`. All three are opaque-token forms the gateway accepts (see
 * CLAUDE.md — JWTs never cross this boundary). Returns 1 if a
 * non-empty token was found. */
static int extract_session_token(const char *headers_raw, size_t headers_raw_len,
                                 char *out, size_t cap)
{
    if (cookie_get(headers_raw, headers_raw_len, "yaafc-sid", out, cap) && out[0])
        return 1;
    if (header_value(headers_raw, headers_raw_len, "yaafc-sid", out, cap) && out[0])
        return 1;
    char auth[128];
    if (header_value(headers_raw, headers_raw_len, "authorization", auth, sizeof(auth))) {
        const char *p = auth;
        if (strncasecmp(p, "Bearer ", 7) == 0) p += 7;
        while (*p == ' ') ++p;
        size_t n = strlen(p);
        if (n && n < cap) { memcpy(out, p, n); out[n] = 0; return 1; }
    }
    out[0] = 0;
    return 0;
}

/* Resolve an opaque sid to the authenticated uid via the session
 * backend. 0 → anonymous / invalid. */
static uint32_t uid_for_sid(uint32_t sid)
{
    if (!sid) return 0;
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return 0;
    struct ctx c = yaafc_engine_service_ctx(e, "session");
    if (!c.peer) return 0;
    struct object_ptr_result o = session_store_create(&c);
    if (YAAFC_IS_ERR(o)) { yaafc_error_destroy(o.error); return 0; }
    struct yaafc_uint32_result lr = session_store_lookup(&c, o.value, NULL, sid);
    object_release_in_ctx(&c, o.value);
    if (YAAFC_IS_ERR(lr)) { yaafc_error_destroy(lr.error); return 0; }
    return lr.value;
}

/* Split a dotted RPC path into the pieces the dispatcher needs.
 *
 *   "session.store.start"        → service "session"
 *                                   class   "session_store"
 *                                   method  "session_store_start"
 *   "git_pipeline.store.enqueue" → service "git_pipeline"
 *                                   class   "git_pipeline_store"
 *                                   method  "git_pipeline_store_enqueue"
 *
 * service = text up to the first '.'; class = text up to the LAST '.'
 * with '.'→'_'; method = the whole path with '.'→'_' (== the codegen's
 * qualified slot name, the jinvoke key). Requires at least two dots.
 * Returns 1 on success. */
static int path_to_qnames(const char *path,
                          char *service, size_t service_cap,
                          char *class_qname, size_t class_cap,
                          char *method_qname, size_t method_cap)
{
    const char *first = strchr(path, '.');
    if (!first) return 0;
    const char *last = strrchr(path, '.');
    if (last == first) return 0; /* need two dots */

    size_t slen = (size_t)(first - path);
    size_t clen = (size_t)(last - path);
    size_t mlen = strlen(path);
    if (slen == 0 || slen >= service_cap) return 0;
    if (clen >= class_cap) return 0;
    if (mlen >= method_cap) return 0;

    for (size_t i = 0; i < mlen; ++i) {
        char ch = path[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '.'))
            return 0; /* keep junk out of class_by_name / jinvoke_for */
    }

    memcpy(service, path, slen); service[slen] = 0;
    memcpy(class_qname, path, clen); class_qname[clen] = 0;
    memcpy(method_qname, path, mlen); method_qname[mlen] = 0;
    for (char *p = class_qname; *p; ++p) if (*p == '.') *p = '_';
    for (char *p = method_qname; *p; ++p) if (*p == '.') *p = '_';
    return 1;
}

/* Mint a fresh 64-bit correlation id. getrandom() needs no process
 * state (so no file-scope counter); the rare failure path falls back to
 * a clock/pid mix so an id is always produced. */
static uint64_t mint_trace_id(void)
{
    uint64_t id = 0;
    if (getrandom(&id, sizeof(id), 0) == (ssize_t)sizeof(id) && id)
        return id;
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    id = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec ^ ((uint64_t)getpid() << 16);
    return id ? id : 1;
}

/* POST /_rpc — yaapp-style public gateway dispatch. Body:
 *   {"path":"service.class.method","args":[...],"kwargs":{...}}
 * Resolves the opaque session token to a uid, builds a REMOTE ctx for
 * the named service, creates a backend object, and forwards the call
 * through the ctx-aware JSON invoker (which packs args to the binary
 * wire). Positional `args` are honoured today; `kwargs` is accepted
 * but not yet mapped to parameters. */
static void route_json_rpc(struct yloop_stream *s,
                           const char *headers_raw, size_t headers_raw_len,
                           const char *body, size_t body_len, int keep_alive)
{
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) { send_json_error(s, 500, "_rpc: no engine", keep_alive); return; }

    struct yjson_doc *doc = yjson_parse(body, body_len);
    if (!doc) { send_json_error(s, 400, "_rpc: invalid JSON", keep_alive); return; }
    const struct yjson_value *root = yjson_doc_root(doc);
    const char *path = yjson_as_string(yjson_object_get(root, "path"), NULL);
    const struct yjson_value *args = yjson_object_get(root, "args");
    if (!path || !*path) {
        yjson_doc_free(doc);
        send_json_error(s, 400, "_rpc: missing 'path'", keep_alive);
        return;
    }

    char service[64], class_qname[160], method_qname[192];
    if (!path_to_qnames(path, service, sizeof(service),
                        class_qname, sizeof(class_qname),
                        method_qname, sizeof(method_qname))) {
        yjson_doc_free(doc);
        send_json_error(s, 400, "_rpc: malformed path (want service.class.method)", keep_alive);
        return;
    }

    char token[64];
    uint32_t sid = 0, uid = 0;
    if (extract_session_token(headers_raw, headers_raw_len, token, sizeof(token))) {
        sid = (uint32_t)strtoul(token, NULL, 10);
        uid = uid_for_sid(sid);
    }

    struct ctx c = yaafc_engine_service_ctx(e, service);
    if (!c.peer) {
        yjson_doc_free(doc);
        send_json_error(s, 502, "_rpc: unknown service", keep_alive);
        return;
    }

    struct object_ptr_result obj_r = object_create_in_ctx(&c, class_qname);
    if (YAAFC_IS_ERR(obj_r)) {
        yaafc_error_destroy(obj_r.error);
        yjson_doc_free(doc);
        send_json_error(s, 502, "_rpc: backend object create failed", keep_alive);
        return;
    }

    jinvoke_fn fn = jinvoke_for(method_qname);
    if (!fn) {
        object_release_in_ctx(&c, obj_r.value);
        yjson_doc_free(doc);
        char msg[256];
        snprintf(msg, sizeof(msg), "_rpc: no method '%s'", method_qname);
        send_json_error(s, 404, msg, keep_alive);
        return;
    }

    /* Request-header bag handed to the backend: resolved auth identity
     * (uid/sid) + correlation id (honour inbound X-Trace-Id, else mint). */
    uint64_t trace_id = 0;
    struct yheaders *hdrs = yheaders_new();
    if (hdrs) {
        yheaders_set_u32(hdrs, "uid", uid);
        yheaders_set_u32(hdrs, "sid", sid);
        char tbuf[24] = {0};
        if (header_value(headers_raw, headers_raw_len, "x-trace-id", tbuf, sizeof(tbuf)))
            trace_id = strtoull(tbuf, NULL, 10);
        if (!trace_id) trace_id = mint_trace_id();
        yheaders_set_u64(hdrs, "trace_id", trace_id);
        yinfo("[gateway] _rpc trace=%llu path=%s uid=%u",
              (unsigned long long)trace_id, path, uid);
    }

    struct yjson_writer *w = yjson_writer_new();
    yjson_w_begin_object(w);
    yjson_w_key(w, "result");
    char err[256] = {0};
    double span_start = yaafc_ytime_monotonic_sec();
    int rc = fn(&c, obj_r.value, hdrs, args, w, err, sizeof(err));
    double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
    ydebug("span trace=%llu op=gateway.%s dur_us=%.0f", (unsigned long long)trace_id,
           path, span_us);
    {
        char span_op[96];
        snprintf(span_op, sizeof(span_op), "gateway.%s", path);
        yspan_record(span_op, span_us);
    }
    yheaders_free(hdrs);
    object_release_in_ctx(&c, obj_r.value);
    if (rc != 0) {
        yjson_writer_free(w);
        yjson_doc_free(doc);
        send_json_error(s, 500, err[0] ? err : "_rpc: call failed", keep_alive);
        return;
    }
    yjson_w_end_object(w);
    size_t len;
    const char *data = yjson_w_data(w, &len);
    send_json(s, 200, data, len, keep_alive);
    yjson_writer_free(w);
    yjson_doc_free(doc);
}

struct describe_emit_ctx { struct yjson_writer *w; };

static void describe_slot_cb(const char *name, method_slot slot, void *ud)
{
    (void)slot;
    struct describe_emit_ctx *dc = ud;
    yjson_w_string(dc->w, name);
}

/* Pull the `service:` field out of one `remotes[]` map entry and append
 * it to the services array. */
static int describe_service_field_cb(const char *key, const struct yconfig_node *val, void *ud)
{
    struct describe_emit_ctx *dc = ud;
    if (strcmp(key, "service") == 0) {
        const char *name = yconfig_node_as_string(val, NULL);
        if (name) yjson_w_string(dc->w, name);
    }
    return 0;
}

/* GET|POST /_describe[_tree] and /<service.class>/_describe[_tree].
 * With a class path it lists that class's method slots; at the root it
 * lists the gateway's configured backend services. `tree` currently
 * shares the shallow shape (one class level); deeper nesting can layer
 * on once a per-domain class enumerator exists. */
static void route_describe(struct yloop_stream *s, const char *class_dotpath,
                           int tree, int keep_alive)
{
    struct yaafc_engine *e = yaafc_active_engine();
    struct yjson_writer *w = yjson_writer_new();

    if (class_dotpath && *class_dotpath) {
        char class_qname[160];
        size_t n = strlen(class_dotpath);
        if (n >= sizeof(class_qname)) {
            yjson_writer_free(w);
            send_json_error(s, 400, "_describe: path too long", keep_alive);
            return;
        }
        memcpy(class_qname, class_dotpath, n + 1);
        for (char *p = class_qname; *p; ++p) if (*p == '.') *p = '_';

        struct class_ptr_result cr = class_by_name(class_qname);
        if (YAAFC_IS_ERR(cr) || !cr.value) {
            if (YAAFC_IS_ERR(cr)) yaafc_error_destroy(cr.error);
            yjson_writer_free(w);
            send_json_error(s, 404, "_describe: unknown class", keep_alive);
            return;
        }
        yjson_w_begin_object(w);
        yjson_w_key(w, "path");    yjson_w_string(w, class_dotpath);
        yjson_w_key(w, "class");   yjson_w_string(w, class_qname);
        yjson_w_key(w, "methods"); yjson_w_begin_array(w);
        struct describe_emit_ctx dc = {.w = w};
        class_for_each_slot(cr.value, describe_slot_cb, &dc);
        yjson_w_end_array(w);
        yjson_w_end_object(w);
    } else {
        /* Root: enumerate configured backend services. The gateway's
         * `remotes:` list is projected onto the config root (gh#1
         * service projection), so it's reachable as the "remotes" list
         * node — each entry a map carrying a `service:` field. */
        yjson_w_begin_object(w);
        yjson_w_key(w, "services");
        yjson_w_begin_array(w);
        const struct yconfig *cfg = e ? yaafc_engine_config(e) : NULL;
        if (cfg) {
            struct yconfig_node_ptr_result lr = yconfig_get(cfg, "remotes");
            if (YAAFC_IS_OK(lr) && lr.value &&
                yconfig_node_kind(lr.value) == YCONFIG_LIST) {
                struct describe_emit_ctx dc = {.w = w};
                size_t n = yconfig_node_size(lr.value);
                for (size_t i = 0; i < n; ++i) {
                    const struct yconfig_node *entry = yconfig_node_at(lr.value, i);
                    if (!entry || yconfig_node_kind(entry) != YCONFIG_MAP) continue;
                    yconfig_node_for_each(entry, describe_service_field_cb, &dc);
                }
            } else if (YAAFC_IS_ERR(lr)) {
                yaafc_error_destroy(lr.error);
            }
        }
        yjson_w_end_array(w);
        yjson_w_key(w, "tree"); yjson_w_bool(w, tree ? true : false);
        yjson_w_end_object(w);
    }

    size_t len;
    const char *data = yjson_w_data(w, &len);
    send_json(s, 200, data, len, keep_alive);
    yjson_writer_free(w);
}

/* If `path` is `/<dotpath>/_describe` or `/<dotpath>/_describe_tree`,
 * dispatch a describe for `<dotpath>` and return 1. The root forms
 * (/_describe, /_describe_tree) are handled by the caller. */
static int try_hierarchical_describe(struct yloop_stream *s, const char *path,
                                     int keep_alive)
{
    const char *q = strchr(path, '?');
    size_t plen = q ? (size_t)(q - path) : strlen(path);

    static const char tree_suffix[] = "/_describe_tree";
    static const char desc_suffix[] = "/_describe";
    size_t tlen = sizeof(tree_suffix) - 1;
    size_t dlen = sizeof(desc_suffix) - 1;

    int tree = 0;
    size_t suffix_len = 0;
    if (plen > tlen && memcmp(path + plen - tlen, tree_suffix, tlen) == 0) {
        tree = 1; suffix_len = tlen;
    } else if (plen > dlen && memcmp(path + plen - dlen, desc_suffix, dlen) == 0) {
        tree = 0; suffix_len = dlen;
    } else {
        return 0;
    }

    size_t dot_len = plen - suffix_len;
    if (dot_len == 0 || path[0] != '/') return 0;
    char dotpath[160];
    if (dot_len - 1 >= sizeof(dotpath)) return 0;
    memcpy(dotpath, path + 1, dot_len - 1);
    dotpath[dot_len - 1] = 0;
    route_describe(s, dotpath, tree, keep_alive);
    return 1;
}

int yhttp_frontend_try(struct yloop_stream *s,
                       const char *method, const char *path,
                       const char *headers_raw, size_t headers_raw_len,
                       const char *body, size_t body_len,
                       int keep_alive)
{
    /* Only run when a `frontend` engine context is wired. The simplest
     * test: do we have a `session` remote? If not, this is a backend
     * yhttp child — leave it alone and let it serve JSON only. */
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return 0;
    if (!yaafc_engine_service_ctx(e, "session").peer) return 0;

    uint32_t uid = resolve_uid(headers_raw, headers_raw_len);
    /* Cookie username is decided up front so route_root can route
     * straight to /<username> when the user is already signed in. */
    char dispatch_uname[64] = {0};
    resolve_uname(headers_raw, headers_raw_len,
                  dispatch_uname, sizeof(dispatch_uname));
    int is_get = strcmp(method, "GET") == 0;
    int is_post = strcmp(method, "POST") == 0;

    /* ---- yaapp-compatible public gateway API ----------------------
     * Matched before the HTML routes. This is the programmatic surface
     * the browser sidecar, MCP, CLI and tests use. */
    {
        const char *q = strchr(path, '?');
        size_t plen = q ? (size_t)(q - path) : strlen(path);

        /* POST /_rpc — JSON {path,args,kwargs}. The legacy binary shim
         * (POST /_rpc?op=&id=) stays internal transport: defer to
         * yhttp.c by returning 0 when an op= query is present. */
        if (is_post && plen == 5 && memcmp(path, "/_rpc", 5) == 0) {
            if (q && strstr(q, "op=")) return 0;
            route_json_rpc(s, headers_raw, headers_raw_len, body, body_len, keep_alive);
            return 1;
        }

        /* _describe / _describe_tree — root + hierarchical. */
        if (is_get || is_post) {
            if (path_eq(path, "/_describe"))      { route_describe(s, NULL, 0, keep_alive); return 1; }
            if (path_eq(path, "/_describe_tree")) { route_describe(s, NULL, 1, keep_alive); return 1; }
            if (try_hierarchical_describe(s, path, keep_alive)) return 1;
        }

        /* GET /_trace — dump the in-memory span collector (this
         * process's spans aggregated by op: count + p50/p90/p99/max us).
         * `?reset` clears the ring first so you can measure a window. */
        if (is_get && path_eq(path, "/_trace")) {
            if (strstr(path, "reset")) yspan_reset();
            char dump[32768];
            size_t n = yspan_dump(dump, sizeof(dump));
            send_html(s, 200, dump, n, keep_alive, NULL);
            return 1;
        }

        /* Retire the legacy public surface ON THE GATEWAY. The mesh
         * control parent keeps /create //invoke //describe — it reaches
         * none of this code (yhttp_frontend_try returned 0 above for a
         * process with no `session` remote). */
        if (is_post && (path_eq(path, "/create") || path_eq(path, "/invoke"))) {
            send_json_error(s, 410, "removed: use POST /_rpc {\"path\":\"service.class.method\",\"args\":[]}", keep_alive);
            return 1;
        }
        if (is_get && plen == 9 && memcmp(path, "/describe", 9) == 0) {
            send_json_error(s, 410, "removed: use GET /<service.class>/_describe", keep_alive);
            return 1;
        }
    }

    /* Public routes — always handled regardless of auth state. */
    if (is_get && path_eq(path, "/"))                      { route_root(s, uid, dispatch_uname, keep_alive); return 1; }
    if (is_get && path_eq(path, "/login"))                 { route_login_get(s, keep_alive); return 1; }
    if (is_post && path_eq(path, "/login"))                { route_login_post(s, body, body_len, keep_alive); return 1; }
    if (is_get && path_eq(path, "/register"))              { route_register_get(s, keep_alive); return 1; }
    if (is_post && path_eq(path, "/register"))             { route_register_post(s, body, body_len, keep_alive); return 1; }
    if (is_post && path_eq(path, "/logout"))               { route_logout(s, headers_raw, headers_raw_len, keep_alive); return 1; }

    /* Static assets MUST be reachable to anonymous visitors —
     * /style.css is loaded by /login itself. Let these fall through
     * to yhttp.c's static_dir handler by returning 0 here. */
    {
        const char *q = strchr(path, '?');
        size_t plen = q ? (size_t)(q - path) : strlen(path);
        static const char *const STATIC_SUFFIX[] = {
            ".css", ".js", ".png", ".jpg", ".jpeg", ".gif",
            ".svg", ".ico", ".woff", ".woff2", ".map", NULL,
        };
        for (size_t i = 0; STATIC_SUFFIX[i]; ++i) {
            size_t sl = strlen(STATIC_SUFFIX[i]);
            if (plen >= sl && memcmp(path + plen - sl, STATIC_SUFFIX[i], sl) == 0)
                return 0;
        }
    }

    /* Everything below is authenticated. Anonymous visitors get
     * bounced to /login — the service nav is hidden for them and the
     * underlying API isn't reachable through the frontend until they
     * sign in. */
    if (!uid) {
        send_redirect(s, "/login", keep_alive, NULL);
        return 1;
    }

    /* Cookie-driven username (for nav display + ownership of new repos).
     * Already resolved up front for route_root; alias to keep the
     * existing per-handler callsites unchanged. */
    const char *uname = dispatch_uname;

    if (is_get  && path_eq(path, "/repos"))                 { render_repos(s, uid, uname, keep_alive); return 1; }
    if (is_post && path_eq(path, "/repos/new"))             { route_repos_new_post(s, uid, uname, body, body_len, keep_alive); return 1; }

    /* /admin/* requires the site-owner role. Non-admins get sent back
     * to their own namespace page (or /login if the cookie's gone). */
    if (strncmp(path, "/admin/", 7) == 0) {
        if (!is_site_admin(uid)) {
            char to[128];
            send_redirect(s, landing_url(uid, uname, to, sizeof(to)),
                          keep_alive, NULL);
            return 1;
        }
        if (is_get  && path_eq(path, "/admin/users"))           { render_admin_users(s, uid, uname, keep_alive); return 1; }
        if (is_post && path_eq(path, "/admin/users/register"))  { route_admin_users_register(s, body, body_len, keep_alive); return 1; }
        if (is_post && path_eq(path, "/admin/users/mint_pat"))  { route_admin_users_mint_pat(s, body, body_len, keep_alive); return 1; }
        /* All other /admin/* paths 404 — including any leftover
         * /admin/storage requests from old bookmarks. */
        send_html(s, 404, "<p>not found</p>", 16, keep_alive, NULL);
        return 1;
    }

    /* /<account>[/<repo>[/<sub>]] — yaapp's URL shape. The account
     * is a username; the repo is a name owned by that account. */
    char acct[64], rname[64];
    const char *rest = NULL;
    if (parse_account_repo(path, acct, sizeof(acct), rname, sizeof(rname), &rest)) {
        if (is_get  && (rest == NULL || *rest == 0 || *rest == '?'))   { render_repo_show(s, uid, uname, acct, rname, keep_alive); return 1; }
        if (is_get  && rest && strncmp(rest, "/issues", 7) == 0 &&
            (rest[7] == 0 || rest[7] == '?')) {
            char filter[32];
            query_get(path, "status", filter, sizeof(filter));
            render_repo_issues(s, uid, uname, acct, rname, filter[0] ? filter : NULL, keep_alive);
            return 1;
        }
        if (is_post && rest && strcmp(rest, "/issues/new") == 0)       { route_repo_issues_new_post(s, uid, acct, rname, body, body_len, keep_alive); return 1; }
        if (is_post && rest && strcmp(rest, "/issues/close") == 0)     { route_repo_issues_close_post(s, uid, acct, rname, body, body_len, keep_alive); return 1; }
        if (is_get  && rest && strcmp(rest, "/runs") == 0)             { render_repo_runs(s, uid, uname, acct, rname, keep_alive); return 1; }
        if (is_post && rest && strcmp(rest, "/runs/new") == 0)         { route_repo_runs_new_post(s, uid, acct, rname, keep_alive); return 1; }
        if (is_post && rest && strcmp(rest, "/runs/lease") == 0)       { route_repo_runs_lease_post(s, uid, acct, rname, body, body_len, keep_alive); return 1; }
    }

    /* /<account> — single segment that's not in the reserved list:
     * treat as user landing page. */
    if (is_get && path[0] == '/' && path[1] != 0) {
        const char *p = path + 1;
        const char *end = p;
        while (*end && *end != '/' && *end != '?') ++end;
        size_t alen = (size_t)(end - p);
        if (!is_reserved_top(p, alen) && (alen >= 1 && alen < sizeof(acct))) {
            char acct2[64];
            memcpy(acct2, p, alen); acct2[alen] = 0;
            if (username_ok(acct2) &&
                (*end == 0 || *end == '?' || (*end == '/' && end[1] == 0))) {
                render_account_landing(s, uid, uname, acct2, keep_alive);
                return 1;
            }
        }
    }

    return 0;
}
