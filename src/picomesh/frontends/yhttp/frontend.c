/* frontend.c — server-side HTML rendering for the picoforge scenario.
 *
 * Mirrors `scenarios/git-yaapp/frontend/frontend.py` from yaapp:
 * Jinja templates → hand-emitted HTML, FastAPI routes → an inline URL
 * router, FastAPI's session cookie → a plain `picomesh-sid` cookie. That
 * cookie carries an OPAQUE 128-bit hex session token (not a parseable
 * integer); the gateway resolves it to a uid via `session_session_lookup`
 * and forwards only the resolved uid to backends.
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
 *   POST /repos/new             git_repo_git_repo_make
 *   GET  /repo/<id>             repo show (issues + runs panels)
 *   GET  /repo/<id>/issues      issues list (with status filter)
 *   POST /repo/<id>/issues/new  issues_issues_open
 *   POST /repo/<id>/issues/<iid>/close
 *   GET  /repo/<id>/runs        pipeline list
 *   POST /repo/<id>/runs/new    git_pipeline_git_pipeline_enqueue
 *
 *   GET  /admin/users           accounts roster (site-owner only)
 *
 * All backend calls go through the engine's `remote()` sessions —
 * loaded from `mesh.services.gateway.config.remotes[]` at startup.
 *
 * NOTE: this file is the LEGACY gateway-side HTML renderer; the
 * planned split moves these routes into a separate `picoforge-webapp`
 * binary that talks to the gateway via `POST /_rpc`, the same way
 * yaapp's `frontend.py` does. Until that work lands, this file lives
 * inside the gateway process. */

#define _POSIX_C_SOURCE 200809L

#include "frontend.h"

#include <picomesh/yengine/engine.h>
#include <picomesh/yengine/resolve.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yclass/jinvoke.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yargv/yargv.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/ycore/idkey.h>
#include <picomesh/ycore/yspan.h>
#include <picomesh/ycore/ytelemetry.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/secret.h>
#include <picomesh/authenticators/registry.h>
#include <picomesh/authorizers/registry.h>

/* Each service header brings in its create() + method stubs. */
#include <picomesh/plugin/sharded_storage/sharded_storage.h>
#include <picomesh/plugin/accounts/accounts.h>
#include <picomesh/plugin/password_authn/password_authn.h>
#include <picomesh/plugin/token_issuer/token_issuer.h>
#include <picomesh/plugin/session/session.h>
#include <picomesh/plugin/issues/issues.h>
#include <picomesh/plugin/git_repo/git_repo.h>
#include <picomesh/plugin/git_pipeline/git_pipeline.h>
#include <picomesh/plugin/personal_access_tokens/personal_access_tokens.h>

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
 * "same name + same password always reach the same account".
 *
 * The username → uid hash is the lookup-cluster SHARD KEY: every producer and
 * consumer (gateway, webapp, accounts) must compute it from the identical bytes
 * with the identical function, or they route to different shards and the
 * lookup/uniqueness guarantee silently breaks. So it goes through the single
 * shared primitive (picomesh_fnv1a32) rather than a re-implemented loop; the
 * only gateway-local policy is the uid==0 (anonymous) reservation. */
