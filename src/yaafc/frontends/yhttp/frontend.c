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
 * loaded from `mesh.services.frontend.config.remotes[]` at startup. */

#define _POSIX_C_SOURCE 200809L

#include "frontend.h"

#include <yaafc/yengine/engine.h>
#include <yaafc/yloop/yloop.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>

/* Each service header brings in its create() + method stubs. */
#include <yaafc/plugin/storage/storage.h>
#include <yaafc/plugin/accounts/accounts.h>
#include <yaafc/plugin/password_authn/password_authn.h>
#include <yaafc/plugin/token_issuer/token_issuer.h>
#include <yaafc/plugin/session/session.h>
#include <yaafc/plugin/issues/issues.h>
#include <yaafc/plugin/git_repo/git_repo.h>
#include <yaafc/plugin/git_pipeline/git_pipeline.h>
#include <yaafc/plugin/personal_access_tokens/personal_access_tokens.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    struct rpc_session *ses = yaafc_engine_remote(e, "session");
    if (!ses) return 0;
    char cookie[64];
    if (!cookie_get(headers_raw, headers_raw_len, "yaafc-sid", cookie, sizeof(cookie)))
        return 0;
    uint32_t sid = (uint32_t)strtoul(cookie, NULL, 10);
    if (!sid) return 0;

    struct ctx c = {.session = ses};
    struct object_ptr_result o = session_store_create(&c);
    if (YAAFC_IS_ERR(o)) { yaafc_error_destroy(o.error); return 0; }
    struct yaafc_uint32_result lr = session_store_lookup(&c, o.value, sid);
    free(o.value);
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
        VAR.c.session = yaafc_engine_remote(_e, SVC);                              \
        if (!VAR.c.session) break;                                                 \
        struct object_ptr_result _o = CREATE_FN(&VAR.c);                           \
        if (YAAFC_IS_ERR(_o)) { yaafc_error_destroy(_o.error); break; }            \
        VAR.obj = _o.value;                                                        \
        VAR.ok = 1;                                                                \
    } while (0)
/* Release a proxy: send RPC_OP_DESTROY to the server so its handle
 * table doesn't grow without bound (gh#2), then free the local proxy
 * memory. The handle is in the trailing u64 the codegen-emitted
 * `*_create` writes after `struct object`. */
static inline void svc_close_impl(struct svc_ctx *v)
{
    if (!v || !v->obj) return;
    if (v->c.session) {
        uint64_t h;
        memcpy(&h, (char *)v->obj + sizeof(struct object), sizeof(h));
        uint8_t r;
        rpc_call(v->c.session, RPC_OP_DESTROY, 0, &h, sizeof(h), &r, 1);
    }
    free(v->obj);
    v->obj = NULL;
}
#define SVC_CLOSE(VAR) svc_close_impl(&(VAR))

/* ---------- route: GET / ---------- */

static void route_root(struct yloop_stream *s, uint32_t uid, int keep_alive)
{
    if (uid) {
        send_redirect(s, "/repos", keep_alive, NULL);
        return;
    }
    send_redirect(s, "/login", keep_alive, NULL);
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
        password_authn_store_authenticate(&pw_sv.c, pw_sv.obj, uid, pw);
    SVC_CLOSE(pw_sv);
    if (YAAFC_IS_ERR(auth) || auth.value != 1) {
        if (YAAFC_IS_ERR(auth)) yaafc_error_destroy(auth.error);
        *err = "invalid username or password";
        return 0;
    }

    SVC_OPEN(ti, "token_issuer", token_issuer_store_create);
    if (!ti.ok) { *err = "token_issuer unreachable"; return 0; }
    token_issuer_store_login(&ti.c, ti.obj, uid, /*provider*/1);
    SVC_CLOSE(ti);

    SVC_OPEN(ses, "session", session_store_create);
    if (!ses.ok) { *err = "session unreachable"; return 0; }
    struct yaafc_uint32_result sid_r =
        session_store_start(&ses.c, ses.obj, uid, /*provider*/1);
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
    struct yaafc_int_result ex = accounts_store_exists(&acc.c, acc.obj, uid);
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
    send_redirect(s, "/repos", keep_alive, cookie_hdr);
}

