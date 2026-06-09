/* HTTP listener + router for the standalone picoforge-webapp binary.
 *
 * Per-peer serve coroutine reads one HTTP request via picohttpparser,
 * routes by method+path, writes the response. Modeled on yhttp.c but
 * carries no picomesh-engine state — all backend calls go out to the
 * gateway via the http client (see http_client.c).
 *
 * The web app is the picoforge page tier: it renders every browser page
 * (login, repos, account, repo browser + Monaco editor, issues, runs,
 * admin) and sources all data from the gateway over POST /_rpc, with the
 * active page set discovered from the gateway's /_describe. */

#include "webapp.h"
#include "http_client.h"

/* stdarg.h before the picomesh headers so va_list is fully defined for
 * buf_printf below (some picomesh headers pull a partial __need___va_list
 * that otherwise leaves va_list incomplete). */
#include <stdarg.h>

#include <picomesh/core/ytrace.h>
#include <picomesh/core/idkey.h>
#include <picomesh/json/json.h>
#include <picomesh/loop/loop.h>

#include <picohttpparser.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define WEBAPP_REQ_BUF      (256 * 1024)
#define WEBAPP_MAX_HEADERS  64

/* Active backend services discovered from the gateway's /_describe.
 * Cached and refreshed on a TTL (see WEBAPP_SERVICES_TTL_SEC): the mesh can
 * activate or reconcile services at runtime, so a process-lifetime cache
 * would go stale. Pages gate on this set: a service the mesh isn't running
 * yields no page. */
struct service_set {
    char   names[32][64];
    char   sources[32][32];
    size_t n;
    int    loaded;
    time_t loaded_at;
};

#define WEBAPP_SERVICES_TTL_SEC 30

/* OK carries the parsed RPC response doc (NULL when the response had no body /
 * no result); ERR carries the transport/parse failure's cause chain. */
PICOMESH_RESULT_DECLARE(json_doc_ptr, struct json_doc *);

struct serve_ud {
    struct loop *loop;
    const struct webapp_config *cfg;
    struct gateway_url gw;
    struct service_set services;
};

/* The caller's authenticated identity, resolved by the gateway from the
 * opaque session token (see resolve_claims). This — NOT the forgeable
 * picomesh-uname cookie — is the authority for admin gating and ownership.
 * uid 0 ⇒ anonymous. */
struct claims {
    uint32_t uid;
    char     username[64];
    int      is_admin;
};

static int header_match(const struct phr_header *hdrs, size_t n,
                        const char *want, char *out, size_t out_cap)
{
    size_t wl = strlen(want);
    for (size_t i = 0; i < n; ++i) {
        if (hdrs[i].name_len != wl) continue;
        int ok = 1;
        for (size_t j = 0; j < wl; ++j) {
            char a = hdrs[i].name[j];
            char b = want[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) { ok = 0; break; }
        }
        if (!ok) continue;
        size_t copy = hdrs[i].value_len < out_cap - 1
                          ? hdrs[i].value_len : out_cap - 1;
        memcpy(out, hdrs[i].value, copy);
        out[copy] = 0;
        return 1;
    }
    return 0;
}

static const char *http_reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 303: return "See Other";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    default:  return "OK";
    }
}

static void send_response(struct loop_stream *s, int status,
                          const char *content_type,
                          const char *body, size_t body_len,
                          const char *extra_headers, int keep_alive)
{
    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "%s"
        "\r\n",
        status, http_reason(status),
        content_type,
        body_len,
        keep_alive ? "keep-alive" : "close",
        extra_headers ? extra_headers : "");
    if (n <= 0) return;
    loop_write(s, header, (size_t)n);
    if (body_len && body) loop_write(s, body, body_len);
}

static void send_redirect(struct loop_stream *s, const char *location,
                          const char *extra_headers, int keep_alive)
{
    char hdrs[8192];
    int n = snprintf(hdrs, sizeof(hdrs), "Location: %s\r\n%s",
                     location, extra_headers ? extra_headers : "");
    if (n <= 0 || (size_t)n >= sizeof(hdrs)) {
        send_response(s, 500, "text/plain", "redirect too large", strlen("redirect too large"), NULL, keep_alive);
        return;
    }
    send_response(s, 303, "text/plain", "", 0, hdrs, keep_alive);
}

static void send_text(struct loop_stream *s, int status,
                      const char *body, int keep_alive)
{
    send_response(s, status, "text/plain; charset=utf-8",
                  body, strlen(body), NULL, keep_alive);
}

/* ---- growable byte buffer (HTML page assembly) --------------------- *
 * The file browser + Monaco editor pages embed arbitrary-size file
 * content, so they can't use fixed snprintf buffers. Grow-on-append; on
 * OOM the buffer goes inert (len frozen) and the caller still sends what
 * was assembled — never a crash. */
struct buf { char *data; size_t len, cap; };

static void buf_init(struct buf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void buf_free(struct buf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static int buf_reserve(struct buf *b, size_t extra)
{
    if (b->data && b->len + extra + 1 <= b->cap) return 1;
    size_t nc = b->cap ? b->cap : 1024;
    while (b->len + extra + 1 > nc) nc *= 2;
    char *nd = realloc(b->data, nc);
    if (!nd) return 0;
    b->data = nd; b->cap = nc;
    return 1;
}

static void buf_putn(struct buf *b, const char *s, size_t n)
{
    if (!buf_reserve(b, n)) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void buf_puts(struct buf *b, const char *s) { buf_putn(b, s, strlen(s)); }

static void buf_printf(struct buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char tmp[2048];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof(tmp)) { buf_putn(b, tmp, (size_t)n); return; }
    char *big = malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_putn(b, big, (size_t)n);
    free(big);
}

/* Append `s` HTML-escaped (the OWASP 5: & < > " '). Safe in element
 * bodies and double-quoted attributes. */
static void buf_esc(struct buf *b, const char *s)
{
    for (const char *p = s ? s : ""; *p; ++p) {
        switch (*p) {
        case '&':  buf_puts(b, "&amp;");  break;
        case '<':  buf_puts(b, "&lt;");   break;
        case '>':  buf_puts(b, "&gt;");   break;
        case '"':  buf_puts(b, "&quot;"); break;
        case '\'': buf_puts(b, "&#39;");  break;
        default:   buf_putn(b, p, 1);     break;
        }
    }
}

/* ---- static files --------------------------------------------------- */

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html") || !strcmp(dot, ".htm")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css"))  return "text/css; charset=utf-8";
    if (!strcmp(dot, ".js"))   return "application/javascript; charset=utf-8";
    if (!strcmp(dot, ".json")) return "application/json; charset=utf-8";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcmp(dot, ".png"))  return "image/png";
    if (!strcmp(dot, ".ico"))  return "image/x-icon";
    if (!strcmp(dot, ".txt"))  return "text/plain; charset=utf-8";
    /* Monaco editor assets (vendored under /static/vendor/monaco/vs). */
    if (!strcmp(dot, ".ttf"))   return "font/ttf";
    if (!strcmp(dot, ".woff"))  return "font/woff";
    if (!strcmp(dot, ".woff2")) return "font/woff2";
    if (!strcmp(dot, ".map"))   return "application/json; charset=utf-8";
    return "application/octet-stream";
}

static int serve_static(struct loop_stream *s, const char *root,
                        const char *url_path, int keep_alive)
{
    if (!root || !*root) return 0;
    /* Strip a leading "/static/" prefix if present — that's the URL
     * convention for served assets (mirrors yaapp's page server). */
    const char *rel = url_path;
    if (strncmp(rel, "/static/", 8) == 0) rel += 8;
    else if (*rel == '/') rel += 1;
    if (!*rel || strstr(rel, "..")) return 0;

    /* Never serve HTML from the static tree. The webapp renders every page;
     * static holds only assets (css/js/fonts/…). This guarantees no
     * prototype/standalone HTML can ever be shipped as product UI. */
    size_t rlen = strlen(rel);
    if (rlen >= 5 && strcasecmp(rel + rlen - 5, ".html") == 0) return 0;
    if (rlen >= 4 && strcasecmp(rel + rlen - 4, ".htm") == 0) return 0;

    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s", root, rel);
    if (n <= 0 || (size_t)n >= sizeof(path)) return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return 0;
    }
    size_t sz = (size_t)st.st_size;
    char *body = malloc(sz);
    if (!body) { close(fd); return 0; }
    size_t got = 0;
    while (got < sz) {
        ssize_t r = read(fd, body + got, sz - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    send_response(s, 200, mime_for(path), body, got, NULL, keep_alive);
    free(body);
    return 1;
}

/* ---- routes (proof-of-life subset) --------------------------------- */

/* Hardcoded login page. The shape matches yaapp's login.html minimally so
 * the smoke can find the <h1>Sign in</h1> and the username/password fields.
 * All assets are served from the webapp's own /static — nothing from a CDN. */
static const char LOGIN_HTML[] =
    "<!doctype html><html><head>"
    "<meta charset=\"utf-8\"><title>Sign in — picoforge</title>"
    "<link rel=\"stylesheet\" href=\"/static/style.css\">"
    "</head><body>"
    "<main class=\"login\">"
    "<h1>Sign in</h1>"
    "<form method=\"post\" action=\"/-/login\">"
    "<label>Username <input name=\"username\" autofocus></label>"
    "<label>Password <input type=\"password\" name=\"password\"></label>"
    "<button type=\"submit\">Sign in</button>"
    "</form>"
    "<p><a href=\"/-/register\">Create an account</a></p>"
    "</main>"
    "</body></html>";

static void route_login_get(struct loop_stream *s, const struct serve_ud *sud, int keep_alive)
{
    if (!sud || !sud->cfg || !sud->cfg->github_client_id || !*sud->cfg->github_client_id) {
        send_response(s, 200, "text/html; charset=utf-8", LOGIN_HTML, sizeof(LOGIN_HTML) - 1, NULL, keep_alive);
        return;
    }
    struct buf b; buf_init(&b);
    buf_puts(&b,
        "<!doctype html><html><head>"
        "<meta charset=\"utf-8\"><title>Sign in — picoforge</title>"
        "<link rel=\"stylesheet\" href=\"/static/style.css\">"
        "</head><body><main class=\"login\"><h1>Sign in</h1>"
        "<form method=\"post\" action=\"/-/login\">"
        "<label>Username <input name=\"username\" autofocus></label>"
        "<label>Password <input type=\"password\" name=\"password\"></label>"
        "<button type=\"submit\">Sign in</button>"
        "</form>"
        "<div class=\"oauth-divider\">or</div>"
        "<p><a class=\"oauth-btn btn\" href=\"/-/auth/github\">Sign in with GitHub</a></p>"
        "<p><a href=\"/-/register\">Create an account</a></p>"
        "</main></body></html>");
    send_response(s, 200, "text/html; charset=utf-8", b.data, b.len, NULL, keep_alive);
    buf_free(&b);
}

/* HTML-escape `src` into `dst[cap]`. Writes a NUL-terminated string.
 * Returns the number of bytes written (excluding NUL). On overflow,
 * silently truncates at a safe boundary — the caller picks a generous
 * cap. Escapes the OWASP "5 chars in text/attribute context" set so the
 * result is safe in both element bodies and double-quoted attributes. */
static size_t html_escape(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return 0;
    size_t n = 0;
    for (const char *p = src ? src : ""; *p; ++p) {
        const char *rep = NULL;
        size_t rl = 0;
        switch (*p) {
        case '<':  rep = "&lt;";   rl = 4; break;
        case '>':  rep = "&gt;";   rl = 4; break;
        case '&':  rep = "&amp;";  rl = 5; break;
        case '"':  rep = "&quot;"; rl = 6; break;
        case '\'': rep = "&#39;";  rl = 5; break;
        }
        if (rep) {
            if (n + rl + 1 > cap) break;
            memcpy(dst + n, rep, rl);
            n += rl;
        } else {
            if (n + 1 + 1 > cap) break;
            dst[n++] = *p;
        }
    }
    dst[n] = 0;
    return n;
}

/* Render the login page with an error message above the form. The
 * static template above doesn't have a slot, so we splice the error
 * banner in before <form>. The `err` string is HTML-escaped before
 * interpolation — gateway errors routinely echo user-supplied content
 * (e.g. "no such user 'alice'") and reflecting that unescaped would be
 * stored/reflected XSS. */
static void route_login_get_with_error(struct loop_stream *s,
                                       const char *err, int keep_alive)
{
    char escaped[1024];
    html_escape(escaped, sizeof(escaped),
                err && *err ? err : "Sign-in failed");
    char body[4096];
    int n = snprintf(body, sizeof(body),
        "<!doctype html><html><head>"
        "<meta charset=\"utf-8\"><title>Sign in — picoforge</title>"
        "<link rel=\"stylesheet\" href=\"/static/style.css\">"
        "</head><body><main class=\"login\">"
        "<h1>Sign in</h1>"
        "<p class=\"error\">%s</p>"
        "<form method=\"post\" action=\"/-/login\">"
        "<label>Username <input name=\"username\" autofocus></label>"
        "<label>Password <input type=\"password\" name=\"password\"></label>"
        "<button type=\"submit\">Sign in</button>"
        "</form>"
        "</main></body></html>",
        escaped);
    if (n <= 0) return;
    send_response(s, 200, "text/html; charset=utf-8",
                  body, (size_t)n, NULL, keep_alive);
}

/* URL-decode in place. `s` is mutated; returns the new length. */
static size_t url_decode(char *s)
{
    char *src = s, *dst = s;
    while (*src) {
        if (*src == '+') { *dst++ = ' '; src++; continue; }
        if (*src == '%' && src[1] && src[2]) {
            char hi = src[1], lo = src[2];
            int h = (hi >= '0' && hi <= '9') ? hi - '0'
                  : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                  : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : -1;
            int l = (lo >= '0' && lo <= '9') ? lo - '0'
                  : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                  : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : -1;
            if (h >= 0 && l >= 0) {
                *dst++ = (char)((h << 4) | l);
                src += 3;
                continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = 0;
    return (size_t)(dst - s);
}

/* application/x-www-form-urlencoded → field extractor. Returns a
 * newly-allocated NUL-terminated decoded string, or NULL if not found. */
static char *form_get(const char *body, size_t blen, const char *key)
{
    size_t klen = strlen(key);
    const char *p = body, *end = body + blen;
    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        const char *seg_end = amp ? amp : end;
        const char *eq = memchr(p, '=', (size_t)(seg_end - p));
        if (eq) {
            size_t name_len = (size_t)(eq - p);
            if (name_len == klen && memcmp(p, key, klen) == 0) {
                size_t vlen = (size_t)(seg_end - eq - 1);
                char *out = malloc(vlen + 1);
                if (!out) return NULL;
                memcpy(out, eq + 1, vlen);
                out[vlen] = 0;
                url_decode(out);
                return out;
            }
        }
        if (!amp) break;
        p = amp + 1;
    }
    return NULL;
}

/* Read `n` bytes off the stream into `dst` (allocated by caller). */
static int read_body(struct loop_stream *s, char *dst, size_t n)
{
    size_t got = 0;
    while (got < n) {
        struct picomesh_size_result chunk_res = loop_read_some(s, dst + got, n - got);
        if (PICOMESH_IS_ERR(chunk_res)) { picomesh_error_destroy(chunk_res.error); return -1; }
        if (chunk_res.value == 0) return -1; /* EOF before the full body arrived */
        got += chunk_res.value;
    }
    return 0;
}

static long header_content_length(const struct phr_header *hdrs, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (hdrs[i].name_len != 14) continue;
        int eq = 1;
        for (size_t j = 0; j < 14; ++j) {
            char a = hdrs[i].name[j], b = "Content-Length"[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) { eq = 0; break; }
        }
        if (!eq) continue;
        char tmp[32];
        size_t copy_len = hdrs[i].value_len < sizeof(tmp) - 1
                        ? hdrs[i].value_len : sizeof(tmp) - 1;
        memcpy(tmp, hdrs[i].value, copy_len);
        tmp[copy_len] = 0;
        return atol(tmp);
    }
    return -1;
}

/* Cookie header parsing: pluck the value of `name=` out of a "Cookie:"
 * header value (possibly multi-cookie semicolon-separated). Returns
 * the value as a newly-allocated string, or NULL. */
static char *cookie_get(const struct phr_header *hdrs, size_t n,
                        const char *name)
{
    size_t name_len = strlen(name);
    for (size_t i = 0; i < n; ++i) {
        if (hdrs[i].name_len != 6) continue;
        int eq = 1;
        for (size_t j = 0; j < 6; ++j) {
            char a = hdrs[i].name[j], b = "Cookie"[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) { eq = 0; break; }
        }
        if (!eq) continue;
        const char *p = hdrs[i].value;
        const char *end = p + hdrs[i].value_len;
        while (p < end) {
            while (p < end && (*p == ' ' || *p == ';')) p++;
            const char *eqsign = memchr(p, '=', (size_t)(end - p));
            const char *semi = memchr(p, ';', (size_t)(end - p));
            const char *kend = (eqsign && (!semi || eqsign < semi)) ? eqsign : semi;
            if (!kend) break;
            size_t kl = (size_t)(kend - p);
            if (kl == name_len && memcmp(p, name, name_len) == 0 && kend == eqsign) {
                const char *v = eqsign + 1;
                const char *vend = semi ? semi : end;
                size_t vl = (size_t)(vend - v);
                char *out = malloc(vl + 1);
                if (!out) return NULL;
                memcpy(out, v, vl);
                out[vl] = 0;
                return out;
            }
            if (!semi) break;
            p = semi + 1;
        }
    }
    return NULL;
}

/* POST /login — forward the credentials to the gateway's composite
 * login endpoint and relay its session cookie.
 *
 * The gateway owns the auth flow end to end (accounts.exists →
 * password_authn.authenticate → token_issuer.login → session.start),
 * keeping every secret server-side; the sidecar is a pure relay. On
 * success the gateway answers 303 + `Set-Cookie: picomesh-sid=…`, which we
 * pass straight back to the browser, then bounce to /repos. On bad
 * credentials the gateway re-renders its form (200), which we surface
 * as an inline error here. The opaque sid cookie is the only token that
 * ever reaches the browser — no JWT crosses this boundary. */
/* Copy `src` into `out` keeping only cookie-safe username chars
 * (lowercase alnum + ._-), lowercased, max 32 — matches the gateway's
 * username_ok normalization so our `picomesh-uname` cookie equals what the
 * gateway would set. (We set it ourselves because the gateway emits two
 * Set-Cookie headers and our HTTP client only captures the first, sid.) */
static void uname_cookie_value(const char *src, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = src ? src : ""; *p && o + 1 < cap && o < 32; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.')
            out[o++] = c;
    }
    out[o] = 0;
}

/* Build the post-auth Set-Cookie block: the relayed opaque sid cookie plus
 * our own picomesh-uname cookie (so the nav + per-user pages know who is
 * signed in). `out` must hold ~1 KiB. */
static void build_auth_cookies(char *out, size_t cap,
                               const char *relayed_sid, const char *username)
{
    char uname_v[40];
    uname_cookie_value(username, uname_v, sizeof(uname_v));
    snprintf(out, cap,
        "Set-Cookie: %s\r\n"
        "Set-Cookie: picomesh-uname=%s; Path=/; SameSite=Lax\r\n",
        relayed_sid, uname_v);
}

static struct picomesh_void_result route_login_post(struct loop *loop, struct loop_stream *s,
                             const struct serve_ud *sud,
                             const char *body, size_t body_len, int keep_alive)
{
    char *username = form_get(body, body_len, "username");
    char *password = form_get(body, body_len, "password");
    int have_both = username && *username && password && *password;
    free(password);
    if (!have_both) {
        free(username);
        route_login_get_with_error(s, "missing username or password", keep_alive);
        return PICOMESH_OK_VOID();
    }

    /* Forward the browser's form payload verbatim to the gateway. */
    struct http_response resp;
    struct picomesh_int_result post = http_post(loop, &sud->gw, "/login",
                       "application/x-www-form-urlencoded",
                       NULL, NULL, body, body_len, &resp);
    if (PICOMESH_IS_ERR(post)) {
        picomesh_error_print(stderr, "route_login_post: /login", post.error);
        picomesh_error_destroy(post.error);
        http_response_free(&resp);
        free(username);
        route_login_get_with_error(s, "gateway unreachable", keep_alive);
        return PICOMESH_OK_VOID();
    }

    if (resp.status == 303 && resp.set_cookie[0]) {
        char hdrs[1024];
        build_auth_cookies(hdrs, sizeof(hdrs), resp.set_cookie, username);
        http_response_free(&resp);
        free(username);
        send_redirect(s, "/-/repos", hdrs, keep_alive);
        return PICOMESH_OK_VOID();
    }

    http_response_free(&resp);
    free(username);
    route_login_get_with_error(s, "invalid username or password", keep_alive);
    return PICOMESH_OK_VOID();
}


static char *query_get(const char *path, const char *key);
static struct picomesh_void_result resolve_claims(struct loop *loop, const struct serve_ud *sud,
                           const char *sid, struct claims *out);

static void public_base_url(const struct serve_ud *sud, const struct phr_header *hdrs, size_t n,
                            char *out, size_t cap)
{
    if (sud && sud->cfg && sud->cfg->public_url && *sud->cfg->public_url) {
        snprintf(out, cap, "%s", sud->cfg->public_url);
        return;
    }
    char host[256] = {0};
    header_match(hdrs, n, "host", host, sizeof(host));
    snprintf(out, cap, "http://%s", host[0] ? host : "127.0.0.1:8080");
}

static void urlenc(char *out, size_t cap, const char *in)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && o + 4 < cap; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_' || *p == '.' || *p == '~') out[o++] = (char)*p;
        else { out[o++] = '%'; out[o++] = hex[*p >> 4]; out[o++] = hex[*p & 15]; }
    }
    out[o] = 0;
}

static void github_redirect_uri(const struct serve_ud *sud, const struct phr_header *hdrs, size_t n,
                                char *out, size_t cap)
{
    char base[512];
    public_base_url(sud, hdrs, n, base, sizeof(base));
    size_t bl = strlen(base);
    while (bl > 0 && base[bl - 1] == '/') base[--bl] = 0;
    snprintf(out, cap, "%s/-/auth/github/callback", base);
}

static int oauth_state(char *out, size_t cap)
{
    unsigned char rnd[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, rnd, sizeof(rnd));
    close(fd);
    if (n != (ssize_t)sizeof(rnd) || cap < sizeof(rnd) * 2 + 1) return 0;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(rnd); ++i) {
        out[i * 2] = hex[rnd[i] >> 4];
        out[i * 2 + 1] = hex[rnd[i] & 15];
    }
    out[sizeof(rnd) * 2] = 0;
    return 1;
}