static uint32_t hash_username(const char *s)
{
    uint32_t h = picomesh_fnv1a32(s);
    return h ? h : 1; /* uid==0 means anonymous */
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
static const char *landing_url(uint32_t uid, const char *uname,
                               char *out, size_t cap);

static void render_head(struct buf *b, const char *title,
                        uint32_t uid, const char *uname)
{
    buf_printf(b,
        "<!doctype html><html lang=\"en\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>%s · picoforge</title>",
        title);

    /* gh#5: the gateway serves NO static files. Its legacy server-rendered
     * HTML therefore inlines the stylesheet instead of linking a
     * gateway-served /style.css. Emitted via buf_puts because the CSS
     * contains '%' (e.g. width: 35%) which buf_printf would misparse.
     * The canonical copy lives in assets/picoforge/static/
     * style.css (served by the picoforge-webapp sidecar); this inline
     * copy is generated from it. */
    buf_puts(b, "<style>\n"
        "/* git-yaapp — minimal, github-inspired stylesheet */\n"
        "\n"
        ":root {\n"
        "  --bg: #f6f8fa;\n"
        "  --fg: #1f2328;\n"
        "  --muted: #57606a;\n"
        "  --card: #ffffff;\n"
        "  --border: #d0d7de;\n"
        "  --accent: #0969da;\n"
        "  --accent-fg: #ffffff;\n"
        "  --success: #1a7f37;\n"
        "  --danger: #cf222e;\n"
        "  --warn: #9a6700;\n"
        "  --code-bg: #f0f3f6;\n"
        "  --radius: 6px;\n"
        "}\n"
        "\n"
        "* { box-sizing: border-box; }\n"
        "\n"
        "body {\n"
        "  margin: 0;\n"
        "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Helvetica, Arial,\n"
        "               sans-serif;\n"
        "  background: var(--bg);\n"
        "  color: var(--fg);\n"
        "  font-size: 14px;\n"
        "  line-height: 1.5;\n"
        "}\n"
        "\n"
        "a { color: var(--accent); text-decoration: none; }\n"
        "a:hover { text-decoration: underline; }\n"
        "\n"
        "code, pre {\n"
        "  font-family: ui-monospace, SFMono-Regular, \"SF Mono\", Consolas, monospace;\n"
        "  background: var(--code-bg);\n"
        "  border-radius: var(--radius);\n"
        "}\n"
        "code { padding: 0.1em 0.4em; font-size: 0.9em; }\n"
        "pre  { padding: 0.75em 1em; overflow-x: auto; }\n"
        "\n"
        "/* top bar */\n"
        ".topnav {\n"
        "  background: #24292f;\n"
        "  color: #fff;\n"
        "  display: flex;\n"
        "  align-items: center;\n"
        "  padding: 0.6em 1.2em;\n"
        "  gap: 1.5em;\n"
        "}\n"
        ".topnav a { color: #fff; }\n"
        ".brand { font-weight: 700; font-size: 1.1em; }\n"
        ".nav-links { list-style: none; margin: 0; padding: 0; display: flex; gap: 1.2em; flex: 1; }\n"
        ".nav-right { display: flex; align-items: center; gap: 1em; }\n"
        ".user { color: #c9d1d9; }\n"
        "button.link {\n"
        "  background: none; border: none; color: #fff; cursor: pointer; padding: 0;\n"
        "  font-size: 1em; text-decoration: underline;\n"
        "}\n"
        "\n"
        "/* layout */\n"
        ".container {\n"
        "  max-width: 1100px;\n"
        "  margin: 1.5em auto;\n"
        "  padding: 0 1.2em;\n"
        "}\n"
        ".page-header {\n"
        "  display: flex;\n"
        "  justify-content: space-between;\n"
        "  align-items: baseline;\n"
        "  margin: 0 0 1em;\n"
        "}\n"
        ".page-header h1 { margin: 0; }\n"
        "\n"
        "footer {\n"
        "  text-align: center;\n"
        "  color: var(--muted);\n"
        "  margin: 3em 0 1.5em;\n"
        "  font-size: 0.85em;\n"
        "}\n"
        "\n"
        "/* card */\n"
        ".card {\n"
        "  background: var(--card);\n"
        "  border: 1px solid var(--border);\n"
        "  border-radius: var(--radius);\n"
        "  padding: 1.2em 1.4em;\n"
        "  margin-bottom: 1em;\n"
        "}\n"
        ".card.narrow { max-width: 480px; margin-left: auto; margin-right: auto; }\n"
        ".card h1, .card h2 { margin-top: 0; }\n"
        ".card-header {\n"
        "  display: flex;\n"
        "  justify-content: space-between;\n"
        "  align-items: baseline;\n"
        "  margin-bottom: 0.6em;\n"
        "}\n"
        ".card-header h2 { margin: 0; }\n"
        "\n"
        "/* lists */\n"
        ".list { list-style: none; margin: 0; padding: 0; }\n"
        ".list li {\n"
        "  padding: 0.6em 0;\n"
        "  border-bottom: 1px solid var(--border);\n"
        "  display: flex;\n"
        "  flex-wrap: wrap;\n"
        "  gap: 0.6em;\n"
        "  align-items: baseline;\n"
        "}\n"
        ".list li:last-child { border-bottom: none; }\n"
        "\n"
        "/* forms */\n"
        "form label { display: block; margin: 0.5em 0; }\n"
        "form input, form textarea, form select {\n"
        "  width: 100%;\n"
        "  padding: 0.5em 0.7em;\n"
        "  font-size: 1em;\n"
        "  border: 1px solid var(--border);\n"
        "  border-radius: var(--radius);\n"
        "  font-family: inherit;\n"
        "  background: #fff;\n"
        "}\n"
        "form input:focus, form textarea:focus, form select:focus {\n"
        "  outline: 2px solid var(--accent);\n"
        "  border-color: var(--accent);\n"
        "}\n"
        "form textarea { resize: vertical; }\n"
        "\n"
        "button, .btn {\n"
        "  display: inline-block;\n"
        "  padding: 0.45em 1em;\n"
        "  border: 1px solid var(--border);\n"
        "  border-radius: var(--radius);\n"
        "  background: #f6f8fa;\n"
        "  color: var(--fg);\n"
        "  font-size: 0.95em;\n"
        "  cursor: pointer;\n"
        "  text-decoration: none;\n"
        "  font-family: inherit;\n"
        "}\n"
        "button:hover, .btn:hover { background: #eaeef2; text-decoration: none; }\n"
        "button.primary, .btn.primary {\n"
        "  background: var(--accent);\n"
        "  color: var(--accent-fg);\n"
        "  border-color: var(--accent);\n"
        "}\n"
        "button.primary:hover, .btn.primary:hover {\n"
        "  background: #0860c8; border-color: #0860c8;\n"
        "}\n"
        "\n"
        "/* badges */\n"
        ".badge {\n"
        "  display: inline-block;\n"
        "  padding: 0.15em 0.55em;\n"
        "  font-size: 0.8em;\n"
        "  border-radius: 9999px;\n"
        "  background: var(--code-bg);\n"
        "  color: var(--fg);\n"
        "  border: 1px solid var(--border);\n"
        "}\n"
        ".badge.open, .badge.running, .badge.queued { background: #ddf4ff; color: #0969da; border-color: #b6e3ff; }\n"
        ".badge.closed, .badge.succeeded { background: #dafbe1; color: var(--success); border-color: #b5e6c6; }\n"
        ".badge.failed, .badge.timeout, .badge.cancelled { background: #ffebe9; color: var(--danger); border-color: #ffcecb; }\n"
        ".badge.priority-low      { background: #fff; color: var(--muted); }\n"
        ".badge.priority-medium   { background: #fff8c5; color: var(--warn); border-color: #ead97c; }\n"
        ".badge.priority-high     { background: #ffebe9; color: var(--danger); border-color: #ffcecb; }\n"
        ".badge.priority-critical { background: var(--danger); color: #fff; border-color: var(--danger); }\n"
        "\n"
        "/* misc */\n"
        ".muted { color: var(--muted); }\n"
        ".small { font-size: 0.85em; }\n"
        ".error {\n"
        "  background: #ffebe9;\n"
        "  color: var(--danger);\n"
        "  border: 1px solid #ffcecb;\n"
        "  border-radius: var(--radius);\n"
        "  padding: 0.6em 0.9em;\n"
        "  margin: 0.7em 0;\n"
        "}\n"
        ".clone-url { user-select: all; }\n"
        "\n"
        ".filters { display: flex; gap: 0.6em; margin-bottom: 1em; }\n"
        ".filters a {\n"
        "  padding: 0.3em 0.8em;\n"
        "  border-radius: 9999px;\n"
        "  background: var(--card);\n"
        "  border: 1px solid var(--border);\n"
        "  color: var(--fg);\n"
        "  text-decoration: none;\n"
        "}\n"
        ".filters a.active { background: var(--accent); color: #fff; border-color: var(--accent); }\n"
        "\n"
        ".comments { list-style: none; margin: 0; padding: 0; }\n"
        ".comments li {\n"
        "  border: 1px solid var(--border);\n"
        "  border-radius: var(--radius);\n"
        "  padding: 0.8em 1em;\n"
        "  margin-bottom: 0.7em;\n"
        "  background: #fff;\n"
        "}\n"
        ".comment-form textarea { width: 100%; }\n"
        ".prose { white-space: pre-wrap; }\n"
        "\n"
        "details summary {\n"
        "  cursor: pointer;\n"
        "  padding: 0.5em;\n"
        "  background: var(--code-bg);\n"
        "  border-radius: var(--radius);\n"
        "  margin: 0.4em 0;\n"
        "}\n"
        "details[open] summary { background: #e6ebf1; }\n"
        "details pre { background: #0d1117; color: #c9d1d9; }\n"
        "\n"
        "/* OAuth login buttons */\n"
        ".oauth-divider {\n"
        "  text-align: center;\n"
        "  margin: 1.5em 0 0.8em;\n"
        "  color: var(--muted);\n"
        "  font-size: 0.9em;\n"
        "  position: relative;\n"
        "}\n"
        ".oauth-divider::before, .oauth-divider::after {\n"
        "  content: \"\";\n"
        "  display: inline-block;\n"
        "  width: 35%;\n"
        "  height: 1px;\n"
        "  background: var(--border);\n"
        "  vertical-align: middle;\n"
        "  margin: 0 0.6em;\n"
        "}\n"
        ".oauth-buttons { display: flex; gap: 0.6em; flex-wrap: wrap; }\n"
        ".oauth-btn {\n"
        "  flex: 1 1 auto;\n"
        "  text-align: center;\n"
        "  background: #24292f;\n"
        "  color: #fff;\n"
        "  border-color: #24292f;\n"
        "}\n"
        ".oauth-btn:hover { background: #1b1f24; color: #fff; text-decoration: none; }\n"
        "\n"
        "/* runners */\n"
        ".chip {\n"
        "  display: inline-block;\n"
        "  font-size: 0.75em;\n"
        "  background: #eaeef2;\n"
        "  color: #1f2328;\n"
        "  padding: 0.1em 0.5em;\n"
        "  border-radius: 1em;\n"
        "  margin: 0 0.2em 0.2em 0;\n"
        "}\n"
        ".grid {\n"
        "  width: 100%;\n"
        "  border-collapse: collapse;\n"
        "  font-size: 0.92em;\n"
        "}\n"
        ".grid th, .grid td {\n"
        "  padding: 0.5em 0.6em;\n"
        "  border-bottom: 1px solid var(--border);\n"
        "  text-align: left;\n"
        "  vertical-align: top;\n"
        "}\n"
        ".grid th { background: #f6f8fa; font-weight: 600; }\n"
        ".token-box {\n"
        "  background: #fff8c5;\n"
        "  border: 1px solid #d4a72c;\n"
        "  padding: 0.8em;\n"
        "  font-family: ui-monospace, monospace;\n"
        "  font-size: 0.9em;\n"
        "  word-break: break-all;\n"
        "  white-space: pre-wrap;\n"
        "}\n"
        ".warn { color: #9a6700; }\n"
        ".link.danger { color: #cf222e; }\n"
        ".link.danger:hover { color: #a40e26; }\n"
        "\n"
        "/* repo tab bar — GitHub/GitLab-style sub-nav under the repo title */\n"
        ".repo-tabs {\n"
        "  display: flex;\n"
        "  gap: 0.25em;\n"
        "  border-bottom: 1px solid var(--border);\n"
        "  margin: 0 0 1.2em;\n"
        "}\n"
        ".repo-tabs a {\n"
        "  padding: 0.5em 0.9em;\n"
        "  color: var(--fg);\n"
        "  border-bottom: 2px solid transparent;\n"
        "  font-weight: 500;\n"
        "}\n"
        ".repo-tabs a:hover {\n"
        "  text-decoration: none;\n"
        "  background: var(--code-bg);\n"
        "  border-radius: var(--radius) var(--radius) 0 0;\n"
        "}\n"
        ".repo-tabs a.active {\n"
        "  border-bottom-color: var(--accent);\n"
        "  font-weight: 600;\n"
        "}\n"
        ".repo-tabs .count {\n"
        "  background: var(--code-bg);\n"
        "  color: var(--muted);\n"
        "  border-radius: 2em;\n"
        "  padding: 0.05em 0.55em;\n"
        "  font-size: 0.85em;\n"
        "  margin-left: 0.15em;\n"
        "}\n"
        "");
    buf_puts(b,
        "</style>"
        "</head><body>"
        "<nav class=\"topnav\">"
        "<a class=\"brand\" href=\"/\">picoforge</a>");

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
        "</main><footer><span>picoforge · ported from yaapp's "
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

/* Defined later — the opaque-token extractor (cookie OR `picomesh-sid:`
 * header OR `Authorization: Bearer`), the token→uid session lookup, and the
 * verified-context resolver the gateway's mutation routes use. */
static int extract_session_token(const char *headers_raw, size_t headers_raw_len,
                                 char *out, size_t cap);
static uint32_t uid_for_token(const char *token);
static void resolve_authctx(const char *headers_raw, size_t headers_raw_len,
                            struct picomesh_authctx *out);

/* ---------- per-route page helpers ---------- */

/* Convenience: build a ctx for a named remote, create a transient
 * object, return both — and free the obj when the caller is done. */
struct svc_ctx {
    struct ctx c;
    struct object *obj;
    int ok;
};

/* Open a backend service object. A NULL `.peer` is NOT an error: it means
 * the service is collocated in THIS process, and CREATE_FN resolves it as a
 * local in-process object (rpc_object_acquire's no-peer path). With a peer
 * it's a remote yrpc proxy. Both paths go through the same CREATE_FN, so we
 * don't bail on a missing peer — that's exactly the all-in-one mode. */
#define SVC_OPEN(VAR, SVC, CREATE_FN)                                              \
    struct svc_ctx VAR = {0};                                                      \
    do {                                                                           \
        struct picomesh_engine *_e = picomesh_active_engine();                           \
        if (!_e) break;                                                            \
        VAR.c = picomesh_engine_service_ctx(_e, SVC);                                 \
        struct object_ptr_result _o = CREATE_FN(&VAR.c);                           \
        if (PICOMESH_IS_ERR(_o)) { picomesh_error_destroy(_o.error); break; }            \
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

/* ---------- username charset guard ---------- */

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

/* Build the Set-Cookie header(s) after a successful auth. Returns
 * non-zero on success; caller has already validated `uname` via
 * username_ok() so it's safe to splice into the cookie value. */
static int build_session_cookies(char *out, size_t cap,
                                 const char *token, const char *uname)
{
    int n = snprintf(out, cap,
        "Set-Cookie: picomesh-sid=%s; Path=/; HttpOnly; SameSite=Lax\r\n"
        "Set-Cookie: picomesh-uname=%s; Path=/; SameSite=Lax\r\n",
        token, uname);
    return n > 0 && (size_t)n < cap;
}

/* Authenticate + mint a session. Used by both /login and /register
 * (after register has put the credential record in place). The gateway is
 * the authenticator FRONTEND: it asks `token_issuer.login` to verify the
 * credential (delegated to password_authn), load groups, and mint an access
 * JWT + refresh token. The gateway then stores that token pair in `session`
 * keyed by a fresh opaque sid and hands ONLY the sid to the browser — the JWT
 * never leaves the mesh.
 *
 * On error the function does NOT render — it returns 0 and fills *err with a
 * static error string. On success returns 1 and writes the opaque sid into
 * tok_out. */
static int auth_and_start_session(uint32_t uid, const char *username, int64_t pw,
                                  char *tok_out, size_t tok_cap,
                                  const char **err)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) { *err = "no engine"; return 0; }

    /* token_issuer.login verifies the password (via password_authn), loads
     * groups, and returns {access_jwt, refresh_token, uid, username, groups}.
     * An auth failure surfaces as an error here → "invalid username/password". */
    SVC_OPEN(ti, "token_issuer", token_issuer_token_issuer_create);
    if (!ti.ok) { *err = "token_issuer unreachable"; return 0; }
    struct picomesh_json_result login =
        token_issuer_token_issuer_login(&ti.c, ti.obj, NULL, "password", uid, username ? username : "", pw);
    SVC_CLOSE(ti);
    if (PICOMESH_IS_ERR(login)) {
        picomesh_error_destroy(login.error);
        *err = "invalid username or password";
        return 0;
    }

    /* Pull the minted token pair out of the issuer's JSON. */
    char access_jwt[2048] = {0};
    char refresh_token[64] = {0};
    struct yjson_doc *doc = yjson_parse(login.value, strlen(login.value));
    if (doc) {
        const struct yjson_value *root = yjson_doc_root(doc);
        const char *aj = yjson_as_string(yjson_object_get(root, "access_jwt"), NULL);
        const char *rt = yjson_as_string(yjson_object_get(root, "refresh_token"), NULL);
        if (aj) snprintf(access_jwt, sizeof(access_jwt), "%s", aj);
        if (rt) snprintf(refresh_token, sizeof(refresh_token), "%s", rt);
        yjson_doc_free(doc);
    }
    free(login.value);
    if (!access_jwt[0]) { *err = "login produced no token"; return 0; }

    SVC_OPEN(ses, "session", session_session_create);
    if (!ses.ok) { *err = "session unreachable"; return 0; }
    struct picomesh_string_result tok_r =
        session_session_start(&ses.c, ses.obj, NULL, uid, access_jwt, refresh_token);
    SVC_CLOSE(ses);
    if (PICOMESH_IS_ERR(tok_r)) {
        picomesh_error_destroy(tok_r.error);
        *err = "session create failed";
        return 0;
    }
    snprintf(tok_out, tok_cap, "%s", tok_r.value ? tok_r.value : "");
    free(tok_r.value);
    if (!tok_out[0]) { *err = "session create failed"; return 0; }
    return 1;
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
    SVC_OPEN(acc, "accounts", accounts_accounts_create);
    if (!acc.ok) { render_login(s, "accounts service unreachable", keep_alive); return; }
    struct picomesh_int_result ex = accounts_accounts_exists(&acc.c, acc.obj, NULL, uid);
    SVC_CLOSE(acc);
    int exists = PICOMESH_IS_OK(ex) && ex.value;
    if (PICOMESH_IS_ERR(ex)) picomesh_error_destroy(ex.error);
    if (!exists) {
        render_login(s, "no such user — register first", keep_alive);
        return;
    }

    const char *err = NULL;
    char tok[64];
    if (!auth_and_start_session(uid, uname, pw, tok, sizeof(tok), &err)) {
        render_login(s, err ? err : "login failed", keep_alive);
        return;
    }

    char cookie_hdr[256];
    if (!build_session_cookies(cookie_hdr, sizeof(cookie_hdr), tok, uname)) {
        render_login(s, "cookie build failed", keep_alive);
        return;
    }
    char to[128];
    send_redirect(s, landing_url(uid, uname, to, sizeof(to)),
                  keep_alive, cookie_hdr);
}

/* Best-effort: release a username claim THIS registration won but could not
 * complete, so a later step's failure does not strand the name permanently
 * (release_username only deletes an UNCONFIRMED claim, never a completed one). */
static void register_release_claim(uint32_t uid, const char *uname)
{
    SVC_OPEN(acc_rel, "accounts", accounts_accounts_create);
    if (!acc_rel.ok) return;
    struct picomesh_int_result rel = accounts_accounts_release_username(&acc_rel.c, acc_rel.obj, NULL, uid, uname);
    if (PICOMESH_IS_ERR(rel)) picomesh_error_destroy(rel.error);
    SVC_CLOSE(acc_rel);
}

/* Build a short-lived INTERNAL system capability for a trusted gateway
 * bootstrap call (creating a new user's namespace, the first-user `site`
 * namespace, the /repos/new repo). It is a JWT the gateway signs itself,
 * carrying the reserved `system:internal` group and `sub` = the uid the
 * operation acts for. Backends recognise this group as the explicit
 * trusted-internal capability, so they can DENY a credential-less call yet
 * still let the gateway perform bootstrap work — replacing the unsafe
 * "no JWT means trusted" path. Returns a yheaders the caller frees (NULL on
 * failure; the caller then fails closed). The capability never leaves the mesh
 * and is never handed to a client. */
static struct yheaders *internal_caps(uint32_t uid)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return NULL;
    struct picomesh_string_result secret = picomesh_security_jwt_secret(e);
    if (PICOMESH_IS_ERR(secret)) { picomesh_error_destroy(secret.error); return NULL; }
    int64_t now = picomesh_security_now();
    char *claims = picomesh_jwt_build_claims("picomesh", uid, "system", PICOMESH_GROUP_SYSTEM, now, now + 60);
    char *jwt = claims ? picomesh_jwt_encode(claims, secret.value) : NULL;
    free(claims);
    free(secret.value);
    if (!jwt) return NULL;
    struct yheaders *hdrs = yheaders_new();
    if (hdrs) { yheaders_set_u32(hdrs, "uid", uid); yheaders_set(hdrs, "jwt", jwt); }
    free(jwt);
    return hdrs;
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

    /* Step 0 — reject if a COMPLETED account already owns this uid. Because
     * uid = FNV(username), a DIFFERENT username can hash to an existing user's
     * uid (a chosen 32-bit collision); that different name would win a fresh
     * claim in step 1, and without this gate the credential overwrite in step 2
     * would then stomp the victim's password — account takeover under the
     * colliding name. The `users` row is the completion marker, so its presence
     * means a real account holds this uid: refuse and NEVER touch the credential.
     * A read error FAILS CLOSED — this gate is a security boundary, so an outage
     * must not be read as "no account". (The real fix is assigned, never-reused
     * uids; this gate is the stopgap while uid is hash-derived.) */
    SVC_OPEN(acc_chk, "accounts", accounts_accounts_create);
    if (!acc_chk.ok) { render_register(s, "accounts service unreachable", keep_alive); return; }
    struct picomesh_int_result exists_r = accounts_accounts_exists(&acc_chk.c, acc_chk.obj, NULL, uid);
    SVC_CLOSE(acc_chk);
    if (PICOMESH_IS_ERR(exists_r)) {
        picomesh_error_destroy(exists_r.error);
        render_register(s, "registration temporarily unavailable — try again", keep_alive);
        return;
    }
    if (exists_r.value) {
        render_register(s, "username already taken", keep_alive);
        return;
    }

    /* Step 1 — CLAIM the username BEFORE touching any credential. The claim
     * (INSERT OR IGNORE on the lookup cluster) elects exactly ONE winner for the
     * name: only the request whose insert actually landed gets claim==1. This is
     * what serializes concurrent registrations of the same name — a loser can
     * never reach step 2 and overwrite the winner's password. A claim already
     * held means the name belongs to a completed account, an in-flight
     * registration, or an abandoned one — in every case we are NOT the owner. */
    SVC_OPEN(acc_claim, "accounts", accounts_accounts_create);
    if (!acc_claim.ok) { render_register(s, "accounts service unreachable", keep_alive); return; }
    struct picomesh_int_result claim_r = accounts_accounts_claim_username(&acc_claim.c, acc_claim.obj, NULL, uid, uname);
    SVC_CLOSE(acc_claim);
    if (PICOMESH_IS_ERR(claim_r)) {
        /* FAIL CLOSED: cannot establish the claim → never touch credentials. */
        picomesh_error_destroy(claim_r.error);
        render_register(s, "registration temporarily unavailable — try again", keep_alive);
        return;
    }
    if (claim_r.value != 1) {
        render_register(s, "username already taken", keep_alive);
        return;
    }

    /* Step 2 — we WON the claim, so we own this registration and may write the
     * credential. register is put-if-absent; a 0 return means a credential
     * already exists for this uid, only possible as a LEGACY orphan (an older
     * flow wrote the credential before the claim, then failed before
     * completing). Overwriting it is safe: step 0 proved no completed account
     * holds this uid, and we hold the fresh claim, so no concurrent registrant
     * can be here — we are not stomping a live account's password. The credential
     * is written before the `users` row, so a crash here leaves no half-created
     * account. On any failure after winning the claim we RELEASE it so the name
     * is not stranded. */
    SVC_OPEN(pw_sv, "password_authn", password_authn_password_authn_create);
    if (!pw_sv.ok) { register_release_claim(uid, uname); render_register(s, "password_authn unreachable", keep_alive); return; }
    struct picomesh_int_result pwreg =
        password_authn_password_authn_register(&pw_sv.c, pw_sv.obj, NULL, uid, pw);
    if (PICOMESH_IS_ERR(pwreg)) {
        picomesh_error_destroy(pwreg.error);
        SVC_CLOSE(pw_sv);
        register_release_claim(uid, uname);
        render_register(s, "could not store credentials (backend not ready?) — try again", keep_alive);
        return;
    }
    if (pwreg.value == 0) {
        struct picomesh_int_result chg =
            password_authn_password_authn_change_password(&pw_sv.c, pw_sv.obj, NULL, uid, pw);
        if (PICOMESH_IS_ERR(chg)) {
            picomesh_error_destroy(chg.error);
            SVC_CLOSE(pw_sv);
            register_release_claim(uid, uname);
            render_register(s, "could not store credentials (backend not ready?) — try again", keep_alive);
            return;
        }
    }
    SVC_CLOSE(pw_sv);

    /* Step 3 — the user's personal namespace (`<username>`, kind=user). This is
     * created BEFORE the account-completion marker and is MANDATORY: its owner
     * membership becomes the `<username>:owner` JWT claim that drives RBAC, so an
     * account must never be committed without it (issue #30). ns_create is
     * idempotent for this owner (a retry re-grants), and rejects if the name is
     * already a namespace owned by someone else (a group) — in which case the
     * registration fails cleanly and the claim is released, rather than
     * stranding a completed account with no namespace. Fail closed: a backend
     * outage here aborts the registration (no completion marker is written). */
    struct yheaders *sys_caps = internal_caps(uid);
    if (!sys_caps) { register_release_claim(uid, uname); render_register(s, "registration temporarily unavailable — try again", keep_alive); return; }
    SVC_OPEN(acc_ns, "accounts", accounts_accounts_create);
    if (!acc_ns.ok) { yheaders_free(sys_caps); register_release_claim(uid, uname); render_register(s, "accounts service unreachable", keep_alive); return; }
    struct picomesh_string_result personal_ns =
        accounts_accounts_ns_create(&acc_ns.c, acc_ns.obj, sys_caps, uid, "user", uname, "");
    SVC_CLOSE(acc_ns);
    yheaders_free(sys_caps);
    if (PICOMESH_IS_ERR(personal_ns)) {
        picomesh_error_destroy(personal_ns.error);
        register_release_claim(uid, uname);
        render_register(s, "username unavailable (reserved namespace) — try another", keep_alive);
        return;
    }
    free(personal_ns.value);

    /* Step 4 — first-user `site` bootstrap, BEFORE the account-completion marker.
     * The deployment's site owner is the FIRST registrant: here the count of
     * COMPLETED accounts is still 0 (our `users` row is not written until step
     * 5), so `count == 0` identifies the first registrant. We create the `site`
     * group namespace owned by this user. Doing it BEFORE register keeps the
     * bootstrap FAIL CLOSED: a backend failure here aborts the registration with
     * nothing committed (no half-registered account without a site owner). The
     * `created_site` flag records that THIS call created `site`, so a later
     * register failure can roll it back (step 5) — there is then no window where
     * `site` is owned by a uid that never completed registration. A concurrent
     * first registrant that lost the race for `site` (already exists, owned by
     * another) did NOT create it, is not the site owner, and continues as a
     * regular user. */
    int first_user = 0, created_site = 0;
    {
        SVC_OPEN(acc_boot, "accounts", accounts_accounts_create);
        if (!acc_boot.ok) { register_release_claim(uid, uname); render_register(s, "accounts service unreachable", keep_alive); return; }
        struct picomesh_size_result cr = accounts_accounts_count(&acc_boot.c, acc_boot.obj, NULL);
        if (PICOMESH_IS_OK(cr)) first_user = (cr.value == 0);
        else picomesh_error_destroy(cr.error);
        if (first_user) {
            struct yheaders *site_caps = internal_caps(uid);
            if (!site_caps) { SVC_CLOSE(acc_boot); register_release_claim(uid, uname); render_register(s, "registration temporarily unavailable — try again", keep_alive); return; }
            struct picomesh_string_result site =
                accounts_accounts_ns_create(&acc_boot.c, acc_boot.obj, site_caps, uid, "group", "site", "");
            yheaders_free(site_caps);
            if (PICOMESH_IS_OK(site)) {
                free(site.value);
                created_site = 1;
                yinfo("authz: uid=%u bootstrapped as site-owner", uid);
            } else {
                picomesh_error_destroy(site.error);
                /* Distinguish "lost the race" (site already exists → continue) from
                 * a real failure (site absent → fail closed, abort the register). */
                struct picomesh_int64_result present =
                    accounts_accounts_ns_resolve(&acc_boot.c, acc_boot.obj, NULL, "site");
                int site_present = PICOMESH_IS_OK(present) && present.value > 0;
                if (PICOMESH_IS_ERR(present)) picomesh_error_destroy(present.error);
                if (!site_present) {
                    SVC_CLOSE(acc_boot);
                    register_release_claim(uid, uname);
                    render_register(s, "registration temporarily unavailable — try again", keep_alive);
                    return;
                }
            }
        }
        SVC_CLOSE(acc_boot);
    }

    /* Step 5 — complete the account: accounts_register writes the `users`
     * completion marker (after the credential, the personal namespace, and any
     * first-user site bootstrap) and confirms the claim. On failure, release the
     * claim AND roll back a site namespace we just created, so a failed first
     * registration never strands `site` under a phantom owner. */
    SVC_OPEN(acc, "accounts", accounts_accounts_create);
    if (!acc.ok) { register_release_claim(uid, uname); render_register(s, "accounts service unreachable", keep_alive); return; }
    struct picomesh_int_result reg = accounts_accounts_register(&acc.c, acc.obj, NULL, uid, uname);
    SVC_CLOSE(acc);
    int was_new = PICOMESH_IS_OK(reg) && reg.value == 1;
    if (PICOMESH_IS_ERR(reg)) picomesh_error_destroy(reg.error);
    if (!was_new) {
        if (created_site) {
            struct yheaders *del_caps = internal_caps(uid);
            if (del_caps) {
                SVC_OPEN(acc_rb, "accounts", accounts_accounts_create);
                if (acc_rb.ok) {
                    struct picomesh_int_result del =
                        accounts_accounts_ns_delete(&acc_rb.c, acc_rb.obj, del_caps, "site");
                    if (PICOMESH_IS_ERR(del)) picomesh_error_destroy(del.error);
                    SVC_CLOSE(acc_rb);
                }
                yheaders_free(del_caps);
            }
        }
        register_release_claim(uid, uname);
        render_register(s, "username already taken", keep_alive);
        return;
    }

    const char *err = NULL;
    char tok[64];
    if (!auth_and_start_session(uid, uname, pw, tok, sizeof(tok), &err)) {
        render_register(s, err ? err : "register failed", keep_alive);
        return;
    }

    char cookie_hdr[256];
    if (!build_session_cookies(cookie_hdr, sizeof(cookie_hdr), tok, uname)) {
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
    char tok[64];
    if (cookie_get(headers_raw, headers_raw_len, "picomesh-sid", tok, sizeof(tok)) && tok[0]) {
        SVC_OPEN(ses, "session", session_session_create);
        if (ses.ok) {
            struct picomesh_int_result r = session_session_destroy(&ses.c, ses.obj, NULL, tok);
            if (PICOMESH_IS_ERR(r)) picomesh_error_destroy(r.error);
            SVC_CLOSE(ses);
        }
    }
    send_redirect(s, "/login", keep_alive,
        "Set-Cookie: picomesh-sid=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0\r\n"
        "Set-Cookie: picomesh-uname=; Path=/; SameSite=Lax; Max-Age=0\r\n");
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
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return 0;
    out->c = picomesh_engine_service_ctx(e, "sharded_storage");
    /* peer==NULL ⇒ storage collocated in-process; create resolves it locally. */
    struct object_ptr_result o = sharded_storage_db_create(&out->c);
    if (PICOMESH_IS_ERR(o)) { picomesh_error_destroy(o.error); return 0; }
    out->obj = o.value;
    out->ok = 1;
    return 1;
}

/* ---------- authz: site-owner / regular-user role check ----------
 *
 * Mirrors yaapp's `'site:owner' in user.groups` check, now sourced from the
 * account's group memberships (issue #19) rather than a raw role:<uid> key:
 * a site admin holds `site:owner` or `site:maintainer` in their groups. The
 * groups are owned by the accounts service; the gateway reads them through
 * `accounts.groups`. Used by /_whoami and the /admin action gate. */

/* 1 if `uid` holds a site-level admin role (maintainer or owner). 0 on any
 * error / absent membership. */
static int is_site_admin(uint32_t uid)
{
    if (!uid) return 0;
    SVC_OPEN(acc, "accounts", accounts_accounts_create);
    if (!acc.ok) return 0;
    struct picomesh_string_result r = accounts_accounts_groups(&acc.c, acc.obj, NULL, uid);
    SVC_CLOSE(acc);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return 0; }
    int admin = r.value && picomesh_groups_max_role(r.value, "site") >= picomesh_role_rank("maintainer");
    free(r.value);
    return admin;
}

