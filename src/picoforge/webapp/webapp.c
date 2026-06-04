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

#include <picomesh/ycore/ytrace.h>
#include <picomesh/ycore/idkey.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/yloop/yloop.h>

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

struct serve_ud {
    struct yloop *loop;
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
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    default:  return "OK";
    }
}

static void send_response(struct yloop_stream *s, int status,
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
    yloop_write(s, header, (size_t)n);
    if (body_len && body) yloop_write(s, body, body_len);
}

static void send_redirect(struct yloop_stream *s, const char *location,
                          const char *extra_headers, int keep_alive)
{
    char hdrs[512];
    snprintf(hdrs, sizeof(hdrs), "Location: %s\r\n%s",
             location, extra_headers ? extra_headers : "");
    send_response(s, 303, "text/plain", "", 0, hdrs, keep_alive);
}

static void send_text(struct yloop_stream *s, int status,
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

static int serve_static(struct yloop_stream *s, const char *root,
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
    "<form method=\"post\" action=\"/login\">"
    "<label>Username <input name=\"username\" autofocus></label>"
    "<label>Password <input type=\"password\" name=\"password\"></label>"
    "<button type=\"submit\">Sign in</button>"
    "</form>"
    "<p><a href=\"/register\">Create an account</a></p>"
    "</main>"
    "</body></html>";

static void route_login_get(struct yloop_stream *s, int keep_alive)
{
    send_response(s, 200, "text/html; charset=utf-8",
                  LOGIN_HTML, sizeof(LOGIN_HTML) - 1, NULL, keep_alive);
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
static void route_login_get_with_error(struct yloop_stream *s,
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
        "<form method=\"post\" action=\"/login\">"
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
static int read_body(struct yloop_stream *s, char *dst, size_t n)
{
    size_t got = 0;
    while (got < n) {
        size_t k = yloop_read_some(s, dst + got, n - got);
        if (k == 0) return -1;
        got += k;
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
        size_t cl = hdrs[i].value_len < sizeof(tmp) - 1
                        ? hdrs[i].value_len : sizeof(tmp) - 1;
        memcpy(tmp, hdrs[i].value, cl);
        tmp[cl] = 0;
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

static void route_login_post(struct yloop *loop, struct yloop_stream *s,
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
        return;
    }

    /* Forward the browser's form payload verbatim to the gateway. */
    struct http_response resp;
    int rc = http_post(loop, &sud->gw, "/login",
                       "application/x-www-form-urlencoded",
                       NULL, NULL, body, body_len, &resp);
    if (rc != 0) {
        http_response_free(&resp);
        free(username);
        route_login_get_with_error(s, "gateway unreachable", keep_alive);
        return;
    }

    if (resp.status == 303 && resp.set_cookie[0]) {
        char hdrs[1024];
        build_auth_cookies(hdrs, sizeof(hdrs), resp.set_cookie, username);
        http_response_free(&resp);
        free(username);
        send_redirect(s, "/repos", hdrs, keep_alive);
        return;
    }

    http_response_free(&resp);
    free(username);
    route_login_get_with_error(s, "invalid username or password", keep_alive);
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
    "<form method=\"post\" action=\"/register\">"
    "<label>Username <input name=\"username\" autofocus></label>"
    "<label>Password <input type=\"password\" name=\"password\"></label>"
    "<button type=\"submit\">Create account</button>"
    "</form>"
    "<p>Already have an account? <a href=\"/login\">Sign in</a>.</p>"
    "</main>"
    "</body></html>";

static void route_register_get(struct yloop_stream *s, int keep_alive)
{
    send_response(s, 200, "text/html; charset=utf-8",
                  REGISTER_HTML, sizeof(REGISTER_HTML) - 1, NULL, keep_alive);
}

/* Register page with an error banner spliced in before the form. `err`
 * is HTML-escaped (gateway errors echo the submitted username). */
static void route_register_get_with_error(struct yloop_stream *s,
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
        "<form method=\"post\" action=\"/register\">"
        "<label>Username <input name=\"username\" autofocus></label>"
        "<label>Password <input type=\"password\" name=\"password\"></label>"
        "<button type=\"submit\">Create account</button>"
        "</form>"
        "<p>Already have an account? <a href=\"/login\">Sign in</a>.</p>"
        "</main></body></html>",
        escaped);
    if (n <= 0) return;
    send_response(s, 200, "text/html; charset=utf-8", body, (size_t)n, NULL, keep_alive);
}

static void route_register_post(struct yloop *loop, struct yloop_stream *s,
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
        return;
    }

    struct http_response resp;
    int rc = http_post(loop, &sud->gw, "/register",
                       "application/x-www-form-urlencoded",
                       NULL, NULL, body, body_len, &resp);
    if (rc != 0) {
        http_response_free(&resp);
        free(username);
        route_register_get_with_error(s, "gateway unreachable", keep_alive);
        return;
    }

    if (resp.status == 303 && resp.set_cookie[0]) {
        char hdrs[1024];
        build_auth_cookies(hdrs, sizeof(hdrs), resp.set_cookie, username);
        http_response_free(&resp);
        free(username);
        send_redirect(s, "/repos", hdrs, keep_alive);
        return;
    }

    http_response_free(&resp);
    free(username);
    route_register_get_with_error(s,
        "could not create account (the username may already be taken)", keep_alive);
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
static long rpc_result_int(struct yloop *loop, const struct serve_ud *sud,
                           const char *sid, const char *rpc_path,
                           const char *args_json, long fallback);
static char *rpc_result_str(struct yloop *loop, const struct serve_ud *sud,
                            const char *sid, const char *rpc_path,
                            const char *args_json, int *was_error);
static uint32_t hash_username(const char *s);
static int service_active(const struct serve_ud *sud, const char *name);
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
                "<a class=\"brand\" href=\"/repos\">picoforge</a>");
    if (uname && *uname) {
        buf_puts(b, "<form class=\"global-search\" method=\"get\" action=\"/search\">"
                    "<input name=\"q\" placeholder=\"Search or jump to\xe2\x80\xa6\" "
                    "aria-label=\"Search\"></form>"
                    "<nav class=\"top-actions\">");
        /* "New" is an app-space action — omit it in the admin area so admin
         * chrome doesn't carry user-space project actions. */
        if (!in_admin_area)
            buf_puts(b, "<a class=\"btn small\" href=\"/repos/new\">New</a>");
        /* Only a site admin gets the Admin entry point — a regular user must
         * not even see the link (the pages 403 anyway, but the nav must not
         * advertise admin access). */
        if (is_admin)
            buf_puts(b, "<a href=\"/admin\">Admin</a>");
        buf_puts(b, "<span class=\"user\">");
        buf_esc(b, uname);
        buf_puts(b, "</span>"
                    "<form method=\"post\" action=\"/logout\">"
                    "<button class=\"link\" type=\"submit\">Sign out</button>"
                    "</form></nav>");
    } else {
        buf_puts(b, "<nav class=\"top-actions\"><a href=\"/login\">Sign in</a></nav>");
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
        {"Projects",  "/repos",            "projects"},
        {"Issues",    "/dashboard/issues", "issues"},
        {"Pipelines", "/dashboard/runs",   "runs"},
    };
    static const struct nav ADMIN[] = {
        {"Overview",     "/admin",          "admin-overview"},
        {"Users",        "/admin/users",    "admin-users"},
        {"Repositories", "/admin/repos",    "admin-repos"},
        {"Tokens",       "/admin/tokens",   "admin-tokens"},
        {"Services",     "/admin/services", "admin-services"},
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
                    "<a href=\"/repos\">\xe2\x86\x90 Back to app</a></nav>");
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

static void render_shell_close(struct yloop_stream *s, struct buf *b, int keep_alive)
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
static void send_forbidden(struct yloop_stream *s, const char *uname, int keep_alive)
{
    struct buf b; buf_init(&b);
    render_shell_open(&b, "Forbidden", uname, NULL, /*is_admin=*/0);
    render_page_header(&b, "Forbidden",
                       "You do not have permission to view this page.", NULL);
    buf_puts(&b, "<p class=\"muted\">This area is restricted to site administrators. "
                 "<a href=\"/repos\">Back to your repositories</a>.</p>"
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
static int require_admin(struct yloop_stream *s, const struct claims *cl, int keep_alive)
{
    if (!cl->uid) { send_redirect(s, "/login", NULL, keep_alive); return 0; }
    if (!cl->is_admin) {
        send_forbidden(s, cl->username[0] ? cl->username : NULL, keep_alive);
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
    buf_puts(b, "/new\">New file</a>"
                "<form method=\"post\" action=\"/");
    buf_esc(b, acct); buf_puts(b, "/"); buf_esc(b, repo);
    buf_puts(b, "/runs/new\"><button class=\"btn\" type=\"submit\">Run pipeline</button>"
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
        {"Code", "", "code", -1},
        {"Issues", "/issues", "issues", issue_count},
        {"Pipelines", "/runs", "runs", run_count},
        {"Settings", "/settings", "settings", -1},
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
static void repo_tab_counts(struct yloop *loop, const struct serve_ud *sud,
                            const char *sid, uint32_t rid,
                            long *issues_out, long *runs_out)
{
    char args[48];
    snprintf(args, sizeof(args), "[%u]", rid);
    *issues_out = service_active(sud, "issues")
        ? rpc_result_int(loop, sud, sid, "issues.issues.count_open_in_repo", args, -1)
        : -1;
    if (service_active(sud, "git_pipeline")) {
        long p = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
        long r = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
        *runs_out = p + r;
    } else {
        *runs_out = -1;
    }
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

/* GET /repos — the signed-in user's repositories, listed by name via
 * git_repo.git_repo.list_for_owner, with a create form and the repo count.
 * All data comes from the gateway over /_rpc; the webapp holds no state.
 * `sid` NULL/empty → not signed in → bounce to /login. */
static void route_repos_get(struct yloop *loop, struct yloop_stream *s,
                            const struct serve_ud *sud, const char *sid,
                            const char *uname, uint32_t owner_uid, int is_admin,
                            int keep_alive)
{
    if (!sid || !*sid || !owner_uid) {
        send_redirect(s, "/login", NULL, keep_alive);
        return;
    }

    long total = rpc_result_int(loop, sud, sid, "git_repo.git_repo.count_total", "[]", -1);

    /* List THIS user's repos by name (newline-separated). The owner key is
     * the session-resolved uid, not anything cookie-derived. */
    char args[48];
    snprintf(args, sizeof(args), "[%u]", owner_uid);
    char *names = rpc_result_str(loop, sud, sid, "git_repo.git_repo.list_for_owner", args, NULL);

    struct buf b; buf_init(&b);
    render_shell_open(&b, "Repositories", uname, "projects", is_admin);
    render_page_header(&b, "Repositories", "Repositories you own across the mesh.",
                       "<a class=\"btn primary\" href=\"/repos/new\">New repository</a>");

    char meta[64];
    if (total >= 0) snprintf(meta, sizeof(meta), "%ld total in the mesh", total);
    else            meta[0] = 0;
    panel_open(&b, "Your repositories", meta[0] ? meta : NULL);

    int any = 0;
    if (names && *names) {
        buf_puts(&b, "<table class=\"file-table\"><tbody>");
        char *save = NULL;
        for (char *line = strtok_r(names, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!*line) continue;
            any = 1;
            buf_puts(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td><td><a class=\"file-name dir\" href=\"/");
            buf_esc(&b, uname); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "\">");
            buf_esc(&b, uname); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "</a></td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    if (!any)
        buf_puts(&b, "<p class=\"muted\">No repositories yet — use "
                     "\xe2\x80\x9cNew repository\xe2\x80\x9d to create one.</p>");
    panel_close(&b);
    free(names);
    render_shell_close(s, &b, keep_alive);
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

/* FNV-1a 32 of a username → uid. MUST match the gateway's hash_username()
 * so an account-landing page resolves to the same owner uid the gateway
 * and git_repo backend key on (see memory repo-id-shared-hash). Routes through
 * the single shared primitive (picomesh_fnv1a32) so the hash cannot drift from
 * the gateway's; the uid==0 (anonymous) reservation is the only local policy. */
static uint32_t hash_username(const char *s)
{
    uint32_t h = picomesh_fnv1a32(s);
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
static char *rpc_result_str(struct yloop *loop, const struct serve_ud *sud,
                            const char *sid, const char *rpc_path,
                            const char *args_json, int *was_error)
{
    if (was_error) *was_error = 0;
    /* Body sized to hold args_json (which may carry an escaped file). */
    size_t need = strlen(rpc_path) + strlen(args_json) + 32;
    char *body = malloc(need);
    if (!body) { if (was_error) *was_error = 1; return NULL; }
    int n = snprintf(body, need, "{\"path\":\"%s\",\"args\":%s}", rpc_path, args_json);
    if (n <= 0 || (size_t)n >= need) { free(body); if (was_error) *was_error = 1; return NULL; }

    struct http_response resp;
    int rc = http_post_json(loop, &sud->gw, "/_rpc", NULL, sid, body, (size_t)n, &resp);
    free(body);
    if (rc != 0) { http_response_free(&resp); if (was_error) *was_error = 1; return NULL; }

    char *out = NULL;
    if (resp.body) {
        struct yjson_doc *doc = yjson_parse(resp.body, resp.body_len);
        if (doc) {
            const struct yjson_value *root = yjson_doc_root(doc);
            const struct yjson_value *r = yjson_object_get(root, "result");
            const struct yjson_value *err = yjson_object_get(root, "error");
            if (r) {
                const char *sv = yjson_as_string(r, NULL);
                if (sv) out = strdup(sv);
            } else if (err && was_error) {
                *was_error = 1;
            }
            yjson_doc_free(doc);
        }
    }
    http_response_free(&resp);
    return out;
}

/* Perform an RPC and return the parsed response document so the caller can
 * walk a structured `result` (e.g. a JSON array). `*result_out` points at
 * the `result` value inside the returned doc (any JSON type, NULL if
 * absent). The caller owns the doc and must yjson_doc_free it; values
 * borrowed from it are valid until then. NULL on transport/parse error. */
static struct yjson_doc *rpc_result_doc(struct yloop *loop, const struct serve_ud *sud,
                                        const char *sid, const char *rpc_path,
                                        const char *args_json,
                                        const struct yjson_value **result_out)
{
    if (result_out) *result_out = NULL;
    size_t need = strlen(rpc_path) + strlen(args_json) + 32;
    char *body = malloc(need);
    if (!body) return NULL;
    int n = snprintf(body, need, "{\"path\":\"%s\",\"args\":%s}", rpc_path, args_json);
    if (n <= 0 || (size_t)n >= need) { free(body); return NULL; }

    struct http_response resp;
    int rc = http_post_json(loop, &sud->gw, "/_rpc", NULL, sid, body, (size_t)n, &resp);
    free(body);
    if (rc != 0) { http_response_free(&resp); return NULL; }

    struct yjson_doc *doc = NULL;
    if (resp.body) {
        doc = yjson_parse(resp.body, resp.body_len);
        if (doc && result_out)
            *result_out = yjson_object_get(yjson_doc_root(doc), "result");
    }
    http_response_free(&resp);
    return doc;
}

/* Split "<account>/<repo>[/<verb>]" out of a path (query stripped). On
 * success fills acct/repo/verb (verb "" if none) and returns 1. */
static int parse_repo_path(const char *path, size_t path_len,
                           char *acct, size_t acct_cap,
                           char *repo, size_t repo_cap,
                           char *verb, size_t verb_cap)
{
    char tmp[1024];
    size_t plen = path_len;
    const char *q = memchr(path, '?', path_len);
    if (q) plen = (size_t)(q - path);
    if (plen == 0 || plen >= sizeof(tmp)) return 0;
    memcpy(tmp, path, plen); tmp[plen] = 0;

    char *segs[4] = {0}; int ns = 0;
    for (char *tok = strtok(tmp, "/"); tok && ns < 4; tok = strtok(NULL, "/"))
        segs[ns++] = tok;
    if (ns < 2 || ns > 3) return 0;
    if (strstr(segs[0], "..") || strstr(segs[1], "..")) return 0;
    snprintf(acct, acct_cap, "%s", segs[0]);
    snprintf(repo, repo_cap, "%s", segs[1]);
    snprintf(verb, verb_cap, "%s", ns == 3 ? segs[2] : "");
    return 1;
}

/* GET /<account>/<repo>[?dir=<subdir>] — file browser. Lists the tree via
 * git_repo.git_repo.read_tree; dirs link deeper, files link to /edit. */
static void route_repo_browse(struct yloop *loop, struct yloop_stream *s,
                              const struct serve_ud *sud, const char *sid,
                              const char *uname, const char *acct, const char *repo,
                              const char *full_path, int is_admin, int keep_alive)
{
    uint32_t rid = repo_hash(acct, repo);
    char *dir = query_get(full_path, "dir");
    const char *dirv = dir ? dir : "";

    char dir_esc[1024];
    if (!json_escape(dir_esc, sizeof(dir_esc), dirv)) { free(dir); send_text(s, 400, "bad dir\n", keep_alive); return; }
    char args[1200];
    snprintf(args, sizeof(args), "[%u,\"\",\"%s\"]", rid, dir_esc);
    int err = 0;
    char *tree = rpc_result_str(loop, sud, sid, "git_repo.git_repo.read_tree", args, &err);

    long ic = -1, rc = -1;
    repo_tab_counts(loop, sud, sid, rid, &ic, &rc);

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "%s/%s", acct, repo);
    render_shell_open(&b, title, uname, "projects", is_admin);
    render_project_header(&b, acct, repo, rid);
    render_project_tabs(&b, acct, repo, "code", ic, rc);

    /* File panel — its header carries the current path/branch + the New
     * file action; the tree renders as a dense file table. */
    buf_puts(&b, "<section class=\"panel repo-browser\"><header class=\"panel-header\"><div>"
                 "<strong>Files</strong> ");
    if (*dirv) { buf_puts(&b, "<span class=\"muted\">/ "); buf_esc(&b, dirv); buf_puts(&b, "</span>"); }
    else       { buf_puts(&b, "<span class=\"branch\">main</span>"); }
    buf_puts(&b, "</div><div class=\"panel-actions\">");
    {
        char href[1200];
        if (*dirv) snprintf(href, sizeof(href), "/%s/%s/new?dir=%s", acct, repo, dirv);
        else       snprintf(href, sizeof(href), "/%s/%s/new", acct, repo);
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
                                      "<td><a class=\"file-name dir\" href=\"/%s/%s?dir=%s\">..</a></td>"
                                      "<td class=\"muted\">-</td><td class=\"muted\">-</td></tr>", acct, repo, up);
            else       buf_printf(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td>"
                                      "<td><a class=\"file-name dir\" href=\"/%s/%s\">..</a></td>"
                                      "<td class=\"muted\">-</td><td class=\"muted\">-</td></tr>", acct, repo);
        }
        char *save = NULL;
        for (char *line = strtok_r(tree, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *tab = strchr(line, '\t');
            if (!tab) continue;
            *tab = 0;
            const char *type = line, *nm = tab + 1;
            char child[1024];
            if (*dirv) snprintf(child, sizeof(child), "%s/%s", dirv, nm);
            else       snprintf(child, sizeof(child), "%s", nm);
            int is_dir = strcmp(type, "tree") == 0;
            buf_puts(&b, "<tr><td class=\"ic\">");
            buf_puts(&b, is_dir ? "\xf0\x9f\x93\x81" : "\xf0\x9f\x93\x84");
            buf_puts(&b, is_dir ? "</td><td><a class=\"file-name dir\" href=\"/"
                                : "</td><td><a class=\"file-name file\" href=\"/");
            buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
            if (is_dir) { buf_puts(&b, "?dir="); buf_esc(&b, child); }
            else        { buf_puts(&b, "/edit?path="); buf_esc(&b, child); }
            buf_puts(&b, "\">"); buf_esc(&b, nm);
            buf_puts(&b, "</a></td><td class=\"muted\">-</td><td class=\"muted\">-</td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    buf_puts(&b, "</section>");
    free(tree); free(dir);
    render_shell_close(s, &b, keep_alive);
}

/* Emit the Monaco editor page. `path` may be empty for a brand-new file
 * where the user types the name; `content` is the current text (empty for
 * new). `is_new` controls the heading + whether the path field is editable. */
static void render_editor_page(struct yloop_stream *s, const char *uname,
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
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo); buf_puts(&b, "\">");
    buf_esc(&b, repo); buf_puts(&b, "</a>");
    if (path && *path) { buf_puts(&b, " <span>/</span> <strong>"); buf_esc(&b, path); buf_puts(&b, "</strong>"); }
    buf_puts(&b, "</div><h1>");
    buf_puts(&b, is_new ? "New file" : "Edit file");
    buf_puts(&b, "</h1></div><a class=\"btn\" href=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo); buf_puts(&b, "\">Cancel</a></header>");

    buf_puts(&b, "<form id=\"f\" class=\"editor-form\" method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo); buf_puts(&b, "/edit\">");

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
static void route_repo_edit_get(struct yloop *loop, struct yloop_stream *s,
                                const struct serve_ud *sud, const char *sid,
                                const char *uname, const char *acct, const char *repo,
                                const char *verb, const char *full_path,
                                int is_admin, int keep_alive)
{
    if (!sid || !*sid) { send_redirect(s, "/login", NULL, keep_alive); return; }
    uint32_t rid = repo_hash(acct, repo);

    if (strcmp(verb, "new") == 0) {
        char *dir = query_get(full_path, "dir");
        char seed[1024] = {0};
        if (dir && *dir) snprintf(seed, sizeof(seed), "%s/", dir);
        render_editor_page(s, uname, acct, repo, seed, "", /*is_new=*/1, is_admin, keep_alive);
        free(dir);
        return;
    }

    char *path = query_get(full_path, "path");
    if (!path || !*path) {
        free(path);
        render_editor_page(s, uname, acct, repo, "", "", /*is_new=*/1, is_admin, keep_alive);
        return;
    }
    char path_esc[1024];
    if (!json_escape(path_esc, sizeof(path_esc), path)) { free(path); send_text(s, 400, "bad path\n", keep_alive); return; }
    char args[1200];
    snprintf(args, sizeof(args), "[%u,\"\",\"%s\"]", rid, path_esc);
    int err = 0;
    char *content = rpc_result_str(loop, sud, sid, "git_repo.git_repo.read_file", args, &err);
    render_editor_page(s, uname, acct, repo, path, content ? content : "",
                       /*is_new=*/(content == NULL), is_admin, keep_alive);
    free(content); free(path);
}

/* POST /<account>/<repo>/edit — save a file (put_file) then redirect to
 * the browser. Body is form-encoded: path, content, message. */
static void route_repo_edit_post(struct yloop *loop, struct yloop_stream *s,
                                 const struct serve_ud *sud, const char *sid,
                                 const char *acct, const char *repo,
                                 const char *body, size_t body_len, int keep_alive)
{
    if (!sid || !*sid) { send_redirect(s, "/login", NULL, keep_alive); return; }
    uint32_t rid = repo_hash(acct, repo);

    char *path = form_get(body, body_len, "path");
    char *content = form_get(body, body_len, "content");
    char *message = form_get(body, body_len, "message");
    if (!path || !*path) {
        free(path); free(content); free(message);
        send_text(s, 400, "path required\n", keep_alive);
        return;
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
        char *oid = rpc_result_str(loop, sud, sid, "git_repo.git_repo.put_file", args, &err);
        free(oid);
        if (err || !oid) ok = 0;
    }
    free(pe); free(ce); free(me); free(args);

    if (!ok) { free(path); free(content); free(message);
               send_text(s, 500, "save failed (forbidden or backend error)\n", keep_alive); return; }

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    char where[1200];
    if (slash) { *slash = 0; snprintf(where, sizeof(where), "/%s/%s?dir=%s", acct, repo, dir); }
    else       snprintf(where, sizeof(where), "/%s/%s", acct, repo);
    free(path); free(content); free(message);
    send_redirect(s, where, NULL, keep_alive);
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
static long rpc_result_int(struct yloop *loop, const struct serve_ud *sud,
                           const char *sid, const char *rpc_path,
                           const char *args_json, long fallback)
{
    size_t need = strlen(rpc_path) + strlen(args_json) + 32;
    char *body = malloc(need);
    if (!body) return fallback;
    int n = snprintf(body, need, "{\"path\":\"%s\",\"args\":%s}", rpc_path, args_json);
    if (n <= 0 || (size_t)n >= need) { free(body); return fallback; }

    struct http_response resp;
    int rc = http_post_json(loop, &sud->gw, "/_rpc", NULL, sid, body, (size_t)n, &resp);
    free(body);
    if (rc != 0) { http_response_free(&resp); return fallback; }

    long out = fallback;
    if (resp.body) {
        struct yjson_doc *doc = yjson_parse(resp.body, resp.body_len);
        if (doc) {
            const struct yjson_value *root = yjson_doc_root(doc);
            const struct yjson_value *r = yjson_object_get(root, "result");
            if (r) out = (long)yjson_as_int(r, fallback);
            yjson_doc_free(doc);
        }
    }
    http_response_free(&resp);
    return out;
}

/* Resolve the opaque session token (hex-string cookie value) back to a uid
 * via the gateway's /_whoami. 0 when unknown / not signed in. /_whoami is the
 * gateway's external→internal identity route; we do NOT call session.lookup
 * over /_rpc — that is an authenticator-internal credential exchange the
 * gateway blocks from public RPC (issue #19). */
static uint32_t resolve_uid(struct yloop *loop, const struct serve_ud *sud,
                            const char *sid)
{
    if (!sid || !*sid) return 0;
    struct http_response resp;
    int rc = http_post(loop, &sud->gw, "/_whoami", "application/json", NULL, sid, "", 0, &resp);
    if (rc != 0) { http_response_free(&resp); return 0; }
    uint32_t uid = 0;
    if (resp.body) {
        struct yjson_doc *doc = yjson_parse(resp.body, resp.body_len);
        if (doc) {
            uid = (uint32_t)yjson_as_int(yjson_object_get(yjson_doc_root(doc), "uid"), 0);
            yjson_doc_free(doc);
        }
    }
    http_response_free(&resp);
    return uid;
}

/* Resolve the caller's authenticated claims (uid, username, admin bit) via
 * the gateway's /_whoami. The sid is forwarded by http_post as the
 * picomesh-sid header; the gateway maps it to the live session. On any
 * transport/parse failure the claims stay zeroed (anonymous), so pages and
 * the admin gate fail closed. This is the ONLY identity source the webapp
 * trusts — the picomesh-uname cookie is never an authority. */
static void resolve_claims(struct yloop *loop, const struct serve_ud *sud,
                           const char *sid, struct claims *out)
{
    memset(out, 0, sizeof(*out));
    if (!sid || !*sid) return;
    struct http_response resp;
    int rc = http_post(loop, &sud->gw, "/_whoami",
                       "application/json", NULL, sid, "", 0, &resp);
    if (rc != 0) { http_response_free(&resp); return; }
    if (resp.body) {
        struct yjson_doc *doc = yjson_parse(resp.body, resp.body_len);
        if (doc) {
            const struct yjson_value *root = yjson_doc_root(doc);
            out->uid = (uint32_t)yjson_as_int(yjson_object_get(root, "uid"), 0);
            const char *u = yjson_as_string(yjson_object_get(root, "username"), NULL);
            if (u) snprintf(out->username, sizeof(out->username), "%s", u);
            out->is_admin = yjson_as_bool(yjson_object_get(root, "is_admin"), 0);
            yjson_doc_free(doc);
        }
    }
    http_response_free(&resp);
}

/* Populate the active-service set from the gateway's /_describe (the list
 * of {service, source} objects). Cached after the first successful fetch.
 * /_describe answers GET or POST; we POST an empty body. On failure the
 * set stays empty and pages fail closed (404) — the safe default. */
static void services_ensure(struct yloop *loop, struct serve_ud *sud)
{
    time_t now = time(NULL);
    if (sud->services.loaded &&
        (now - sud->services.loaded_at) < WEBAPP_SERVICES_TTL_SEC)
        return;
    struct http_response resp;
    int rc = http_post(loop, &sud->gw, "/_describe",
                       "application/json", NULL, NULL, "", 0, &resp);
    /* Transport failure → keep the previous set (fail to the last known
     * topology rather than dropping every page). */
    if (rc != 0) { http_response_free(&resp); return; }
    if (resp.body) {
        struct yjson_doc *doc = yjson_parse(resp.body, resp.body_len);
        if (doc) {
            const struct yjson_value *root = yjson_doc_root(doc);
            const struct yjson_value *svcs = yjson_object_get(root, "services");
            size_t cnt = svcs ? yjson_array_size(svcs) : 0;
            /* Rebuild from scratch — a refresh can add OR drop services. */
            sud->services.n = 0;
            for (size_t i = 0; i < cnt &&
                 sud->services.n < sizeof(sud->services.names) / sizeof(sud->services.names[0]);
                 ++i) {
                const struct yjson_value *e = yjson_array_at(svcs, i);
                const char *nm = yjson_as_string(yjson_object_get(e, "service"), NULL);
                const char *src = yjson_as_string(yjson_object_get(e, "source"), NULL);
                if (nm && *nm) {
                    size_t slot = sud->services.n++;
                    snprintf(sud->services.names[slot], 64, "%s", nm);
                    snprintf(sud->services.sources[slot], 32, "%s", src && *src ? src : "-");
                }
            }
            sud->services.loaded = 1;
            sud->services.loaded_at = now;
            yjson_doc_free(doc);
        }
    }
    http_response_free(&resp);
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

/* Single-segment paths that are NOT user accounts (mirrors the gateway's
 * is_reserved_top), so `/login` etc. don't render an account-landing. */
static int is_reserved_top(const char *seg)
{
    static const char *const RESERVED[] = {
        "login", "logout", "register", "repos", "admin", "static",
        "search", "dashboard", "favicon.ico", "robots.txt", NULL,
    };
    for (size_t i = 0; RESERVED[i]; ++i)
        if (strcmp(RESERVED[i], seg) == 0) return 1;
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
static void page_close_and_send(struct yloop_stream *s, struct buf *b, int keep_alive)
{
    render_shell_close(s, b, keep_alive);
}

/* GET /<account> — account landing: the account's repositories listed by
 * name (git_repo.git_repo.list_for_owner) + the count. */
static void page_account_landing(struct yloop *loop, struct yloop_stream *s,
                                 const struct serve_ud *sud, const char *sid,
                                 const char *uname, const char *acct, int is_admin,
                                 int keep_alive)
{
    uint32_t owner = hash_username(acct);
    char args[48];
    snprintf(args, sizeof(args), "[%u]", owner);
    long repos = rpc_result_int(loop, sud, sid, "git_repo.git_repo.count_for_owner", args, -1);
    char *names = rpc_result_str(loop, sud, sid, "git_repo.git_repo.list_for_owner", args, NULL);

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
            buf_puts(&b, "\">"); buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "</a></td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    if (!any) buf_puts(&b, "<p class=\"muted\">No repositories.</p>");
    panel_close(&b);
    free(names);
    page_close_and_send(s, &b, keep_alive);
}

/* GET /<account>/<repo>/issues[?status=open|closed] — open-issue count for
 * the repo, plus the new/close action forms. Data: issues.issues.count_open_in_repo. */
static void page_repo_issues(struct yloop *loop, struct yloop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *uname, const char *acct, const char *repo,
                             const char *full_path, int is_admin, int keep_alive)
{
    (void)full_path;
    uint32_t rid = repo_hash(acct, repo);
    char args[48];
    snprintf(args, sizeof(args), "[%u]", rid);
    long open_n = rpc_result_int(loop, sud, sid, "issues.issues.count_open_in_repo", args, -1);

    long rc = -1;
    if (service_active(sud, "git_pipeline")) {
        long p = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
        long r = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
        rc = p + r;
    }

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "Issues — %s/%s", acct, repo);
    page_open(&b, title, uname, "projects", is_admin);
    render_project_header(&b, acct, repo, rid);
    render_project_tabs(&b, acct, repo, "issues", open_n, rc);

    /* Issues work queue. List API isn't available yet, so the rows are an
     * empty/summary state — but the panel header, filters, .issue-list and
     * the New issue action are the final shape. */
    buf_puts(&b, "<section class=\"panel\"><header class=\"panel-header\"><div><strong>Issues</strong> ");
    if (open_n >= 0) buf_printf(&b, "<span class=\"muted\">%ld open</span>", open_n);
    else             buf_puts(&b, "<span class=\"muted\">service unreachable</span>");
    buf_puts(&b, "</div><div class=\"panel-actions\"><form method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "/issues/new\"><button class=\"primary\" type=\"submit\">New issue</button></form></div></header>");

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
    buf_puts(&b, "/issues/close\"><label>Close issue <input type=\"number\" name=\"issue_id\" required></label>"
                 "<button type=\"submit\">Close</button></form></div>");
    buf_puts(&b, "</section>");
    page_close_and_send(s, &b, keep_alive);
}

/* GET /<account>/<repo>/runs — pipeline run counts + enqueue/lease forms.
 * Data: git_pipeline.git_pipeline.count_pending/running/done (global counts today). */
static void page_repo_runs(struct yloop *loop, struct yloop_stream *s,
                           const struct serve_ud *sud, const char *sid,
                           const char *uname, const char *acct, const char *repo,
                           const char *full_path, int is_admin, int keep_alive)
{
    (void)full_path;
    long q = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
    long r = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
    long d = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_done",    "[]", 0);

    uint32_t rid = repo_hash(acct, repo);
    long ic = -1;
    if (service_active(sud, "issues")) {
        char ia[48]; snprintf(ia, sizeof(ia), "[%u]", rid);
        ic = rpc_result_int(loop, sud, sid, "issues.issues.count_open_in_repo", ia, -1);
    }

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "Pipelines — %s/%s", acct, repo);
    page_open(&b, title, uname, "projects", is_admin);
    render_project_header(&b, acct, repo, rid);
    render_project_tabs(&b, acct, repo, "runs", ic, q + r);

    /* Pipeline runs. No per-run list API yet, so the rows summarize the
     * state counts — but the panel header, .pipeline-table and the Run
     * pipeline action are the final shape. */
    buf_puts(&b, "<section class=\"panel\"><header class=\"panel-header\"><div>"
                 "<strong>Pipeline runs</strong> <span class=\"muted\">queued, running, finished</span>"
                 "</div><div class=\"panel-actions\"><form method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "/runs/new\"><button class=\"primary\" type=\"submit\">Run pipeline</button></form></div></header>");
    buf_printf(&b,
        "<table class=\"pipeline-table\"><thead><tr>"
        "<th>Status</th><th>Run</th><th>Ref</th><th>Runner</th><th>Count</th></tr></thead><tbody>"
        "<tr><td><span class=\"badge queued\">queued</span></td><td class=\"muted\">-</td>"
        "<td><code>main</code></td><td class=\"muted\">-</td><td>%ld</td></tr>"
        "<tr><td><span class=\"badge running\">running</span></td><td class=\"muted\">-</td>"
        "<td><code>main</code></td><td class=\"muted\">-</td><td>%ld</td></tr>"
        "<tr><td><span class=\"badge succeeded\">finished</span></td><td class=\"muted\">-</td>"
        "<td><code>main</code></td><td class=\"muted\">-</td><td>%ld</td></tr>"
        "</tbody></table>", q, r, d);
    /* Lease action for a runner, until per-run controls exist. */
    buf_puts(&b, "<div class=\"panel-body\"><form class=\"inline-form\" method=\"post\" action=\"/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "/runs/lease\"><label>Lease next job — runner uid "
                 "<input type=\"number\" name=\"runner\" value=\"1\"></label>"
                 "<button type=\"submit\">Lease</button></form></div>");
    buf_puts(&b, "</section>");
    page_close_and_send(s, &b, keep_alive);
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
static void page_admin_overview(struct yloop *loop, struct yloop_stream *s,
                                const struct serve_ud *sud, const char *sid,
                                const char *uname, int keep_alive)
{
    long users = service_active(sud, "accounts")
        ? rpc_result_int(loop, sud, sid, "accounts.accounts.count", "[]", -1) : -1;
    long repos = service_active(sud, "git_repo")
        ? rpc_result_int(loop, sud, sid, "git_repo.git_repo.count_total", "[]", -1) : -1;
    long toks  = service_active(sud, "personal_access_tokens")
        ? rpc_result_int(loop, sud, sid, "personal_access_tokens.personal_access_tokens.count_active", "[]", -1) : -1;
    long runs = -1;
    if (service_active(sud, "git_pipeline")) {
        long p = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
        long r = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
        long d = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_done",    "[]", 0);
        runs = p + r + d;
    }

    struct buf b; buf_init(&b);
    render_shell_open(&b, "Admin", uname, "admin-overview", /*is_admin=*/1);
    render_page_header(&b, "Admin", "Operational overview of this picoforge deployment.", NULL);
    buf_puts(&b, "<section class=\"stats-grid\">");
    stat_tile(&b, users, "users");
    stat_tile(&b, repos, "repositories");
    stat_tile(&b, toks,  "active tokens");
    stat_tile(&b, (long)sud->services.n, "services");
    stat_tile(&b, runs,  "pipeline runs");
    buf_puts(&b, "</section>");

    panel_open(&b, "Manage", NULL);
    buf_puts(&b, "<table class=\"file-table\"><tbody>"
                 "<tr><td><a class=\"file-name\" href=\"/admin/users\">Users</a></td>"
                 "<td class=\"muted\">accounts &amp; registration</td></tr>"
                 "<tr><td><a class=\"file-name\" href=\"/admin/repos\">Repositories</a></td>"
                 "<td class=\"muted\">all repositories in the mesh</td></tr>"
                 "<tr><td><a class=\"file-name\" href=\"/admin/tokens\">Tokens</a></td>"
                 "<td class=\"muted\">personal access tokens</td></tr>"
                 "<tr><td><a class=\"file-name\" href=\"/admin/services\">Services</a></td>"
                 "<td class=\"muted\">mesh service health</td></tr>"
                 "</tbody></table>");
    panel_close(&b);
    render_shell_close(s, &b, keep_alive);
}

/* GET /admin/users — accounts administration. Data: accounts.accounts.count. */
static void page_admin_users(struct yloop *loop, struct yloop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *uname, int keep_alive)
{
    long users = rpc_result_int(loop, sud, sid, "accounts.accounts.count", "[]", -1);
    /* The real user roster: a JSON array of {"uid":<n>,"name":"<s>"}. */
    const struct yjson_value *roster = NULL;
    struct yjson_doc *roster_doc =
        rpc_result_doc(loop, sud, sid, "accounts.accounts.list", "[]", &roster);

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
        size_t roster_n = roster ? yjson_array_size(roster) : 0;
        for (size_t i = 0; i < roster_n; ++i) {
            const struct yjson_value *e = yjson_array_at(roster, i);
            const char *name = yjson_as_string(yjson_object_get(e, "name"), "");
            char uid_buf[24];
            snprintf(uid_buf, sizeof(uid_buf), "%lld",
                     (long long)yjson_as_int(yjson_object_get(e, "uid"), 0));
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
                 "<a href=\"/register\">/register</a> (username + password). Admin-side "
                 "user creation with credentials and role assignment is not implemented "
                 "yet.</p>");
    panel_close(&b);
    yjson_doc_free(roster_doc);
    page_close_and_send(s, &b, keep_alive);
}

/* GET /admin/repos — repositories administration. Data:
 * git_repo.git_repo.count_total (no global list API yet). */
static void page_admin_repos(struct yloop *loop, struct yloop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *uname, int keep_alive)
{
    long total = rpc_result_int(loop, sud, sid, "git_repo.git_repo.count_total", "[]", -1);

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
}

/* GET /admin/tokens — personal access token administration. Data:
 * personal_access_tokens.personal_access_tokens.count_active. */
static void page_admin_tokens(struct yloop *loop, struct yloop_stream *s,
                              const struct serve_ud *sud, const char *sid,
                              const char *uname, int keep_alive)
{
    int active = service_active(sud, "personal_access_tokens");
    long toks = active
        ? rpc_result_int(loop, sud, sid, "personal_access_tokens.personal_access_tokens.count_active", "[]", -1)
        : -1;

    struct buf b; buf_init(&b);
    page_open(&b, "Tokens", uname, "admin-tokens", /*is_admin=*/1);
    render_page_header(&b, "Tokens", "Personal access tokens issued by the gateway.", NULL);
    buf_puts(&b, "<section class=\"stats-grid\">");
    stat_tile(&b, toks, "active tokens");
    buf_puts(&b, "</section>");
    panel_open(&b, "Mint personal access token", NULL);
    if (!active) buf_puts(&b, "<p class=\"muted\">PAT service not active.</p>");
    buf_puts(&b, "<form class=\"form-grid\" method=\"post\" action=\"/admin/tokens/mint_pat\">"
                 "<label>uid <input type=\"number\" name=\"uid\" value=\"100\"></label>"
                 "<button type=\"submit\" class=\"primary\">Mint</button></form>");
    panel_close(&b);
    page_close_and_send(s, &b, keep_alive);
}

/* GET /<account>/<repo>/settings — repository settings overview. Read-only
 * project metadata (owner, default branch, visibility) + how to reach the
 * repo. No mutation surface yet; the tab exists so the project shell is
 * complete (Code / Issues / Pipelines / Settings). */
static void page_repo_settings(struct yloop *loop, struct yloop_stream *s,
                               const struct serve_ud *sud, const char *sid,
                               const char *uname, const char *acct, const char *repo,
                               int is_admin, int keep_alive)
{
    uint32_t rid = repo_hash(acct, repo);
    long ic = -1, rc = -1;
    repo_tab_counts(loop, sud, sid, rid, &ic, &rc);

    struct buf b; buf_init(&b);
    char title[160];
    snprintf(title, sizeof(title), "Settings — %s/%s", acct, repo);
    render_shell_open(&b, title, uname, "projects", is_admin);
    render_project_header(&b, acct, repo, rid);
    render_project_tabs(&b, acct, repo, "settings", ic, rc);

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
    buf_puts(&b, "\">/");
    buf_esc(&b, acct); buf_puts(&b, "/"); buf_esc(&b, repo);
    buf_puts(&b, "</a>.</p><p class=\"muted small\">Files are served over the gateway "
                 "<code>/_rpc</code> surface; direct git clone is not wired in this build.</p>");
    panel_close(&b);

    render_shell_close(s, &b, keep_alive);
}

/* GET /search?q=<term> — filter the signed-in user's repositories by name
 * substring (case-insensitive). Reuses git_repo.git_repo.list_for_owner over
 * the gateway and matches in-process; no backend search method is assumed.
 * Not signed in → bounce to /login. */
static void page_search(struct yloop *loop, struct yloop_stream *s,
                        const struct serve_ud *sud, const char *sid,
                        const char *uname, const char *full_path, int is_admin,
                        int keep_alive)
{
    if (!sid || !*sid || !uname || !*uname) {
        send_redirect(s, "/login", NULL, keep_alive);
        return;
    }
    char *q = query_get(full_path, "q");
    char ql[128];
    size_t qn = 0;
    for (const char *p = q ? q : ""; *p && qn + 1 < sizeof(ql); ++p) {
        char c = *p; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        ql[qn++] = c;
    }
    ql[qn] = 0;

    char args[48];
    snprintf(args, sizeof(args), "[%u]", hash_username(uname));
    char *names = rpc_result_str(loop, sud, sid, "git_repo.git_repo.list_for_owner", args, NULL);

    struct buf b; buf_init(&b);
    page_open(&b, "Search", uname, "projects", is_admin);
    render_page_header(&b, "Search", "Find a repository by name.", NULL);
    char metabuf[180];
    if (q && *q) snprintf(metabuf, sizeof(metabuf),
                          "Your repositories matching \xe2\x80\x9c%s\xe2\x80\x9d", q);
    else         snprintf(metabuf, sizeof(metabuf), "Your repositories");
    panel_open(&b, metabuf, NULL);
    int any = 0;
    if (names && *names) {
        buf_puts(&b, "<table class=\"file-table\"><tbody>");
        char *save = NULL;
        for (char *line = strtok_r(names, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!*line) continue;
            if (qn) {
                char low[256]; size_t ln = 0;
                for (const char *p = line; *p && ln + 1 < sizeof(low); ++p) {
                    char c = *p; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                    low[ln++] = c;
                }
                low[ln] = 0;
                if (!strstr(low, ql)) continue;
            }
            any = 1;
            buf_puts(&b, "<tr><td class=\"ic\">\xf0\x9f\x93\x81</td><td><a class=\"file-name dir\" href=\"/");
            buf_esc(&b, uname); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "\">"); buf_esc(&b, uname); buf_puts(&b, "/"); buf_esc(&b, line);
            buf_puts(&b, "</a></td></tr>");
        }
        buf_puts(&b, "</tbody></table>");
    }
    if (!any) buf_puts(&b, "<p class=\"muted\">No matching repositories.</p>");
    panel_close(&b);
    free(names); free(q);
    page_close_and_send(s, &b, keep_alive);
}

/* GET /repos/new — the create-repository form (the POST action is
 * webapp_repos_new on the same path). Needs a signed-in user. */
static void page_repos_new_form(struct yloop_stream *s, const char *uname,
                                int is_admin, int keep_alive)
{
    if (!uname || !*uname) { send_redirect(s, "/login", NULL, keep_alive); return; }
    struct buf b; buf_init(&b);
    render_shell_open(&b, "New repository", uname, "projects", is_admin);
    render_page_header(&b, "New repository", "Create a repository you own.", NULL);
    panel_open(&b, "Repository details", NULL);
    buf_puts(&b, "<form class=\"form-grid\" method=\"post\" action=\"/repos/new\">"
                 "<label>Name <input name=\"name\" placeholder=\"my-repo\" "
                 "pattern=\"[a-zA-Z0-9._-]{1,32}\" required autofocus></label>"
                 "<button type=\"submit\" class=\"primary\">Create repository</button></form>");
    panel_close(&b);
    render_shell_close(s, &b, keep_alive);
}

/* GET /admin/services — the live service roster discovered from the
 * gateway's /_describe. Force a fresh fetch here so the admin sees the
 * current topology, not whatever was cached within the TTL window. */
static void page_admin_services(struct yloop *loop, struct yloop_stream *s,
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
}

/* GET /dashboard/runs — global pipeline activity across the mesh. */
static void page_dashboard_runs(struct yloop *loop, struct yloop_stream *s,
                                const struct serve_ud *sud, const char *sid,
                                const char *uname, int is_admin, int keep_alive)
{
    long q = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_pending", "[]", 0);
    long r = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_running", "[]", 0);
    long d = rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.count_done",    "[]", 0);
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
        "</tbody></table>", q, r, d);
    panel_close(&b);
    render_shell_close(s, &b, keep_alive);
}

/* GET /dashboard/issues — open issues summed across the user's repos
 * (no global list API yet; aggregate per-repo counts). */
static void page_dashboard_issues(struct yloop *loop, struct yloop_stream *s,
                                  const struct serve_ud *sud, const char *sid,
                                  const char *uname, int is_admin, int keep_alive)
{
    char args[48];
    snprintf(args, sizeof(args), "[%u]", hash_username(uname));
    char *names = rpc_result_str(loop, sud, sid, "git_repo.git_repo.list_for_owner", args, NULL);

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
            uint32_t rid = repo_hash(uname, line);
            long open_n = -1;
            if (service_active(sud, "issues")) {
                char ia[48]; snprintf(ia, sizeof(ia), "[%u]", rid);
                open_n = rpc_result_int(loop, sud, sid, "issues.issues.count_open_in_repo", ia, -1);
            }
            buf_puts(&b, "<tr><td><a class=\"file-name\" href=\"/");
            buf_esc(&b, uname); buf_puts(&b, "/"); buf_esc(&b, line); buf_puts(&b, "/issues\">");
            buf_esc(&b, uname); buf_puts(&b, "/"); buf_esc(&b, line);
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
}


/* ---- action POSTs forwarded to the gateway /_rpc -------------------- *
 * Each fires one backend call then 303s back to the page. The gateway
 * resolves the sid → uid for auth context; methods that need the actor's
 * uid as an explicit arg (issues.open) get it from session.lookup. */

static void post_issue_new(struct yloop *loop, struct yloop_stream *s,
                           const struct serve_ud *sud, const char *sid,
                           const char *acct, const char *repo, int keep_alive)
{
    /* No valid session → no mutation. Never attribute an issue to a
     * fallback uid; an unresolved actor is an auth failure. */
    uint32_t uid = resolve_uid(loop, sud, sid);
    if (!uid) { send_redirect(s, "/login", NULL, keep_alive); return; }
    uint32_t rid = repo_hash(acct, repo);
    char args[64];
    snprintf(args, sizeof(args), "[%u,%u]", rid, uid);
    rpc_result_int(loop, sud, sid, "issues.issues.open", args, -1);
    char where[300];
    snprintf(where, sizeof(where), "/%s/%s/issues", acct, repo);
    send_redirect(s, where, NULL, keep_alive);
}

static void post_issue_close(struct yloop *loop, struct yloop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *acct, const char *repo,
                             const char *body, size_t body_len, int keep_alive)
{
    if (!resolve_uid(loop, sud, sid)) { send_redirect(s, "/login", NULL, keep_alive); return; }
    char *iid_s = form_get(body, body_len, "issue_id");
    if (iid_s && *iid_s) {
        char args[48];
        snprintf(args, sizeof(args), "[%lu]", strtoul(iid_s, NULL, 10));
        rpc_result_int(loop, sud, sid, "issues.issues.close", args, -1);
    }
    free(iid_s);
    char where[300];
    snprintf(where, sizeof(where), "/%s/%s/issues", acct, repo);
    send_redirect(s, where, NULL, keep_alive);
}

static void post_run_new(struct yloop *loop, struct yloop_stream *s,
                         const struct serve_ud *sud, const char *sid,
                         const char *acct, const char *repo, int keep_alive)
{
    if (!resolve_uid(loop, sud, sid)) { send_redirect(s, "/login", NULL, keep_alive); return; }
    char args[48];
    snprintf(args, sizeof(args), "[%u]", repo_hash(acct, repo));
    rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.enqueue", args, -1);
    char where[300];
    snprintf(where, sizeof(where), "/%s/%s/runs", acct, repo);
    send_redirect(s, where, NULL, keep_alive);
}

static void post_run_lease(struct yloop *loop, struct yloop_stream *s,
                           const struct serve_ud *sud, const char *sid,
                           const char *acct, const char *repo,
                           const char *body, size_t body_len, int keep_alive)
{
    if (!resolve_uid(loop, sud, sid)) { send_redirect(s, "/login", NULL, keep_alive); return; }
    char *runner_s = form_get(body, body_len, "runner");
    unsigned long runner = runner_s && *runner_s ? strtoul(runner_s, NULL, 10) : 1;
    if (!runner) runner = 1;
    free(runner_s);
    char args[48];
    snprintf(args, sizeof(args), "[%lu]", runner);
    rpc_result_int(loop, sud, sid, "git_pipeline.git_pipeline.lease", args, -1);
    char where[300];
    snprintf(where, sizeof(where), "/%s/%s/runs", acct, repo);
    send_redirect(s, where, NULL, keep_alive);
}

/* Forward a form POST verbatim to the gateway at `gw_path` carrying the
 * caller's sid, then 303 to `redirect_to`. Used for gateway-owned action
 * POSTs (admin register / mint_pat) the sidecar only relays — the gateway
 * resolves the sid → uid and enforces the site-owner gate. Best-effort:
 * always redirects back, even on a gateway error. */
static void relay_post(struct yloop *loop, struct yloop_stream *s,
                       const struct serve_ud *sud, const char *sid,
                       const char *gw_path, const char *body, size_t body_len,
                       const char *redirect_to, int keep_alive)
{
    struct http_response resp;
    (void)http_post(loop, &sud->gw, gw_path,
                    "application/x-www-form-urlencoded", NULL, sid,
                    body, body_len, &resp);
    http_response_free(&resp);
    send_redirect(s, redirect_to, NULL, keep_alive);
}

/* POST /repos/new — create a repository for the signed-in user. We call
 * git_repo.git_repo.make over the gateway /_rpc directly (owner uid =
 * hash_username(uname), matching how the backend keys repos), rather than
 * relaying the gateway's HTML action — that action needs the uname cookie,
 * which a header-only relay doesn't carry. Then bounce to /repos. */
static void webapp_repos_new(struct yloop *loop, struct yloop_stream *s,
                             const struct serve_ud *sud, const char *sid,
                             const char *body, size_t body_len, int keep_alive)
{
    /* Owner is the session identity, never a cookie. No valid session →
     * no repo creation. */
    struct claims cl;
    resolve_claims(loop, sud, sid, &cl);
    if (!cl.uid || !cl.username[0]) {
        send_redirect(s, "/login", NULL, keep_alive);
        return;
    }
    char *name = form_get(body, body_len, "name");
    if (name && *name) {
        char name_esc[96], uname_esc[96];
        if (json_escape(name_esc, sizeof(name_esc), name) &&
            json_escape(uname_esc, sizeof(uname_esc), cl.username)) {
            char args[256];
            snprintf(args, sizeof(args), "[%u,\"%s\",\"%s\"]",
                     cl.uid, uname_esc, name_esc);
            rpc_result_int(loop, sud, sid, "git_repo.git_repo.make", args, -1);
        }
    }
    free(name);
    send_redirect(s, "/repos", NULL, keep_alive);
}

/* POST /logout — invalidate the server-side session at the gateway
 * (best-effort), then clear the browser cookies and bounce to /login. We
 * emit the clearing Set-Cookie headers ourselves (the gateway sets two and
 * our client only captures the first), so the browser reliably forgets the
 * opaque sid + uname. */
static void route_logout(struct yloop *loop, struct yloop_stream *s,
                         const struct serve_ud *sud, const char *sid, int keep_alive)
{
    struct http_response resp;
    (void)http_post(loop, &sud->gw, "/logout",
                    "application/x-www-form-urlencoded", NULL, sid, "", 0, &resp);
    http_response_free(&resp);
    send_redirect(s, "/login",
        "Set-Cookie: picomesh-sid=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0\r\n"
        "Set-Cookie: picomesh-uname=; Path=/; SameSite=Lax; Max-Age=0\r\n",
        keep_alive);
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
    char tmp[1024];
    size_t plen = path_len;
    const char *q = memchr(path, '?', path_len);
    if (q) plen = (size_t)(q - path);
    if (plen == 0 || plen >= sizeof(tmp)) return 0;
    memcpy(tmp, path, plen); tmp[plen] = 0;

    char *segs[5] = {0}; int ns = 0;
    for (char *tok = strtok(tmp, "/"); tok && ns < 5; tok = strtok(NULL, "/"))
        segs[ns++] = tok;
    if (ns < 2 || ns > 4) return 0;
    for (int i = 0; i < ns; ++i) if (strstr(segs[i], "..")) return 0;
    snprintf(acct, acct_cap, "%s", segs[0]);
    snprintf(repo, repo_cap, "%s", segs[1]);
    snprintf(sect, sect_cap, "%s", ns >= 3 ? segs[2] : "");
    snprintf(act,  act_cap,  "%s", ns >= 4 ? segs[3] : "");
    return ns;
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
        { "",         "git_repo",     RP_BROWSE   },
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
    size_t n;
    const struct repo_route *r = repo_routes(&n);
    for (size_t i = 0; i < n; ++i)
        if (strcmp(r[i].verb, verb) == 0) return &r[i];
    return NULL;
}

/* ---- per-peer serve coro -------------------------------------------- */

static void serve_one(struct yloop *l, struct yloop_stream *s, void *ud)
{
    (void)l;
    struct serve_ud *sud = ud;
    const struct webapp_config *cfg = sud->cfg;

    /* Read request bytes until picohttpparser is satisfied. */
    char *buf = malloc(WEBAPP_REQ_BUF);
    if (!buf) { yloop_close(s); return; }
    size_t total = 0, last = 0;
    const char *method = NULL, *path = NULL;
    size_t method_len = 0, path_len = 0;
    int minor_version = 0;
    struct phr_header headers[WEBAPP_MAX_HEADERS];
    size_t num_headers;

    while (total < WEBAPP_REQ_BUF) {
        size_t got = yloop_read_some(s, buf + total, WEBAPP_REQ_BUF - total);
        if (got == 0) { free(buf); yloop_close(s); return; }
        total += got;
        num_headers = WEBAPP_MAX_HEADERS;
        int r = phr_parse_request(buf, total, &method, &method_len,
                                  &path, &path_len, &minor_version,
                                  headers, &num_headers, last);
        if (r > 0) break;
        if (r == -1) { free(buf); yloop_close(s); return; }
        last = total;
    }

    char ka_hdr[32] = {0};
    int keep_alive = (minor_version >= 1);
    if (header_match(headers, num_headers, "connection", ka_hdr, sizeof(ka_hdr))) {
        if (strstr(ka_hdr, "close")) keep_alive = 0;
        if (strstr(ka_hdr, "keep-alive")) keep_alive = 1;
    }

    /* Method+path dispatch. /login + /static are always served (login is
     * the gateway's auth, static is the sidecar's own assets). Every data
     * page is service-driven: discover the active mesh services once, then
     * gate each page on its backing service — no service, no page. */
    if (method_len == 3 && memcmp(method, "GET", 3) == 0) {
        if (path_equals(path, path_len, "/")) {
            send_redirect(s, "/repos", NULL, keep_alive);
        } else if (path_equals(path, path_len, "/login")) {
            route_login_get(s, keep_alive);
        } else if (path_equals(path, path_len, "/register")) {
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
            struct claims cl;
            resolve_claims(l, sud, sid, &cl);
            const char *who = cl.uid
                ? (cl.username[0] ? cl.username : (uname_cookie ? uname_cookie : ""))
                : "";

            /* Full NUL-terminated path (query kept, for ?dir/?path). */
            char fp[1024];
            size_t copy = path_len < sizeof(fp) - 1 ? path_len : sizeof(fp) - 1;
            memcpy(fp, path, copy); fp[copy] = 0;

            char acct[128], repo[128], verb[32];
            if (path_equals(path, path_len, "/repos")) {
                if (service_active(sud, "git_repo")) route_repos_get(l, s, sud, sid, who, cl.uid, cl.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/search") ||
                       starts_with(path, path_len, "/search?")) {
                if (service_active(sud, "git_repo")) page_search(l, s, sud, sid, who, fp, cl.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/repos/new")) {
                if (service_active(sud, "git_repo")) page_repos_new_form(s, who, cl.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/admin") ||
                       starts_with(path, path_len, "/admin/")) {
                /* Admin space: prove a signed-in site admin BEFORE rendering
                 * any admin content. Anonymous → /login; non-admin → 403. */
                if (require_admin(s, &cl, keep_alive)) {
                    if (path_equals(path, path_len, "/admin")) {
                        page_admin_overview(l, s, sud, sid, who, keep_alive);
                    } else if (path_equals(path, path_len, "/admin/users")) {
                        if (service_active(sud, "accounts")) page_admin_users(l, s, sud, sid, who, keep_alive);
                        else send_text(s, 404, "no such page (accounts not active)\n", keep_alive);
                    } else if (path_equals(path, path_len, "/admin/repos")) {
                        if (service_active(sud, "git_repo")) page_admin_repos(l, s, sud, sid, who, keep_alive);
                        else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
                    } else if (path_equals(path, path_len, "/admin/tokens")) {
                        page_admin_tokens(l, s, sud, sid, who, keep_alive);
                    } else if (path_equals(path, path_len, "/admin/services")) {
                        page_admin_services(l, s, sud, who, keep_alive);
                    } else {
                        send_text(s, 404, "not found\n", keep_alive);
                    }
                }
            } else if (path_equals(path, path_len, "/dashboard/issues") ||
                       starts_with(path, path_len, "/dashboard/issues?")) {
                if (service_active(sud, "git_repo")) page_dashboard_issues(l, s, sud, sid, who, cl.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_repo not active)\n", keep_alive);
            } else if (path_equals(path, path_len, "/dashboard/runs")) {
                if (service_active(sud, "git_pipeline")) page_dashboard_runs(l, s, sud, sid, who, cl.is_admin, keep_alive);
                else send_text(s, 404, "no such page (git_pipeline not active)\n", keep_alive);
            } else if (parse_repo_path(path, path_len, acct, sizeof(acct),
                                       repo, sizeof(repo), verb, sizeof(verb))) {
                /* /<account>/<repo>[/<verb>] — looked up in the route table,
                 * gated on the verb's backing service. */
                const struct repo_route *rt = repo_route_for(verb);
                if (!rt) {
                    send_text(s, 404, "not found\n", keep_alive);
                } else if (!service_active(sud, rt->service)) {
                    send_text(s, 404, "no such page (service not active)\n", keep_alive);
                } else switch (rt->page) {
                case RP_BROWSE:   route_repo_browse(l, s, sud, sid, who, acct, repo, fp, cl.is_admin, keep_alive); break;
                case RP_ISSUES:   page_repo_issues(l, s, sud, sid, who, acct, repo, fp, cl.is_admin, keep_alive); break;
                case RP_RUNS:     page_repo_runs(l, s, sud, sid, who, acct, repo, fp, cl.is_admin, keep_alive);   break;
                case RP_EDIT:     route_repo_edit_get(l, s, sud, sid, who, acct, repo, verb, fp, cl.is_admin, keep_alive); break;
                case RP_SETTINGS: page_repo_settings(l, s, sud, sid, who, acct, repo, cl.is_admin, keep_alive); break;
                }
            } else {
                /* Single-segment /<account> → account landing. */
                char seg[128];
                const char *qm = memchr(path, '?', path_len);
                size_t plen = qm ? (size_t)(qm - path) : path_len;
                if (plen > 1 && path[0] == '/' &&
                    !memchr(path + 1, '/', plen - 1) && plen - 1 < sizeof(seg)) {
                    memcpy(seg, path + 1, plen - 1); seg[plen - 1] = 0;
                    if (!is_reserved_top(seg) && service_active(sud, "git_repo"))
                        page_account_landing(l, s, sud, sid, who, seg, cl.is_admin, keep_alive);
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
         * Content-Length is treated as an empty body (cl=0) — the
         * bodyless action forms (issue/run "new") submit that way, and a
         * bare `curl -XPOST` with no data should not be rejected. */
        long cl = header_content_length(headers, num_headers);
        if (cl < 0) cl = 0;
        if (cl > 1 << 20) {
            send_text(s, 400, "Content-Length too large\n", keep_alive);
        } else {
            /* Body may already be partly buffered after the parser
             * accepted the header. picohttpparser returns the offset
             * past CRLF-CRLF as `r` (the parser result we threw away
             * — re-derive by re-running once). */
            size_t hdr_end = 0;
            {
                num_headers = WEBAPP_MAX_HEADERS;
                int rr = phr_parse_request(buf, total, &method, &method_len,
                                           &path, &path_len, &minor_version,
                                           headers, &num_headers, 0);
                if (rr > 0) hdr_end = (size_t)rr;
            }
            size_t buffered = total - hdr_end;
            char *body_buf = malloc((size_t)cl + 1);
            if (!body_buf) {
                send_text(s, 500, "out of memory\n", keep_alive);
            } else {
                size_t copy = buffered < (size_t)cl ? buffered : (size_t)cl;
                memcpy(body_buf, buf + hdr_end, copy);
                if (copy < (size_t)cl) {
                    if (read_body(s, body_buf + copy, (size_t)cl - copy) < 0) {
                        free(body_buf);
                        send_text(s, 400, "short body\n", keep_alive);
                        goto post_done;
                    }
                }
                body_buf[cl] = 0;

                char acct[128], repo[128], sect[64], act[64];
                if (path_equals(path, path_len, "/login")) {
                    route_login_post(l, s, sud, body_buf, (size_t)cl, keep_alive);
                } else if (path_equals(path, path_len, "/register")) {
                    route_register_post(l, s, sud, body_buf, (size_t)cl, keep_alive);
                } else if (path_equals(path, path_len, "/logout")) {
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    route_logout(l, s, sud, sid, keep_alive);
                    free(sid);
                } else if (path_equals(path, path_len, "/repos/new")) {
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    webapp_repos_new(l, s, sud, sid, body_buf, (size_t)cl, keep_alive);
                    free(sid);
                } else if (starts_with(path, path_len, "/admin/")) {
                    /* Admin mutations: relay to the gateway only for a
                     * signed-in site admin. The gateway enforces this too —
                     * this is defense in depth so the webapp never knowingly
                     * forwards an unauthorized mutation. (cl here is the
                     * content-length, so the claims live in cl_admin.) */
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    struct claims cl_admin;
                    resolve_claims(l, sud, sid, &cl_admin);
                    if (!cl_admin.is_admin) {
                        if (cl_admin.uid)
                            send_forbidden(s, cl_admin.username[0] ? cl_admin.username : NULL, keep_alive);
                        else
                            send_redirect(s, "/login", NULL, keep_alive);
                    } else if (path_equals(path, path_len, "/admin/tokens/mint_pat")) {
                        relay_post(l, s, sud, sid, "/admin/tokens/mint_pat",
                                   body_buf, (size_t)cl, "/admin/tokens", keep_alive);
                    } else {
                        send_text(s, 404, "not found\n", keep_alive);
                    }
                    free(sid);
                } else {
                    int ns = parse_action_path(path, path_len, acct, sizeof(acct),
                                               repo, sizeof(repo), sect, sizeof(sect),
                                               act, sizeof(act));
                    char *sid = cookie_get(headers, num_headers, "picomesh-sid");
                    if (ns == 3 && strcmp(sect, "edit") == 0) {
                        route_repo_edit_post(l, s, sud, sid, acct, repo,
                                             body_buf, (size_t)cl, keep_alive);
                    } else if (ns == 4 && strcmp(sect, "issues") == 0 && strcmp(act, "new") == 0) {
                        post_issue_new(l, s, sud, sid, acct, repo, keep_alive);
                    } else if (ns == 4 && strcmp(sect, "issues") == 0 && strcmp(act, "close") == 0) {
                        post_issue_close(l, s, sud, sid, acct, repo, body_buf, (size_t)cl, keep_alive);
                    } else if (ns == 4 && strcmp(sect, "runs") == 0 && strcmp(act, "new") == 0) {
                        post_run_new(l, s, sud, sid, acct, repo, keep_alive);
                    } else if (ns == 4 && strcmp(sect, "runs") == 0 && strcmp(act, "lease") == 0) {
                        post_run_lease(l, s, sud, sid, acct, repo, body_buf, (size_t)cl, keep_alive);
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
    yloop_close(s);
}

/* ---- public entry --------------------------------------------------- */

struct picomesh_void_result webapp_start(struct yloop *loop,
                                         const char *host, int port,
                                         const struct webapp_config *cfg)
{
    /* `ud` outlives the listener: leaked intentionally — process-lifetime,
     * tiny, freed on exit by the OS. The config strings come from yargv
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

    struct picomesh_void_result r =
        yloop_listen_tcp(loop, host, port, serve_one, ud);
    PICOMESH_RETURN_IF_ERR(picomesh_void, r, "webapp_start: yloop_listen_tcp");
    return PICOMESH_OK_VOID();
}