static void route_github_start(struct loop_stream *s, const struct serve_ud *sud,
                               const struct phr_header *hdrs, size_t n, int keep_alive)
{
    const char *client = sud && sud->cfg ? sud->cfg->github_client_id : NULL;
    const char *ghurl = (sud && sud->cfg && sud->cfg->github_url && *sud->cfg->github_url)
        ? sud->cfg->github_url : "https://github.com";
    if (!client || !*client) { route_login_get_with_error(s, "GitHub login is not configured", keep_alive); return; }
    char state[33];
    if (!oauth_state(state, sizeof(state))) { route_login_get_with_error(s, "GitHub login is temporarily unavailable", keep_alive); return; }
    char redirect[1024], enc_redirect[1400], enc_client[512], enc_state[80];
    github_redirect_uri(sud, hdrs, n, redirect, sizeof(redirect));
    urlenc(enc_redirect, sizeof(enc_redirect), redirect);
    urlenc(enc_client, sizeof(enc_client), client);
    urlenc(enc_state, sizeof(enc_state), state);
    char location[4096];
    int ln = snprintf(location, sizeof(location), "%s/login/oauth/authorize?client_id=%s&redirect_uri=%s&scope=repo,user:email&state=%s",
                      ghurl, enc_client, enc_redirect, enc_state);
    if (ln <= 0 || (size_t)ln >= sizeof(location)) { route_login_get_with_error(s, "GitHub redirect URL is too large", keep_alive); return; }
    char cookie[128];
    snprintf(cookie, sizeof(cookie), "Set-Cookie: picoforge-oauth-state=%s; Path=/-/auth/github; HttpOnly; SameSite=Lax; Max-Age=600\r\n", state);
    send_redirect(s, location, cookie, keep_alive);
}

static int sid_from_set_cookie(const char *set_cookie, char *out, size_t cap)
{
    const char *p = set_cookie ? strstr(set_cookie, "picomesh-sid=") : NULL;
    if (!p) return 0;
    p += strlen("picomesh-sid=");
    const char *e = strchr(p, ';');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (!n || n >= cap) return 0;
    memcpy(out, p, n); out[n] = 0;
    return 1;
}

static struct picomesh_void_result route_github_callback(struct loop *loop, struct loop_stream *s,
                                  const struct serve_ud *sud,
                                  const struct phr_header *hdrs, size_t hn,
                                  const char *full_path, int keep_alive)
{
    char *code = query_get(full_path, "code");
    char *state = query_get(full_path, "state");
    char *state_cookie = cookie_get(hdrs, hn, "picoforge-oauth-state");
    if (!state || !state_cookie || strcmp(state, state_cookie) != 0) {
        free(code); free(state); free(state_cookie);
        route_login_get_with_error(s, "GitHub OAuth state did not match", keep_alive);
        return PICOMESH_OK_VOID();
    }
    char enc_state[160];
    urlenc(enc_state, sizeof(enc_state), state);
    free(state); free(state_cookie);
    if (!code || !*code) { free(code); route_login_get_with_error(s, "GitHub did not return an OAuth code", keep_alive); return PICOMESH_OK_VOID(); }
    char redirect[1024], enc_code[1024], enc_redirect[1400];
    github_redirect_uri(sud, hdrs, hn, redirect, sizeof(redirect));
    urlenc(enc_code, sizeof(enc_code), code);
    urlenc(enc_redirect, sizeof(enc_redirect), redirect);
    free(code);
    char body[2600];
    int bn = snprintf(body, sizeof(body), "code=%s&redirect_uri=%s&state=%s",
                      enc_code, enc_redirect, enc_state);
    if (bn <= 0 || (size_t)bn >= sizeof(body)) { route_login_get_with_error(s, "GitHub callback is too large", keep_alive); return PICOMESH_OK_VOID(); }

    /* The gateway's /auth/github/callback ONLY accepts this custom Content-Type.
     * A browser cross-site form POST is limited to the three "simple" content
     * types, and a cross-origin fetch declaring this type triggers a CORS
     * preflight the API-only gateway never answers — so only this server-side
     * relay (which has already validated the state cookie above) can reach the
     * gateway handler. That closes the login-CSRF surface of a callback POST
     * sent straight to the gateway to bypass this state check. Keep the literal
     * in sync with the gateway (frontend.c route_github_callback_post). */
    struct http_response resp;
    struct picomesh_int_result post = http_post(loop, &sud->gw, "/auth/github/callback",
                       "application/x-picoforge-oauth-relay", NULL, NULL, body, (size_t)bn, &resp);
    if (PICOMESH_IS_ERR(post)) {
        picomesh_error_print(stderr, "route_github_callback: /auth/github/callback", post.error);
        picomesh_error_destroy(post.error);
        http_response_free(&resp);
        route_login_get_with_error(s, "gateway unreachable", keep_alive);
        return PICOMESH_OK_VOID();
    }
    if (resp.status == 303 && resp.set_cookie[0]) {
        char sid[80] = {0};
        sid_from_set_cookie(resp.set_cookie, sid, sizeof(sid));
        struct claims claims;
        resolve_claims(loop, sud, sid, &claims);
        char hdrs_out[1024];
        build_auth_cookies(hdrs_out, sizeof(hdrs_out), resp.set_cookie, claims.username);
        strncat(hdrs_out, "Set-Cookie: picoforge-oauth-state=; Path=/-/auth/github; HttpOnly; SameSite=Lax; Max-Age=0\r\n", sizeof(hdrs_out) - strlen(hdrs_out) - 1);
        http_response_free(&resp);
        send_redirect(s, "/-/repos", hdrs_out, keep_alive);
        return PICOMESH_OK_VOID();
    }
    http_response_free(&resp);
    route_login_get_with_error(s, "GitHub sign-in failed", keep_alive);
    return PICOMESH_OK_VOID();
}

/* ---- /register: same relay shape as /login ------------------------- *
 * The gateway owns account creation end to end (it writes the credential,
 * registers the account, bootstraps the first user as site-owner, then
 * mints a session). The webapp renders the form and relays the POST,
 * passing back the gateway's opaque sid cookie. No secret is held here. */
static const char REGISTER_HTML[] =
    "<!doctype html><html><head>"
    "<meta charset=\"utf-8\"><title>Create account — picoforge</title>"
    "<link rel=\"stylesheet\" href=\"/static/style.css\">"
    "</head><body>"
    "<main class=\"login\">"
    "<h1>Create account</h1>"
    "<form method=\"post\" action=\"/-/register\">"
    "<label>Username <input name=\"username\" autofocus></label>"
    "<label>Password <input type=\"password\" name=\"password\"></label>"
    "<button type=\"submit\">Create account</button>"
    "</form>"
    "<p>Already have an account? <a href=\"/-/login\">Sign in</a>.</p>"
    "</main>"
    "</body></html>";

static void route_register_get(struct loop_stream *s, int keep_alive)
{
    send_response(s, 200, "text/html; charset=utf-8",
                  REGISTER_HTML, sizeof(REGISTER_HTML) - 1, NULL, keep_alive);
}

/* Register page with an error banner spliced in before the form. `err`
 * is HTML-escaped (gateway errors echo the submitted username). */
static void route_register_get_with_error(struct loop_stream *s,
                                          const char *err, int keep_alive)
{
    char escaped[1024];
    html_escape(escaped, sizeof(escaped), err && *err ? err : "Sign-up failed");
    char body[4096];
    int n = snprintf(body, sizeof(body),
        "<!doctype html><html><head>"
        "<meta charset=\"utf-8\"><title>Create account — picoforge</title>"
        "<link rel=\"stylesheet\" href=\"/static/style.css\">"
        "</head><body><main class=\"login\">"
        "<h1>Create account</h1>"
        "<p class=\"error\">%s</p>"
        "<form method=\"post\" action=\"/-/register\">"
        "<label>Username <input name=\"username\" autofocus></label>"
        "<label>Password <input type=\"password\" name=\"password\"></label>"
        "<button type=\"submit\">Create account</button>"
        "</form>"
        "<p>Already have an account? <a href=\"/-/login\">Sign in</a>.</p>"
        "</main></body></html>",
        escaped);
    if (n <= 0) return;
    send_response(s, 200, "text/html; charset=utf-8", body, (size_t)n, NULL, keep_alive);
}

static struct picomesh_void_result route_register_post(struct loop *loop, struct loop_stream *s,
                                const struct serve_ud *sud,
                                const char *body, size_t body_len, int keep_alive)
{
    char *username = form_get(body, body_len, "username");
    char *password = form_get(body, body_len, "password");
    int have_both = username && *username && password && *password;
    free(password);
    if (!have_both) {
        free(username);
        route_register_get_with_error(s, "username and password are required", keep_alive);
        return PICOMESH_OK_VOID();
    }

    struct http_response resp;
    struct picomesh_int_result post = http_post(loop, &sud->gw, "/register",
                       "application/x-www-form-urlencoded",
                       NULL, NULL, body, body_len, &resp);
    if (PICOMESH_IS_ERR(post)) {
        picomesh_error_print(stderr, "route_register_post: /register", post.error);
        picomesh_error_destroy(post.error);
        http_response_free(&resp);
        free(username);
        route_register_get_with_error(s, "gateway unreachable", keep_alive);
        return PICOMESH_OK_VOID();
    }

    if (resp.status == 303 && resp.set_cookie[0]) {
        char hdrs[1024];
        build_auth_cookies(hdrs, sizeof(hdrs), resp.set_cookie, username);
        http_response_free(&resp);
        free(username);
        send_redirect(s, "/-/repos", hdrs, keep_alive);
        return PICOMESH_OK_VOID();
    }

    http_response_free(&resp);
    free(username);
    route_register_get_with_error(s,
        "could not create account (the username may already be taken)", keep_alive);
    return PICOMESH_OK_VOID();
}

static int starts_with(const char *p, size_t pl, const char *pref)
{
    size_t n = strlen(pref);
    return pl >= n && memcmp(p, pref, n) == 0;
}

static int path_equals(const char *p, size_t pl, const char *want)
{
    size_t n = strlen(want);
    return pl == n && memcmp(p, want, n) == 0;
}

/* Forward decls — the /_rpc helpers live further down, but the page
 * renderers below want them. */
static struct picomesh_int64_result rpc_result_int(struct loop *loop, const struct serve_ud *sud,
                           const char *sid, const char *rpc_path,
                           const char *args_json, long fallback);
static struct picomesh_string_result rpc_result_str(struct loop *loop, const struct serve_ud *sud,
                            const char *sid, const char *rpc_path,
                            const char *args_json, int *was_error);
static int service_active(const struct serve_ud *sud, const char *name);
struct repo_route;
static const struct repo_route *repo_route_for(const char *verb);
static uint32_t repo_hash(const char *account, const char *name);

/* ---- shared page shell (GitLab-like): topbar + sidebar + content ---- *
 * Every signed-in page renders the same chrome so the app reads as one
 * operational forge rather than a set of disconnected stubs: a stable top
 * bar (brand + global search + actions), a left navigation sidebar, then
 * the page content. `uname` NULL/"" → anonymous (brand + sign in only).
 * All classes are defined in the served /static/style.css. */

static void render_topbar(struct buf *b, const char *uname, const char *active_nav,
                          int is_admin)
{
    int in_admin_area = active_nav && strncmp(active_nav, "admin-", 6) == 0;
    buf_puts(b, "<header class=\"topbar\">"
                "<a class=\"brand\" href=\"/-/repos\">picoforge</a>");
    if (uname && *uname) {
        buf_puts(b, "<form class=\"global-search\" method=\"get\" action=\"/-/search\">"
                    "<input name=\"q\" placeholder=\"Search or jump to\xe2\x80\xa6\" "
                    "aria-label=\"Search\"></form>"
                    "<nav class=\"top-actions\">");
        /* "New" is an app-space action — omit it in the admin area so admin
         * chrome doesn't carry user-space project actions. */
        if (!in_admin_area)
            buf_puts(b, "<a class=\"btn small\" href=\"/-/repos/new\">New</a>");
        /* Only a site admin gets the Admin entry point — a regular user must
         * not even see the link (the pages 403 anyway, but the nav must not
         * advertise admin access). */
        if (is_admin)
            buf_puts(b, "<a href=\"/-/admin\">Admin</a>");
        buf_puts(b, "<span class=\"user\">");
        buf_esc(b, uname);
        buf_puts(b, "</span>"
                    "<form method=\"post\" action=\"/-/logout\">"
                    "<button class=\"link\" type=\"submit\">Sign out</button>"
                    "</form></nav>");
    } else {
        buf_puts(b, "<nav class=\"top-actions\"><a href=\"/-/login\">Sign in</a></nav>");
    }
    buf_puts(b, "</header>");
}

/* Left navigation. Two distinct contexts: the regular app nav, and the
 * Admin area's own nav. `active` keys starting with "admin-" switch the
 * sidebar to the admin menu (and highlight the matching admin item);
 * everything else uses the app menu. This keeps admin a separate area,
 * not a peer item wedged into the project nav. */
static void render_sidebar(struct buf *b, const char *active)
{
    struct nav { const char *label; const char *href; const char *key; };
    static const struct nav APP[] = {
        {"Projects",  "/-/repos",            "projects"},
        {"Groups",    "/-/groups",           "groups"},
        {"Issues",    "/-/dashboard/issues", "issues"},
        {"Pipelines", "/-/dashboard/runs",   "runs"},
    };
    static const struct nav ADMIN[] = {
        {"Overview",     "/-/admin",            "admin-overview"},
        {"Users",        "/-/admin/users",      "admin-users"},
        {"Namespaces",   "/-/admin/namespaces", "admin-namespaces"},
        {"Repositories", "/-/admin/repos",      "admin-repos"},
        {"Tokens",       "/-/admin/tokens",     "admin-tokens"},
        {"Services",     "/-/admin/services",   "admin-services"},
    };
    int admin = active && strncmp(active, "admin-", 6) == 0;
    const struct nav *items = admin ? ADMIN : APP;
    size_t n = admin ? sizeof(ADMIN) / sizeof(ADMIN[0]) : sizeof(APP) / sizeof(APP[0]);

    buf_puts(b, "<aside class=\"sidebar\">");
    if (admin) buf_puts(b, "<div class=\"sidebar-title\">Admin area</div>");
    buf_puts(b, "<nav class=\"sidebar-nav\">");
    for (size_t i = 0; i < n; ++i) {
        int on = active && strcmp(items[i].key, active) == 0;
        buf_printf(b, "<a%s href=\"%s\">%s</a>",
                   on ? " class=\"active\"" : "", items[i].href, items[i].label);
    }
    buf_puts(b, "</nav>");
    if (admin)
        buf_puts(b, "<nav class=\"sidebar-nav sidebar-foot\">"
                    "<a href=\"/-/repos\">\xe2\x86\x90 Back to app</a></nav>");
    buf_puts(b, "</aside>");
}

/* Open the full shell: <head> + topbar + sidebar + <main class=content>.
 * `active_nav` highlights the matching sidebar item. */