/* Record uid→username so /admin/users can list the actual users. Storage
 * otherwise holds only uid hashes (usernames are mapped to uids here at the
 * boundary), so we capture the name where we still have it: a `name:<uid>`
 * key plus an append to the `index` ("<uid>\t<username>\n" per row).
 * accounts.accounts.list returns that index. */

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
    struct picomesh_string_result cur = sharded_storage_db_get(&st.c, st.obj, NULL, "repos", k);
    int64_t count = (PICOMESH_IS_OK(cur) && cur.value ? strtoll(cur.value, NULL, 10) : 0) + 1;
    if (PICOMESH_IS_OK(cur)) free(cur.value); else picomesh_error_destroy(cur.error);
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "%lld", (long long)count);
    sharded_storage_db_set(&st.c, st.obj, NULL, "repos", k, cbuf);
    SVC_CLOSE(st);
    return rid;
}

/* ---------- route: POST /repos/new ---------- */

static void route_repos_new_post(struct yloop_stream *s, uint32_t uid,
                                 const char *uname,
                                 const char *body, size_t body_len, int keep_alive)
{
    if (!uname || !*uname) { send_redirect(s, "/login", keep_alive, NULL); return; }

    char name[64];
    if (!form_get(body, body_len, "name", name, sizeof(name)) ||
        !reponame_ok(name, strlen(name))) {
        /* Invalid repo name — bounce back to the list (the webapp owns the
         * form + error display; the gateway never renders a page). */
        send_redirect(s, "/repos", keep_alive, NULL);
        return;
    }

    /* Create the real repo FIRST and require it to SUCCEED: git_repo.make
     * validates the owning namespace exists and applies authorization. Only
     * after it lands do we mirror the binding into the legacy registry and
     * redirect — so a failed make never leaves stale registry metadata/counts
     * nor redirects as if the repo exists. */
    int made = 0;
    struct yheaders *sys_caps = internal_caps(uid);
    SVC_OPEN(repo, "git_repo", git_repo_git_repo_create);
    if (repo.ok && sys_caps) {
        struct picomesh_uint32_result mk =
            git_repo_git_repo_make(&repo.c, repo.obj, sys_caps, uid, uname, name);
        made = PICOMESH_IS_OK(mk) && mk.value != 0;
        if (PICOMESH_IS_ERR(mk)) picomesh_error_destroy(mk.error);
        SVC_CLOSE(repo);
    } else if (repo.ok) {
        SVC_CLOSE(repo);
    }
    if (sys_caps) yheaders_free(sys_caps);
    if (!made) { send_redirect(s, "/repos", keep_alive, NULL); return; }

    uint32_t rid = repo_register(uname, name, uid);
    if (!rid) { send_redirect(s, "/repos", keep_alive, NULL); return; }

    char where[128];
    snprintf(where, sizeof(where), "/%s/%s", uname, name);
    send_redirect(s, where, keep_alive, NULL);
}