/* POST /register — create the account + credential, then start a
 * session and redirect to /repos. Fails if the username is already
 * taken (so the flow forks cleanly from /login). */
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

    SVC_OPEN(acc, "accounts", accounts_store_create);
    if (!acc.ok) { render_register(s, "accounts service unreachable", keep_alive); return; }
    struct yaafc_int_result reg = accounts_store_register(&acc.c, acc.obj, uid);
    SVC_CLOSE(acc);
    int was_new = YAAFC_IS_OK(reg) && reg.value == 1;
    if (YAAFC_IS_ERR(reg)) yaafc_error_destroy(reg.error);
    if (!was_new) {
        render_register(s, "username already taken", keep_alive);
        return;
    }

    SVC_OPEN(pw_sv, "password_authn", password_authn_store_create);
    if (!pw_sv.ok) { render_register(s, "password_authn unreachable", keep_alive); return; }
    password_authn_store_register(&pw_sv.c, pw_sv.obj, uid, pw);
    SVC_CLOSE(pw_sv);

    /* Bootstrap: the very first user becomes site-owner. After
     * `accounts.register` succeeds and the running total is 1, this
     * uid is the only registered account → promote it. */
    SVC_OPEN(acc2, "accounts", accounts_store_create);
    if (acc2.ok) {
        struct yaafc_size_result cr = accounts_store_count(&acc2.c, acc2.obj);
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
    send_redirect(s, "/repos", keep_alive, cookie_hdr);
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
                struct yaafc_int_result r = session_store_destroy(&ses.c, ses.obj, sid);
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
    out->c.session = yaafc_engine_remote(e, "storage");
    if (!out->c.session) return 0;
    struct object_ptr_result o = storage_sql_create(&out->c);
    if (YAAFC_IS_ERR(o)) { yaafc_error_destroy(o.error); return 0; }
    out->obj = o.value;
    out->ok = 1;
    return 1;
}

/* ---------- authz: site-owner / regular-user role check ----------
 *
 * Mirrors yaapp's `'site:owner' in user.groups` check. We store the
 * role as an int64 at `accounts:role:<uid>` in the shared kv table
 * (0 = regular user, 1 = site-owner). The first user to /register
 * gets role=1 automatically (bootstrap); subsequent users get 0.
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
    snprintf(k, sizeof(k), "accounts:role:%u", uid);
    struct yaafc_int64_result r = storage_sql_get(&st.c, st.obj, k);
    int admin = YAAFC_IS_OK(r) && r.value >= 1;
    if (YAAFC_IS_ERR(r)) yaafc_error_destroy(r.error);
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
    snprintf(k, sizeof(k), "accounts:role:%u", uid);
    storage_sql_set(&st.c, st.obj, k, 1);
    SVC_CLOSE(st);
    yinfo("authz: uid=%u bootstrapped as site-owner", uid);
}

/* Mark <account>/<name> as a registered repo and return its repo_id.
 * Storage layout:
 *   repos:by_id:<repo_id> = 1
 *   repos:owner:<account_uid>:count = <N>
 */
static uint32_t repo_register(const char *account, const char *name, uint32_t owner_uid)
{
    uint32_t rid = hash_repo(account, name);
    struct svc_ctx st = {0};
    if (!repo_storage_open(&st)) return 0;
    char k[96];
    snprintf(k, sizeof(k), "repos:by_id:%u", rid);
    storage_sql_set(&st.c, st.obj, k, 1);
    snprintf(k, sizeof(k), "repos:owner:%u:count", owner_uid);
    struct yaafc_int64_result cur = storage_sql_get(&st.c, st.obj, k);
    int64_t count = (YAAFC_IS_OK(cur) ? cur.value : 0) + 1;
    if (YAAFC_IS_ERR(cur)) yaafc_error_destroy(cur.error);
    storage_sql_set(&st.c, st.obj, k, count);
    SVC_CLOSE(st);
    return rid;
}