static void render_shell_open(struct buf *b, const char *title, const char *uname,
                              const char *active_nav, int is_admin)
{
    buf_puts(b, "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                "<title>");
    buf_esc(b, title);
    buf_puts(b, " \xc2\xb7 picoforge</title>"
                "<link rel=\"stylesheet\" href=\"/static/style.css\"></head><body>");
    render_topbar(b, uname, active_nav, is_admin);
    buf_puts(b, "<div class=\"app-shell\">");
    render_sidebar(b, active_nav);
    buf_puts(b, "<main class=\"content\">");
}

static void render_shell_close(struct loop_stream *s, struct buf *b, int keep_alive)
{
    buf_puts(b, "</main></div>"
                "<footer class=\"app-footer\"><span>picoforge \xc2\xb7 served by "
                "picoforge-webapp via the gateway <code>/_rpc</code></span></footer>"
                "</body></html>");
    send_response(s, 200, "text/html; charset=utf-8", b->data ? b->data : "", b->len,
                  NULL, keep_alive);
    buf_free(b);
}

/* Non-repo page header: title + optional muted subtitle, with optional
 * right-aligned `actions_html` (caller-supplied, already-safe markup —
 * pass only literals, never user input). */
static void render_page_header(struct buf *b, const char *title,
                               const char *subtitle, const char *actions_html)
{
    buf_puts(b, "<header class=\"page-header\"><div><h1>");
    buf_esc(b, title);
    buf_puts(b, "</h1>");
    if (subtitle && *subtitle) { buf_puts(b, "<p class=\"muted\">"); buf_esc(b, subtitle); buf_puts(b, "</p>"); }
    buf_puts(b, "</div>");
    if (actions_html && *actions_html) {
        buf_puts(b, "<div class=\"page-actions\">");
        buf_puts(b, actions_html);
        buf_puts(b, "</div>");
    }
    buf_puts(b, "</header>");
}

/* Render a styled 403 page inside the app shell and send it with a real
 * 403 status (render_shell_close hardcodes 200, so we assemble + send it
 * here). `uname` is the trustworthy display name (may be ""). */
static void send_forbidden(struct loop_stream *s, const char *uname, int keep_alive)
{
    struct buf b; buf_init(&b);
    render_shell_open(&b, "Forbidden", uname, NULL, /*is_admin=*/0);
    render_page_header(&b, "Forbidden",
                       "You do not have permission to view this page.", NULL);
    buf_puts(&b, "<p class=\"muted\">This area is restricted to site administrators. "
                 "<a href=\"/-/repos\">Back to your repositories</a>.</p>"
                 "</main></div>"
                 "<footer class=\"app-footer\"><span>picoforge \xc2\xb7 served by "
                 "picoforge-webapp via the gateway <code>/_rpc</code></span></footer>"
                 "</body></html>");
    send_response(s, 403, "text/html; charset=utf-8", b.data ? b.data : "", b.len,
                  NULL, keep_alive);
    buf_free(&b);
}

/* Gate an admin page on a signed-in site admin. Returns 1 when the caller
 * may proceed; otherwise it has already written the response (redirect to
 * /login for an anonymous caller, 403 for a signed-in non-admin) and the
 * caller must return without rendering admin content. */
static int require_admin(struct loop_stream *s, const struct claims *claims, int keep_alive)
{
    if (!claims->uid) { send_redirect(s, "/-/login", NULL, keep_alive); return 0; }
    if (!claims->is_admin) {
        send_forbidden(s, claims->username[0] ? claims->username : NULL, keep_alive);
        return 0;
    }
    return 1;
}

/* Repository header: breadcrumb + repo title + repo id, plus the standard
 * project actions (New file / Run pipeline). The same header renders on
 * Code / Issues / Pipelines / Settings; the active tab marks the section. */
static void render_project_header(struct buf *b, const char *acct,
                                  const char *repo, uint32_t rid)
{
    buf_puts(b, "<header class=\"project-header\"><div class=\"project-title\">"
                "<div class=\"breadcrumbs\"><a href=\"/");
    buf_esc(b, acct); buf_puts(b, "\">"); buf_esc(b, acct);
    buf_puts(b, "</a> <span>/</span> <strong>");
    buf_esc(b, repo); buf_puts(b, "</strong></div><h1>");
    buf_esc(b, repo);
    buf_printf(b, "</h1><p class=\"muted\">Repository #%u</p></div>", rid);

    buf_puts(b, "<div class=\"project-actions\"><a class=\"btn\" href=\"/");
    buf_esc(b, acct); buf_puts(b, "/"); buf_esc(b, repo);
    buf_puts(b, "/-/new\">New file</a>"
                "<form method=\"post\" action=\"/");
    buf_esc(b, acct); buf_puts(b, "/"); buf_esc(b, repo);
    buf_puts(b, "/-/runs/new\"><button class=\"btn\" type=\"submit\">Run pipeline</button>"
                "</form></div></header>");
}

/* Per-repo sub-nav (Code / Issues / Pipelines / Settings). `active` is
 * "code"/"issues"/"runs"/"settings". `issue_count`/`run_count` < 0 omit
 * the count badge (service inactive / unknown). */
static void render_project_tabs(struct buf *b, const char *acct, const char *repo,
                                const char *active, long issue_count, long run_count)
{
    struct tab { const char *label; const char *suffix; const char *key; long count; };
    const struct tab TABS[] = {
        {"Code", "/-/tree", "code", -1},
        {"Issues", "/-/issues", "issues", issue_count},
        {"Pipelines", "/-/runs", "runs", run_count},
        {"Settings", "/-/settings", "settings", -1},
    };
    buf_puts(b, "<nav class=\"project-tabs\">");
    for (size_t i = 0; i < sizeof(TABS) / sizeof(TABS[0]); ++i) {
        buf_puts(b, strcmp(TABS[i].key, active) == 0 ? "<a class=\"active\" href=\"/"
                                                     : "<a href=\"/");
        buf_esc(b, acct); buf_puts(b, "/"); buf_esc(b, repo);
        buf_puts(b, TABS[i].suffix);
        buf_puts(b, "\">"); buf_puts(b, TABS[i].label);
        if (TABS[i].count >= 0) buf_printf(b, " <span class=\"count\">%ld</span>", TABS[i].count);
        buf_puts(b, "</a>");
    }
    buf_puts(b, "</nav>");
}

/* Fetch the tab count badges for a repo: open issues + active runs
 * (pending+running). Either is set to -1 when its service is inactive. */
static struct picomesh_void_result repo_tab_counts(struct loop *loop, const struct serve_ud *sud,
                            const char *sid, uint32_t rid,
                            long *issues_out, long *runs_out)
{
    char args[48];
    snprintf(args, sizeof(args), "[%u]", rid);
    if (service_active(sud, "issues")) {
        struct picomesh_int64_result issues_r = rpc_result_int(loop, sud, sid, "issues.issues.count_open_in_repo", args, -1);
        PICOMESH_RETURN_IF_ERR(picomesh_void, issues_r, "repo_tab_counts: open-issue count");
        *issues_out = issues_r.value;
    } else {
        *issues_out = -1;
    }
    if (service_active(sud, "git_pipeline")) {
        struct picomesh_int64_result pending = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
        PICOMESH_RETURN_IF_ERR(picomesh_void, pending, "repo_tab_counts: pending count");
        struct picomesh_int64_result running = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
        PICOMESH_RETURN_IF_ERR(picomesh_void, running, "repo_tab_counts: running count");
        *runs_out = pending.value + running.value;
    } else {
        *runs_out = -1;
    }
    return PICOMESH_OK_VOID();
}

/* A bordered content panel with an optional header (title + muted meta)
 * and a padded body. panel_close() must balance every panel_open(). */
static void panel_open(struct buf *b, const char *title, const char *meta)
{
    buf_puts(b, "<section class=\"panel\">");
    if ((title && *title) || (meta && *meta)) {
        buf_puts(b, "<header class=\"panel-header\"><div>");
        if (title && *title) { buf_puts(b, "<strong>"); buf_esc(b, title); buf_puts(b, "</strong>"); }
        if (meta && *meta) { buf_puts(b, " <span class=\"muted\">"); buf_esc(b, meta); buf_puts(b, "</span>"); }
        buf_puts(b, "</div></header>");
    }
    buf_puts(b, "<div class=\"panel-body\">");
}
static void panel_close(struct buf *b) { buf_puts(b, "</div></section>"); }

/* Forward declarations — these helpers are defined further down but are used by
 * the RBAC-based /repos discovery below. */
static struct json_doc_ptr_result whoami_doc(struct loop *loop, const struct serve_ud *sud, const char *sid);
static int role_at_least(const char *role, const char *floor);
static int json_escape(char *dst, size_t cap, const char *src);
static struct json_doc_ptr_result rpc_result_doc(struct loop *loop, const struct serve_ud *sud,
                                        const char *sid, const char *rpc_path,
                                        const char *args_json,
                                        const struct json_value **result_out);

/* True iff `full` already appears as a "\n<full>\n" token in the newline-wrapped
 * `seen` buffer — the dedup test for the /repos discovery merge. */
static int repo_seen(const char *seen, const char *full)
{
    if (!seen || !full || !*full) return 0;
    char needle[322];
    snprintf(needle, sizeof(needle), "\n%s\n", full);
    return strstr(seen, needle) != NULL;
}

/* Collect every repo FULL PATH (<namespace>/<repo>) the signed-in caller can
 * READ, by the SAME role-based discovery the /repos page uses (issue #30): for
 * each direct membership at reporter+ (from /_whoami), expand to its subtree
 * (accounts.ns_subtree — the role inherits down the whole subtree) and list each
 * namespace once (git_repo.list_for_namespace), deduplicating namespaces and
 * repos. The creator index (list_for_owner) is intentionally NOT consulted — it
 * misses RBAC-visible group/subgroup repos AND leaks repos the caller created
 * but has since lost access to. Returns a heap, newline-separated,
 * NUL-terminated list (NULL/empty when the caller can read nothing); caller
 * frees. Shared by /repos, /search and /dashboard/issues so all three agree. */
static struct picomesh_string_result collect_accessible_repos(struct loop *loop, const struct serve_ud *sud,
                                      const char *sid)
{
    struct json_doc_ptr_result who_r = whoami_doc(loop, sud, sid);
    if (PICOMESH_IS_ERR(who_r)) { picomesh_error_print(stderr, "webapp: collect_accessible_repos whoami", who_r.error); picomesh_error_destroy(who_r.error); }
    struct json_doc *who = PICOMESH_IS_OK(who_r) ? who_r.value : NULL;
    const struct json_value *nss = who ? json_object_get(json_doc_root(who), "namespaces") : NULL;

    /* Two dedup sets of "\n<token>\n": namespaces already expanded (a nested
     * membership is covered by an ancestor's subtree) and repos already added. */
    struct buf ns_seen; buf_init(&ns_seen); buf_puts(&ns_seen, "\n");
    struct buf seen;    buf_init(&seen);    buf_puts(&seen, "\n");
    struct buf out;     buf_init(&out);

    size_t ns_count = nss ? json_array_size(nss) : 0;
    for (size_t i = 0; i < ns_count; ++i) {
        const struct json_value *ns_entry = json_array_at(nss, i);
        const char *root = json_as_string(json_object_get(ns_entry, "path"), NULL);
        const char *role = json_as_string(json_object_get(ns_entry, "role"), NULL);
        if (!root || !*root || !role || !role_at_least(role, "reporter")) continue;

        char esc[256], sargs[300];
        if (!json_escape(esc, sizeof(esc), root)) continue;
        snprintf(sargs, sizeof(sargs), "[\"%s\"]", esc);
        const struct json_value *paths = NULL;
        struct json_doc_ptr_result sub_r = rpc_result_doc(loop, sud, sid, "accounts.accounts.ns_subtree", sargs, &paths);
        if (PICOMESH_IS_ERR(sub_r)) { picomesh_error_print(stderr, "webapp: ns_subtree", sub_r.error); picomesh_error_destroy(sub_r.error); }
        struct json_doc *sub = PICOMESH_IS_OK(sub_r) ? sub_r.value : NULL;
        size_t path_count = (sub && paths && json_is_array(paths)) ? json_array_size(paths) : 0;
        for (size_t j = 0; j < path_count; ++j) {
            const char *nspath = json_as_string(json_object_get(json_array_at(paths, j), "path"), NULL);
            if (!nspath || !*nspath) continue;
            if (repo_seen(ns_seen.data, nspath)) continue;  /* already listed via another membership */
            buf_puts(&ns_seen, "\n"); buf_puts(&ns_seen, nspath); buf_puts(&ns_seen, "\n");

            char nesc[256], nargs[300];
            if (!json_escape(nesc, sizeof(nesc), nspath)) continue;
            snprintf(nargs, sizeof(nargs), "[\"%s\"]", nesc);
            struct picomesh_string_result names_r = rpc_result_str(loop, sud, sid, "git_repo.git_repo.list_for_namespace", nargs, NULL);
            if (PICOMESH_IS_ERR(names_r)) { picomesh_error_print(stderr, "webapp: list_for_namespace", names_r.error); picomesh_error_destroy(names_r.error); }
            char *names = PICOMESH_IS_OK(names_r) ? names_r.value : NULL;
            if (names && *names) {
                char *save = NULL;
                for (char *line = strtok_r(names, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                    if (!*line) continue;
                    char full[320];
                    snprintf(full, sizeof(full), "%s/%s", nspath, line);
                    if (repo_seen(seen.data, full)) continue;
                    buf_puts(&seen, "\n"); buf_puts(&seen, full); buf_puts(&seen, "\n");
                    buf_puts(&out, full); buf_puts(&out, "\n");
                }
            }
            free(names);
        }
        if (sub) json_doc_free(sub);
    }
    buf_free(&ns_seen);
    buf_free(&seen);
    if (who) json_doc_free(who);
    return PICOMESH_OK(picomesh_string, out.data); /* ownership transferred to caller (NULL when empty) */
}

/* GET /-/repos — the signed-in user's accessible repositories: every repo in
 * every namespace the caller holds a role on (RBAC discovery via
 * collect_accessible_repos). All data comes from the gateway over /_rpc; the
 * webapp holds no state. `sid` NULL/empty → bounce to /login. */
static struct picomesh_void_result route_repos_get(struct loop *loop, struct loop_stream *s,
                            const struct serve_ud *sud, const char *sid,
                            const char *uname, uint32_t owner_uid, int is_admin,
                            int keep_alive)
{
    if (!sid || !*sid || !owner_uid) {
        send_redirect(s, "/-/login", NULL, keep_alive);
        return PICOMESH_OK_VOID();
    }

    struct picomesh_int64_result total_r = rpc_result_int(loop, sud, sid, "git_repo.git_repo.count_total", "[]", -1);
    if (PICOMESH_IS_ERR(total_r)) { picomesh_error_print(stderr, "webapp: count_total", total_r.error); picomesh_error_destroy(total_r.error); }
    long total = PICOMESH_IS_OK(total_r) ? total_r.value : -1;

    /* Projects discovery is ROLE-based, not creator-index-based (issue #30): the
     * page lists every repo in every namespace the caller can READ, INCLUDING
     * subgroups reached by INHERITED role and EXCLUDING repos the caller created
     * but has since lost access to. collect_accessible_repos walks /_whoami →
     * accounts.ns_subtree → git_repo.list_for_namespace; the creator index
     * (list_for_owner) is intentionally NOT consulted. */
    struct picomesh_string_result repos_r = collect_accessible_repos(loop, sud, sid);
    if (PICOMESH_IS_ERR(repos_r)) { picomesh_error_print(stderr, "webapp: collect repos", repos_r.error); picomesh_error_destroy(repos_r.error); }
    char *repos = PICOMESH_IS_OK(repos_r) ? repos_r.value : NULL;

    struct buf b; buf_init(&b);
    render_shell_open(&b, "Repositories", uname, "projects", is_admin);
    render_page_header(&b, "Repositories", "Repositories you can access across the mesh.",
                       "<a class=\"btn primary\" href=\"/-/repos/new\">New repository</a>");

    char meta[64];
    if (total >= 0) snprintf(meta, sizeof(meta), "%ld total in the mesh", total);
    else            meta[0] = 0;
    panel_open(&b, "Your repositories", meta[0] ? meta : NULL);

    int any = 0;
    buf_puts(&b, "<table class=\"file-table\"><tbody>");
    if (repos && *repos) {
        char *save = NULL;
        for (char *full = strtok_r(repos, "\n", &save); full; full = strtok_r(NULL, "\n", &save)) {
            if (!*full) continue;
            any = 1;
            buf_puts(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td><td><a class=\"file-name dir\" href=\"/");
            buf_esc(&b, full); buf_puts(&b, "/-/tree\">");
            buf_esc(&b, full); buf_puts(&b, "</a></td></tr>");
        }
    }
    buf_puts(&b, "</tbody></table>");

    if (!any)
        buf_puts(&b, "<p class=\"muted\">No repositories yet — use "
                     "\xe2\x80\x9cNew repository\xe2\x80\x9d to create one.</p>");
    panel_close(&b);
    free(repos);
    render_shell_close(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* ---- repo file browser + Monaco editor ----------------------------- *
 *
 * These routes live in the SIDECAR (the picoforge webapp) — it serves
 * its own static assets (Monaco under /static/vendor/monaco), per gh#5
 * the gateway serves none. Data comes from the gateway over POST /_rpc:
 * git_repo.git_repo.read_tree / read_file / put_file. The sidecar holds no
 * plugins and no backend ports; it relays the opaque picomesh-sid so the
 * gateway authenticates and the git_repo backend authorizes (public
 * repos world-readable, writes owner-only).                             */

/* Deterministic repo id — FNV-1a of "<account>/<repo>". MUST match the
 * gateway's hash_repo() and git_repo's repo_hash() (see memory
 * repo-id-shared-hash) so all three agree without a lookup. */
static uint32_t repo_hash(const char *account, const char *name)
{
    char key[160];
    snprintf(key, sizeof(key), "%s/%s", account, name);
    uint32_t h = 2166136261u;
    for (const char *p = key; *p; ++p) { h ^= (unsigned char)*p; h *= 16777619u; }
    return h ? h : 1;
}

/* repo_id for a repo given its FULL PATH (<namespace>/<repo>) — the same FNV-1a
 * as repo_hash but over the already-joined path. */
static uint32_t repo_hash_full(const char *full_path)
{
    uint32_t h = 2166136261u;
    for (const char *p = full_path; p && *p; ++p) { h ^= (unsigned char)*p; h *= 16777619u; }
    return h ? h : 1;
}

/* Extract a query-string parameter (URL-decoded) from a full path like
 * "/a/b/edit?path=src%2Fmain.c". malloc'd result or NULL. */
static char *query_get(const char *path, const char *key)
{
    const char *q = strchr(path, '?');
    if (!q) return NULL;
    size_t klen = strlen(key);
    const char *p = q + 1;
    while (*p) {
        const char *amp = strchr(p, '&');
        const char *seg_end = amp ? amp : p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(seg_end - p));
        if (eq && (size_t)(eq - p) == klen && memcmp(p, key, klen) == 0) {
            size_t vlen = (size_t)(seg_end - eq - 1);
            char *out = malloc(vlen + 1);
            if (!out) return NULL;
            memcpy(out, eq + 1, vlen);
            out[vlen] = 0;
            url_decode(out);
            return out;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return NULL;
}

/* JSON-escape `src` into `dst[cap]` (always NUL-terminated). Returns 1 on
 * success, 0 on overflow. An empty string escapes successfully. */
static int json_escape(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    for (const char *p = src ? src : ""; *p; ++p) {
        const char *rep = NULL; char ubuf[8];
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  rep = "\\\""; break;
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n";  break;
        case '\r': rep = "\\r";  break;
        case '\t': rep = "\\t";  break;
        default:
            if (c < 0x20) { snprintf(ubuf, sizeof(ubuf), "\\u%04x", c); rep = ubuf; }
            break;
        }
        if (rep) { size_t rl = strlen(rep); if (n + rl >= cap) return 0; memcpy(dst + n, rep, rl); n += rl; }
        else { if (n + 1 >= cap) return 0; dst[n++] = (char)c; }
    }
    if (n >= cap) return 0;
    dst[n] = 0;
    return 1;
}

/* POST the gateway's /_rpc with {path,args:<args_json>} and return the
 * `result` STRING (malloc'd; caller frees) or NULL. `*was_error` (opt)
 * is set when the gateway answered with an error object. */
static struct picomesh_string_result rpc_result_str(struct loop *loop, const struct serve_ud *sud,
                            const char *sid, const char *rpc_path,
                            const char *args_json, int *was_error)
{
    if (was_error) *was_error = 0;
    /* Body sized to hold args_json (which may carry an escaped file). */
    size_t need = strlen(rpc_path) + strlen(args_json) + 32;
    char *body = malloc(need);
    if (!body) { if (was_error) *was_error = 1; return PICOMESH_OK(picomesh_string, NULL); }
    int n = snprintf(body, need, "{\"path\":\"%s\",\"args\":%s}", rpc_path, args_json);
    if (n <= 0 || (size_t)n >= need) { free(body); if (was_error) *was_error = 1; return PICOMESH_OK(picomesh_string, NULL); }

    struct http_response resp;
    struct picomesh_int_result post = http_post_json(loop, &sud->gw, "/_rpc", NULL, sid, body, (size_t)n, &resp);
    free(body);
    if (PICOMESH_IS_ERR(post)) { http_response_free(&resp); if (was_error) *was_error = 1; return PICOMESH_ERR(picomesh_string, "rpc_result_str: gateway POST failed", post); }

    char *out = NULL;
    if (resp.body) {
        struct json_doc *doc = json_parse(resp.body, resp.body_len);
        if (doc) {
            const struct json_value *root = json_doc_root(doc);
            const struct json_value *result = json_object_get(root, "result");
            const struct json_value *err = json_object_get(root, "error");
            if (result) {
                const char *str_val = json_as_string(result, NULL);
                if (str_val) out = strdup(str_val);
            } else if (err && was_error) {
                *was_error = 1;
            }
            json_doc_free(doc);
        }
    }
    http_response_free(&resp);
    return PICOMESH_OK(picomesh_string, out);
}

/* Perform an RPC and return the parsed response document so the caller can
 * walk a structured `result` (e.g. a JSON array). `*result_out` points at
 * the `result` value inside the returned doc (any JSON type, NULL if
 * absent). The caller owns the doc and must json_doc_free it; values
 * borrowed from it are valid until then. NULL on transport/parse error. */
static struct json_doc_ptr_result rpc_result_doc(struct loop *loop, const struct serve_ud *sud,
                                        const char *sid, const char *rpc_path,
                                        const char *args_json,
                                        const struct json_value **result_out)
{
    if (result_out) *result_out = NULL;
    size_t need = strlen(rpc_path) + strlen(args_json) + 32;
    char *body = malloc(need);
    if (!body) return PICOMESH_OK(json_doc_ptr, NULL);
    int n = snprintf(body, need, "{\"path\":\"%s\",\"args\":%s}", rpc_path, args_json);
    if (n <= 0 || (size_t)n >= need) { free(body); return PICOMESH_OK(json_doc_ptr, NULL); }

    struct http_response resp;
    struct picomesh_int_result post = http_post_json(loop, &sud->gw, "/_rpc", NULL, sid, body, (size_t)n, &resp);
    free(body);
    if (PICOMESH_IS_ERR(post)) { http_response_free(&resp); return PICOMESH_ERR(json_doc_ptr, "rpc_result_doc: gateway POST failed", post); }

    struct json_doc *doc = NULL;
    if (resp.body) {
        doc = json_parse(resp.body, resp.body_len);
        if (doc && result_out)
            *result_out = json_object_get(json_doc_root(doc), "result");
    }
    http_response_free(&resp);
    return PICOMESH_OK(json_doc_ptr, doc);
}

/* Split "<account>/<repo>[/<verb>]" out of a path (query stripped). On
 * success fills acct/repo/verb (verb "" if none) and returns 1. */
/* Byte offset of the GitLab-style "/-/" separator within [path, path+plen)
 * (query already excluded by the caller), or -1 if absent. Offset 0 means the
 * path is a top-level command (/-/login); a positive offset means a project
 * sub-page (<namespace>/<repo>/-/<verb>). */
static long dash_sep(const char *path, size_t plen)
{
    if (plen >= 3) {
        for (size_t i = 0; i + 3 <= plen; ++i)
            if (path[i] == '/' && path[i + 1] == '-' && path[i + 2] == '/')
                return (long)i;
    }
    return -1;
}

/* Split the RESOURCE part of a project URL (everything before "/-/", e.g.
 * "/acme/platform/svc") into the namespace path (`acct`, all but the last
 * segment) and the repo (`repo`, the last segment). Returns 1 on success
 * (needs >= 1 namespace segment + a repo), 0 otherwise. */
static int split_namespace_repo(const char *resource, size_t rlen,
                                char *acct, size_t acct_cap,
                                char *repo, size_t repo_cap)
{
    char tmp[1024];
    if (rlen == 0 || rlen >= sizeof(tmp)) return 0;
    memcpy(tmp, resource, rlen); tmp[rlen] = 0;
    char *segs[16] = {0}; int ns = 0;
    for (char *tok = strtok(tmp, "/"); tok && ns < 16; tok = strtok(NULL, "/"))
        segs[ns++] = tok;
    if (ns < 2) return 0; /* need namespace + repo */
    for (int i = 0; i < ns; ++i) if (strstr(segs[i], "..")) return 0;
    size_t al = 0; acct[0] = 0;
    for (int i = 0; i < ns - 1; ++i)
        al += (size_t)snprintf(acct + al, al < acct_cap ? acct_cap - al : 0, "%s%s", i ? "/" : "", segs[i]);
    if (al >= acct_cap) return 0;
    snprintf(repo, repo_cap, "%s", segs[ns - 1]);
    return 1;
}

/* Parse a project sub-page URL "<namespace>/<repo>/-/<verb>" (GitLab style).
 * Fills acct/repo/verb and returns 1; 0 if the path is not a project sub-page
 * (no "/-/" separator at a positive offset, or a malformed resource). The file
 * or directory operated on rides in the query string, not the URL path. */
static int parse_repo_path(const char *path, size_t path_len,
                           char *acct, size_t acct_cap,
                           char *repo, size_t repo_cap,
                           char *verb, size_t verb_cap)
{
    size_t plen = path_len;
    const char *q = memchr(path, '?', path_len);
    if (q) plen = (size_t)(q - path);
    long d = dash_sep(path, plen);
    if (d <= 0) return 0; /* 0 = top-level command, -1 = bare namespace path */
    if (!split_namespace_repo(path, (size_t)d, acct, acct_cap, repo, repo_cap)) return 0;
    /* verb = first segment after "/-/" (the rest, if any, is ignored here; the
     * acted-on file/dir is a query parameter). */
    const char *tail = path + d + 3;
    size_t tlen = plen - (size_t)d - 3;
    size_t vlen = 0;
    while (vlen < tlen && tail[vlen] != '/') ++vlen;
    if (vlen == 0 || vlen >= verb_cap) return 0;
    memcpy(verb, tail, vlen); verb[vlen] = 0;
    return 1;
}

/* GET /<account>/<repo>[?dir=<subdir>] — file browser. Lists the tree via
 * git_repo.git_repo.read_tree; dirs link deeper, files link to /edit. */
static struct picomesh_void_result route_repo_browse(struct loop *loop, struct loop_stream *s,
                              const struct serve_ud *sud, const char *sid,
                              const char *uname, const char *acct, const char *repo,
                              const char *full_path, int is_admin, int keep_alive)
{
    uint32_t rid = repo_hash(acct, repo);
    char *dir = query_get(full_path, "dir");
    const char *dirv = dir ? dir : "";

    char dir_esc[1024];
    if (!json_escape(dir_esc, sizeof(dir_esc), dirv)) { free(dir); send_text(s, 400, "bad dir\n", keep_alive); return PICOMESH_OK_VOID(); }
    char args[1200];
    snprintf(args, sizeof(args), "[%u,\"\",\"%s\"]", rid, dir_esc);
    int err = 0;
    struct picomesh_string_result tree_r = rpc_result_str(loop, sud, sid, "git_repo.git_repo.read_tree", args, &err);
    if (PICOMESH_IS_ERR(tree_r)) { picomesh_error_print(stderr, "webapp: read_tree", tree_r.error); picomesh_error_destroy(tree_r.error); }
    char *tree = PICOMESH_IS_OK(tree_r) ? tree_r.value : NULL;

    long issue_count = -1, run_count = -1;
    struct picomesh_void_result counts_r = repo_tab_counts(loop, sud, sid, rid, &issue_count, &run_count);
    if (PICOMESH_IS_ERR(counts_r)) { picomesh_error_print(stderr, "webapp: repo_tab_counts", counts_r.error); picomesh_error_destroy(counts_r.error); }

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "%s/%s", acct, repo);
    render_shell_open(&b, title, uname, "projects", is_admin);
    render_project_header(&b, acct, repo, rid);
    render_project_tabs(&b, acct, repo, "code", issue_count, run_count);

    /* File panel — its header carries the current path/branch + the New
     * file action; the tree renders as a dense file table. */
    buf_puts(&b, "<section class=\"panel repo-browser\"><header class=\"panel-header\"><div>"
                 "<strong>Files</strong> ");
    if (*dirv) { buf_puts(&b, "<span class=\"muted\">/ "); buf_esc(&b, dirv); buf_puts(&b, "</span>"); }
    else       { buf_puts(&b, "<span class=\"branch\">main</span>"); }
    buf_puts(&b, "</div><div class=\"panel-actions\">");
    {
        char href[1200];
        if (*dirv) snprintf(href, sizeof(href), "/%s/%s/-/new?dir=%s", acct, repo, dirv);
        else       snprintf(href, sizeof(href), "/%s/%s/-/new", acct, repo);
        buf_puts(&b, "<a class=\"btn small\" href=\"");
        buf_esc(&b, href);
        buf_puts(&b, "\">New file</a>");
    }
    buf_puts(&b, "</div></header>");

    if (!tree || !*tree) {
        buf_puts(&b, "<div class=\"panel-body\">");
        if (tree == NULL && err)
            buf_puts(&b, "<p class=\"error\">Cannot read this repository "
                         "(it may be private, or the gateway is unreachable).</p>");
        else
            buf_puts(&b, "<p class=\"muted\">Empty repository. Use "
                         "\xe2\x80\x9cNew file\xe2\x80\x9d to add the first file.</p>");
        buf_puts(&b, "</div>");
    } else {
        buf_puts(&b, "<table class=\"file-table\"><thead><tr>"
                     "<th></th><th>Name</th><th>Last commit</th><th>Updated</th></tr></thead><tbody>");
        if (*dirv) {
            char up[1024]; snprintf(up, sizeof(up), "%s", dirv);
            char *slash = strrchr(up, '/');
            if (slash) *slash = 0; else up[0] = 0;
            if (up[0]) buf_printf(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td>"
                                      "<td><a class=\"file-name dir\" href=\"/%s/%s/-/tree?dir=%s\">..</a></td>"
                                      "<td class=\"muted\">-</td><td class=\"muted\">-</td></tr>", acct, repo, up);
            else       buf_printf(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td>"
                                      "<td><a class=\"file-name dir\" href=\"/%s/%s/-/tree\">..</a></td>"
                                      "<td class=\"muted\">-</td><td class=\"muted\">-</td></tr>", acct, repo);
        }
        char *save = NULL;
        for (char *line = strtok_r(tree, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *tab = strchr(line, '\t');
            if (!tab) continue;
            *tab = 0;
            const char *type = line, *entry_name = tab + 1;
            char child[1024];
            if (*dirv) snprintf(child, sizeof(child), "%s/%s", dirv, entry_name);
            else       snprintf(child, sizeof(child), "%s", entry_name);
            int is_dir = strcmp(type, "tree") == 0;
            buf_puts(&b, "<tr><td class=\"ic\">");
            buf_puts(&b, is_dir ? "\xf0\x9f\x93\x81" : "\xf0\x9f\x93\x84");
            buf_puts(&b, is_dir ? "</td><td><a class=\"file-name dir\" href=\"/"
                                : "</td><td><a class=\"file-name file\" href=\"/");
            buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
            if (is_dir) { buf_puts(&b, "/-/tree?dir="); buf_esc(&b, child); }
            else        { buf_puts(&b, "/-/edit?path="); buf_esc(&b, child); }
            buf_puts(&b, "\">"); buf_esc(&b, entry_name);
            buf_puts(&b, "</a></td><td class=\"muted\">-</td><td class=\"muted\">-</td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    buf_puts(&b, "</section>");
    free(tree); free(dir);
    render_shell_close(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* Emit the Monaco editor page. `path` may be empty for a brand-new file
 * where the user types the name; `content` is the current text (empty for
 * new). `is_new` controls the heading + whether the path field is editable. */
static void render_editor_page(struct loop_stream *s, const char *uname,
                               const char *acct,
                               const char *repo, const char *path,
                               const char *content, int is_new, int is_admin,
                               int keep_alive)
{
    struct buf b; buf_init(&b);
    char title[200];
    snprintf(title, sizeof(title), "%s %s/%s", is_new ? "New file" : "Edit", acct, repo);
    /* render_shell_open emits the topbar + sidebar + <main>; then the
     * Monaco stylesheet (vendored under /static — no CDN, the in-browser
     * VM is offline). */
    render_shell_open(&b, title, uname, "projects", is_admin);
    buf_puts(&b,
        "<link rel=\"stylesheet\" href=\"/static/vendor/monaco/vs/editor/editor.main.css\">");

    /* Editor header: breadcrumb to the file + a Cancel back to the repo. */
    buf_puts(&b, "<header class=\"editor-header\"><div><div class=\"breadcrumbs\"><a href=\"/");
    buf_esc(&b, acct); buf_puts(&b, "\">"); buf_esc(&b, acct);
    buf_puts(&b, "</a> <span>/</span> <a href=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo); buf_puts(&b, "/-/tree\">");
    buf_esc(&b, repo); buf_puts(&b, "</a>");
    if (path && *path) { buf_puts(&b, " <span>/</span> <strong>"); buf_esc(&b, path); buf_puts(&b, "</strong>"); }
    buf_puts(&b, "</div><h1>");
    buf_puts(&b, is_new ? "New file" : "Edit file");
    buf_puts(&b, "</h1></div><a class=\"btn\" href=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo); buf_puts(&b, "/-/tree\">Cancel</a></header>");

    buf_puts(&b, "<form id=\"f\" class=\"editor-form\" method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo); buf_puts(&b, "/-/edit\">");

    /* Toolbar: path + commit message + commit, all on one dense row. */
    buf_puts(&b, "<div class=\"editor-toolbar\">"
                 "<input class=\"path\" name=\"path\" required placeholder=\"path/to/file.ext\" value=\"");
    buf_esc(&b, path ? path : "");
    buf_puts(&b, "\"");
    if (!is_new) buf_puts(&b, " readonly");
    buf_puts(&b, "><input class=\"message\" name=\"message\" placeholder=\"Commit message\" value=\"");
    buf_esc(&b, is_new ? "create file" : "update file");
    buf_puts(&b, "\"><button type=\"submit\" class=\"primary\">Commit changes</button></div>");

    /* Hidden mirror of the file content + the Monaco mount point. */
    buf_puts(&b, "<textarea id=\"content\" name=\"content\" style=\"display:none\">");
    buf_esc(&b, content ? content : "");
    buf_puts(&b, "</textarea><div id=\"editor\"></div></form>");

    /* Monaco AMD loader for the file content — all from /static (offline). */
    buf_puts(&b,
        "<script src=\"/static/vendor/monaco/vs/loader.js\"></script>"
        "<script>"
        "require.config({paths:{vs:'/static/vendor/monaco/vs'}});"
        "require(['vs/editor/editor.main'],function(){"
        "var ta=document.getElementById('content');"
        "var pathField=document.querySelector('input[name=path]');"
        "function uriFor(p){try{return monaco.Uri.file(p||'untitled.txt');}catch(e){return undefined;}}"
        "var model=monaco.editor.createModel(ta.value, undefined, uriFor(pathField.value));"
        "var ed=monaco.editor.create(document.getElementById('editor'),"
        "{model:model,theme:'vs-dark',automaticLayout:true});"
        "pathField.addEventListener('change',function(){"
        "var nm=pathField.value; if(!nm)return;"
        "var lang=(monaco.languages.getLanguages().find(function(l){"
        "return (l.extensions||[]).some(function(e){return nm.endsWith(e);});})||{}).id;"
        "if(lang)monaco.editor.setModelLanguage(model,lang);});"
        "document.getElementById('f').addEventListener('submit',function(){"
        "ta.value=ed.getValue();});"
        "});"
        "</script>");
    render_shell_close(s, &b, keep_alive);
}

/* GET /<account>/<repo>/edit?path=<p> — open a file in Monaco (read_file;
 * a missing file opens blank as new at that path). GET /<account>/<repo>/new
 * — blank editor (optionally seeded with ?dir= as a path prefix). */
static struct picomesh_void_result route_repo_edit_get(struct loop *loop, struct loop_stream *s,
                                const struct serve_ud *sud, const char *sid,
                                const char *uname, const char *acct, const char *repo,
                                const char *verb, const char *full_path,
                                int is_admin, int keep_alive)
{
    if (!sid || !*sid) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }
    uint32_t rid = repo_hash(acct, repo);

    if (strcmp(verb, "new") == 0) {
        char *dir = query_get(full_path, "dir");
        char seed[1024] = {0};
        if (dir && *dir) snprintf(seed, sizeof(seed), "%s/", dir);
        render_editor_page(s, uname, acct, repo, seed, "", /*is_new=*/1, is_admin, keep_alive);
        free(dir);
        return PICOMESH_OK_VOID();
    }

    char *path = query_get(full_path, "path");
    if (!path || !*path) {
        free(path);
        render_editor_page(s, uname, acct, repo, "", "", /*is_new=*/1, is_admin, keep_alive);
        return PICOMESH_OK_VOID();
    }
    char path_esc[1024];
    if (!json_escape(path_esc, sizeof(path_esc), path)) { free(path); send_text(s, 400, "bad path\n", keep_alive); return PICOMESH_OK_VOID(); }
    char args[1200];
    snprintf(args, sizeof(args), "[%u,\"\",\"%s\"]", rid, path_esc);
    int err = 0;
    struct picomesh_string_result content_r = rpc_result_str(loop, sud, sid, "git_repo.git_repo.read_file", args, &err);
    if (PICOMESH_IS_ERR(content_r)) { picomesh_error_print(stderr, "webapp: read_file", content_r.error); picomesh_error_destroy(content_r.error); }
    char *content = PICOMESH_IS_OK(content_r) ? content_r.value : NULL;
    render_editor_page(s, uname, acct, repo, path, content ? content : "",
                       /*is_new=*/(content == NULL), is_admin, keep_alive);
    free(content); free(path);
    return PICOMESH_OK_VOID();
}

/* POST /<account>/<repo>/edit — save a file (put_file) then redirect to
 * the browser. Body is form-encoded: path, content, message. */
static struct picomesh_void_result route_repo_edit_post(struct loop *loop, struct loop_stream *s,
                                 const struct serve_ud *sud, const char *sid,
                                 const char *acct, const char *repo,
                                 const char *body, size_t body_len, int keep_alive)
{
    if (!sid || !*sid) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }
    uint32_t rid = repo_hash(acct, repo);

    char *path = form_get(body, body_len, "path");
    char *content = form_get(body, body_len, "content");
    char *message = form_get(body, body_len, "message");
    if (!path || !*path) {
        free(path); free(content); free(message);
        send_text(s, 400, "path required\n", keep_alive);
        return PICOMESH_OK_VOID();
    }

    /* put_file args: [rid, path, content, message, "", ""] (author left
     * empty → backend defaults). content can be large. */
    enum { CAP = 1 << 20 };  /* 1 MiB escaped ceiling */
    char *pe = malloc(2048), *ce = malloc(CAP), *me = malloc(2048);
    char *args = ce ? malloc((size_t)CAP + 4096) : NULL;
    int ok = pe && ce && me && args
          && json_escape(pe, 2048, path)
          && json_escape(ce, CAP, content ? content : "")
          && json_escape(me, 2048, message ? message : "");
    if (ok) {
        snprintf(args, (size_t)CAP + 4096, "[%u,\"%s\",\"%s\",\"%s\",\"\",\"\"]", rid, pe, ce, me);
        int err = 0;
        struct picomesh_string_result oid_r = rpc_result_str(loop, sud, sid, "git_repo.git_repo.put_file", args, &err);
        if (PICOMESH_IS_ERR(oid_r)) { picomesh_error_print(stderr, "webapp: put_file", oid_r.error); picomesh_error_destroy(oid_r.error); }
        char *oid = PICOMESH_IS_OK(oid_r) ? oid_r.value : NULL;
        free(oid);
        if (err || !oid) ok = 0;
    }
    free(pe); free(ce); free(me); free(args);

    if (!ok) { free(path); free(content); free(message);
               send_text(s, 500, "save failed (forbidden or backend error)\n", keep_alive); return PICOMESH_OK_VOID(); }

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    char where[1200];
    if (slash) { *slash = 0; snprintf(where, sizeof(where), "/%s/%s/-/tree?dir=%s", acct, repo, dir); }
    else       snprintf(where, sizeof(where), "/%s/%s/-/tree", acct, repo);
    free(path); free(content); free(message);
    send_redirect(s, where, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

/* ===================================================================== *
 * Service-driven page set.
 *
 * The sidecar holds no plugins and no backend ports — it learns which
 * services the mesh is running from the gateway's /_describe, then only
 * serves the pages whose backing service is active. The page set is
 * driven by the live mesh, not a hardcoded route ladder: a service the
 * mesh isn't running yields a 404 for its page ("no service → no page").
 * Data for every page is sourced from the gateway over POST /_rpc.
 * ===================================================================== */

/* POST /_rpc {path,args} and return the integer `result`, or `fallback`
 * on transport error / missing result. Mirrors rpc_result_str for the
 * many backend methods that return a count / id rather than a string. */
static struct picomesh_int64_result rpc_result_int(struct loop *loop, const struct serve_ud *sud,
                           const char *sid, const char *rpc_path,
                           const char *args_json, long fallback)
{
    size_t need = strlen(rpc_path) + strlen(args_json) + 32;
    char *body = malloc(need);
    if (!body) return PICOMESH_OK(picomesh_int64, fallback);
    int n = snprintf(body, need, "{\"path\":\"%s\",\"args\":%s}", rpc_path, args_json);
    if (n <= 0 || (size_t)n >= need) { free(body); return PICOMESH_OK(picomesh_int64, fallback); }

    struct http_response resp;
    struct picomesh_int_result post = http_post_json(loop, &sud->gw, "/_rpc", NULL, sid, body, (size_t)n, &resp);
    free(body);
    if (PICOMESH_IS_ERR(post)) { http_response_free(&resp); return PICOMESH_ERR(picomesh_int64, "rpc_result_int: gateway POST failed", post); }

    long out = fallback;
    if (resp.body) {
        struct json_doc *doc = json_parse(resp.body, resp.body_len);
        if (doc) {
            const struct json_value *root = json_doc_root(doc);
            const struct json_value *result = json_object_get(root, "result");
            if (result) out = (long)json_as_int(result, fallback);
            json_doc_free(doc);
        }
    }
    http_response_free(&resp);
    return PICOMESH_OK(picomesh_int64, out);
}

/* Resolve the caller's authenticated claims (uid, username, admin bit) via
 * the gateway's /_whoami. The sid is forwarded by http_post as the
 * picomesh-sid header; the gateway maps it to the live session. On any
 * transport/parse failure the claims stay zeroed (anonymous), so pages and
 * the admin gate fail closed. This is the ONLY identity source the webapp
 * trusts — the picomesh-uname cookie is never an authority. */
static struct picomesh_void_result resolve_claims(struct loop *loop, const struct serve_ud *sud,
                           const char *sid, struct claims *out)
{
    memset(out, 0, sizeof(*out));
    if (!sid || !*sid) return PICOMESH_OK_VOID();
    struct http_response resp;
    struct picomesh_int_result post = http_post(loop, &sud->gw, "/_whoami",
                       "application/json", NULL, sid, "", 0, &resp);
    if (PICOMESH_IS_ERR(post)) { http_response_free(&resp); return PICOMESH_ERR(picomesh_void, "resolve_claims: /_whoami POST failed", post); }
    if (resp.body) {
        struct json_doc *doc = json_parse(resp.body, resp.body_len);
        if (doc) {
            const struct json_value *root = json_doc_root(doc);
            out->uid = (uint32_t)json_as_int(json_object_get(root, "uid"), 0);
            const char *username = json_as_string(json_object_get(root, "username"), NULL);
            if (username) snprintf(out->username, sizeof(out->username), "%s", username);
            out->is_admin = json_as_bool(json_object_get(root, "is_admin"), 0);
            json_doc_free(doc);
        }
    }
    http_response_free(&resp);
    return PICOMESH_OK_VOID();
}

/* Fetch the caller's full /_whoami document (uid/username/is_admin + the
 * `namespaces` array of {path,role}). The caller owns and frees the doc; NULL
 * on failure. Used by the repo-create namespace picker and the groups area. */
static struct json_doc_ptr_result whoami_doc(struct loop *loop, const struct serve_ud *sud, const char *sid)
{
    if (!sid || !*sid) return PICOMESH_OK(json_doc_ptr, NULL);
    struct http_response resp;
    struct picomesh_int_result post = http_post(loop, &sud->gw, "/_whoami", "application/json", NULL, sid, "", 0, &resp);
    if (PICOMESH_IS_ERR(post)) { http_response_free(&resp); return PICOMESH_ERR(json_doc_ptr, "whoami_doc: /_whoami POST failed", post); }
    struct json_doc *doc = resp.body ? json_parse(resp.body, resp.body_len) : NULL;
    http_response_free(&resp);
    return PICOMESH_OK(json_doc_ptr, doc);
}

/* Invoke an /_rpc mutation. Returns 1 on a `result`, 0 on an `error` (or
 * transport failure), copying the gateway error message into `errbuf` so the
 * caller can SHOW a failed grant/revoke/create rather than silently redirecting
 * as if it succeeded. */
static struct picomesh_int_result rpc_invoke(struct loop *loop, const struct serve_ud *sud, const char *sid,
                      const char *rpc_path, const char *args_json, char *errbuf, size_t errcap)
{
    if (errbuf && errcap) snprintf(errbuf, errcap, "the action could not be completed");
    size_t need = strlen(rpc_path) + strlen(args_json) + 32;
    char *body = malloc(need);
    if (!body) return PICOMESH_ERR(picomesh_int, "rpc_invoke: out of memory");
    int n = snprintf(body, need, "{\"path\":\"%s\",\"args\":%s}", rpc_path, args_json);
    if (n <= 0 || (size_t)n >= need) { free(body); return PICOMESH_ERR(picomesh_int, "rpc_invoke: request body format failed"); }
    struct http_response resp;
    struct picomesh_int_result post = http_post_json(loop, &sud->gw, "/_rpc", NULL, sid, body, (size_t)n, &resp);
    free(body);
    if (PICOMESH_IS_ERR(post)) { http_response_free(&resp); return PICOMESH_ERR(picomesh_int, "rpc_invoke: gateway POST failed", post); }
    int ok = 0;
    if (resp.body) {
        struct json_doc *doc = json_parse(resp.body, resp.body_len);
        if (doc) {
            const struct json_value *root = json_doc_root(doc);
            if (json_object_get(root, "result")) {
                ok = 1;
            } else {
                const struct json_value *err = json_object_get(root, "error");
                const char *msg = err ? json_as_string(json_object_get(err, "message"), NULL) : NULL;
                if (msg && errbuf && errcap) snprintf(errbuf, errcap, "%s", msg);
            }
            json_doc_free(doc);
        }
    }
    http_response_free(&resp);
    return PICOMESH_OK(picomesh_int, ok);
}

/* Render a small "action failed" page with the gateway's error and a link back,
 * so a failed RBAC mutation is visible instead of a silent redirect. */
static void render_action_error(struct loop_stream *s, const char *uname, int is_admin,
                                const char *msg, const char *back_url, int keep_alive)
{
    struct buf b; buf_init(&b);
    render_shell_open(&b, "Action failed", uname, is_admin ? "admin-namespaces" : "projects", is_admin);
    render_page_header(&b, "Action failed", "The requested change was not applied.", NULL);
    panel_open(&b, "Error", NULL);
    buf_puts(&b, "<p>");
    buf_esc(&b, msg && *msg ? msg : "the action could not be completed");
    buf_puts(&b, "</p><p class=\"muted\"><a href=\"");
    buf_esc(&b, back_url);
    buf_puts(&b, "\">\xe2\x86\x90 Back</a></p>");
    panel_close(&b);
    render_shell_close(s, &b, keep_alive);
}

/* True for a role at or above `developer` on the ladder (can create repos in a
 * namespace); and a separate check for maintainer+ (can manage a namespace). */
static int role_at_least(const char *role, const char *floor)
{
    static const char *ladder[] = {"guest", "reporter", "developer", "maintainer", "owner"};
    int role_index = -1, floor_index = -1;
    for (int i = 0; i < 5; ++i) {
        if (role && strcmp(role, ladder[i]) == 0) role_index = i;
        if (floor && strcmp(floor, ladder[i]) == 0) floor_index = i;
    }
    return role_index >= 0 && floor_index >= 0 && role_index >= floor_index;
}

/* Populate the active-service set from the gateway's /_describe (the list
 * of {service, source} objects). Cached after the first successful fetch.
 * /_describe answers GET or POST; we POST an empty body. On failure the
 * set stays empty and pages fail closed (404) — the safe default. */
static struct picomesh_void_result services_ensure(struct loop *loop, struct serve_ud *sud)
{
    time_t now = time(NULL);
    if (sud->services.loaded &&
        (now - sud->services.loaded_at) < WEBAPP_SERVICES_TTL_SEC)
        return PICOMESH_OK_VOID();
    struct http_response resp;
    struct picomesh_int_result post = http_post(loop, &sud->gw, "/_describe",
                       "application/json", NULL, NULL, "", 0, &resp);
    /* Transport failure → keep the previous set (fail to the last known
     * topology rather than dropping every page). This is an explicit decision
     * that the error is recoverable; log it, then continue with the cache. */
    if (PICOMESH_IS_ERR(post)) {
        picomesh_error_print(stderr, "services_ensure: /_describe", post.error);
        picomesh_error_destroy(post.error);
        http_response_free(&resp);
        return PICOMESH_OK_VOID();
    }
    if (resp.body) {
        struct json_doc *doc = json_parse(resp.body, resp.body_len);
        if (doc) {
            const struct json_value *root = json_doc_root(doc);
            const struct json_value *svcs = json_object_get(root, "services");
            size_t cnt = svcs ? json_array_size(svcs) : 0;
            /* Rebuild from scratch — a refresh can add OR drop services. */
            sud->services.n = 0;
            for (size_t i = 0; i < cnt &&
                 sud->services.n < sizeof(sud->services.names) / sizeof(sud->services.names[0]);
                 ++i) {
                const struct json_value *svc_entry = json_array_at(svcs, i);
                const char *svc_name = json_as_string(json_object_get(svc_entry, "service"), NULL);
                const char *svc_source = json_as_string(json_object_get(svc_entry, "source"), NULL);
                if (svc_name && *svc_name) {
                    size_t slot = sud->services.n++;
                    snprintf(sud->services.names[slot], 64, "%s", svc_name);
                    snprintf(sud->services.sources[slot], 32, "%s", svc_source && *svc_source ? svc_source : "-");
                }
            }
            sud->services.loaded = 1;
            sud->services.loaded_at = now;
            json_doc_free(doc);
        }
    }
    http_response_free(&resp);
    return PICOMESH_OK_VOID();
}

/* Force the next services_ensure to re-fetch (e.g. when the admin opens the
 * Services page and wants the live roster, not a cached one). */
static void services_invalidate(struct serve_ud *sud)
{
    sud->services.loaded = 0;
    sud->services.loaded_at = 0;
}

static int service_active(const struct serve_ud *sud, const char *name)
{
    for (size_t i = 0; i < sud->services.n; ++i)
        if (strcmp(sud->services.names[i], name) == 0) return 1;
    return 0;
}

/* Shared page chrome — every data page opens with the same shell (via
 * render_shell_open) and closes with render_shell_close, so the app is one
 * whole UI rather than disconnected stubs. */
static void page_open(struct buf *b, const char *title, const char *uname,
                      const char *active_nav, int is_admin)
{
    render_shell_open(b, title, uname, active_nav, is_admin);
}
static void page_close_and_send(struct loop_stream *s, struct buf *b, int keep_alive)
{
    render_shell_close(s, b, keep_alive);
}

/* GET /<account> — account landing: the namespace's repositories listed by
 * name (git_repo.git_repo.list_for_namespace) + the count. */
static struct picomesh_void_result page_account_landing(struct loop *loop, struct loop_stream *s,
                                 const struct serve_ud *sud, const char *sid,
                                 const char *uname, const char *acct, int is_admin,
                                 int keep_alive)
{
    /* Namespace-based discovery (issue #30): list the repos owned by the
     * namespace PATH, so a group's repos appear on its namespace page — not just
     * a personal owner's. */
    char acct_esc[160], args[200];
    if (!json_escape(acct_esc, sizeof(acct_esc), acct)) acct_esc[0] = 0;
    snprintf(args, sizeof(args), "[\"%s\"]", acct_esc);
    struct picomesh_int64_result repos_r = rpc_result_int(loop, sud, sid, "git_repo.git_repo.count_for_namespace", args, -1);
    if (PICOMESH_IS_ERR(repos_r)) { picomesh_error_print(stderr, "webapp: count_for_namespace", repos_r.error); picomesh_error_destroy(repos_r.error); }
    long repos = PICOMESH_IS_OK(repos_r) ? repos_r.value : -1;
    struct picomesh_string_result names_r = rpc_result_str(loop, sud, sid, "git_repo.git_repo.list_for_namespace", args, NULL);
    if (PICOMESH_IS_ERR(names_r)) { picomesh_error_print(stderr, "webapp: list_for_namespace", names_r.error); picomesh_error_destroy(names_r.error); }
    char *names = PICOMESH_IS_OK(names_r) ? names_r.value : NULL;

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "%s", acct);
    page_open(&b, title, uname, "projects", is_admin);
    render_page_header(&b, acct, "Account namespace and its repositories.", NULL);
    char meta[48];
    if (repos >= 0) snprintf(meta, sizeof(meta), "%ld owned", repos);
    else            meta[0] = 0;
    panel_open(&b, "Repositories", meta[0] ? meta : NULL);
    int any = 0;
    if (names && *names) {
        buf_puts(&b, "<table class=\"file-table\"><tbody>");
        char *save = NULL;
        for (char *line = strtok_r(names, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!*line) continue;
            any = 1;
            buf_puts(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td><td><a class=\"file-name dir\" href=\"/");
            buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "/-/tree\">"); buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "</a></td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    if (!any) buf_puts(&b, "<p class=\"muted\">No repositories.</p>");
    panel_close(&b);
    free(names);
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /<account>/<repo>/issues[?status=open|closed] — open-issue count for
 * the repo, plus the new/close action forms. Data: issues.issues.count_open_in_repo. */
static struct picomesh_void_result page_repo_issues(struct loop *loop, struct loop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *uname, const char *acct, const char *repo,
                             const char *full_path, int is_admin, int keep_alive)
{
    (void)full_path;
    uint32_t rid = repo_hash(acct, repo);
    char args[48];
    snprintf(args, sizeof(args), "[%u]", rid);
    struct picomesh_int64_result open_n_rr = rpc_result_int(loop, sud, sid, "issues.issues.count_open_in_repo", args, -1);
    if (PICOMESH_IS_ERR(open_n_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", open_n_rr.error); picomesh_error_destroy(open_n_rr.error); }
    long open_n = PICOMESH_IS_OK(open_n_rr) ? open_n_rr.value : (-1);

    long run_count = -1;
    if (service_active(sud, "git_pipeline")) {
        struct picomesh_int64_result pending_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
        if (PICOMESH_IS_ERR(pending_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", pending_rr.error); picomesh_error_destroy(pending_rr.error); }
        long pending = PICOMESH_IS_OK(pending_rr) ? pending_rr.value : (0);
        struct picomesh_int64_result running_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
        if (PICOMESH_IS_ERR(running_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", running_rr.error); picomesh_error_destroy(running_rr.error); }
        long running = PICOMESH_IS_OK(running_rr) ? running_rr.value : (0);
        run_count = pending + running;
    }

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "Issues — %s/%s", acct, repo);
    page_open(&b, title, uname, "projects", is_admin);
    render_project_header(&b, acct, repo, rid);
    render_project_tabs(&b, acct, repo, "issues", open_n, run_count);

    /* Issues work queue. List API isn't available yet, so the rows are an
     * empty/summary state — but the panel header, filters, .issue-list and
     * the New issue action are the final shape. */
    buf_puts(&b, "<section class=\"panel\"><header class=\"panel-header\"><div><strong>Issues</strong> ");
    if (open_n >= 0) buf_printf(&b, "<span class=\"muted\">%ld open</span>", open_n);
    else             buf_puts(&b, "<span class=\"muted\">service unreachable</span>");
    buf_puts(&b, "</div><div class=\"panel-actions\"><form method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "/-/issues/new\"><button class=\"primary\" type=\"submit\">New issue</button></form></div></header>");

    buf_puts(&b, "<div class=\"filters\"><a class=\"active\" href=\"?status=open\">Open</a>"
                 "<a href=\"?status=closed\">Closed</a></div>");

    buf_puts(&b, "<ul class=\"issue-list\">");
    if (open_n > 0) {
        buf_printf(&b, "<li class=\"issue-row\"><div class=\"issue-main\">"
                       "<span class=\"issue-title\">%ld open issue%s</span>"
                       "<div class=\"issue-meta\">per-issue rows arrive with the list API</div>"
                       "</div><span class=\"badge open\">open</span></li>",
                   open_n, open_n == 1 ? "" : "s");
    } else {
        buf_puts(&b, "<li class=\"issue-row empty\"><div class=\"issue-main\">"
                     "<span class=\"muted\">No open issues.</span></div></li>");
    }
    buf_puts(&b, "</ul>");

    /* Close-by-id, until per-row close actions exist. */
    buf_puts(&b, "<div class=\"panel-body\"><form class=\"inline-form\" method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "/-/issues/close\"><label>Close issue <input type=\"number\" name=\"issue_id\" required></label>"
                 "<button type=\"submit\">Close</button></form></div>");
    buf_puts(&b, "</section>");
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /<account>/<repo>/runs — pipeline run counts + enqueue/lease forms.
 * Data: git_pipeline.git_pipeline.count_pending/running/done (global counts today). */
static struct picomesh_void_result page_repo_runs(struct loop *loop, struct loop_stream *s,
                           const struct serve_ud *sud, const char *sid,
                           const char *uname, const char *acct, const char *repo,
                           const char *full_path, int is_admin, int keep_alive)
{
    (void)full_path;
    struct picomesh_int64_result pending_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
    if (PICOMESH_IS_ERR(pending_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", pending_rr.error); picomesh_error_destroy(pending_rr.error); }
    long pending = PICOMESH_IS_OK(pending_rr) ? pending_rr.value : (0);
    struct picomesh_int64_result running_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
    if (PICOMESH_IS_ERR(running_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", running_rr.error); picomesh_error_destroy(running_rr.error); }
    long running = PICOMESH_IS_OK(running_rr) ? running_rr.value : (0);
    struct picomesh_int64_result done_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_done",    "[]", 0);
    if (PICOMESH_IS_ERR(done_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", done_rr.error); picomesh_error_destroy(done_rr.error); }
    long done = PICOMESH_IS_OK(done_rr) ? done_rr.value : (0);

    uint32_t rid = repo_hash(acct, repo);
    long issue_count = -1;
    if (service_active(sud, "issues")) {
        char issue_args[48]; snprintf(issue_args, sizeof(issue_args), "[%u]", rid);
        struct picomesh_int64_result issue_rr = rpc_result_int(loop, sud, sid, "issues.issues.count_open_in_repo", issue_args, -1);
        if (PICOMESH_IS_ERR(issue_rr)) { picomesh_error_print(stderr, "webapp: count_open_in_repo", issue_rr.error); picomesh_error_destroy(issue_rr.error); }
        else issue_count = issue_rr.value;
    }

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "Pipelines — %s/%s", acct, repo);
    page_open(&b, title, uname, "projects", is_admin);
    render_project_header(&b, acct, repo, rid);
    render_project_tabs(&b, acct, repo, "runs", issue_count, pending + running);

    /* Pipeline runs. No per-run list API yet, so the rows summarize the
     * state counts — but the panel header, .pipeline-table and the Run
     * pipeline action are the final shape. */
    buf_puts(&b, "<section class=\"panel\"><header class=\"panel-header\"><div>"
                 "<strong>Pipeline runs</strong> <span class=\"muted\">queued, running, finished</span>"
                 "</div><div class=\"panel-actions\"><form method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "/-/runs/new\"><button class=\"primary\" type=\"submit\">Run pipeline</button></form></div></header>");
    buf_printf(&b,
        "<table class=\"pipeline-table\"><thead><tr>"
        "<th>Status</th><th>Run</th><th>Ref</th><th>Runner</th><th>Count</th></tr></thead><tbody>"
        "<tr><td><span class=\"badge queued\">queued</span></td><td class=\"muted\">-</td>"
        "<td><code>main</code></td><td class=\"muted\">-</td><td>%ld</td></tr>"
        "<tr><td><span class=\"badge running\">running</span></td><td class=\"muted\">-</td>"
        "<td><code>main</code></td><td class=\"muted\">-</td><td>%ld</td></tr>"
        "<tr><td><span class=\"badge succeeded\">finished</span></td><td class=\"muted\">-</td>"
        "<td><code>main</code></td><td class=\"muted\">-</td><td>%ld</td></tr>"
        "</tbody></table>", pending, running, done);
    /* Lease action for a runner, until per-run controls exist. */
    buf_puts(&b, "<div class=\"panel-body\"><form class=\"inline-form\" method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "/runs/lease\"><label>Lease next job — runner uid "
                 "<input type=\"number\" name=\"runner\" value=\"1\"></label>"
                 "<button type=\"submit\">Lease</button></form></div>");
    buf_puts(&b, "</section>");
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* Render a single stat tile into an open .stats-grid. `value` < 0 → em-dash. */
static void stat_tile(struct buf *b, long value, const char *label)
{
    buf_puts(b, "<div class=\"stat\"><span class=\"stat-value\">");
    if (value >= 0) buf_printf(b, "%ld", value); else buf_puts(b, "\xe2\x80\x94");
    buf_puts(b, "</span><span class=\"stat-label\">");
    buf_esc(b, label);
    buf_puts(b, "</span></div>");
}

/* GET /admin — admin area landing: a deployment overview (counts across
 * every aspect) + quick links into the admin pages. */
static struct picomesh_void_result page_admin_overview(struct loop *loop, struct loop_stream *s,
                                const struct serve_ud *sud, const char *sid,
                                const char *uname, int keep_alive)
{
    long users = -1;
    if (service_active(sud, "accounts")) {
        struct picomesh_int64_result users_rr = rpc_result_int(loop, sud, sid, "accounts.accounts.count", "[]", -1);
        if (PICOMESH_IS_ERR(users_rr)) { picomesh_error_print(stderr, "webapp: accounts.count", users_rr.error); picomesh_error_destroy(users_rr.error); }
        else users = users_rr.value;
    }
    long repos = -1;
    if (service_active(sud, "git_repo")) {
        struct picomesh_int64_result repos_rr = rpc_result_int(loop, sud, sid, "git_repo.git_repo.count_total", "[]", -1);
        if (PICOMESH_IS_ERR(repos_rr)) { picomesh_error_print(stderr, "webapp: count_total", repos_rr.error); picomesh_error_destroy(repos_rr.error); }
        else repos = repos_rr.value;
    }
    long token_count = -1;
    if (service_active(sud, "personal_access_tokens")) {
        struct picomesh_int64_result tok_rr = rpc_result_int(loop, sud, sid, "personal_access_tokens.personal_access_tokens.count_active", "[]", -1);
        if (PICOMESH_IS_ERR(tok_rr)) { picomesh_error_print(stderr, "webapp: pat.count_active", tok_rr.error); picomesh_error_destroy(tok_rr.error); }
        else token_count = tok_rr.value;
    }
    long runs = -1;
    if (service_active(sud, "git_pipeline")) {
        struct picomesh_int64_result pending_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
        if (PICOMESH_IS_ERR(pending_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", pending_rr.error); picomesh_error_destroy(pending_rr.error); }
        long pending = PICOMESH_IS_OK(pending_rr) ? pending_rr.value : (0);
        struct picomesh_int64_result running_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
        if (PICOMESH_IS_ERR(running_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", running_rr.error); picomesh_error_destroy(running_rr.error); }
        long running = PICOMESH_IS_OK(running_rr) ? running_rr.value : (0);
        struct picomesh_int64_result done_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_done",    "[]", 0);
        if (PICOMESH_IS_ERR(done_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", done_rr.error); picomesh_error_destroy(done_rr.error); }
        long done = PICOMESH_IS_OK(done_rr) ? done_rr.value : (0);
        runs = pending + running + done;
    }

    struct buf b; buf_init(&b);
    render_shell_open(&b, "Admin", uname, "admin-overview", /*is_admin=*/1);
    render_page_header(&b, "Admin", "Operational overview of this picoforge deployment.", NULL);
    buf_puts(&b, "<section class=\"stats-grid\">");
    stat_tile(&b, users, "users");
    stat_tile(&b, repos, "repositories");
    stat_tile(&b, token_count,  "active tokens");
    stat_tile(&b, (long)sud->services.n, "services");
    stat_tile(&b, runs,  "pipeline runs");
    buf_puts(&b, "</section>");

    panel_open(&b, "Manage", NULL);
    buf_puts(&b, "<table class=\"file-table\"><tbody>"
                 "<tr><td><a class=\"file-name\" href=\"/-/admin/users\">Users</a></td>"
                 "<td class=\"muted\">accounts &amp; registration</td></tr>"
                 "<tr><td><a class=\"file-name\" href=\"/-/admin/repos\">Repositories</a></td>"
                 "<td class=\"muted\">all repositories in the mesh</td></tr>"
                 "<tr><td><a class=\"file-name\" href=\"/-/admin/tokens\">Tokens</a></td>"
                 "<td class=\"muted\">personal access tokens</td></tr>"
                 "<tr><td><a class=\"file-name\" href=\"/-/admin/services\">Services</a></td>"
                 "<td class=\"muted\">mesh service health</td></tr>"
                 "</tbody></table>");
    panel_close(&b);
    render_shell_close(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /admin/users — accounts administration. Data: accounts.accounts.count. */
static struct picomesh_void_result page_admin_users(struct loop *loop, struct loop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *uname, int keep_alive)
{
    struct picomesh_int64_result users_rr = rpc_result_int(loop, sud, sid, "accounts.accounts.count", "[]", -1);
    if (PICOMESH_IS_ERR(users_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", users_rr.error); picomesh_error_destroy(users_rr.error); }
    long users = PICOMESH_IS_OK(users_rr) ? users_rr.value : (-1);
    /* The real user roster: a JSON array of {"uid":<n>,"name":"<s>"}. */
    const struct json_value *roster = NULL;
    struct json_doc_ptr_result roster_doc_rr = rpc_result_doc(loop, sud, sid, "accounts.accounts.list", "[]", &roster);
    if (PICOMESH_IS_ERR(roster_doc_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_doc", roster_doc_rr.error); picomesh_error_destroy(roster_doc_rr.error); }
    struct json_doc *roster_doc = PICOMESH_IS_OK(roster_doc_rr) ? roster_doc_rr.value : NULL;

    struct buf b; buf_init(&b);
    page_open(&b, "Users", uname, "admin-users", /*is_admin=*/1);
    render_page_header(&b, "Users", "Registered accounts on this deployment.", NULL);
    buf_puts(&b, "<section class=\"stats-grid\">");
    stat_tile(&b, users, "users");
    buf_puts(&b, "</section>");

    panel_open(&b, "Registered users", NULL);
    if (users < 0) {
        buf_puts(&b, "<p class=\"muted\">accounts service unreachable.</p>");
    } else {
        int any = 0;
        buf_puts(&b, "<table class=\"file-table\"><thead><tr><th>uid</th>"
                     "<th>username</th></tr></thead><tbody>");
        size_t roster_n = roster ? json_array_size(roster) : 0;
        for (size_t i = 0; i < roster_n; ++i) {
            const struct json_value *roster_entry = json_array_at(roster, i);
            /* accounts.accounts.list rows are {"uid":N,"username":"…"} (the
             * relational `SELECT uid,username FROM users` column names). The
             * key is "username", not "name" — reading "name" left every row
             * blank, so the roster never showed the registered users. */
            const char *name = json_as_string(json_object_get(roster_entry, "username"), "");
            char uid_buf[24];
            snprintf(uid_buf, sizeof(uid_buf), "%lld",
                     (long long)json_as_int(json_object_get(roster_entry, "uid"), 0));
            any = 1;
            buf_puts(&b, "<tr><td class=\"muted\">");
            buf_esc(&b, uid_buf);
            buf_puts(&b, "</td><td>");
            buf_esc(&b, name);
            buf_puts(&b, "</td></tr>");
        }
        if (!any)
            buf_puts(&b, "<tr><td colspan=\"2\" class=\"muted\">No users registered yet.</td></tr>");
        buf_puts(&b, "</tbody></table>");
    }
    buf_puts(&b, "<p class=\"muted small\">Users are created by signing up at "
                 "<a href=\"/-/register\">/register</a> (username + password). Admin-side "
                 "user creation with credentials and role assignment is not implemented "
                 "yet.</p>");
    panel_close(&b);
    json_doc_free(roster_doc);
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /admin/repos — repositories administration. Data:
 * git_repo.git_repo.count_total (no global list API yet). */
static struct picomesh_void_result page_admin_repos(struct loop *loop, struct loop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *uname, int keep_alive)
{
    struct picomesh_int64_result total_rr = rpc_result_int(loop, sud, sid, "git_repo.git_repo.count_total", "[]", -1);
    if (PICOMESH_IS_ERR(total_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", total_rr.error); picomesh_error_destroy(total_rr.error); }
    long total = PICOMESH_IS_OK(total_rr) ? total_rr.value : (-1);

    struct buf b; buf_init(&b);
    page_open(&b, "Repositories", uname, "admin-repos", /*is_admin=*/1);
    render_page_header(&b, "Repositories", "All repositories across the mesh.", NULL);
    buf_puts(&b, "<section class=\"stats-grid\">");
    stat_tile(&b, total, "repositories");
    buf_puts(&b, "</section>");
    panel_open(&b, "Repositories", NULL);
    buf_puts(&b, "<p class=\"muted\">A mesh-wide repository listing arrives with the "
                 "<code>git_repo.git_repo.list_all</code> API; today only the total is "
                 "exposed. Browse a specific account's repositories from its namespace "
                 "page (e.g. <code>/&lt;account&gt;</code>).</p>");
    panel_close(&b);
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /admin/tokens — personal access token administration. Data:
 * personal_access_tokens.personal_access_tokens.count_active. */
static struct picomesh_void_result page_admin_tokens(struct loop *loop, struct loop_stream *s,
                              const struct serve_ud *sud, const char *sid,
                              const char *uname, int keep_alive)
{
    int active = service_active(sud, "personal_access_tokens");
    long token_count = -1;
    if (active) {
        struct picomesh_int64_result tok_rr = rpc_result_int(loop, sud, sid, "personal_access_tokens.personal_access_tokens.count_active", "[]", -1);
        if (PICOMESH_IS_ERR(tok_rr)) { picomesh_error_print(stderr, "webapp: pat.count_active", tok_rr.error); picomesh_error_destroy(tok_rr.error); }
        else token_count = tok_rr.value;
    }

    struct buf b; buf_init(&b);
    page_open(&b, "Tokens", uname, "admin-tokens", /*is_admin=*/1);
    render_page_header(&b, "Tokens", "Personal access tokens issued by the gateway.", NULL);
    buf_puts(&b, "<section class=\"stats-grid\">");
    stat_tile(&b, token_count, "active tokens");
    buf_puts(&b, "</section>");
    panel_open(&b, "Mint personal access token", NULL);
    if (!active) buf_puts(&b, "<p class=\"muted\">PAT service not active.</p>");
    buf_puts(&b, "<form class=\"form-grid\" method=\"post\" action=\"/-/admin/tokens/mint_pat\">"
                 "<label>uid <input type=\"number\" name=\"uid\" value=\"100\"></label>"
                 "<button type=\"submit\" class=\"primary\">Mint</button></form>");
    panel_close(&b);
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* ---- admin: RBAC namespace management (issue #30) ---------------------- *
 *
 * The site-admin RBAC surface. /admin/namespaces lists every namespace and
 * creates group namespaces; /admin/namespaces/<path> shows a namespace's role
 * memberships and grants/revokes them. Everything is sourced from and mutated
 * through the gateway /_rpc (accounts.ns_list / ns_members / ns_create /
 * ns_add_member / ns_remove_member) — the webapp holds no plugins.            */

/* GET /admin/namespaces — the namespace tree + a create-group form. */
static struct picomesh_void_result page_admin_namespaces(struct loop *loop, struct loop_stream *s,
                                  const struct serve_ud *sud, const char *sid,
                                  const char *uname, int keep_alive)
{
    const struct json_value *list = NULL;
    struct json_doc_ptr_result doc_rr = rpc_result_doc(loop, sud, sid, "accounts.accounts.ns_list", "[]", &list);
    if (PICOMESH_IS_ERR(doc_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_doc", doc_rr.error); picomesh_error_destroy(doc_rr.error); }
    struct json_doc *doc = PICOMESH_IS_OK(doc_rr) ? doc_rr.value : NULL;
    size_t n = list ? json_array_size(list) : 0;

    struct buf b; buf_init(&b);
    page_open(&b, "Namespaces", uname, "admin-namespaces", /*is_admin=*/1);
    render_page_header(&b, "Namespaces",
                       "RBAC namespaces — personal and group — and their role memberships.", NULL);
    buf_puts(&b, "<section class=\"stats-grid\">");
    stat_tile(&b, doc ? (long)n : -1, "namespaces");
    buf_puts(&b, "</section>");

    panel_open(&b, "Namespaces", NULL);
    if (!doc) {
        buf_puts(&b, "<p class=\"muted\">accounts service unreachable.</p>");
    } else {
        buf_puts(&b, "<table class=\"file-table\"><thead><tr><th>Path</th><th>Kind</th>"
                     "<th>Owner uid</th><th></th></tr></thead><tbody>");
        for (size_t i = 0; i < n; ++i) {
            const struct json_value *ns_entry = json_array_at(list, i);
            const char *ns_path = json_as_string(json_object_get(ns_entry, "path"), "");
            const char *kind = json_as_string(json_object_get(ns_entry, "kind"), "");
            long owner = (long)json_as_int(json_object_get(ns_entry, "owner_uid"), 0);
            buf_puts(&b, "<tr><td><a class=\"file-name\" href=\"/-/admin/namespaces/");
            buf_esc(&b, ns_path);
            buf_puts(&b, "\">");
            buf_esc(&b, ns_path);
            buf_puts(&b, "</a></td><td>");
            buf_esc(&b, kind);
            buf_puts(&b, "</td><td class=\"muted\">");
            buf_printf(&b, "%ld", owner);
            buf_puts(&b, "</td><td><a href=\"/-/admin/namespaces/");
            buf_esc(&b, ns_path);
            buf_puts(&b, "\">manage</a></td></tr>");
        }
        if (n == 0)
            buf_puts(&b, "<tr><td colspan=\"4\" class=\"muted\">No namespaces yet.</td></tr>");
        buf_puts(&b, "</tbody></table>");
    }
    panel_close(&b);

    panel_open(&b, "Create group namespace", NULL);
    buf_puts(&b, "<form class=\"form-grid\" method=\"post\" action=\"/-/admin/namespaces/create\">"
                 "<label>Slug <input type=\"text\" name=\"slug\" placeholder=\"acme\"></label>"
                 "<label>Parent path <input type=\"text\" name=\"parent\" placeholder=\"(empty = root group)\"></label>"
                 "<button type=\"submit\" class=\"primary\">Create group</button></form>"
                 "<p class=\"muted small\">A root group has no parent; a subgroup names its "
                 "parent path (e.g. parent <code>acme</code> &rarr; <code>acme/platform</code>). "
                 "You become its owner. Personal namespaces are created automatically at sign-up.</p>");
    panel_close(&b);
    json_doc_free(doc);
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /admin/namespaces/<path> — a namespace's members + grant/revoke forms. */
/* Render a namespace's Members panel + Grant-a-role + Create-subgroup forms
 * into `b`. The forms post to <base>/{remove_member,add_member,create}, so the
 * same UI backs the site-admin (/admin/namespaces) and group-owner (/groups)
 * surfaces. */
static struct picomesh_void_result render_namespace_members(struct buf *b, struct loop *loop,
                                     const struct serve_ud *sud, const char *sid,
                                     const char *nspath, const char *base)
{
    char esc[256], args[300];
    if (!json_escape(esc, sizeof(esc), nspath)) esc[0] = 0;
    snprintf(args, sizeof(args), "[\"%s\"]", esc);
    const struct json_value *members = NULL;
    /* ns_members carries the username (joined server-side), so the page needs no
     * site-admin roster fetch — it works for ordinary namespace maintainers. */
    struct json_doc_ptr_result mdoc_rr = rpc_result_doc(loop, sud, sid, "accounts.accounts.ns_members", args, &members);
    if (PICOMESH_IS_ERR(mdoc_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_doc", mdoc_rr.error); picomesh_error_destroy(mdoc_rr.error); }
    struct json_doc *mdoc = PICOMESH_IS_OK(mdoc_rr) ? mdoc_rr.value : NULL;

    panel_open(b, "Members", NULL);
    if (!mdoc || !members) {
        buf_puts(b, "<p class=\"muted\">members are not available (you may not manage this namespace).</p>");
    } else {
        buf_puts(b, "<table class=\"file-table\"><thead><tr><th>User</th><th>uid</th>"
                    "<th>Role</th><th></th></tr></thead><tbody>");
        size_t member_count = json_array_size(members);
        for (size_t i = 0; i < member_count; ++i) {
            const struct json_value *member_entry = json_array_at(members, i);
            long member_uid = (long)json_as_int(json_object_get(member_entry, "uid"), 0);
            const char *role = json_as_string(json_object_get(member_entry, "role"), "");
            const char *username = json_as_string(json_object_get(member_entry, "username"), NULL);
            buf_puts(b, "<tr><td>");
            buf_esc(b, username ? username : "(unknown)");
            buf_puts(b, "</td><td class=\"muted\">");
            buf_printf(b, "%ld", member_uid);
            buf_puts(b, "</td><td>");
            buf_esc(b, role);
            buf_puts(b, "</td><td><form method=\"post\" action=\"");
            buf_esc(b, base);
            buf_puts(b, "/remove_member\" style=\"display:inline\"><input type=\"hidden\" name=\"path\" value=\"");
            buf_esc(b, nspath);
            buf_puts(b, "\"><input type=\"hidden\" name=\"uid\" value=\"");
            buf_printf(b, "%ld", member_uid);
            buf_puts(b, "\"><button type=\"submit\">revoke</button></form></td></tr>");
        }
        if (member_count == 0)
            buf_puts(b, "<tr><td colspan=\"4\" class=\"muted\">No members.</td></tr>");
        buf_puts(b, "</tbody></table>");
    }
    panel_close(b);

    panel_open(b, "Grant a role", NULL);
    /* A username field, not a roster dropdown — a maintainer who can't list all
     * users (that is site-admin only) can still grant by name. The webapp maps
     * the name to its uid; the backend rejects an unregistered name. */
    buf_puts(b, "<form class=\"form-grid\" method=\"post\" action=\"");
    buf_esc(b, base);
    buf_puts(b, "/add_member\"><input type=\"hidden\" name=\"path\" value=\"");
    buf_esc(b, nspath);
    buf_puts(b, "\"><label>Username <input type=\"text\" name=\"username\" placeholder=\"alice\" required></label>"
                "<label>Role <select name=\"role\">"
                "<option>guest</option><option>reporter</option>"
                "<option selected>developer</option><option>maintainer</option>"
                "<option>owner</option></select></label>"
                "<button type=\"submit\" class=\"primary\">Grant</button></form>");
    panel_close(b);

    panel_open(b, "Create subgroup", NULL);
    buf_puts(b, "<form class=\"form-grid\" method=\"post\" action=\"");
    buf_esc(b, base);
    buf_puts(b, "/create\"><input type=\"hidden\" name=\"parent\" value=\"");
    buf_esc(b, nspath);
    buf_puts(b, "\"><label>Slug <input type=\"text\" name=\"slug\" placeholder=\"backend\"></label>"
                "<button type=\"submit\" class=\"primary\">Create subgroup</button></form>");
    panel_close(b);

    json_doc_free(mdoc);
    return PICOMESH_OK_VOID();
}

/* GET /admin/namespaces/<path> — a namespace's members + grant/revoke forms. */
static struct picomesh_void_result page_admin_namespace(struct loop *loop, struct loop_stream *s,
                                 const struct serve_ud *sud, const char *sid,
                                 const char *uname, const char *nspath, int keep_alive)
{
    struct buf b; buf_init(&b);
    char title[160]; snprintf(title, sizeof(title), "Namespace \xc2\xb7 %s", nspath);
    page_open(&b, title, uname, "admin-namespaces", /*is_admin=*/1);
    render_page_header(&b, nspath, "Role memberships for this namespace. Roles inherit to "
                                   "child namespaces.", NULL);
    buf_puts(&b, "<p class=\"muted small\"><a href=\"/-/admin/namespaces\">\xe2\x86\x90 All namespaces</a></p>");
    render_namespace_members(&b, loop, sud, sid, nspath, "/-/admin/namespaces");
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* The caller's effective role on `nspath` per their /_whoami namespaces
 * (inherited from ancestors), or NULL if none. */
static const char *whoami_role_on(const struct json_value *nss, const char *nspath)
{
    const char *best = NULL;
    size_t n = nss ? json_array_size(nss) : 0;
    /* Walk the path and its ancestors; the highest membership wins. */
    char prefix[256];
    for (size_t plen = strlen(nspath); plen > 0; ) {
        if (plen < sizeof(prefix)) {
            memcpy(prefix, nspath, plen); prefix[plen] = 0;
            for (size_t i = 0; i < n; ++i) {
                const struct json_value *ns_entry = json_array_at(nss, i);
                const char *ns_path = json_as_string(json_object_get(ns_entry, "path"), NULL);
                const char *role = json_as_string(json_object_get(ns_entry, "role"), NULL);
                if (ns_path && role && strcmp(ns_path, prefix) == 0 &&
                    (!best || role_at_least(role, best)))
                    best = role;
            }
        }
        const char *slash = NULL;
        for (size_t i = plen; i > 0; --i) if (nspath[i - 1] == '/') { slash = nspath + i - 1; break; }
        if (!slash) break;
        plen = (size_t)(slash - nspath);
    }
    return best;
}

/* GET /groups — the namespaces this user can MANAGE (maintainer+), with links
 * into per-group member management. Non-admin surface for the GitLab-like model. */
static struct picomesh_void_result page_groups(struct loop *loop, struct loop_stream *s,
                        const struct serve_ud *sud, const char *sid,
                        const char *uname, int is_admin, int keep_alive)
{
    if (!uname || !*uname) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }
    struct json_doc_ptr_result who_rr = whoami_doc(loop, sud, sid);
    if (PICOMESH_IS_ERR(who_rr)) { picomesh_error_print(stderr, "webapp: whoami_doc", who_rr.error); picomesh_error_destroy(who_rr.error); }
    struct json_doc *who = PICOMESH_IS_OK(who_rr) ? who_rr.value : NULL;
    const struct json_value *nss = who ? json_object_get(json_doc_root(who), "namespaces") : NULL;

    struct buf b; buf_init(&b);
    page_open(&b, "Groups", uname, "groups", is_admin);
    render_page_header(&b, "Groups", "Namespaces you belong to. Maintainers and owners can "
                                     "also manage members and create subgroups.", NULL);
    panel_open(&b, "Your namespaces", NULL);
    buf_puts(&b, "<table class=\"file-table\"><thead><tr><th>Namespace</th><th>Your role</th>"
                 "<th></th></tr></thead><tbody>");
    int any = 0;
    size_t n = nss ? json_array_size(nss) : 0;
    for (size_t i = 0; i < n; ++i) {
        const struct json_value *ns_entry = json_array_at(nss, i);
        const char *ns_path = json_as_string(json_object_get(ns_entry, "path"), NULL);
        const char *role = json_as_string(json_object_get(ns_entry, "role"), NULL);
        if (!ns_path || !*ns_path || !role) continue;
        /* List EVERY membership, whatever the role — a member must be able to see
         * a namespace they were added to even before it has any repos. Only a
         * maintainer+ gets the management link (the detail page is gated and the
         * backend re-checks every mutation); lower roles get a browse link to the
         * namespace's repo listing. */
        int can_manage = role_at_least(role, "maintainer");
        any = 1;
        /* The namespace NAME links to its landing page (bare path); the
         * "manage" action column (below) carries the maintainer-only link. */
        buf_puts(&b, "<tr><td><a class=\"file-name\" href=\"/");
        buf_esc(&b, ns_path);
        buf_puts(&b, "\">");
        buf_esc(&b, ns_path);
        buf_puts(&b, "</a></td><td>");
        buf_esc(&b, role);
        buf_puts(&b, "</td><td>");
        if (can_manage) {
            buf_puts(&b, "<a href=\"/-/groups/");
            buf_esc(&b, ns_path);
            buf_puts(&b, "\">manage</a>");
        } else {
            buf_puts(&b, "<a href=\"/");
            buf_esc(&b, ns_path);
            buf_puts(&b, "\">browse</a>");
        }
        buf_puts(&b, "</td></tr>");
    }
    if (!any)
        buf_puts(&b, "<tr><td colspan=\"3\" class=\"muted\">You are not a member of any "
                     "namespace yet.</td></tr>");
    buf_puts(&b, "</tbody></table>");
    panel_close(&b);

    /* Create a top-level group (GitLab-style). Allowed for any signed-in user
     * unless the deployment restricts it (accounts.allow_user_root_groups) or
     * the slug is taken — either way the backend surfaces the error. */
    panel_open(&b, "Create a group", NULL);
    buf_puts(&b, "<form class=\"form-grid\" method=\"post\" action=\"/-/groups/create\">"
                 "<label>Group name <input type=\"text\" name=\"slug\" "
                 "placeholder=\"acme\" pattern=\"[A-Za-z0-9._-]{1,63}\" required></label>"
                 "<button type=\"submit\" class=\"primary\">Create group</button></form>"
                 "<p class=\"muted small\">Creates a top-level group you own. To make a "
                 "subgroup, open the parent group and use its \xe2\x80\x9c" "Create subgroup" "\xe2\x80\x9d form.</p>");
    panel_close(&b);

    /* Direct memberships are listed above; INHERITED subgroups (a maintainer of
     * `acme` also maintains `acme/platform`) can't be enumerated without the
     * site-admin tree view, so offer a path box to jump straight to one — the
     * detail page authorizes by inherited role. */
    panel_open(&b, "Manage a namespace by path", NULL);
    buf_puts(&b, "<form class=\"form-grid\" method=\"post\" action=\"/-/groups/go\">"
                 "<label>Namespace path <input type=\"text\" name=\"path\" "
                 "placeholder=\"acme/platform\" pattern=\"[A-Za-z0-9._/-]{1,128}\" required></label>"
                 "<button type=\"submit\" class=\"primary\">Manage</button></form>");
    panel_close(&b);
    json_doc_free(who);
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /groups/<path> — member management for a namespace the user maintains. */
static struct picomesh_void_result page_group_detail(struct loop *loop, struct loop_stream *s,
                              const struct serve_ud *sud, const char *sid,
                              const char *uname, const char *nspath, int is_admin, int keep_alive)
{
    if (!uname || !*uname) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }
    /* Gate the page on the caller being maintainer+ on this namespace (the
     * backend enforces it too on every mutation). */
    struct json_doc_ptr_result who_rr = whoami_doc(loop, sud, sid);
    if (PICOMESH_IS_ERR(who_rr)) { picomesh_error_print(stderr, "webapp: whoami_doc", who_rr.error); picomesh_error_destroy(who_rr.error); }
    struct json_doc *who = PICOMESH_IS_OK(who_rr) ? who_rr.value : NULL;
    const struct json_value *nss = who ? json_object_get(json_doc_root(who), "namespaces") : NULL;
    const char *role = whoami_role_on(nss, nspath);
    if (!is_admin && !(role && role_at_least(role, "maintainer"))) {
        json_doc_free(who);
        send_forbidden(s, uname, keep_alive);
        return PICOMESH_OK_VOID();
    }
    json_doc_free(who);

    struct buf b; buf_init(&b);
    char title[160]; snprintf(title, sizeof(title), "Group \xc2\xb7 %s", nspath);
    page_open(&b, title, uname, "groups", is_admin);
    render_page_header(&b, nspath, "Members and subgroups of this namespace.", NULL);
    buf_puts(&b, "<p class=\"muted small\"><a href=\"/-/groups\">\xe2\x86\x90 Your groups</a></p>");

    /* The namespace's repositories (works for NESTED groups too, which the
     * /<account> URL can't reach because /a/b parses as a repo). */
    char esc[256], args[300];
    if (!json_escape(esc, sizeof(esc), nspath)) esc[0] = 0;
    snprintf(args, sizeof(args), "[\"%s\"]", esc);
    struct picomesh_string_result names_rr = rpc_result_str(loop, sud, sid, "git_repo.git_repo.list_for_namespace", args, NULL);
    if (PICOMESH_IS_ERR(names_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_str", names_rr.error); picomesh_error_destroy(names_rr.error); }
    char *names = PICOMESH_IS_OK(names_rr) ? names_rr.value : NULL;
    panel_open(&b, "Repositories", NULL);
    int any_repo = 0;
    if (names && *names) {
        buf_puts(&b, "<table class=\"file-table\"><tbody>");
        char *save = NULL;
        for (char *line = strtok_r(names, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!*line) continue;
            any_repo = 1;
            buf_puts(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td><td><a class=\"file-name dir\" href=\"/");
            buf_esc(&b, nspath); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "/-/tree\">");
            buf_esc(&b, nspath); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "</a></td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    if (!any_repo) buf_puts(&b, "<p class=\"muted\">No repositories.</p>");
    panel_close(&b);
    free(names);

    render_namespace_members(&b, loop, sud, sid, nspath, "/-/groups");
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* POST <base>/create — create a group namespace. `base` is "/-/admin/namespaces"
 * or "/-/groups", so the same handler serves the site-admin and the group-owner
 * surfaces; the backend enforces the real authority (root creation = site
 * admin; subgroup = maintainer on parent). Failures are shown, not swallowed. */
static struct picomesh_void_result webapp_ns_create(struct loop *loop, struct loop_stream *s,
                             const struct serve_ud *sud, const char *sid, const char *base,
                             const char *body, size_t body_len, int keep_alive)
{
    struct claims claims; resolve_claims(loop, sud, sid, &claims);
    char *slug = form_get(body, body_len, "slug");
    char *parent = form_get(body, body_len, "parent");
    if (slug && *slug) {
        char eslug[160], eparent[256], args[480], err[256];
        if (json_escape(eslug, sizeof(eslug), slug) &&
            json_escape(eparent, sizeof(eparent), parent ? parent : "")) {
            snprintf(args, sizeof(args), "[0,\"group\",\"%s\",\"%s\"]", eslug, eparent);
            struct picomesh_int_result rpc_inv_1 = rpc_invoke(loop, sud, sid, "accounts.accounts.ns_create", args, err, sizeof(err));
            if (PICOMESH_IS_ERR(rpc_inv_1)) { picomesh_error_print(stderr, "webapp: rpc_invoke", rpc_inv_1.error); picomesh_error_destroy(rpc_inv_1.error); }
            if (PICOMESH_IS_ERR(rpc_inv_1) || !rpc_inv_1.value) {
                free(slug); free(parent);
                render_action_error(s, claims.username, claims.is_admin, err, base, keep_alive);
                return PICOMESH_OK_VOID();
            }
        }
    }
    free(slug); free(parent);
    send_redirect(s, base, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

/* POST <base>/add_member — grant `uid` a `role` on `path`. */
static struct picomesh_void_result webapp_ns_add_member(struct loop *loop, struct loop_stream *s,
                                 const struct serve_ud *sud, const char *sid, const char *base,
                                 const char *body, size_t body_len, int keep_alive)
{
    struct claims claims; resolve_claims(loop, sud, sid, &claims);
    char *path = form_get(body, body_len, "path");
    char *username = form_get(body, body_len, "username");
    char *role = form_get(body, body_len, "role");
    char redirect[320]; snprintf(redirect, sizeof(redirect), "%s", base);
    if (path && *path) snprintf(redirect, sizeof(redirect), "%s/%s", base, path);
    if (path && *path && username && *username && role && *role) {
        /* Resolve the typed username to its ASSIGNED uid via the gateway
         * (issue #29 — no longer hash(username)). The backend rejects an
         * unregistered name; surface "no such user" rather than passing a uid
         * that has no account. */
        char euname[96];
        uint32_t target = 0;
        if (json_escape(euname, sizeof(euname), username)) {
            char uargs[112];
            snprintf(uargs, sizeof(uargs), "[\"%s\"]", euname);
            struct picomesh_int64_result uid_res =
                rpc_result_int(loop, sud, sid, "accounts.accounts.uid_for_username", uargs, 0);
            if (PICOMESH_IS_ERR(uid_res)) {
                picomesh_error_print(stderr, "webapp: uid_for_username", uid_res.error);
                picomesh_error_destroy(uid_res.error);
            } else if (uid_res.value > 0) {
                target = (uint32_t)uid_res.value;
            }
        }
        if (!target) {
            free(path); free(username); free(role);
            render_action_error(s, claims.username, claims.is_admin, "no such user", redirect, keep_alive);
            return PICOMESH_OK_VOID();
        }
        char epath[256], erole[32], args[360], err[256];
        if (json_escape(epath, sizeof(epath), path) && json_escape(erole, sizeof(erole), role)) {
            snprintf(args, sizeof(args), "[\"%s\",%u,\"%s\"]", epath, target, erole);
            struct picomesh_int_result rpc_inv_2 = rpc_invoke(loop, sud, sid, "accounts.accounts.ns_add_member", args, err, sizeof(err));
            if (PICOMESH_IS_ERR(rpc_inv_2)) { picomesh_error_print(stderr, "webapp: rpc_invoke", rpc_inv_2.error); picomesh_error_destroy(rpc_inv_2.error); }
            if (PICOMESH_IS_ERR(rpc_inv_2) || !rpc_inv_2.value) {
                free(path); free(username); free(role);
                render_action_error(s, claims.username, claims.is_admin, err, redirect, keep_alive);
                return PICOMESH_OK_VOID();
            }
        }
    }
    free(path); free(username); free(role);
    send_redirect(s, redirect, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

/* POST <base>/remove_member — revoke `uid`'s membership on `path`. */
static struct picomesh_void_result webapp_ns_remove_member(struct loop *loop, struct loop_stream *s,
                                    const struct serve_ud *sud, const char *sid, const char *base,
                                    const char *body, size_t body_len, int keep_alive)
{
    struct claims claims; resolve_claims(loop, sud, sid, &claims);
    char *path = form_get(body, body_len, "path");
    char *uid = form_get(body, body_len, "uid");
    char redirect[320]; snprintf(redirect, sizeof(redirect), "%s", base);
    if (path && *path) snprintf(redirect, sizeof(redirect), "%s/%s", base, path);
    if (path && *path && uid && *uid) {
        char epath[256], args[320], err[256];
        if (json_escape(epath, sizeof(epath), path)) {
            snprintf(args, sizeof(args), "[\"%s\",%ld]", epath, strtol(uid, NULL, 10));
            struct picomesh_int_result rpc_inv_3 = rpc_invoke(loop, sud, sid, "accounts.accounts.ns_remove_member", args, err, sizeof(err));
            if (PICOMESH_IS_ERR(rpc_inv_3)) { picomesh_error_print(stderr, "webapp: rpc_invoke", rpc_inv_3.error); picomesh_error_destroy(rpc_inv_3.error); }
            if (PICOMESH_IS_ERR(rpc_inv_3) || !rpc_inv_3.value) {
                free(path); free(uid);
                render_action_error(s, claims.username, claims.is_admin, err, redirect, keep_alive);
                return PICOMESH_OK_VOID();
            }
        }
    }
    free(path); free(uid);
    send_redirect(s, redirect, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /<account>/<repo>/settings — repository settings overview. Read-only
 * project metadata (owner, default branch, visibility) + how to reach the
 * repo. No mutation surface yet; the tab exists so the project shell is
 * complete (Code / Issues / Pipelines / Settings). */
static struct picomesh_void_result page_repo_settings(struct loop *loop, struct loop_stream *s,
                               const struct serve_ud *sud, const char *sid,
                               const char *uname, const char *acct, const char *repo,
                               int is_admin, int keep_alive)
{
    uint32_t rid = repo_hash(acct, repo);
    long issue_count = -1, run_count = -1;
    repo_tab_counts(loop, sud, sid, rid, &issue_count, &run_count);

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "Settings — %s/%s", acct, repo);
    render_shell_open(&b, title, uname, "projects", is_admin);
    render_project_header(&b, acct, repo, rid);
    render_project_tabs(&b, acct, repo, "settings", issue_count, run_count);

    panel_open(&b, "Project", NULL);
    buf_puts(&b, "<table class=\"grid\"><tbody><tr><th>Owner</th><td>");
    buf_esc(&b, acct);
    buf_puts(&b, "</td></tr><tr><th>Repository</th><td>");
    buf_esc(&b, repo);
    buf_puts(&b, "</td></tr><tr><th>Default branch</th><td><span class=\"branch\">main</span></td></tr>"
                 "<tr><th>Visibility</th><td>public</td></tr></tbody></table>");
    panel_close(&b);

    panel_open(&b, "Access", NULL);
    buf_puts(&b, "<p>Browse this repository at <a href=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "/-/tree\">/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "</a>.</p><p class=\"muted small\">Files are served over the gateway "
                 "<code>/_rpc</code> surface; direct git clone is not wired in this build.</p>");
    panel_close(&b);

    render_shell_close(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /-/search?q=<term> — filter the signed-in user's accessible repositories
 * by name substring (case-insensitive). Uses the SAME RBAC discovery as /-/repos
 * (collect_accessible_repos: whoami → ns_subtree → list_for_namespace) so
 * group/subgroup repos visible through inherited membership are searchable, then
 * matches in-process; no backend search method is assumed. Not signed in →
 * bounce to /login. */
static struct picomesh_void_result page_search(struct loop *loop, struct loop_stream *s,
                        const struct serve_ud *sud, const char *sid,
                        const char *uname, const char *full_path, int is_admin,
                        int keep_alive)
{
    if (!sid || !*sid || !uname || !*uname) {
        send_redirect(s, "/-/login", NULL, keep_alive);
        return PICOMESH_OK_VOID();
    }
    char *query = query_get(full_path, "q");
    char query_lower[128];
    size_t query_len = 0;
    for (const char *p = query ? query : ""; *p && query_len + 1 < sizeof(query_lower); ++p) {
        char c = *p; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        query_lower[query_len++] = c;
    }
    query_lower[query_len] = 0;

    struct picomesh_string_result names_rr = collect_accessible_repos(loop, sud, sid);
    if (PICOMESH_IS_ERR(names_rr)) { picomesh_error_print(stderr, "webapp: collect_accessible_repos", names_rr.error); picomesh_error_destroy(names_rr.error); }
    char *names = PICOMESH_IS_OK(names_rr) ? names_rr.value : NULL;

    struct buf b; buf_init(&b);
    page_open(&b, "Search", uname, "projects", is_admin);
    render_page_header(&b, "Search", "Find a repository by name.", NULL);
    char metabuf[180];
    if (query && *query) snprintf(metabuf, sizeof(metabuf),
                          "Your repositories matching \xe2\x80\x9c%s\xe2\x80\x9d", query);
    else         snprintf(metabuf, sizeof(metabuf), "Your repositories");
    panel_open(&b, metabuf, NULL);
    int any = 0;
    if (names && *names) {
        buf_puts(&b, "<table class=\"file-table\"><tbody>");
        char *save = NULL;
        for (char *line = strtok_r(names, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!*line) continue;
            if (query_len) {
                char line_lower[256]; size_t line_len = 0;
                for (const char *p = line; *p && line_len + 1 < sizeof(line_lower); ++p) {
                    char c = *p; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                    line_lower[line_len++] = c;
                }
                line_lower[line_len] = 0;
                if (!strstr(line_lower, query_lower)) continue;
            }
            any = 1;
            /* `line` is the full repo path (<namespace>/<repo>). */
            buf_puts(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td><td><a class=\"file-name dir\" href=\"/");
            buf_esc(&b, line);
            buf_puts(&b, "/-/tree\">"); buf_esc(&b, line);
            buf_puts(&b, "</a></td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    if (!any) buf_puts(&b, "<p class=\"muted\">No matching repositories.</p>");
    panel_close(&b);
    free(names); free(query);
    page_close_and_send(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /repos/new — the create-repository form (the POST action is
 * webapp_repos_new on the same path). Needs a signed-in user. */
static struct picomesh_void_result page_repos_new_form(struct loop *loop, struct loop_stream *s,
                                const struct serve_ud *sud, const char *sid,
                                const char *uname, int is_admin, int keep_alive)
{
    if (!uname || !*uname) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }

    /* The namespaces this user may create a repo in: any where they hold
     * developer+ (their personal namespace, plus groups they can push to).
     * Sourced from /_whoami (issue #30). */
    struct json_doc_ptr_result who_rr = whoami_doc(loop, sud, sid);
    if (PICOMESH_IS_ERR(who_rr)) { picomesh_error_print(stderr, "webapp: whoami_doc", who_rr.error); picomesh_error_destroy(who_rr.error); }
    struct json_doc *who = PICOMESH_IS_OK(who_rr) ? who_rr.value : NULL;
    const struct json_value *nss = who ? json_object_get(json_doc_root(who), "namespaces") : NULL;

    struct buf b; buf_init(&b);
    render_shell_open(&b, "New repository", uname, "projects", is_admin);
    render_page_header(&b, "New repository",
                       "Create a repository in a namespace you can push to.", NULL);
    panel_open(&b, "Repository details", NULL);
    /* A free-text namespace with the user's DIRECT namespaces as datalist
     * suggestions. Free text (not a closed dropdown) so an INHERITED subgroup —
     * e.g. a developer on `acme` creating in `acme/platform` — can be entered;
     * the backend authorizes by inherited namespace role. Defaults to the
     * personal namespace. */
    buf_puts(&b, "<form class=\"form-grid\" method=\"post\" action=\"/-/repos/new\">"
                 "<label>Namespace <input name=\"namespace\" list=\"ns-options\" value=\"");
    buf_esc(&b, uname);
    buf_puts(&b, "\" pattern=\"[A-Za-z0-9._/-]{1,128}\" required></label>"
                 "<datalist id=\"ns-options\">");
    size_t ns_count = nss ? json_array_size(nss) : 0;
    for (size_t i = 0; i < ns_count; ++i) {
        const struct json_value *ns_entry = json_array_at(nss, i);
        const char *ns_path = json_as_string(json_object_get(ns_entry, "path"), NULL);
        const char *role = json_as_string(json_object_get(ns_entry, "role"), NULL);
        if (!ns_path || !*ns_path || !role || strcmp(ns_path, "site") == 0) continue;
        if (!role_at_least(role, "developer")) continue;
        buf_puts(&b, "<option value=\"");
        buf_esc(&b, ns_path);
        buf_puts(&b, "\">");
    }
    buf_puts(&b, "</datalist>"
                 "<label>Name <input name=\"name\" placeholder=\"my-repo\" "
                 "pattern=\"[a-zA-Z0-9._-]{1,32}\" required autofocus></label>"
                 "<button type=\"submit\" class=\"primary\">Create repository</button></form>"
                 "<p class=\"muted small\">Pick a group you can push to (developer+), or type "
                 "a subgroup path you have inherited access to, e.g. <code>acme/platform</code>.</p>");
    panel_close(&b);
    json_doc_free(who);
    render_shell_close(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /admin/services — the live service roster discovered from the
 * gateway's /_describe. Force a fresh fetch here so the admin sees the
 * current topology, not whatever was cached within the TTL window. */
static struct picomesh_void_result page_admin_services(struct loop *loop, struct loop_stream *s,
                                struct serve_ud *sud,
                                const char *uname, int keep_alive)
{
    services_invalidate(sud);
    services_ensure(loop, sud);

    struct buf b; buf_init(&b);
    render_shell_open(&b, "Services", uname, "admin-services", /*is_admin=*/1);
    render_page_header(&b, "Services",
                       "Active services discovered from the gateway /_describe.", NULL);
    panel_open(&b, "Active services", "from gateway /_describe");
    /* Link to the generic /_alpine service console (a separate node) where an
     * admin can introspect and invoke these services directly. */
    const char *console_url = sud->cfg ? sud->cfg->console_url : NULL;
    if (console_url && *console_url) {
        buf_puts(&b, "<p class=\"muted\">Inspect &amp; invoke these services in the "
                     "generic console: <a class=\"console-link\" href=\"");
        buf_esc(&b, console_url);
        buf_puts(&b, "\" target=\"_blank\" rel=\"noopener\"><code>");
        buf_esc(&b, console_url);
        buf_puts(&b, "</code></a></p>");
    }
    buf_puts(&b, "<table class=\"service-table\"><thead><tr>"
                 "<th>Service</th><th>Source</th><th>Status</th></tr></thead><tbody>");
    for (size_t i = 0; i < sud->services.n; ++i) {
        buf_puts(&b, "<tr><td><code>"); buf_esc(&b, sud->services.names[i]);
        buf_puts(&b, "</code></td><td>"); buf_esc(&b, sud->services.sources[i]);
        buf_puts(&b, "</td><td><span class=\"badge succeeded\">active</span></td></tr>");
    }
    if (sud->services.n == 0)
        buf_puts(&b, "<tr><td colspan=\"3\" class=\"muted\">No services discovered.</td></tr>");
    buf_puts(&b, "</tbody></table>");
    panel_close(&b);
    render_shell_close(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /dashboard/runs — global pipeline activity across the mesh. */
static struct picomesh_void_result page_dashboard_runs(struct loop *loop, struct loop_stream *s,
                                const struct serve_ud *sud, const char *sid,
                                const char *uname, int is_admin, int keep_alive)
{
    struct picomesh_int64_result pending_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
    if (PICOMESH_IS_ERR(pending_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", pending_rr.error); picomesh_error_destroy(pending_rr.error); }
    long pending = PICOMESH_IS_OK(pending_rr) ? pending_rr.value : (0);
    struct picomesh_int64_result running_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
    if (PICOMESH_IS_ERR(running_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", running_rr.error); picomesh_error_destroy(running_rr.error); }
    long running = PICOMESH_IS_OK(running_rr) ? running_rr.value : (0);
    struct picomesh_int64_result done_rr = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_done",    "[]", 0);
    if (PICOMESH_IS_ERR(done_rr)) { picomesh_error_print(stderr, "webapp: rpc_result_int", done_rr.error); picomesh_error_destroy(done_rr.error); }
    long done = PICOMESH_IS_OK(done_rr) ? done_rr.value : (0);
    struct buf b; buf_init(&b);
    render_shell_open(&b, "Pipelines", uname, "runs", is_admin);
    render_page_header(&b, "Pipelines", "Pipeline activity across the mesh.", NULL);
    panel_open(&b, "Pipeline runs", "queued, running, finished");
    buf_printf(&b,
        "<table class=\"pipeline-table\"><thead><tr>"
        "<th>Status</th><th>Ref</th><th>Count</th></tr></thead><tbody>"
        "<tr><td><span class=\"badge queued\">queued</span></td><td><code>main</code></td><td>%ld</td></tr>"
        "<tr><td><span class=\"badge running\">running</span></td><td><code>main</code></td><td>%ld</td></tr>"
        "<tr><td><span class=\"badge succeeded\">finished</span></td><td><code>main</code></td><td>%ld</td></tr>"
        "</tbody></table>", pending, running, done);
    panel_close(&b);
    render_shell_close(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}

/* GET /-/dashboard/issues — open issues summed across every repo the user can
 * access (no global list API yet; aggregate per-repo counts). Uses the SAME RBAC
 * discovery as /-/repos (collect_accessible_repos) so group-managed and
 * inherited-subgroup repos are included, not just the creator index. */
static struct picomesh_void_result page_dashboard_issues(struct loop *loop, struct loop_stream *s,
                                  const struct serve_ud *sud, const char *sid,
                                  const char *uname, int is_admin, int keep_alive)
{
    struct picomesh_string_result names_rr = collect_accessible_repos(loop, sud, sid);
    if (PICOMESH_IS_ERR(names_rr)) { picomesh_error_print(stderr, "webapp: collect_accessible_repos", names_rr.error); picomesh_error_destroy(names_rr.error); }
    char *names = PICOMESH_IS_OK(names_rr) ? names_rr.value : NULL;

    struct buf b; buf_init(&b);
    render_shell_open(&b, "Issues", uname, "issues", is_admin);
    render_page_header(&b, "Issues", "Open issues across your repositories.", NULL);
    panel_open(&b, "By repository", NULL);
    int any = 0;
    if (names && *names) {
        buf_puts(&b, "<table class=\"file-table\"><thead><tr>"
                     "<th>Repository</th><th>Open</th></tr></thead><tbody>");
        char *save = NULL;
        for (char *line = strtok_r(names, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!*line) continue;
            any = 1;
            /* `line` is the full repo path (<namespace>/<repo>). */
            uint32_t rid = repo_hash_full(line);
            long open_n = -1;
            if (service_active(sud, "issues")) {
                char issue_args[48]; snprintf(issue_args, sizeof(issue_args), "[%u]", rid);
                struct picomesh_int64_result open_rr = rpc_result_int(loop, sud, sid, "issues.issues.count_open_in_repo", issue_args, -1);
                if (PICOMESH_IS_ERR(open_rr)) { picomesh_error_print(stderr, "webapp: count_open_in_repo", open_rr.error); picomesh_error_destroy(open_rr.error); }
                else open_n = open_rr.value;
            }
            buf_puts(&b, "<tr><td><a class=\"file-name\" href=\"/");
            buf_esc(&b, line); buf_puts(&b, "/-/issues\">");
            buf_esc(&b, line);
            buf_puts(&b, "</a></td><td>");
            if (open_n >= 0) buf_printf(&b, "<span class=\"badge open\">%ld</span>", open_n);
            else             buf_puts(&b, "<span class=\"muted\">-</span>");
            buf_puts(&b, "</td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    if (!any) buf_puts(&b, "<p class=\"muted\">No repositories.</p>");
    panel_close(&b);
    free(names);
    render_shell_close(s, &b, keep_alive);
    return PICOMESH_OK_VOID();
}


/* ---- action POSTs forwarded to the gateway /_rpc -------------------- *
 * Each fires one backend call then 303s back to the page. The gateway
 * resolves the sid → uid for auth context; methods that need the actor's
 * uid as an explicit arg (issues.open) get it from session.lookup. */

static struct picomesh_void_result post_issue_new(struct loop *loop, struct loop_stream *s,
                           const struct serve_ud *sud, const char *sid,
                           const char *acct, const char *repo, int keep_alive)
{
    /* No valid session → no mutation. Never attribute an issue to a
     * fallback uid; an unresolved actor is an auth failure. */
    struct claims claims;
    struct picomesh_void_result claims_res = resolve_claims(loop, sud, sid, &claims);
    if (PICOMESH_IS_ERR(claims_res)) { picomesh_error_print(stderr, "webapp: resolve_claims", claims_res.error); picomesh_error_destroy(claims_res.error); }
    if (!claims.uid) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }
    uint32_t rid = repo_hash(acct, repo);
    char args[64];
    snprintf(args, sizeof(args), "[%u,%u]", rid, claims.uid);
    char where[300];
    snprintf(where, sizeof(where), "/%s/%s/-/issues", acct, repo);
    /* A rejected or undelivered mutation must be shown, never redirected as
     * success — the user has to know the issue was not opened. */
    char errbuf[256];
    struct picomesh_int_result open_res = rpc_invoke(loop, sud, sid, "issues.issues.open", args, errbuf, sizeof(errbuf));
    if (PICOMESH_IS_ERR(open_res)) {
        yerror("webapp: issues.open transport failure");
        render_action_error(s, claims.username, claims.is_admin, errbuf, where, keep_alive);
        picomesh_error_destroy(open_res.error);
        return PICOMESH_OK_VOID();
    }
    if (open_res.value == 0) {
        render_action_error(s, claims.username, claims.is_admin, errbuf, where, keep_alive);
        return PICOMESH_OK_VOID();
    }
    send_redirect(s, where, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result post_issue_close(struct loop *loop, struct loop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *acct, const char *repo,
                             const char *body, size_t body_len, int keep_alive)
{
    struct claims claims;
    struct picomesh_void_result claims_res = resolve_claims(loop, sud, sid, &claims);
    if (PICOMESH_IS_ERR(claims_res)) { picomesh_error_print(stderr, "webapp: resolve_claims", claims_res.error); picomesh_error_destroy(claims_res.error); }
    if (!claims.uid) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }
    char where[300];
    snprintf(where, sizeof(where), "/%s/%s/-/issues", acct, repo);
    char *issue_id_str = form_get(body, body_len, "issue_id");
    if (issue_id_str && *issue_id_str) {
        char args[48];
        snprintf(args, sizeof(args), "[%lu]", strtoul(issue_id_str, NULL, 10));
        char errbuf[256];
        struct picomesh_int_result close_res = rpc_invoke(loop, sud, sid, "issues.issues.close", args, errbuf, sizeof(errbuf));
        free(issue_id_str);
        if (PICOMESH_IS_ERR(close_res)) {
            yerror("webapp: issues.close transport failure");
            render_action_error(s, claims.username, claims.is_admin, errbuf, where, keep_alive);
            picomesh_error_destroy(close_res.error);
            return PICOMESH_OK_VOID();
        }
        if (close_res.value == 0) {
            render_action_error(s, claims.username, claims.is_admin, errbuf, where, keep_alive);
            return PICOMESH_OK_VOID();
        }
    } else {
        free(issue_id_str);
    }
    send_redirect(s, where, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result post_run_new(struct loop *loop, struct loop_stream *s,
                         const struct serve_ud *sud, const char *sid,
                         const char *acct, const char *repo, int keep_alive)
{
    struct claims claims;
    struct picomesh_void_result claims_res = resolve_claims(loop, sud, sid, &claims);
    if (PICOMESH_IS_ERR(claims_res)) { picomesh_error_print(stderr, "webapp: resolve_claims", claims_res.error); picomesh_error_destroy(claims_res.error); }
    if (!claims.uid) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }
    char args[48];
    snprintf(args, sizeof(args), "[%u]", repo_hash(acct, repo));
    char where[300];
    snprintf(where, sizeof(where), "/%s/%s/-/runs", acct, repo);
    char errbuf[256];
    struct picomesh_int_result enqueue_res = rpc_invoke(loop, sud, sid, "git_pipeline.git_pipeline.enqueue", args, errbuf, sizeof(errbuf));
    if (PICOMESH_IS_ERR(enqueue_res)) {
        yerror("webapp: git_pipeline.enqueue transport failure");
        render_action_error(s, claims.username, claims.is_admin, errbuf, where, keep_alive);
        picomesh_error_destroy(enqueue_res.error);
        return PICOMESH_OK_VOID();
    }
    if (enqueue_res.value == 0) {
        render_action_error(s, claims.username, claims.is_admin, errbuf, where, keep_alive);
        return PICOMESH_OK_VOID();
    }
    send_redirect(s, where, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result post_run_lease(struct loop *loop, struct loop_stream *s,
                           const struct serve_ud *sud, const char *sid,
                           const char *acct, const char *repo,
                           const char *body, size_t body_len, int keep_alive)
{
    struct claims claims;
    struct picomesh_void_result claims_res = resolve_claims(loop, sud, sid, &claims);
    if (PICOMESH_IS_ERR(claims_res)) { picomesh_error_print(stderr, "webapp: resolve_claims", claims_res.error); picomesh_error_destroy(claims_res.error); }
    if (!claims.uid) { send_redirect(s, "/-/login", NULL, keep_alive); return PICOMESH_OK_VOID(); }
    char *runner_str = form_get(body, body_len, "runner");
    unsigned long runner = runner_str && *runner_str ? strtoul(runner_str, NULL, 10) : 1;
    if (!runner) runner = 1;
    free(runner_str);
    char args[48];
    snprintf(args, sizeof(args), "[%lu]", runner);
    char where[300];
    snprintf(where, sizeof(where), "/%s/%s/-/runs", acct, repo);
    char errbuf[256];
    struct picomesh_int_result lease_res = rpc_invoke(loop, sud, sid, "git_pipeline.git_pipeline.lease", args, errbuf, sizeof(errbuf));
    if (PICOMESH_IS_ERR(lease_res)) {
        yerror("webapp: git_pipeline.lease transport failure");
        render_action_error(s, claims.username, claims.is_admin, errbuf, where, keep_alive);
        picomesh_error_destroy(lease_res.error);
        return PICOMESH_OK_VOID();
    }
    if (lease_res.value == 0) {
        render_action_error(s, claims.username, claims.is_admin, errbuf, where, keep_alive);
        return PICOMESH_OK_VOID();
    }
    send_redirect(s, where, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

/* Forward a form POST verbatim to the gateway at `gw_path` carrying the
 * caller's sid, then 303 to `redirect_to`. Used for gateway-owned action
 * POSTs (admin register / mint_pat) the sidecar only relays — the gateway
 * resolves the sid → uid and enforces the site-owner gate. Best-effort:
 * always redirects back, even on a gateway error. */
static struct picomesh_void_result relay_post(struct loop *loop, struct loop_stream *s,
                       const struct serve_ud *sud, const char *sid,
                       const char *gw_path, const char *body, size_t body_len,
                       const char *redirect_to, int keep_alive)
{
    struct http_response resp;
    struct picomesh_int_result post = http_post(loop, &sud->gw, gw_path,
                    "application/x-www-form-urlencoded", NULL, sid,
                    body, body_len, &resp);
    /* Best-effort relay: a transport failure is logged but still redirects
     * back. Discard the error here so its cause chain is not leaked. */
    if (PICOMESH_IS_ERR(post)) {
        picomesh_error_print(stderr, "relay_post: gateway POST", post.error);
        picomesh_error_destroy(post.error);
    }
    http_response_free(&resp);
    send_redirect(s, redirect_to, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

/* POST /repos/new — create a repository for the signed-in user. We call
 * git_repo.git_repo.make over the gateway /_rpc directly with the owner uid
 * taken from the verified session identity (claims.uid via /_whoami), rather
 * than relaying the gateway's HTML action — that action needs the uname cookie,
 * which a header-only relay doesn't carry. Then bounce to /repos. */
static struct picomesh_void_result webapp_repos_new(struct loop *loop, struct loop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *body, size_t body_len, int keep_alive)
{
    /* Owner is the session identity, never a cookie. No valid session →
     * no repo creation. */
    struct claims claims;
    resolve_claims(loop, sud, sid, &claims);
    if (!claims.uid || !claims.username[0]) {
        send_redirect(s, "/-/login", NULL, keep_alive);
        return PICOMESH_OK_VOID();
    }
    char *name = form_get(body, body_len, "name");
    char *namespace = form_get(body, body_len, "namespace");
    /* The owning namespace is the form's choice (a group the user can push to),
     * defaulting to their personal namespace. make is gated developer+ on it. */
    const char *owner_name = (namespace && *namespace) ? namespace : claims.username;
    char redirect[160]; snprintf(redirect, sizeof(redirect), "/%s", owner_name);
    if (name && *name) {
        char name_esc[96], owner_esc[160], args[320], err[256];
        if (json_escape(name_esc, sizeof(name_esc), name) &&
            json_escape(owner_esc, sizeof(owner_esc), owner_name)) {
            snprintf(args, sizeof(args), "[%u,\"%s\",\"%s\"]", claims.uid, owner_esc, name_esc);
            struct picomesh_int_result rpc_inv_4 = rpc_invoke(loop, sud, sid, "git_repo.git_repo.make", args, err, sizeof(err));
            if (PICOMESH_IS_ERR(rpc_inv_4)) { picomesh_error_print(stderr, "webapp: rpc_invoke", rpc_inv_4.error); picomesh_error_destroy(rpc_inv_4.error); }
            if (PICOMESH_IS_ERR(rpc_inv_4) || !rpc_inv_4.value) {
                free(name); free(namespace);
                render_action_error(s, claims.username, claims.is_admin, err, "/-/repos/new", keep_alive);
                return PICOMESH_OK_VOID();
            }
            snprintf(redirect, sizeof(redirect), "/%s/%s/-/tree", owner_name, name);
        }
    }
    free(name); free(namespace);
    send_redirect(s, redirect, NULL, keep_alive);
    return PICOMESH_OK_VOID();
}

/* POST /logout — invalidate the server-side session at the gateway
 * (best-effort), then clear the browser cookies and bounce to /login. We
 * emit the clearing Set-Cookie headers ourselves (the gateway sets two and
 * our client only captures the first), so the browser reliably forgets the
 * opaque sid + uname. */
static struct picomesh_void_result route_logout(struct loop *loop, struct loop_stream *s,
                         const struct serve_ud *sud, const char *sid, int keep_alive)
{
    struct http_response resp;
    struct picomesh_int_result post = http_post(loop, &sud->gw, "/logout",
                    "application/x-www-form-urlencoded", NULL, sid, "", 0, &resp);
    /* Best-effort: clear the browser cookies regardless. Discard any transport
     * error here so its cause chain is not leaked. */
    if (PICOMESH_IS_ERR(post)) {
        picomesh_error_print(stderr, "route_logout: /logout", post.error);
        picomesh_error_destroy(post.error);
    }
    http_response_free(&resp);
    send_redirect(s, "/-/login",
        "Set-Cookie: picomesh-sid=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0\r\n"
        "Set-Cookie: picomesh-uname=; Path=/; SameSite=Lax; Max-Age=0\r\n",
        keep_alive);
    return PICOMESH_OK_VOID();
}

/* Split "<acct>/<repo>[/<section>[/<action>]]" (query stripped) into its
 * parts and return the segment count (2..4); absent trailing parts are set
 * to "". Rejects "..", <2 or >4 segments. The action POSTs use 4 segments
 * (/<acct>/<repo>/issues/new), which parse_repo_path (max 3) won't take. */
static int parse_action_path(const char *path, size_t path_len,
                             char *acct, size_t acct_cap,
                             char *repo, size_t repo_cap,
                             char *sect, size_t sect_cap,
                             char *act, size_t act_cap)
{
    size_t plen = path_len;
    const char *q = memchr(path, '?', path_len);
    if (q) plen = (size_t)(q - path);
    long d = dash_sep(path, plen);
    if (d <= 0) return 0;
    if (!split_namespace_repo(path, (size_t)d, acct, acct_cap, repo, repo_cap)) return 0;
    /* tail = "<section>[/<act>]" after "/-/" (e.g. "issues/new", "edit"). */
    char tail[128];
    size_t tlen = plen - (size_t)d - 3;
    if (tlen == 0 || tlen >= sizeof(tail)) return 0;
    memcpy(tail, path + d + 3, tlen); tail[tlen] = 0;
    if (strstr(tail, "..")) return 0;
    char *slash = strchr(tail, '/');
    if (slash) { *slash = 0; snprintf(act, act_cap, "%s", slash + 1); }
    else       act[0] = 0;
    snprintf(sect, sect_cap, "%s", tail);
    return 1;
}

/* The repo sub-page route table. Maps a URL verb to the backend service
 * that must be active for the page to exist (gated against /_describe)
 * and the page it renders. This — not a hardcoded path ladder — is what
 * makes the page set service-driven. */
enum repo_page { RP_BROWSE, RP_ISSUES, RP_RUNS, RP_EDIT, RP_SETTINGS };
struct repo_route { const char *verb; const char *service; enum repo_page page; };

static const struct repo_route *repo_routes(size_t *count)
{
    static const struct repo_route ROUTES[] = {
        { "tree",     "git_repo",     RP_BROWSE   },
        { "issues",   "issues",       RP_ISSUES   },
        { "runs",     "git_pipeline", RP_RUNS     },
        { "edit",     "git_repo",     RP_EDIT     },
        { "new",      "git_repo",     RP_EDIT     },
        { "settings", "git_repo",     RP_SETTINGS },
    };
    *count = sizeof(ROUTES) / sizeof(ROUTES[0]);
    return ROUTES;
}

static const struct repo_route *repo_route_for(const char *verb)
{
    size_t count;
    const struct repo_route *routes = repo_routes(&count);
    for (size_t i = 0; i < count; ++i)
        if (strcmp(routes[i].verb, verb) == 0) return &routes[i];
    return NULL;
}

/* ---- per-peer serve coro -------------------------------------------- */

/* Per-connection serve coroutine — its void signature is fixed by the loop's
 * accept-handler API (loop_listen_tcp). Every route/page handler renders its
 * own response and absorbs+logs RPC failures internally (returning OK), so
 * their results are intentionally not propagated past this boundary. */
PICOMESH_EXTERNAL_CALLBACK
static void serve_one(struct loop *l, struct loop_stream *s, void *ud)
{
    (void)l;
    struct serve_ud *sud = ud;
    const struct webapp_config *cfg = sud->cfg;

    /* Read request bytes until picohttpparser is satisfied. This is a
     * per-connection coroutine root: internal failures (OOM) are logged with
     * yerror; client-caused failures (malformed / oversized request) get a
     * 4xx response instead of a silent close. */
    char *buf = malloc(WEBAPP_REQ_BUF);
    if (!buf) { yerror("[webapp] serve_one: out of memory allocating %d-byte request buffer", WEBAPP_REQ_BUF); loop_close(s); return; }
    size_t total = 0, last = 0;
    const char *method = NULL, *path = NULL;
    size_t method_len = 0, path_len = 0;
    int minor_version = 0;
    struct phr_header headers[WEBAPP_MAX_HEADERS];
    size_t num_headers;

    int parsed = 0;
    while (total < WEBAPP_REQ_BUF) {
        struct picomesh_size_result chunk_res = loop_read_some(s, buf + total, WEBAPP_REQ_BUF - total);
        if (PICOMESH_IS_ERR(chunk_res)) {
            ywarn("[webapp] serve_one: request read error: %s", chunk_res.error.msg ? chunk_res.error.msg : "?");
            picomesh_error_destroy(chunk_res.error);
            free(buf); loop_close(s); return;
        }
        if (chunk_res.value == 0) { free(buf); loop_close(s); return; } /* clean client close / EOF — normal */
        total += chunk_res.value;
        num_headers = WEBAPP_MAX_HEADERS;
        int parse_result = phr_parse_request(buf, total, &method, &method_len,
                                  &path, &path_len, &minor_version,
                                  headers, &num_headers, last);
        if (parse_result > 0) { parsed = 1; break; }
        if (parse_result == -1) {
            ywarn("[webapp] serve_one: malformed request, rejecting with 400");
            send_text(s, 400, "bad request\n", 0);
            free(buf);
            loop_close(s);
            return;
        }
        last = total;
    }
    if (!parsed) {
        /* Filled the buffer without a complete request line + headers. */
        ywarn("[webapp] serve_one: request headers exceed %d bytes, rejecting with 431", WEBAPP_REQ_BUF);
        send_text(s, 431, "request header fields too large\n", 0);
        free(buf);
        loop_close(s);
        return;
    }

    char ka_hdr[32] = {0};
    int keep_alive = (minor_version >= 1);
    if (header_match(headers, num_headers, "connection", ka_hdr, sizeof(ka_hdr))) {
        if (strstr(ka_hdr, "close")) keep_alive = 0;
        if (strstr(ka_hdr, "keep-alive")) keep_alive = 1;
    }

    /* High-level request log: every page/action the webapp serves, at the
     * entry point, before routing. method/path are length-counted slices of
     * the read buffer (not NUL-terminated), so print with %.*s. */
    yinfo("[webapp] %.*s %.*s", (int)method_len, method, (int)path_len, path);

    /* Method+path dispatch. /login + /static are always served (login is
     * the gateway's auth, static is the sidecar's own assets). Every data
     * page is service-driven: discover the active mesh services once, then
     * gate each page on its backing service — no service, no page. */
    if (method_len == 3 && memcmp(method, "GET", 3) == 0) {
        if (path_equals(path, path_len, "/")) {
            send_redirect(s, "/-/repos", NULL, keep_alive);
        } else if (path_equals(path, path_len, "/-/login")) {
            route_login_get(s, sud, keep_alive);
        } else if (path_equals(path, path_len, "/-/auth/github")) {
            route_github_start(s, sud, headers, num_headers, keep_alive);
        } else if (starts_with(path, path_len, "/-/auth/github/callback")) {
            char fp[1024];
            size_t copy = path_len < sizeof(fp) - 1 ? path_len : sizeof(fp) - 1;
            memcpy(fp, path, copy); fp[copy] = 0;
            route_github_callback(l, s, sud, headers, num_headers, fp, keep_alive);
        } else if (path_equals(path, path_len, "/-/register")) {
            route_register_get(s, keep_alive);
        } else if (starts_with(path, path_len, "/static/")) {
            char tmp[1024];
            size_t copy = path_len < sizeof(tmp) - 1 ? path_len : sizeof(tmp) - 1;
            memcpy(tmp, path, copy); tmp[copy] = 0;
            if (!serve_static(s, cfg->static_dir, tmp, keep_alive))
                send_text(s, 404, "not found\n", keep_alive);
        } else {
            services_ensure(l, sud);
            char *sid = cookie_get(headers, num_headers, "picomesh-sid");
            char *uname_cookie = cookie_get(headers, num_headers, "picomesh-uname");

            /* Trustworthy identity for this request, resolved by the gateway
             * from the live session. The picomesh-uname cookie is only a
             * cosmetic fallback for the display name — never an authority for
             * ownership or admin access. Anonymous callers render as such even
             * if a stale/forged uname cookie is present. */
            struct claims claims;
            resolve_claims(l, sud, sid, &claims);
            const char *who = claims.uid
                ? (claims.username[0] ? claims.username : (uname_cookie ? uname_cookie : ""))
                : "";

            /* Full NUL-terminated path (query kept, for ?dir/?path). */
            char fp[1024];
            size_t copy = path_len < sizeof(fp) - 1 ? path_len : sizeof(fp) - 1;
            memcpy(fp, path, copy); fp[copy] = 0;

            char acct[128], repo[128], verb[32];
            if (path_equals(path, path_len, "/-/repos")) {
                if (service_active(sud, "git_repo")) route_repos_get(l, s, sud, sid, who, claims.uid, claims.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/-/search") ||
                       starts_with(path, path_len, "/-/search?")) {
                if (service_active(sud, "git_repo")) page_search(l, s, sud, sid, who, fp, claims.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/-/repos/new")) {
                if (service_active(sud, "git_repo")) page_repos_new_form(l, s, sud, sid, who, claims.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/-/admin") ||
                       starts_with(path, path_len, "/-/admin/")) {
                /* Admin space: prove a signed-in site admin BEFORE rendering
                 * any admin content. Anonymous → /-/login; non-admin → 403. */
                if (require_admin(s, &claims, keep_alive)) {
                    if (path_equals(path, path_len, "/-/admin")) {
                        page_admin_overview(l, s, sud, sid, who, keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/users")) {
                        if (service_active(sud, "accounts")) page_admin_users(l, s, sud, sid, who, keep_alive);
                        else send_text(s, 404, "no such page (accounts not active)\n", keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/repos")) {
                        if (service_active(sud, "git_repo")) page_admin_repos(l, s, sud, sid, who, keep_alive);
                        else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/tokens")) {
                        page_admin_tokens(l, s, sud, sid, who, keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/services")) {
                        page_admin_services(l, s, sud, who, keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/namespaces")) {
                        if (service_active(sud, "accounts")) page_admin_namespaces(l, s, sud, sid, who, keep_alive);
                        else send_text(s, 404, "no such page (accounts not active)\n", keep_alive);
                    } else if (starts_with(path, path_len, "/-/admin/namespaces/")) {
                        /* /-/admin/namespaces/<path> — the namespace path may itself
                         * contain '/' (a subgroup), so take everything after the
                         * prefix and URL-decode it. */
                        char nspath[256];
                        size_t pfx = strlen("/-/admin/namespaces/");
                        size_t rest = path_len > pfx ? path_len - pfx : 0;
                        if (rest >= sizeof(nspath)) rest = sizeof(nspath) - 1;
                        memcpy(nspath, path + pfx, rest);
                        nspath[rest] = 0;
                        url_decode(nspath);
                        page_admin_namespace(l, s, sud, sid, who, nspath, keep_alive);
                    } else {
                        send_text(s, 404, "not found\n", keep_alive);
                    }
                }
            } else if (path_equals(path, path_len, "/-/dashboard/issues") ||
                       starts_with(path, path_len, "/-/dashboard/issues?")) {
                if (service_active(sud, "git_repo")) page_dashboard_issues(l, s, sud, sid, who, claims.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/-/dashboard/runs")) {
                if (service_active(sud, "git_pipeline")) page_dashboard_runs(l, s, sud, sid, who, claims.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_pipeline not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/-/groups")) {
                if (service_active(sud, "accounts")) page_groups(l, s, sud, sid, who, claims.is_admin, keep_alive);
                else send_text(s, 404, "no such page (accounts not active)\n", keep_alive);
            } else if (starts_with(path, path_len, "/-/groups/")) {
                /* /-/groups/<path> — the path may contain '/' (a subgroup). */
                char gpath[256];
                size_t pfx = strlen("/-/groups/");
                size_t rest = path_len > pfx ? path_len - pfx : 0;
                if (rest >= sizeof(gpath)) rest = sizeof(gpath) - 1;
                memcpy(gpath, path + pfx, rest);
                gpath[rest] = 0;
                url_decode(gpath);
                page_group_detail(l, s, sud, sid, who, gpath, claims.is_admin, keep_alive);
            } else if (starts_with(path, path_len, "/-/")) {
                /* Unknown top-level command. */
                send_text(s, 404, "not found\n", keep_alive);
            } else if (parse_repo_path(path, path_len, acct, sizeof(acct),
                                       repo, sizeof(repo), verb, sizeof(verb))) {
                /* GitLab-style project sub-page: /<namespace>/<repo>/-/<verb>
                 * (verb in the route table, gated on its backing service). */
                const struct repo_route *route = repo_route_for(verb);
                if (!route) {
                    send_text(s, 404, "not found\n", keep_alive);
                } else if (!service_active(sud, route->service)) {
                    send_text(s, 404, "no such page (service not active)\n", keep_alive);
                } else switch (route->page) {
                case RP_BROWSE:   route_repo_browse(l, s, sud, sid, who, acct, repo, fp, claims.is_admin, keep_alive); break;
                case RP_ISSUES:   page_repo_issues(l, s, sud, sid, who, acct, repo, fp, claims.is_admin, keep_alive); break;
                case RP_RUNS:     page_repo_runs(l, s, sud, sid, who, acct, repo, fp, claims.is_admin, keep_alive);   break;
                case RP_EDIT:     route_repo_edit_get(l, s, sud, sid, who, acct, repo, verb, fp, claims.is_admin, keep_alive); break;
                case RP_SETTINGS: page_repo_settings(l, s, sud, sid, who, acct, repo, claims.is_admin, keep_alive); break;
                }
            } else if (!claims.uid) {
                /* Any other path (no "/-/") is a NAMESPACE landing — member-gated
                 * data, not a public page. An anonymous caller must authenticate
                 * first, exactly like every other data route. Without this gate a
                 * bare path such as /login renders a 200 namespace page to a
                 * signed-out visitor (and hammers git_repo.*_for_namespace, which
                 * the backend rightly rejects as "insufficient namespace role"). */
                send_redirect(s, "/-/login", NULL, keep_alive);
            } else {
                /* Any other path (no "/-/") is a NAMESPACE path: /<group>[/<subgroup>…].
                 * Render the namespace landing — its repos (each linking to the
                 * project under /-/tree) and, for a member, its subgroups. */
                char nspath[256];
                const char *qm = memchr(path, '?', path_len);
                size_t plen = qm ? (size_t)(qm - path) : path_len;
                if (plen > 1 && path[0] == '/' && plen - 1 < sizeof(nspath) &&
                    !memchr(path, '\\', plen)) {
                    memcpy(nspath, path + 1, plen - 1); nspath[plen - 1] = 0;
                    url_decode(nspath);
                    if (!strstr(nspath, "..") && service_active(sud, "git_repo"))
                        page_account_landing(l, s, sud, sid, who, nspath, claims.is_admin, keep_alive);
                    else
                        send_text(s, 404, "not found\n", keep_alive);
                } else {
                    send_text(s, 404, "not found\n", keep_alive);
                }
            }
            free(sid);
            free(uname_cookie);
        }
    } else if (method_len == 4 && memcmp(method, "POST", 4) == 0) {
        /* Pull the body off the wire so route handlers see the
         * form-encoded payload as a contiguous buffer. A POST with no
         * Content-Length is treated as an empty body (content_length=0) — the
         * bodyless action forms (issue/run "new") submit that way, and a
         * bare `curl -XPOST` with no data should not be rejected. */
        long content_length = header_content_length(headers, num_headers);
        if (content_length < 0) content_length = 0;
        if (content_length > 1 << 20) {
            send_text(s, 400, "Content-Length too large\n", keep_alive);
        } else {
            /* Body may already be partly buffered after the parser
             * accepted the header. picohttpparser returns the offset
             * past CRLF-CRLF as the parse result (which we threw away
             * — re-derive by re-running once). */
            size_t hdr_end = 0;
            {
                num_headers = WEBAPP_MAX_HEADERS;
                int reparse_result = phr_parse_request(buf, total, &method, &method_len,
                                           &path, &path_len, &minor_version,
                                           headers, &num_headers, 0);
                if (reparse_result > 0) hdr_end = (size_t)reparse_result;
            }
            size_t buffered = total - hdr_end;
            char *body_buf = malloc((size_t)content_length + 1);
            if (!body_buf) {
                send_text(s, 500, "out of memory\n", keep_alive);
            } else {
                size_t copy = buffered < (size_t)content_length ? buffered : (size_t)content_length;
                memcpy(body_buf, buf + hdr_end, copy);
                if (copy < (size_t)content_length) {
                    if (read_body(s, body_buf + copy, (size_t)content_length - copy) < 0) {
                        free(body_buf);
                        send_text(s, 400, "short body\n", keep_alive);
                        goto post_done;
                    }
                }
                body_buf[content_length] = 0;

                char acct[128], repo[128], sect[64], act[64];
                if (path_equals(path, path_len, "/-/login")) {
                    route_login_post(l, s, sud, body_buf, (size_t)content_length, keep_alive);
                } else if (path_equals(path, path_len, "/-/register")) {
                    route_register_post(l, s, sud, body_buf, (size_t)content_length, keep_alive);
                } else if (path_equals(path, path_len, "/-/logout")) {
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    route_logout(l, s, sud, sid, keep_alive);
                    free(sid);
                } else if (path_equals(path, path_len, "/-/repos/new")) {
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    webapp_repos_new(l, s, sud, sid, body_buf, (size_t)content_length, keep_alive);
                    free(sid);
                } else if (starts_with(path, path_len, "/-/admin/")) {
                    /* Admin mutations: relay to the gateway only for a
                     * signed-in site admin. The gateway enforces this too —
                     * this is defense in depth so the webapp never knowingly
                     * forwards an unauthorized mutation. (content_length is the
                     * request body length, so the claims live in admin_claims.) */
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    struct claims admin_claims;
                    resolve_claims(l, sud, sid, &admin_claims);
                    if (!admin_claims.is_admin) {
                        if (admin_claims.uid)
                            send_forbidden(s, admin_claims.username[0] ? admin_claims.username : NULL, keep_alive);
                        else
                            send_redirect(s, "/-/login", NULL, keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/tokens/mint_pat")) {
                        /* First arg is the GATEWAY path (unchanged API), last is
                         * the webapp redirect target (GitLab-style /-/). */
                        relay_post(l, s, sud, sid, "/admin/tokens/mint_pat",
                                   body_buf, (size_t)content_length, "/-/admin/tokens", keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/namespaces/create")) {
                        webapp_ns_create(l, s, sud, sid, "/-/admin/namespaces", body_buf, (size_t)content_length, keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/namespaces/add_member")) {
                        webapp_ns_add_member(l, s, sud, sid, "/-/admin/namespaces", body_buf, (size_t)content_length, keep_alive);
                    } else if (path_equals(path, path_len, "/-/admin/namespaces/remove_member")) {
                        webapp_ns_remove_member(l, s, sud, sid, "/-/admin/namespaces", body_buf, (size_t)content_length, keep_alive);
                    } else {
                        send_text(s, 404, "not found\n", keep_alive);
                    }
                    free(sid);
                } else if (starts_with(path, path_len, "/-/groups/")) {
                    /* Non-admin group management: any signed-in user; the backend
                     * enforces maintainer-on-namespace (or site bypass). Failures
                     * are surfaced by the handlers. */
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    struct claims group_claims;
                    resolve_claims(l, sud, sid, &group_claims);
                    if (!group_claims.uid) {
                        send_redirect(s, "/-/login", NULL, keep_alive);
                    } else if (path_equals(path, path_len, "/-/groups/go")) {
                        /* Jump to a namespace's management page by typed path. */
                        char *group_path = form_get(body_buf, (size_t)content_length, "path");
                        char to[300] = "/-/groups";
                        if (group_path && *group_path) snprintf(to, sizeof(to), "/-/groups/%s", group_path);
                        free(group_path);
                        send_redirect(s, to, NULL, keep_alive);
                    } else if (path_equals(path, path_len, "/-/groups/create")) {
                        webapp_ns_create(l, s, sud, sid, "/-/groups", body_buf, (size_t)content_length, keep_alive);
                    } else if (path_equals(path, path_len, "/-/groups/add_member")) {
                        webapp_ns_add_member(l, s, sud, sid, "/-/groups", body_buf, (size_t)content_length, keep_alive);
                    } else if (path_equals(path, path_len, "/-/groups/remove_member")) {
                        webapp_ns_remove_member(l, s, sud, sid, "/-/groups", body_buf, (size_t)content_length, keep_alive);
                    } else {
                        send_text(s, 404, "not found\n", keep_alive);
                    }
                    free(sid);
                } else {
                    /* parse_action_path identifies the section/act from the
                     * right, so `acct` may be a multi-segment namespace path. */
                    int act_ok = parse_action_path(path, path_len, acct, sizeof(acct),
                                                   repo, sizeof(repo), sect, sizeof(sect),
                                                   act, sizeof(act));
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    if (act_ok && strcmp(sect, "edit") == 0) {
                        route_repo_edit_post(l, s, sud, sid, acct, repo,
                                             body_buf, (size_t)content_length, keep_alive);
                    } else if (act_ok && strcmp(sect, "issues") == 0 && strcmp(act, "new") == 0) {
                        post_issue_new(l, s, sud, sid, acct, repo, keep_alive);
                    } else if (act_ok && strcmp(sect, "issues") == 0 && strcmp(act, "close") == 0) {
                        post_issue_close(l, s, sud, sid, acct, repo, body_buf, (size_t)content_length, keep_alive);
                    } else if (act_ok && strcmp(sect, "runs") == 0 && strcmp(act, "new") == 0) {
                        post_run_new(l, s, sud, sid, acct, repo, keep_alive);
                    } else if (act_ok && strcmp(sect, "runs") == 0 && strcmp(act, "lease") == 0) {
                        post_run_lease(l, s, sud, sid, acct, repo, body_buf, (size_t)content_length, keep_alive);
                    } else {
                        send_text(s, 404, "not found\n", keep_alive);
                    }
                    free(sid);
                }
                free(body_buf);
            }
        }
    post_done:
        (void)0;
    } else {
        send_text(s, 405, "method not allowed\n", keep_alive);
    }

    free(buf);
    loop_close(s);
}

/* ---- public entry --------------------------------------------------- */

struct picomesh_void_result webapp_start(struct loop *loop,
                                         const char *host, int port,
                                         const struct webapp_config *cfg)
{
    /* `ud` outlives the listener: leaked intentionally — process-lifetime,
     * tiny, freed on exit by the OS. The config strings come from argv
     * which holds them for the chain's lifetime (== whole process). */
    struct serve_ud *ud = calloc(1, sizeof(*ud));
    if (!ud) return PICOMESH_ERR(picomesh_void, "webapp_start: calloc failed");
    ud->loop = loop;
    ud->cfg  = cfg;
    if (gateway_url_parse(cfg->gateway_url, &ud->gw) != 0) {
        free(ud);
        return PICOMESH_ERR(picomesh_void,
                         "webapp_start: cannot parse --gateway-url");
    }

    struct picomesh_void_result listen_res =
        loop_listen_tcp(loop, host, port, serve_one, ud);
    PICOMESH_RETURN_IF_ERR(picomesh_void, listen_res, "webapp_start: loop_listen_tcp");
    return PICOMESH_OK_VOID();
}