/* Mint a personal access token for an existing uid. (There is deliberately
 * NO admin "register account id" action: a bare numeric account with no
 * username/credential/role is not a user — real users sign up via
 * /register.) */
static void route_admin_tokens_mint_pat(struct yloop_stream *s,
                                        const char *body, size_t body_len, int keep_alive)
{
    char uid_s[32];
    if (form_get(body, body_len, "uid", uid_s, sizeof(uid_s))) {
        uint32_t uid = (uint32_t)strtoul(uid_s, NULL, 10);
        SVC_OPEN(pat, "personal_access_tokens", personal_access_tokens_personal_access_tokens_create);
        if (pat.ok && uid) {
            personal_access_tokens_personal_access_tokens_mint(&pat.c, pat.obj, NULL, uid);
            SVC_CLOSE(pat);
        }
    }
    send_redirect(s, "/admin/tokens", keep_alive, NULL);
}

/* `/admin/storage` was removed: backend kv state isn't a UI concern,
 * even for the site owner. Operators inspect /tmp/picoforge/central.db
 * with sqlite3(1) directly. */

/* ---------- top-level dispatcher ---------- */

static int path_eq(const char *p, const char *target)
{
    /* `/repos?...` should match `/repos`. */
    const char *q = strchr(p, '?');
    size_t n = q ? (size_t)(q - p) : strlen(p);
    return n == strlen(target) && memcmp(p, target, n) == 0;
}