static int repo_exists(uint32_t repo_id)
{
    struct svc_ctx st = {0};
    if (!repo_storage_open(&st)) return 0;
    char k[64];
    snprintf(k, sizeof(k), "repos:by_id:%u", repo_id);
    struct yaafc_int_result r = storage_sql_exists(&st.c, st.obj, k);
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
    snprintf(k, sizeof(k), "repos:owner:%u:count", owner_uid);
    struct yaafc_int64_result r = storage_sql_get(&st.c, st.obj, k);
    int64_t v = YAAFC_IS_OK(r) ? r.value : 0;
    if (YAAFC_IS_ERR(r)) yaafc_error_destroy(r.error);
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
        struct yaafc_size_result r = git_repo_store_count_total(&repo.c, repo.obj);
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
        buf_printf(&b, "<p class=\"muted small\">You own %lld repo%s — "
                       "browse them at <a href=\"/%s\">/%s</a>.</p>",
                   (long long)mine, mine == 1 ? "" : "s", uname, uname);
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
     * reality — repo_register only writes to storage_sql. */
    SVC_OPEN(repo, "git_repo", git_repo_store_create);
    if (repo.ok) {
        git_repo_store_make(&repo.c, repo.obj, uid);
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
        buf_puts(&b, "<p class=\"muted small\">Repo names aren't indexed yet "
                     "in the wire format — visit "
                     "<code>/&lt;account&gt;/&lt;name&gt;</code> directly.</p>");
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
        struct yaafc_uint32_result r = git_repo_store_owner_of(&rep.c, rep.obj, repo_id);
        if (YAAFC_IS_OK(r)) owner = r.value;
        else yaafc_error_destroy(r.error);
        SVC_CLOSE(rep);
    }

    int64_t open_issues = -1, queued = -1, running = -1, done = -1;
    SVC_OPEN(iss, "issues", issues_store_create);
    if (iss.ok) {
        struct yaafc_size_result o = issues_store_count_open_in_repo(&iss.c, iss.obj, repo_id);
        if (YAAFC_IS_OK(o)) open_issues = (int64_t)o.value; else yaafc_error_destroy(o.error);
        SVC_CLOSE(iss);
    }
    SVC_OPEN(gp, "git_pipeline", git_pipeline_store_create);
    if (gp.ok) {
        struct yaafc_size_result p = git_pipeline_store_count_pending(&gp.c, gp.obj);
        struct yaafc_size_result r = git_pipeline_store_count_running(&gp.c, gp.obj);
        struct yaafc_size_result d = git_pipeline_store_count_done(&gp.c, gp.obj);
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
        struct yaafc_size_result o = issues_store_count_open_in_repo(&iss.c, iss.obj, repo_id);
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
        issues_store_open(&iss.c, iss.obj, repo_id, uid ? uid : 1);
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
                issues_store_close(&iss.c, iss.obj, iid);
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
        struct yaafc_size_result a = git_pipeline_store_count_pending(&gp.c, gp.obj);
        struct yaafc_size_result b2 = git_pipeline_store_count_running(&gp.c, gp.obj);
        struct yaafc_size_result c = git_pipeline_store_count_done(&gp.c, gp.obj);
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
        git_pipeline_store_enqueue(&gp.c, gp.obj, repo_id);
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
            git_pipeline_store_lease(&gp.c, gp.obj, runner);
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
        struct yaafc_size_result r = accounts_store_count(&acc.c, acc.obj);
        if (YAAFC_IS_OK(r)) total = (int64_t)r.value;
        else yaafc_error_destroy(r.error);
        SVC_CLOSE(acc);
    }

    int64_t patc = -1;
    SVC_OPEN(pat, "personal_access_tokens", personal_access_tokens_store_create);
    if (pat.ok) {
        struct yaafc_size_result r = personal_access_tokens_store_count_active(&pat.c, pat.obj);
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
            accounts_store_register(&acc.c, acc.obj, uid);
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
            personal_access_tokens_store_mint(&pat.c, pat.obj, uid);
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
    if (!yaafc_engine_remote(e, "session")) return 0;

    uint32_t uid = resolve_uid(headers_raw, headers_raw_len);
    int is_get = strcmp(method, "GET") == 0;
    int is_post = strcmp(method, "POST") == 0;

    /* Public routes — always handled regardless of auth state. */
    if (is_get && path_eq(path, "/"))                      { route_root(s, uid, keep_alive); return 1; }
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

    /* Cookie-driven username (for nav display + ownership of new repos). */
    char uname[64] = {0};
    resolve_uname(headers_raw, headers_raw_len, uname, sizeof(uname));

    if (is_get  && path_eq(path, "/repos"))                 { render_repos(s, uid, uname, keep_alive); return 1; }
    if (is_post && path_eq(path, "/repos/new"))             { route_repos_new_post(s, uid, uname, body, body_len, keep_alive); return 1; }

    /* /admin/* requires the site-owner role. Anyone else hitting
     * these paths gets bounced to /repos (the kv-internals page
     * `/admin/storage` is removed entirely — backend state isn't a
     * UI surface). */
    if (strncmp(path, "/admin/", 7) == 0) {
        if (!is_site_admin(uid)) {
            send_redirect(s, "/repos", keep_alive, NULL);
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