/* ---------- yaapp-compatible gateway API (/_rpc, /_describe) ---------- */

static void send_json_ex(struct yloop_stream *s, int status,
                         const char *body, size_t body_len, int keep_alive,
                         const char *extra_headers)
{
    const char *reason = "OK";
    if (status == 400) reason = "Bad Request";
    else if (status == 401) reason = "Unauthorized";
    else if (status == 403) reason = "Forbidden";
    else if (status == 404) reason = "Not Found";
    else if (status == 410) reason = "Gone";
    else if (status == 500) reason = "Internal Server Error";
    else if (status == 502) reason = "Bad Gateway";

    char hdr[640];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
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

static void send_json(struct yloop_stream *s, int status,
                      const char *body, size_t body_len, int keep_alive)
{
    send_json_ex(s, status, body, body_len, keep_alive, NULL);
}


static void write_error_detail(struct yjson_writer *w, const char *message)
{
    const char *msg = message ? message : "";
    size_t first_len = strcspn(msg, "\n");
    char first[512];
    size_t copy = first_len < sizeof(first) - 1 ? first_len : sizeof(first) - 1;
    memcpy(first, msg, copy);
    first[copy] = 0;
    yjson_writer_key(w, "message"); yjson_writer_string(w, first[0] ? first : msg);
    yjson_writer_key(w, "detail");  yjson_writer_string(w, msg);
    yjson_writer_key(w, "trace");   yjson_writer_begin_array(w);
    const char *p = msg;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char line[1024];
        size_t lc = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        memcpy(line, p, lc);
        line[lc] = 0;
        yjson_writer_string(w, line);
        if (!nl) break;
        p = nl + 1;
    }
    yjson_writer_end_array(w);
}

static void send_json_error(struct yloop_stream *s, int status,
                            const char *message, int keep_alive)
{
    if (status >= 500) yerror("yhttp gateway request failed: %s", message ? message : "");
    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "error");
    yjson_writer_begin_object(w);
    write_error_detail(w, message);
    yjson_writer_end_object(w);
    yjson_writer_end_object(w);
    size_t len;
    const char *data = yjson_writer_data(w, &len);
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
 * picomesh-sid cookie, a `picomesh-sid:` header, or `Authorization: Bearer
 * <token>`. All three are opaque-token forms the gateway accepts (see
 * CLAUDE.md — JWTs never cross this boundary). Returns 1 if a
 * non-empty token was found. */
static int extract_session_token(const char *headers_raw, size_t headers_raw_len,
                                 char *out, size_t cap)
{
    if (cookie_get(headers_raw, headers_raw_len, "picomesh-sid", out, cap) && out[0])
        return 1;
    if (header_value(headers_raw, headers_raw_len, "picomesh-sid", out, cap) && out[0])
        return 1;
    char auth[128];
    if (header_value(headers_raw, headers_raw_len, "authorization", auth, sizeof(auth))) {
        /* Only the Bearer scheme carries an opaque session token. A non-Bearer
         * Authorization (Basic, Digest, …) is NOT a session token: reject it
         * rather than treating the raw header value as one. */
        if (strncasecmp(auth, "Bearer ", 7) == 0) {
            const char *p = auth + 7;
            while (*p == ' ') ++p;
            size_t n = strlen(p);
            if (n && n < cap) { memcpy(out, p, n); out[n] = 0; return 1; }
        }
    }
    out[0] = 0;
    return 0;
}

/* Resolve an opaque session token to the authenticated uid via the
 * session backend. 0 → anonymous / invalid. */
static uint32_t uid_for_token(const char *token)
{
    if (!token || !*token) return 0;
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return 0;
    struct ctx c = picomesh_engine_service_ctx(e, "session");
    /* peer==NULL ⇒ session collocated in-process; create resolves it locally. */
    struct object_ptr_result o = session_session_create(&c);
    if (PICOMESH_IS_ERR(o)) { picomesh_error_destroy(o.error); return 0; }
    struct picomesh_uint32_result lr = session_session_lookup(&c, o.value, NULL, token);
    object_release_in_ctx(&c, o.value);
    if (PICOMESH_IS_ERR(lr)) { picomesh_error_destroy(lr.error); return 0; }
    return lr.value;
}

/* Resolve the caller's opaque session token to a VERIFIED auth context, the
 * same exchange the `session_cookie` authenticator performs for /_rpc: the
 * opaque sid → stored access JWT (session.session.jwt) → signature+expiry
 * verification against the shared signing secret. FAILS CLOSED — a missing,
 * expired, or invalid JWT (or unavailable key material) yields an anonymous
 * context (authenticated 0, uid 0). Unlike the bare sid→uid `uid_for_token`
 * lookup, an expired stored JWT can no longer authorize: the gateway's
 * non-login mutation routes resolve identity through this so they cannot be
 * authorized by a stale credential. Always populates `out`. */
static void resolve_authctx(const char *headers_raw, size_t headers_raw_len,
                            struct picomesh_authctx *out)
{
    memset(out, 0, sizeof(*out));
    char token[64];
    if (!extract_session_token(headers_raw, headers_raw_len, token, sizeof(token)))
        return;
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return;
    struct ctx c = picomesh_engine_service_ctx(e, "session");
    struct object_ptr_result o = session_session_create(&c);
    if (PICOMESH_IS_ERR(o)) { picomesh_error_destroy(o.error); return; }
    struct picomesh_string_result jr = session_session_jwt(&c, o.value, NULL, token);
    object_release_in_ctx(&c, o.value);
    if (PICOMESH_IS_ERR(jr)) { picomesh_error_destroy(jr.error); return; }
    if (jr.value && jr.value[0]) {
        struct picomesh_string_result secret = picomesh_security_jwt_secret(e);
        if (PICOMESH_IS_OK(secret)) {
            struct picomesh_void_result ctx_r = picomesh_authctx_from_jwt(jr.value, secret.value, out);
            if (PICOMESH_IS_ERR(ctx_r)) picomesh_error_destroy(ctx_r.error);
            free(secret.value);
        } else {
            picomesh_error_destroy(secret.error);
        }
    }
    free(jr.value);
}

/* The frontend's security pipeline, built ONCE per worker and reused for every
 * gated request (cached on the worker via the engine). `secured` records
 * whether this node carries `security.authenticators` at all; when it does,
 * `build_ok` records whether the chain + authorizer built cleanly. Per-worker,
 * so it is thread-confined and reuses this worker's own remotes — no locking. */
struct gateway_security {
    int secured;
    int build_ok;
    struct picomesh_authn_chain *chain;
    struct picomesh_authorizer *authorizer;
};

static void gateway_security_free(void *ptr)
{
    struct gateway_security *security = ptr;
    if (!security) return;
    picomesh_authn_chain_free(security->chain);
    picomesh_authorizer_free(security->authorizer);
    free(security);
}

/* Return this worker's cached pipeline, building it on first use. NULL only on
 * allocation failure. A build error is cached as `secured && !build_ok` so a
 * misconfig is reported per request without rebuilding every time. */
static struct gateway_security *gateway_security_get(struct picomesh_engine *e)
{
    struct gateway_security *security = picomesh_engine_worker_security(e);
    if (security) return security;

    security = calloc(1, sizeof(*security));
    if (!security) return NULL;

    const struct yconfig *config = picomesh_engine_config(e);
    struct yconfig_node_ptr_result authn_r =
        config ? yconfig_get(config, "security.authenticators") : (struct yconfig_node_ptr_result){.ok = 0};
    const struct yconfig_node *authn_list = PICOMESH_IS_OK(authn_r) ? authn_r.value : NULL;
    if (PICOMESH_IS_ERR(authn_r)) picomesh_error_destroy(authn_r.error);
    security->secured = authn_list && yconfig_node_kind(authn_list) == YCONFIG_LIST;

    if (security->secured) {
        struct picomesh_void_ptr_result chain_r = picomesh_authn_chain_build(e, authn_list);
        if (PICOMESH_IS_OK(chain_r)) {
            security->chain = chain_r.value;
            struct yconfig_node_ptr_result authz_r = yconfig_get(config, "security.authorizer");
            const struct yconfig_node *authz_cfg = PICOMESH_IS_OK(authz_r) ? authz_r.value : NULL;
            if (PICOMESH_IS_ERR(authz_r)) picomesh_error_destroy(authz_r.error);
            struct picomesh_void_ptr_result authorizer_r = picomesh_authorizer_build(e, authz_cfg);
            if (PICOMESH_IS_OK(authorizer_r)) {
                security->authorizer = authorizer_r.value;
                security->build_ok = 1;
            } else {
                picomesh_error_destroy(authorizer_r.error);
            }
        } else {
            picomesh_error_destroy(chain_r.error);
        }
    }
    picomesh_engine_worker_set_security(e, security, gateway_security_free);
    return security;
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
    struct picomesh_engine *e = picomesh_active_engine();
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

    /* Resolve + gate the path through the shared active-service resolver:
     * the service must be activated locally or configured as a remote on
     * this node before any class/method is looked up. No global method
     * table is reached without that gate. */
    struct picomesh_service_call_result call_r = picomesh_resolve_service_call(e, path);
    if (PICOMESH_IS_ERR(call_r)) {
        const char *rmsg = call_r.error.msg ? call_r.error.msg : "resolve failed";
        int code = strstr(rmsg, "bad path") ? 400 : strstr(rmsg, "not active") ? 404 : 502;
        char msg[320];
        snprintf(msg, sizeof(msg), "_rpc: %s", rmsg);
        picomesh_error_destroy(call_r.error);
        yjson_doc_free(doc);
        send_json_error(s, code, msg, keep_alive);
        return;
    }
    struct picomesh_service_call call = call_r.value;

    /* Method resolution first: an unknown method is a 404 regardless of auth,
     * so a probe cannot use the auth status to distinguish real methods. */
    jinvoke_fn fn = jinvoke_for(call.method_qname);
    if (!fn) {
        picomesh_service_call_release(&call);
        yjson_doc_free(doc);
        char msg[256];
        snprintf(msg, sizeof(msg), "_rpc: no method '%s'", call.method_qname);
        send_json_error(s, 404, msg, keep_alive);
        return;
    }

    /* Authentication + authorization. A frontend configured with
     * `security.authenticators` (the gateway) runs the configured authenticator
     * chain, then the configured authorizer, BEFORE the backend is invoked.
     * Both are pluggable framework categories selected by `type:` — this
     * frontend just builds and runs them. A node with no `security` block (a
     * plain transport bridge) keeps the legacy session→uid resolution. */
    uint32_t uid = 0;
    char *verified_jwt = NULL; /* owned; copied into yheaders below */
    struct gateway_security *security = gateway_security_get(e);
    if (!security) {
        picomesh_service_call_release(&call);
        yjson_doc_free(doc);
        send_json_error(s, 500, "_rpc: security pipeline unavailable", keep_alive);
        return;
    }

    if (security->secured) {
        if (!security->build_ok) {
            picomesh_service_call_release(&call);
            yjson_doc_free(doc);
            send_json_error(s, 500, "_rpc: security config error (authenticators/authorizer)", keep_alive);
            return;
        }
        struct picomesh_authn_request authn_req = {
            .engine = e, .headers_raw = headers_raw,
            .headers_raw_len = headers_raw_len, .endpoint = path,
        };
        struct picomesh_authn_outcome outcome = picomesh_authn_chain_run(security->chain, &authn_req);
        if (picomesh_authn_outcome_failed(&outcome)) {
            char msg[288];
            snprintf(msg, sizeof(msg), "_rpc: %s", outcome.error ? outcome.error : "invalid credentials");
            picomesh_authn_outcome_free(&outcome);
            picomesh_service_call_release(&call);
            yjson_doc_free(doc);
            send_json_error(s, 401, msg, keep_alive);
            return;
        }
        struct picomesh_authz_decision decision =
            picomesh_authorizer_decide(security->authorizer, path, args, outcome.jwt);
        if (!decision.allowed) {
            char msg[288];
            snprintf(msg, sizeof(msg), "_rpc: %s", decision.reason[0] ? decision.reason : "forbidden");
            picomesh_authn_outcome_free(&outcome);
            picomesh_service_call_release(&call);
            yjson_doc_free(doc);
            send_json_error(s, decision.status ? decision.status : 403, msg, keep_alive);
            return;
        }
        /* Allowed: carry the verified JWT downstream; derive uid from its
         * claims for the trace/log line (backends use the JWT, not the uid). */
        if (outcome.jwt) {
            verified_jwt = strdup(outcome.jwt);
            struct picomesh_string_result secret = picomesh_security_jwt_secret(e);
            if (PICOMESH_IS_OK(secret)) {
                struct picomesh_authctx claims;
                struct picomesh_void_result ctx_r =
                    picomesh_authctx_from_jwt(outcome.jwt, secret.value, &claims);
                if (PICOMESH_IS_OK(ctx_r) && claims.authenticated) uid = claims.uid;
                else if (PICOMESH_IS_ERR(ctx_r)) picomesh_error_destroy(ctx_r.error);
                free(secret.value);
            } else {
                picomesh_error_destroy(secret.error);
            }
        }
        picomesh_authn_outcome_free(&outcome);
    } else {
        char token[64];
        if (extract_session_token(headers_raw, headers_raw_len, token, sizeof(token)))
            uid = uid_for_token(token);
    }

    /* Request-header bag handed to the backend: the verified auth context +
     * distributed-trace context. The backend receives the verified access JWT
     * (claims, not just a numeric uid) plus the resolved uid for back-compat;
     * the opaque session token / bearer credential never leaves the gateway.
     * The gateway is the trace root: it continues an inbound W3C `traceparent`
     * (or mints a fresh trace) and opens the root SERVER span; downstream
     * client stubs read parent_span_id from the bag and nest beneath it. */
    char span_name[96];
    snprintf(span_name, sizeof(span_name), "gateway.%s", path);
    struct yheaders *hdrs = yheaders_new();
    struct ytelemetry_span sp;
    memset(&sp, 0, sizeof(sp));
    if (hdrs) {
        yheaders_set_u32(hdrs, "uid", uid);
        if (verified_jwt) yheaders_set(hdrs, "jwt", verified_jwt);
        char traceparent[128] = {0};
        header_value(headers_raw, headers_raw_len, "traceparent",
                     traceparent, sizeof(traceparent));
        ytelemetry_hdrs_seed_root(hdrs, traceparent[0] ? traceparent : NULL);
        ytelemetry_server_span_begin(&sp, hdrs, span_name);
        yinfo("[gateway] _rpc trace=%s path=%s uid=%u", sp.trace_id, path, uid);
    }
    free(verified_jwt);

    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "result");
    char err[8192] = {0};
    int rc = fn(&call.ctx, call.obj, hdrs, args, w, err, sizeof(err));
    if (hdrs) ytelemetry_span_end(&sp, rc == 0, rc != 0 ? err : NULL);
    yheaders_free(hdrs);
    picomesh_service_call_release(&call);
    if (rc != 0) {
        yjson_writer_free(w);
        yjson_doc_free(doc);
        send_json_error(s, 500, err[0] ? err : "_rpc: call failed", keep_alive);
        return;
    }
    yjson_writer_end_object(w);
    size_t len;
    const char *data = yjson_writer_data(w, &len);
    /* Echo the trace context (W3C traceparent) so clients/tests/UI can
     * correlate the response with the trace stored by the collector. */
    char tp_header[160] = {0};
    if (sp.trace_id[0]) {
        char tpval[64];
        ytelemetry_traceparent_format(tpval, sizeof(tpval), sp.trace_id, sp.span_id, sp.sampled);
        snprintf(tp_header, sizeof(tp_header), "traceparent: %s\r\n", tpval);
    }
    send_json_ex(s, 200, data, len, keep_alive, tp_header[0] ? tp_header : NULL);
    yjson_writer_free(w);
    yjson_doc_free(doc);
}

/* GET|POST /_whoami — resolve the caller's opaque session token (cookie /
 * picomesh-sid header / Bearer) to its authenticated claims:
 *   {"uid":N,"username":"…","is_admin":true|false}
 * uid 0 ⇒ anonymous. This is the gateway's external→internal identity
 * translation, exposed so the webapp can gate admin space and derive
 * ownership from the live session instead of a forgeable cookie. The
 * payload carries only non-sensitive claims (uid, username, admin bit) —
 * never the internal JWT or any secret. The username comes from the
 * server-side accounts/name:<uid> index, not from anything client-supplied. */
static void route_whoami(struct yloop_stream *s,
                         const char *headers_raw, size_t headers_raw_len,
                         int keep_alive)
{
    /* Resolve the opaque sid → verified JWT → claims. The username and groups
     * are carried IN the access JWT (minted by the token issuer at login), so
     * we read them straight from the verified context — NOT from a storage
     * lookup. (The old `accounts`/`name:<uid>` key in sharded_storage is dead:
     * the security/rel-db refactor moved accounts into the relational store, so
     * that read always missed and username came back empty.) */
    struct picomesh_authctx caller;
    resolve_authctx(headers_raw, headers_raw_len, &caller);
    int admin = caller.uid &&
        picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer");

    struct yjson_writer *w = yjson_writer_new();
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "uid");      yjson_writer_int(w, (int64_t)caller.uid);
    yjson_writer_key(w, "username"); yjson_writer_string(w, caller.username);
    yjson_writer_key(w, "is_admin"); yjson_writer_bool(w, admin);
    /* The caller's own namespace memberships ("<path>:<role>") as
     * [{"path","role"}, …] — non-secret self data (issue #30). The webapp uses
     * it to offer a repo-creation namespace picker and a group-management area;
     * it never carries a JWT. */
    yjson_writer_key(w, "namespaces");
    yjson_writer_begin_array(w);
    for (const char *cursor = caller.groups_csv; cursor && *cursor; ) {
        const char *comma = strchr(cursor, ',');
        size_t span = comma ? (size_t)(comma - cursor) : strlen(cursor);
        const char *colon = memchr(cursor, ':', span);
        if (colon) {
            size_t path_len = (size_t)(colon - cursor);
            size_t role_len = span - path_len - 1;
            char ns_path[256], ns_role[32];
            if (path_len < sizeof(ns_path) && role_len < sizeof(ns_role)) {
                memcpy(ns_path, cursor, path_len); ns_path[path_len] = 0;
                memcpy(ns_role, colon + 1, role_len); ns_role[role_len] = 0;
                yjson_writer_begin_object(w);
                yjson_writer_key(w, "path"); yjson_writer_string(w, ns_path);
                yjson_writer_key(w, "role"); yjson_writer_string(w, ns_role);
                yjson_writer_end_object(w);
            }
        }
        if (!comma) break;
        cursor = comma + 1;
    }
    yjson_writer_end_array(w);
    yjson_writer_end_object(w);
    size_t len;
    const char *data = yjson_writer_data(w, &len);
    send_json(s, 200, data, len, keep_alive);
    yjson_writer_free(w);
}

struct describe_emit_ctx { struct yjson_writer *w; };

static void describe_slot_cb(const char *name, method_slot slot, void *ud)
{
    (void)slot;
    struct describe_emit_ctx *dc = ud;
    yjson_writer_string(dc->w, name);
}

/* --- root /_describe: list active services, each with a source tag ---
 *
 * A service is `remote` when the engine holds a peer channel for it (it
 * lives in another process, reached over yrpc) and `local` when it's a
 * plugin activated in THIS process (no peer — dispatched in-process).
 * The active set is exactly what activate_plugins() turned on: this
 * node's `plugins:` list (local candidates) plus its `remotes:` list
 * (proxied peers). We read those same two config sources and let the
 * peer check decide the tag, so a collocated all-in-one run reports
 * everything `local` while the split mesh reports the backends `remote`. */
struct describe_services_ctx {
    struct picomesh_engine *e;
    struct yjson_writer *w;
    char seen[64][64];
    size_t seen_n;
};

/* While emitting a service's `classes[]`: match registered classes whose
 * qualified name is `<service>_<class>` and emit {class, qname, methods}.
 * The service name is the plugin domain, so the qname prefix `<service>_`
 * selects exactly that service's classes; the remainder is the class part
 * of the dotted path the console feeds back to /_rpc and /<svc.class>/_describe. */
struct describe_class_ctx {
    struct yjson_writer *w;
    const char *service;
    const char *prefix;   /* "<service>_" */
    size_t prefix_len;
};

static void describe_method_cb(const char *name, method_slot slot, void *ud)
{
    (void)slot;
    yjson_writer_string((struct yjson_writer *)ud, name);
}

static void describe_class_cb(const struct class *cls, const char *qname, void *ud)
{
    struct describe_class_ctx *cc = ud;
    if (strncmp(qname, cc->prefix, cc->prefix_len) != 0) return;
    const char *class_part = qname + cc->prefix_len;
    if (!*class_part) return;
    char dotpath[192];
    snprintf(dotpath, sizeof(dotpath), "%s.%s", cc->service, class_part);
    yjson_writer_begin_object(cc->w);
    yjson_writer_key(cc->w, "class");   yjson_writer_string(cc->w, dotpath);
    yjson_writer_key(cc->w, "qname");   yjson_writer_string(cc->w, qname);
    yjson_writer_key(cc->w, "methods"); yjson_writer_begin_array(cc->w);
    class_for_each_slot(cls, describe_method_cb, cc->w);
    yjson_writer_end_array(cc->w);
    yjson_writer_end_object(cc->w);
}

/* Emit one {service, source, classes:[...]} object, deduped by name. The
 * service list stays config-driven (active plugins + remotes); `classes`
 * just enriches each ACTIVE service with the method tree the generic
 * console renders — pulled from the registry, which in this process holds
 * only the activated/proxied service classes (gh#15). */
static void describe_emit_service(struct describe_services_ctx *dc, const char *name)
{
    if (!name || !*name) return;
    for (size_t i = 0; i < dc->seen_n; ++i)
        if (strcmp(dc->seen[i], name) == 0) return;
    if (dc->seen_n < sizeof(dc->seen) / sizeof(dc->seen[0]))
        snprintf(dc->seen[dc->seen_n++], sizeof(dc->seen[0]), "%s", name);
    int remote = dc->e && picomesh_engine_service_ctx(dc->e, name).peer != NULL;
    yjson_writer_begin_object(dc->w);
    yjson_writer_key(dc->w, "service"); yjson_writer_string(dc->w, name);
    yjson_writer_key(dc->w, "source");  yjson_writer_string(dc->w, remote ? "remote" : "local");
    yjson_writer_key(dc->w, "classes"); yjson_writer_begin_array(dc->w);
    char prefix[80];
    int pl = snprintf(prefix, sizeof(prefix), "%s_", name);
    struct describe_class_ctx cc = {
        .w = dc->w, .service = name, .prefix = prefix,
        .prefix_len = pl > 0 ? (size_t)pl : 0,
    };
    class_for_each(describe_class_cb, &cc);
    yjson_writer_end_array(dc->w);
    yjson_writer_end_object(dc->w);
}

/* `plugins:` entries are plain strings — each names a local plugin. */
static void describe_emit_plugins(struct describe_services_ctx *dc,
                                  const struct yconfig_node *list)
{
    if (!list || yconfig_node_kind(list) != YCONFIG_LIST) return;
    size_t n = yconfig_node_size(list);
    for (size_t i = 0; i < n; ++i) {
        const struct yconfig_node *entry = yconfig_node_at(list, i);
        describe_emit_service(dc, entry ? yconfig_node_as_string(entry, NULL) : NULL);
    }
}

/* for_each cb: capture one `remotes[]` map entry's `service:` value. */
static int describe_grab_service_cb(const char *key, const struct yconfig_node *val, void *ud)
{
    if (strcmp(key, "service") == 0) {
        const char *s = yconfig_node_as_string(val, NULL);
        if (s) { snprintf((char *)ud, 64, "%s", s); return 1; /* stop */ }
    }
    return 0;
}

/* `remotes:` entries are maps carrying a `service:` field. */
static void describe_emit_remotes(struct describe_services_ctx *dc,
                                  const struct yconfig_node *list)
{
    if (!list || yconfig_node_kind(list) != YCONFIG_LIST) return;
    size_t n = yconfig_node_size(list);
    for (size_t i = 0; i < n; ++i) {
        const struct yconfig_node *entry = yconfig_node_at(list, i);
        if (!entry || yconfig_node_kind(entry) != YCONFIG_MAP) continue;
        char rn[64] = {0};
        yconfig_node_for_each(entry, describe_grab_service_cb, rn);
        if (rn[0]) describe_emit_service(dc, rn);
    }
}

/* Resolve a config list node with activate_plugins' precedence:
 * `mesh.services.<--name>.<named_suffix>` first (parent-config / collocated
 * run where --name is set), else the top-level `<top_key>` (split per-node
 * file the mesh writes per child). Returns the node or NULL. */
static const struct yconfig_node *describe_node_list(struct picomesh_engine *e,
                                                     const char *named_suffix,
                                                     const char *top_key)
{
    const struct yconfig *cfg = picomesh_engine_config(e);
    if (!cfg) return NULL;
    struct yargv_chain *cli = picomesh_engine_cli(e);
    const char *name = cli ? yargv_get_string(cli, "name", NULL) : NULL;
    if (name && *name) {
        char path[256];
        snprintf(path, sizeof(path), "mesh.services.%s.%s", name, named_suffix);
        struct yconfig_node_ptr_result r = yconfig_get(cfg, path);
        if (PICOMESH_IS_OK(r) && r.value) return r.value;
        if (PICOMESH_IS_ERR(r)) picomesh_error_destroy(r.error);
    }
    struct yconfig_node_ptr_result r = yconfig_get(cfg, top_key);
    if (PICOMESH_IS_OK(r) && r.value) return r.value;
    if (PICOMESH_IS_ERR(r)) picomesh_error_destroy(r.error);
    return NULL;
}

/* GET|POST /_describe[_tree] and /<service.class>/_describe[_tree].
 * With a class path it lists that class's method slots; at the root it
 * lists the gateway's configured backend services. `tree` currently
 * shares the shallow shape (one class level); deeper nesting can layer
 * on once a per-domain class enumerator exists. */
static void route_describe(struct yloop_stream *s, const char *class_dotpath,
                           int tree, int keep_alive)
{
    struct picomesh_engine *e = picomesh_active_engine();
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
        if (PICOMESH_IS_OK(cr) && cr.value) {
            /* Class path: list its method slots (names only). */
            yjson_writer_begin_object(w);
            yjson_writer_key(w, "path");    yjson_writer_string(w, class_dotpath);
            yjson_writer_key(w, "class");   yjson_writer_string(w, class_qname);
            yjson_writer_key(w, "methods"); yjson_writer_begin_array(w);
            struct describe_emit_ctx dc = {.w = w};
            class_for_each_slot(cr.value, describe_slot_cb, &dc);
            yjson_writer_end_array(w);
            yjson_writer_end_object(w);
        } else {
            if (PICOMESH_IS_ERR(cr)) picomesh_error_destroy(cr.error);
            /* Not a class — maybe a fully-qualified method (service.class.method).
             * Reflect its call signature: the USER parameters in declared order,
             * baked into the binary by codegen (gh#15). This is what a generic
             * console renders one field per — namespace/key/value, not one blob. */
            const struct jinvoke_params *mp = jinvoke_params_for(class_qname);
            if (!mp) {
                yjson_writer_free(w);
                send_json_error(s, 404, "_describe: unknown class or method", keep_alive);
                return;
            }
            yjson_writer_begin_object(w);
            yjson_writer_key(w, "path");   yjson_writer_string(w, class_dotpath);
            yjson_writer_key(w, "method"); yjson_writer_string(w, class_qname);
            yjson_writer_key(w, "params"); yjson_writer_begin_array(w);
            for (size_t i = 0; i < mp->count; ++i) {
                yjson_writer_begin_object(w);
                yjson_writer_key(w, "name"); yjson_writer_string(w, mp->items[i].name);
                yjson_writer_key(w, "type"); yjson_writer_string(w, mp->items[i].type);
                yjson_writer_end_object(w);
            }
            yjson_writer_end_array(w);
            yjson_writer_end_object(w);
        }
    } else {
        /* Root: list the services THIS node can route to, each tagged
         * with its source (yaapp Seed.describe shape). Local services are
         * the plugins activated in-process; remote services are the
         * `remotes:` peers reached over yrpc. Both come from the same
         * config the engine activated from — emitted local-first, then
         * remote, deduped, with the source decided by the peer check. */
        yjson_writer_begin_object(w);
        yjson_writer_key(w, "services");
        yjson_writer_begin_array(w);
        if (e) {
            struct describe_services_ctx dc = {.e = e, .w = w, .seen_n = 0};
            describe_emit_plugins(&dc, describe_node_list(e, "plugins", "plugins"));
            describe_emit_remotes(&dc, describe_node_list(e, "config.remotes", "remotes"));
        }
        yjson_writer_end_array(w);
        yjson_writer_key(w, "tree"); yjson_writer_bool(w, tree ? true : false);
        yjson_writer_end_object(w);
    }

    size_t len;
    const char *data = yjson_writer_data(w, &len);
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

/* A non-gateway yhttp node with a non-empty `remotes:` list is a
 * yrpc->yhttp transport bridge (gh#15): it fronts its configured remote
 * yrpc services with the generic JSON API and nothing else. */
static int yhttp_node_has_remotes(struct picomesh_engine *e)
{
    const struct yconfig_node *r = describe_node_list(e, "config.remotes", "remotes");
    return r && yconfig_node_kind(r) == YCONFIG_LIST && yconfig_node_size(r) > 0;
}

int yhttp_frontend_try(struct yloop_stream *s,
                       const char *method, const char *path,
                       const char *headers_raw, size_t headers_raw_len,
                       const char *body, size_t body_len,
                       int keep_alive)
{
    /* Decide whether THIS process is the web-facing gateway (serve the
     * HTML app + /_rpc) or just a backend that should answer JSON only.
     * Two ways to be the gateway:
     *   - `session` is wired as a REMOTE (classic split mesh: the gateway
     *     process forwards to separate backend processes), or
     *   - `yhttp.serve_app` is set in config (collocated / all-in-one mode:
     *     gateway + every service live in ONE process, so `session` has no
     *     remote peer — it's a local in-process object — yet we still serve
     *     the app).
     * A pure backend child has neither, so it falls through to JSON. */
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return 0;
    int has_session_remote = picomesh_engine_service_ctx(e, "session").peer != NULL;
    struct yconfig_node_ptr_result serve_app_r =
        yconfig_get(picomesh_engine_config(e), "yhttp.serve_app");
    int serve_app = PICOMESH_IS_OK(serve_app_r) && serve_app_r.value &&
                    yconfig_node_as_bool(serve_app_r.value, 0);
    /* `yhttp.bridge_only`: an explicit opt-out of gateway mode. A pure
     * transport bridge that wants to front EVERY backend (so the service
     * console can inspect them all) must list `session` as a remote, which
     * would otherwise look like the auth gateway — this flag keeps it a
     * bridge: generic JSON API, no picoforge auth/HTML routes (gh#15). */
    struct yconfig_node_ptr_result bridge_only_r =
        yconfig_get(picomesh_engine_config(e), "yhttp.bridge_only");
    int bridge_only = PICOMESH_IS_OK(bridge_only_r) && bridge_only_r.value &&
                      yconfig_node_as_bool(bridge_only_r.value, 0);
    /* The picoforge gateway (auth boundary): `session` wired as a remote, or
     * the collocated all-in-one `yhttp.serve_app`. It owns the auth/HTML
     * POSTs below. A non-gateway yhttp node with remotes is a generic
     * transport bridge that serves ONLY the JSON API block below (gh#15). A
     * node with neither (the mesh control parent, a standalone backend on
     * yhttp) is none of our business — fall through to the yhttp serve layer. */
    int is_gateway = (has_session_remote || serve_app) && !bridge_only;
    int is_bridge = !is_gateway && yhttp_node_has_remotes(e);
    if (!is_gateway && !is_bridge) return 0;

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
            if (path_eq(path, "/_whoami"))        { route_whoami(s, headers_raw, headers_raw_len, keep_alive); return 1; }
            if (try_hierarchical_describe(s, path, keep_alive)) return 1;
        }

        /* GET /_perf — dump this process's LOCAL latency aggregate (op →
         * count + p50/p90/p99/max us). This is NOT distributed tracing:
         * it is a per-process op-latency table. `?reset` clears the ring
         * so you can measure a fresh window. Real per-request traces
         * (trace_id/span_id/parent_span_id, cross-process) live on the
         * trace collector — query GET /traces/<trace_id> there. */
        if (is_get && path_eq(path, "/_perf")) {
            if (strstr(path, "reset")) yspan_reset();
            char dump[32768];
            size_t n = yspan_dump(dump, sizeof(dump));
            send_html(s, 200, dump, n, keep_alive, NULL);
            return 1;
        }
        /* /_trace was a misnomer for the local op aggregate above; it is
         * not a trace query. Point callers at /_perf and the collector. */
        if (is_get && path_eq(path, "/_trace")) {
            const char *moved =
                "/_trace is retired: it was a local op-latency aggregate, not "
                "distributed tracing.\n"
                "  - local latency table  -> GET /_perf\n"
                "  - a request's trace     -> GET /traces/<trace_id> on the trace collector\n";
            send_html(s, 200, moved, strlen(moved), keep_alive, NULL);
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

    /* A transport bridge stops here: the generic JSON API above is the
     * whole contract. The picoforge auth/HTML POSTs below belong only to
     * the gateway. Returning 0 lets the yhttp serve layer answer the
     * remaining transport-compat ops (binary /_rpc?op=, /create, /invoke). */
    if (!is_gateway) return 0;

    /* ---- AUTH + ACTION POSTs (the only non-API surface the gateway
     * keeps). These mint/clear the picomesh-sid cookie or mutate state and
     * answer with a redirect/JSON — NO HTML rendering. The picoforge
     * frontend app forwards its form POSTs here; the gateway is the auth
     * boundary that owns the session cookie. Per CLAUDE.md the gateway
     * serves NO pages and NO static files — all GET HTML routes and the
     * static fallthrough have been removed (the frontend app renders every
     * page, sourcing data from this gateway over /_rpc + /_describe). ---- */
    if (is_post && path_eq(path, "/login"))    { route_login_post(s, body, body_len, keep_alive); return 1; }
    if (is_post && path_eq(path, "/register")) { route_register_post(s, body, body_len, keep_alive); return 1; }
    if (is_post && path_eq(path, "/logout"))   { route_logout(s, headers_raw, headers_raw_len, keep_alive); return 1; }

    /* Resolve identity only here, for the authenticated action POSTs — the
     * API routes above (/_rpc, /_describe, /_whoami, /_perf) and the
     * login/register/logout POSTs either resolve sid→uid themselves or
     * don't need it. Resolving up front would charge every /_rpc a second,
     * redundant session exchange (the dominant per-call cost).
     *
     * These non-login mutation routes resolve identity through the SAME
     * verified exchange as /_rpc — the opaque sid → stored access JWT →
     * signature+expiry verification — so an expired or invalid session JWT
     * cannot authorize a mutation, and the writable namespace + admin gate are
     * derived from the verified JWT claims (uid, username, groups), never the
     * forgeable picomesh-uname cookie. */
    struct picomesh_authctx caller;
    resolve_authctx(headers_raw, headers_raw_len, &caller);
    uint32_t uid = caller.authenticated ? caller.uid : 0;

    /* Authenticated action POSTs (forwarded by the frontend app with the
     * session cookie). They redirect or 404 — never render a page. */
    if (uid) {
        const char *uname = caller.username;
        if (is_post && path_eq(path, "/repos/new"))
            { route_repos_new_post(s, uid, uname, body, body_len, keep_alive); return 1; }
        int site_admin = picomesh_groups_max_role(caller.groups_csv, "site") >=
                         picomesh_role_rank("maintainer");
        if (strncmp(path, "/admin/", 7) == 0 && site_admin) {
            /* Token administration lives in its own admin section. */
            if (is_post && path_eq(path, "/admin/tokens/mint_pat"))
                { route_admin_tokens_mint_pat(s, body, body_len, keep_alive); return 1; }
        }
    }

    /* The gateway serves NO GET pages (it is API-only). Every GET that
     * survived the API routes above is an HTML page the frontend app owns
     * — 404 it here rather than returning 0. Returning 0 would let the
     * yhttp.c serve layer answer unmatched GETs from its own table, and
     * that table still maps `GET /` to a 200 control banner. The mesh
     * control parent never reaches this code (it returned 0 at the very
     * top, having neither a `session` remote nor `serve_app`), so its
     * bootstrap banner is unaffected. */
    if (is_get) {
        send_json_error(s, 404, "no such GET route", keep_alive);
        return 1;
    }

    /* Anything else — unknown POST, static asset — is NOT the gateway's
     * concern. Fall through to a 404 (the frontend app, not the gateway,
     * owns pages). */
    return 0;
}
