/* picoforge-perf — load generator + latency/throughput harness for the
 * picoforge gateway.
 *
 * Drives the GATEWAY only (the production hot path, per CLAUDE.md): it
 * never touches a backend port directly.
 *
 * Concurrency model — HYBRID threads + coroutines (the same shape the
 * server uses): M OS threads, each running its OWN libuv event loop and
 * libco coroutine scheduler, hosting K coroutines. Every coroutine drives
 * one async keep-alive connection (yloop_connect_tcp + yloop_read_some +
 * yloop_write, all coroutine-yielding). So C concurrent connections cost
 * M threads × K coroutines — NOT C OS threads. This lets one box emulate
 * tens of thousands of clients without drowning the scheduler.
 *
 *   total connections = --threads × --coros-per-thread
 *   (or --connections spread evenly across --threads)
 *
 * Scenarios (selected with --scenario), each timed per iteration:
 *   rpc_count    POST /_rpc git_repo.git_repo.count_total  (read; gateway→1 backend)
 *   login        POST /login                             (auth composite; 4 backends)
 *   repo_create  POST /repos/new                         (write; sharded_storage + git_repo)
 *   register     POST /register (new user each time)     (write; accounts + authn)
 *   full         register → login → repo_create → /repos (end-to-end journey)
 *   mixed        each connection = a user doing a random op stream, including
 *                GitLab-style group/namespace ops (ns_subtree/ns_members reads,
 *                ns_create subgroup + ns_add_member writes) on its own namespace
 *
 * Usage:
 *   picoforge-perf [--host H] [--port P]
 *              [--threads M] [--connections N | --coros-per-thread K]
 *              [--duration SECS | --requests R] [--scenario NAME]
 *              [--seed-users N] [--repos-per-worker K]
 */

#define _GNU_SOURCE

#include <picomesh/yco/coro.h>
#include <picomesh/ycore/result.h>
#include <picomesh/yloop/yloop.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* Small per-coroutine stack: the request frames are tiny (~4 KB deep), and
 * at high fan-out (tens of thousands of coroutines) the stack reservation is
 * what dominates memory. */
#define CORO_STACK_HINT (48 * 1024)
#define RBUF_CAP (16 * 1024)

/* ---- config ---------------------------------------------------------- */

enum scenario_id {
    SCENARIO_RPC_COUNT = 0,
    SCENARIO_LOGIN,
    SCENARIO_REPO_CREATE,
    SCENARIO_REGISTER,
    SCENARIO_FULL,
    SCENARIO_MIXED,
};

enum mixed_op {
    OP_READ_COUNT = 0,
    OP_READ_LIST,
    OP_PUT_FILE,
    OP_OPEN_ISSUE,
    OP_ENQUEUE_RUN,
    OP_MAKE_REPO,
    OP_LOGIN,
    OP_NS_SUBTREE,      /* read  — accounts.ns_subtree(<own ns>)            */
    OP_NS_MEMBERS,      /* read  — accounts.ns_members(<own ns>)            */
    OP_NS_CREATE,       /* write — accounts.ns_create(group under own ns)   */
    OP_NS_ADD_MEMBER,   /* write — accounts.ns_add_member(<own ns>, uid)    */
    OP__COUNT,
};

struct perf_config {
    const char *host;
    int port;
    int threads;            /* OS threads, each a loop + coro scheduler */
    int connections;        /* total virtual connections (coroutines) */
    int total;              /* resolved total = threads × coros_per_thread */
    double duration_secs;
    long requests_per_conn; /* >0 overrides duration */
    enum scenario_id scenario;
    const char *scenario_name;
    long run_nonce;
    long seed_users;
    int repos_per_conn;
    int emulate;            /* --emulate: run the client machinery, skip the
                             * network + server (synthetic success responses).
                             * Measures the load generator's own ceiling. */
};

/* ---- monotonic clock ------------------------------------------------- */

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ---- FNV-1a (MUST match the gateway's hash_username/hash_repo) ------- */
static uint32_t hash_username(const char *s)
{
    uint32_t h = 2166136261u;
    if (s) for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        h ^= *p; h *= 16777619u;
    }
    return h ? h : 1u;
}

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
    return h ? h : 1u;
}

static uint32_t rng_next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    *state = x;
    return (uint32_t)(x >> 32);
}

static const char *const OP_NAMES[OP__COUNT] = {
    "read_count", "read_list", "put_file",
    "open_issue", "enqueue_run", "make_repo", "login",
    "ns_subtree", "ns_members", "ns_create", "ns_add_member",
};

/* No raw-storage op: clients never touch a storage node directly — every
 * write goes through a business service (e.g. put_file = a real libgit2
 * commit into the bare repo on disk; make_repo/open_issue likewise go via
 * git_repo/issues, not the KV store). The ns_* ops exercise the GitLab-style
 * group/namespace authority (accounts service, issue #30): each connection
 * acts on its OWN personal namespace, which it owns, so the reads (subtree,
 * members) and writes (subgroup create, add member) all pass the namespace
 * RBAC gates. Weights sum to 100. */
static const int OP_WEIGHTS[OP__COUNT] = {
    /*read_count*/ 22, /*read_list*/ 15, /*put_file*/ 20,
    /*open_issue*/ 8, /*enqueue_run*/ 5, /*make_repo*/ 5, /*login*/ 2,
    /*ns_subtree*/ 9, /*ns_members*/ 6, /*ns_create*/ 4, /*ns_add_member*/ 4,
};

static enum mixed_op pick_op(uint64_t *rng)
{
    int r = (int)(rng_next(rng) % 100);
    int acc = 0;
    for (int i = 0; i < OP__COUNT; ++i) {
        acc += OP_WEIGHTS[i];
        if (r < acc) return (enum mixed_op)i;
    }
    return OP_READ_COUNT;
}

/* ---- per-connection latency capture ---------------------------------- */

struct latencies {
    uint64_t *v;
    size_t len;
    size_t cap;
};

static void lat_push(struct latencies *l, uint64_t ns)
{
    if (l->len == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 1024;
        uint64_t *nv = realloc(l->v, nc * sizeof(*nv));
        if (!nv) return;
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->len++] = ns;
}

/* ---- a virtual connection = one coroutine driving one async socket --- */

struct vconn {
    struct yloop *loop;          /* this coro's thread loop */
    struct yloop_stream *stream; /* NULL until connected / after a drop */
    const struct perf_config *cfg;
    volatile int *stop;
    int id;                      /* global connection index */
    char *rbuf;                  /* response scratch (heap) */

    char sid[64];
    char uname[40];
    int session_ok;

    uint64_t rng;
    uint32_t uid;
    int n_repos;
    int n_groups;   /* subgroups this connection has created under its own ns */

    /* results */
    struct latencies lat;
    uint64_t ok, errors;
    uint64_t seed_ok, seed_err;
    uint64_t op_ok[OP__COUNT];
    uint64_t op_err[OP__COUNT];
};

static int vconn_connect(struct vconn *vc)
{
    if (vc->stream) return 0;
    if (vc->cfg->emulate) return 0; /* no server in emulate mode: stream stays NULL */
    struct yloop_stream_ptr_result r = yloop_connect_tcp(vc->loop, vc->cfg->host, vc->cfg->port);
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); vc->stream = NULL; return -1; }
    vc->stream = r.value;
    return 0;
}

static void vconn_close(struct vconn *vc)
{
    if (vc->stream) { yloop_close(vc->stream); vc->stream = NULL; }
}

/* Issue one HTTP request on the keep-alive coroutine connection and consume
 * the full response (headers + Content-Length body). Returns the HTTP status
 * code, or -1 on transport failure. */
static int http_request(struct vconn *vc, const char *method, const char *path,
                        const char *content_type, const char *cookie_sid,
                        const char *body, size_t body_len,
                        char *out_sid, size_t out_sid_cap)
{
    char hdr[1024];
    int n = snprintf(hdr, sizeof(hdr),
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: keep-alive\r\n",
        method, path, vc->cfg->host, vc->cfg->port);
    if (n <= 0 || (size_t)n >= sizeof(hdr)) return -1;
    if (content_type && body_len) {
        n += snprintf(hdr + n, sizeof(hdr) - n,
                      "Content-Type: %s\r\nContent-Length: %zu\r\n",
                      content_type, body_len);
    } else {
        n += snprintf(hdr + n, sizeof(hdr) - n, "Content-Length: 0\r\n");
    }
    if (cookie_sid && *cookie_sid)
        n += snprintf(hdr + n, sizeof(hdr) - n, "Cookie: picomesh-sid=%s\r\n", cookie_sid);
    n += snprintf(hdr + n, sizeof(hdr) - n, "\r\n");
    if (n <= 0 || (size_t)n >= sizeof(hdr)) return -1;

    if (vc->cfg->emulate) {
        /* --emulate: everything the client does per request EXCEPT the network
         * and a real server. The request line/headers were just built and the
         * caller built the body, so all client-side CPU (op selection, string
         * formatting, the sid cookie threading, latency capture, stats) is
         * still measured. We yield once — the await a real request would block
         * on — which keeps the cooperative scheduler fair and lets the deadline
         * timer fire; then hand back a synthetic success. This benchmarks the
         * load generator's OWN ceiling: how many ops/s its threads+coroutines
         * can push on this host with a zero-cost peer, so a real run's numbers
         * can be read against the harness's own limit. */
        yloop_sleep_ms(vc->loop, 0);
        if (out_sid && out_sid_cap)
            snprintf(out_sid, out_sid_cap, "emu%08xsid", (unsigned)vc->id);
        /* Match the status the real route returns on success so ok/err
         * accounting and the n_repos bookkeeping behave identically: the
         * form-POST routes redirect (303), everything else is 200. */
        int redirect = (strcmp(path, "/login") == 0 ||
                        strcmp(path, "/register") == 0 ||
                        strcmp(path, "/repos/new") == 0);
        return redirect ? 303 : 200;
    }

    if (yloop_write(vc->stream, hdr, (size_t)n) != (size_t)n) return -1;
    if (body_len && yloop_write(vc->stream, body, body_len) != body_len) return -1;

    /* Read until the header terminator is in. */
    size_t total = 0;
    char *hdr_end = NULL;
    while (total < RBUF_CAP - 1) {
        size_t r = yloop_read_some(vc->stream, vc->rbuf + total, RBUF_CAP - 1 - total);
        if (r == 0) return -1; /* EOF / error */
        total += r;
        vc->rbuf[total] = 0;
        hdr_end = strstr(vc->rbuf, "\r\n\r\n");
        if (hdr_end) break;
    }
    if (!hdr_end) return -1;

    int status = 0;
    if (sscanf(vc->rbuf, "HTTP/1.%*d %d", &status) != 1) return -1;

    if (out_sid && out_sid_cap) {
        out_sid[0] = 0;
        for (char *p = vc->rbuf; p < hdr_end; ++p) {
            if ((p == vc->rbuf || p[-1] == '\n') &&
                strncasecmp(p, "set-cookie:", 11) == 0) {
                char *v = strstr(p, "picomesh-sid=");
                if (v && v < hdr_end) {
                    v = strchr(v, '=') + 1;
                    size_t i = 0;
                    while (v[i] && v[i] != ';' && v[i] != '\r' && v[i] != '\n'
                           && i < out_sid_cap - 1) { out_sid[i] = v[i]; i++; }
                    out_sid[i] = 0;
                }
                break;
            }
        }
    }

    long content_length = 0;
    for (char *p = vc->rbuf; p < hdr_end; ++p) {
        if ((p == vc->rbuf || p[-1] == '\n') &&
            strncasecmp(p, "content-length:", 15) == 0) {
            content_length = strtol(p + 15, NULL, 10);
            break;
        }
    }
    size_t header_len = (size_t)(hdr_end - vc->rbuf) + 4;
    size_t body_have = total - header_len;
    long remaining = content_length - (long)body_have;
    while (remaining > 0) {
        size_t want = (size_t)remaining < RBUF_CAP ? (size_t)remaining : RBUF_CAP;
        size_t r = yloop_read_some(vc->stream, vc->rbuf, want);
        if (r == 0) return -1;
        remaining -= (long)r;
    }
    return status;
}

/* A request with one transparent reconnect on transport failure. */
static int http_try(struct vconn *vc, const char *method, const char *path,
                    const char *ctype, const char *sid,
                    const char *body, size_t body_len,
                    char *out_sid, size_t out_sid_cap)
{
    int st = http_request(vc, method, path, ctype, sid, body, body_len,
                          out_sid, out_sid_cap);
    if (st >= 0) return st;
    vconn_close(vc);
    if (vconn_connect(vc) != 0) return -1;
    return http_request(vc, method, path, ctype, sid, body, body_len,
                        out_sid, out_sid_cap);
}

/* ---- workload (identical to the threaded version, vc transport) ------ */

static void make_user(char *out, size_t cap, long nonce, int conn, long counter)
{
    if (counter < 0)
        snprintf(out, cap, "p%ldw%d", nonce % 100000, conn);
    else
        snprintf(out, cap, "p%ldw%dn%ld", nonce % 100000, conn, counter);
}

static int establish_session(struct vconn *vc)
{
    char user[40];
    make_user(user, sizeof(user), vc->cfg->run_nonce, vc->id, -1);
    snprintf(vc->uname, sizeof(vc->uname), "%s", user);
    char body[128];
    int bn = snprintf(body, sizeof(body), "username=%s&password=x", user);

    http_try(vc, "POST", "/register",
             "application/x-www-form-urlencoded", NULL, body, (size_t)bn, NULL, 0);
    for (int attempt = 0; attempt < 3; ++attempt) {
        vc->sid[0] = 0;
        int st = http_try(vc, "POST", "/login",
                          "application/x-www-form-urlencoded", NULL,
                          body, (size_t)bn, vc->sid, sizeof(vc->sid));
        if ((st == 303 || st == 200) && vc->sid[0]) return 1;
    }
    return 0;
}

static int scenario_step(struct vconn *vc, long counter)
{
    switch (vc->cfg->scenario) {
    case SCENARIO_RPC_COUNT: {
        static const char body[] = "{\"path\":\"git_repo.git_repo.count_total\",\"args\":[]}";
        return http_try(vc, "POST", "/_rpc", "application/json", vc->sid,
                        body, sizeof(body) - 1, NULL, 0) == 200;
    }
    case SCENARIO_LOGIN: {
        char user[40], body[128];
        make_user(user, sizeof(user), vc->cfg->run_nonce, vc->id, -1);
        int bn = snprintf(body, sizeof(body), "username=%s&password=x", user);
        char sid[64];
        int st = http_try(vc, "POST", "/login",
                          "application/x-www-form-urlencoded", NULL,
                          body, (size_t)bn, sid, sizeof(sid));
        return st == 303 || st == 200;
    }
    case SCENARIO_REPO_CREATE: {
        if (!vc->sid[0] || !vc->uname[0]) return 0;
        char body[64];
        int bn = snprintf(body, sizeof(body), "name=r%ld", counter);
        char cookie[128];
        snprintf(cookie, sizeof(cookie), "%s; picomesh-uname=%s", vc->sid, vc->uname);
        return http_try(vc, "POST", "/repos/new",
                        "application/x-www-form-urlencoded", cookie,
                        body, (size_t)bn, NULL, 0) == 303;
    }
    case SCENARIO_REGISTER: {
        char user[40], body[128];
        make_user(user, sizeof(user), vc->cfg->run_nonce, vc->id, counter);
        int bn = snprintf(body, sizeof(body), "username=%s&password=x", user);
        int st = http_try(vc, "POST", "/register",
                          "application/x-www-form-urlencoded", NULL,
                          body, (size_t)bn, NULL, 0);
        return st == 303 || st == 200;
    }
    case SCENARIO_FULL: {
        char user[40], body[128], sid[64];
        make_user(user, sizeof(user), vc->cfg->run_nonce, vc->id, counter);
        int bn = snprintf(body, sizeof(body), "username=%s&password=x", user);
        if (http_try(vc, "POST", "/register", "application/x-www-form-urlencoded",
                     NULL, body, (size_t)bn, NULL, 0) < 0) return 0;
        sid[0] = 0;
        int st = http_try(vc, "POST", "/login", "application/x-www-form-urlencoded",
                          NULL, body, (size_t)bn, sid, sizeof(sid));
        if (!(st == 303 || st == 200) || !sid[0]) return 0;
        char rbody[64];
        int rn = snprintf(rbody, sizeof(rbody), "name=r%ld", counter);
        char cookie[128];
        snprintf(cookie, sizeof(cookie), "%s; picomesh-uname=%s", sid, user);
        if (http_try(vc, "POST", "/repos/new", "application/x-www-form-urlencoded",
                     cookie, rbody, (size_t)rn, NULL, 0) < 0) return 0;
        st = http_try(vc, "GET", "/repos", NULL, cookie, "", 0, NULL, 0);
        return st == 200;
    }
    case SCENARIO_MIXED: {
        enum mixed_op op = pick_op(&vc->rng);
        enum mixed_op did = op;
        char body[512];
        int st = -1;
        if (op == OP_PUT_FILE || op == OP_OPEN_ISSUE || op == OP_ENQUEUE_RUN) {
            if (vc->n_repos <= 0) { op = OP_READ_COUNT; did = OP_READ_COUNT; }
        }
        if (op == OP_MAKE_REPO && vc->n_repos >= vc->cfg->repos_per_conn) {
            op = OP_READ_LIST; did = OP_READ_LIST;
        }
        /* Cap subgroup creation the same way repos are capped; once full,
         * read the subtree instead so the op still exercises the namespace. */
        if (op == OP_NS_CREATE && vc->n_groups >= vc->cfg->repos_per_conn) {
            op = OP_NS_SUBTREE; did = OP_NS_SUBTREE;
        }
        switch (op) {
        case OP_READ_COUNT: {
            static const char b[] = "{\"path\":\"git_repo.git_repo.count_total\",\"args\":[]}";
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, b, sizeof(b) - 1, NULL, 0);
            break;
        }
        case OP_READ_LIST: {
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"git_repo.git_repo.list_for_owner\",\"args\":[%u]}", vc->uid);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            break;
        }
        case OP_MAKE_REPO: {
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"git_repo.git_repo.make\",\"args\":[%u,\"%s\",\"r%d\"]}",
                vc->uid, vc->uname, vc->n_repos);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            if (st == 200) vc->n_repos++;
            break;
        }
        case OP_PUT_FILE: {
            uint32_t idx = rng_next(&vc->rng) % (uint32_t)vc->n_repos;
            char repo[16]; snprintf(repo, sizeof(repo), "r%u", idx);
            uint32_t rid = hash_repo(vc->uname, repo);
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"git_repo.git_repo.put_file\",\"args\":"
                "[%u,\"f%ld.txt\",\"hello %ld\",\"commit %ld\",\"\",\"\"]}",
                rid, counter, counter, counter);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            break;
        }
        case OP_OPEN_ISSUE: {
            uint32_t idx = rng_next(&vc->rng) % (uint32_t)vc->n_repos;
            char repo[16]; snprintf(repo, sizeof(repo), "r%u", idx);
            uint32_t rid = hash_repo(vc->uname, repo);
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"issues.issues.open\",\"args\":[%u,%u]}", rid, vc->uid);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            break;
        }
        case OP_ENQUEUE_RUN: {
            uint32_t idx = rng_next(&vc->rng) % (uint32_t)vc->n_repos;
            char repo[16]; snprintf(repo, sizeof(repo), "r%u", idx);
            uint32_t rid = hash_repo(vc->uname, repo);
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"git_pipeline.git_pipeline.enqueue\",\"args\":[%u]}", rid);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            break;
        }
        case OP_LOGIN: {
            char lbody[128];
            int bn = snprintf(lbody, sizeof(lbody), "username=%s&password=x", vc->uname);
            char new_sid[64];
            st = http_try(vc, "POST", "/login", "application/x-www-form-urlencoded", NULL,
                          lbody, (size_t)bn, new_sid, sizeof(new_sid));
            /* Adopt the fresh session. Otherwise the connection keeps using its
             * stale sid and the just-minted session is orphaned in the store —
             * over a long run that bloats the session lookup and drags throughput. */
            if ((st == 303 || st == 200) && new_sid[0])
                memcpy(vc->sid, new_sid, sizeof(vc->sid));
            break;
        }
        case OP_NS_SUBTREE: {
            /* The connection owns its personal namespace (path == its username),
             * so it has reporter+ on the whole subtree — the gate ns_subtree
             * requires. This is the call the webapp Projects page makes. */
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"accounts.accounts.ns_subtree\",\"args\":[\"%s\"]}", vc->uname);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            break;
        }
        case OP_NS_MEMBERS: {
            /* Owner of the namespace ⇒ maintainer, which ns_members requires. */
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"accounts.accounts.ns_members\",\"args\":[\"%s\"]}", vc->uname);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            break;
        }
        case OP_NS_CREATE: {
            /* A subgroup under the connection's own namespace; it is maintainer
             * of the parent, which is what subgroup creation requires (creating
             * a ROOT group is site-admin only, so we never do that here). */
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"accounts.accounts.ns_create\",\"args\":[%u,\"group\",\"g%d\",\"%s\"]}",
                vc->uid, vc->n_groups, vc->uname);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            if (st == 200) vc->n_groups++;
            break;
        }
        case OP_NS_ADD_MEMBER: {
            /* Grant a role on the connection's own namespace. The target must be
             * a REAL account (accounts.ns_add_member rejects non-existent uids):
             * with a seed population, pick a seeded uid (u1..uN); otherwise pick a
             * sibling connection's uid, which it registered for itself. */
            uint32_t member;
            if (vc->cfg->seed_users > 0)
                member = 1u + (uint32_t)(rng_next(&vc->rng) % (uint32_t)vc->cfg->seed_users);
            else {
                char other[40];
                int oid = (int)(rng_next(&vc->rng) % (uint32_t)vc->cfg->total);
                make_user(other, sizeof(other), vc->cfg->run_nonce, oid, -1);
                member = hash_username(other);
            }
            int bn = snprintf(body, sizeof(body),
                "{\"path\":\"accounts.accounts.ns_add_member\",\"args\":[\"%s\",%u,\"developer\"]}",
                vc->uname, member);
            st = http_try(vc, "POST", "/_rpc", "application/json", vc->sid, body, (size_t)bn, NULL, 0);
            break;
        }
        case OP__COUNT: break;
        }
        int is_ok = (op == OP_LOGIN) ? (st == 303 || st == 200) : (st == 200);
        if (is_ok) vc->op_ok[did]++; else vc->op_err[did]++;
        return is_ok;
    }
    }
    return 0;
}

static int scenario_needs_session(enum scenario_id s)
{
    return s == SCENARIO_RPC_COUNT || s == SCENARIO_REPO_CREATE || s == SCENARIO_MIXED;
}

/* ---- coroutine entries ----------------------------------------------- */

static void load_coro(void *arg)
{
    struct vconn *vc = arg;
    if (vconn_connect(vc) != 0) return;

    vc->session_ok = 1;
    if (scenario_needs_session(vc->cfg->scenario)) {
        vc->session_ok = establish_session(vc);
        if (!vc->session_ok) { vconn_close(vc); return; }
    } else {
        establish_session(vc);
    }

    if (vc->cfg->scenario == SCENARIO_MIXED) {
        vc->uid = hash_username(vc->uname);
        vc->rng = (uint64_t)(vc->cfg->run_nonce) * 0x9e3779b97f4a7c15ull
                ^ ((uint64_t)(vc->id + 1) << 32) ^ now_ns();
        if (!vc->rng) vc->rng = 0x123456789abcdefull;
        char body[160];
        int bn = snprintf(body, sizeof(body),
            "{\"path\":\"git_repo.git_repo.make\",\"args\":[%u,\"%s\",\"r0\"]}",
            vc->uid, vc->uname);
        if (http_try(vc, "POST", "/_rpc", "application/json", vc->sid,
                     body, (size_t)bn, NULL, 0) == 200)
            vc->n_repos = 1;
    }

    long counter = 0;
    if (vc->cfg->requests_per_conn > 0) {
        for (long i = 0; i < vc->cfg->requests_per_conn && !*vc->stop; ++i) {
            uint64_t t0 = now_ns();
            int ok = scenario_step(vc, counter++);
            lat_push(&vc->lat, now_ns() - t0);
            if (ok) vc->ok++; else vc->errors++;
        }
    } else {
        while (!*vc->stop) {
            uint64_t t0 = now_ns();
            int ok = scenario_step(vc, counter++);
            lat_push(&vc->lat, now_ns() - t0);
            if (ok) vc->ok++; else vc->errors++;
        }
    }
    vconn_close(vc);
}

static void seed_coro(void *arg)
{
    struct vconn *vc = arg;
    if (vconn_connect(vc) != 0) return;

    long total_users = vc->cfg->seed_users;
    int n = vc->cfg->total;
    long per = (total_users + n - 1) / n;
    long start = (long)vc->id * per;
    long end = start + per;
    if (end > total_users) end = total_users;
    for (long i = start; i < end && !*vc->stop; ++i) {
        uint32_t uid = (uint32_t)(i + 1);
        char body[96];
        int bn = snprintf(body, sizeof(body),
            "{\"path\":\"accounts.accounts.register\",\"args\":[%u,\"u%u\"]}", uid, uid);
        int st = http_try(vc, "POST", "/_rpc", "application/json", NULL,
                          body, (size_t)bn, NULL, 0);
        if (st == 200) vc->seed_ok++; else vc->seed_err++;
    }
    vconn_close(vc);
}

struct deadline_arg {
    struct yloop *loop;
    volatile int *stop;
    unsigned int ms;
};

static void deadline_coro(void *arg)
{
    struct deadline_arg *d = arg;
    yloop_sleep_ms(d->loop, d->ms);
    *d->stop = 1;
    yloop_stop(d->loop);
}

/* ---- one OS thread: a loop + K coroutines ---------------------------- */

struct lt_thread {
    pthread_t thread;
    const struct perf_config *cfg;
    volatile int *stop;
    struct vconn *vconns;  /* slice into the shared vconn array */
    int lo, hi;            /* this thread owns vconns[lo..hi) */
    int seed_phase;        /* 1 = run seed_coro, 0 = load_coro */
};

static void *lt_thread_main(void *arg)
{
    struct lt_thread *t = arg;
    struct yloop_ptr_result lr = yloop_create();
    if (PICOMESH_IS_ERR(lr)) { picomesh_error_destroy(lr.error); return NULL; }
    struct yloop *loop = lr.value;

    int k = t->hi - t->lo;
    struct picomesh_coro **coros = calloc((size_t)k + 1, sizeof(*coros));
    if (!coros) { yloop_destroy(loop); return NULL; }

    for (int i = 0; i < k; ++i) {
        struct vconn *vc = &t->vconns[t->lo + i];
        vc->loop = loop;
        struct picomesh_coro_ptr_result cr = picomesh_coro_spawn(
            t->seed_phase ? seed_coro : load_coro, vc, CORO_STACK_HINT, "vc");
        if (PICOMESH_IS_ERR(cr)) { picomesh_error_destroy(cr.error); coros[i] = NULL; continue; }
        coros[i] = cr.value;
        picomesh_coro_resume(coros[i]); /* run to first park (connect) */
    }

    /* Load phase, duration mode: a timer coro that ends the run. */
    struct deadline_arg da = {.loop = loop, .stop = t->stop,
        .ms = (unsigned int)(t->cfg->duration_secs * 1000.0)};
    struct picomesh_coro *dcoro = NULL;
    if (!t->seed_phase && t->cfg->requests_per_conn <= 0) {
        struct picomesh_coro_ptr_result dr =
            picomesh_coro_spawn(deadline_coro, &da, CORO_STACK_HINT, "deadline");
        if (PICOMESH_IS_OK(dr)) { dcoro = dr.value; picomesh_coro_resume(dcoro); }
    }

    yloop_run(loop);

    /* Reap finished coros; parked ones (abandoned mid-request at the
     * deadline) are left for process exit — destroying a suspended coro
     * is unsafe (its resume callback would touch freed memory). */
    for (int i = 0; i < k; ++i)
        if (coros[i] && picomesh_coro_is_finished(coros[i]))
            picomesh_coro_destroy(coros[i]);
    if (dcoro && picomesh_coro_is_finished(dcoro)) picomesh_coro_destroy(dcoro);
    free(coros);
    yloop_destroy(loop);
    return NULL;
}

/* Run one phase (seed or load) across all threads, then join. */
static void run_phase(struct lt_thread *threads, int nthreads, struct vconn *vconns,
                      int total, const struct perf_config *cfg, volatile int *stop,
                      int seed_phase)
{
    int per = (total + nthreads - 1) / nthreads;
    for (int t = 0; t < nthreads; ++t) {
        threads[t].cfg = cfg;
        threads[t].stop = stop;
        threads[t].vconns = vconns;
        threads[t].lo = t * per;
        threads[t].hi = (t + 1) * per < total ? (t + 1) * per : total;
        threads[t].seed_phase = seed_phase;
        if (threads[t].lo >= threads[t].hi) { threads[t].hi = threads[t].lo; }
        pthread_create(&threads[t].thread, NULL, lt_thread_main, &threads[t]);
    }
    for (int t = 0; t < nthreads; ++t) pthread_join(threads[t].thread, NULL);
}

/* ---- stats ----------------------------------------------------------- */

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static double pctl_ms(const uint64_t *sorted, size_t n, double p)
{
    if (!n) return 0.0;
    size_t idx = (size_t)(p / 100.0 * (double)(n - 1) + 0.5);
    if (idx >= n) idx = n - 1;
    return (double)sorted[idx] / 1e6;
}

static enum scenario_id scenario_lookup(const char *name, const char **canonical)
{
    struct row { const char *name; enum scenario_id id; };
    static const struct row SCENARIOS[] = {
        {"rpc_count",   SCENARIO_RPC_COUNT},
        {"login",       SCENARIO_LOGIN},
        {"repo_create", SCENARIO_REPO_CREATE},
        {"register",    SCENARIO_REGISTER},
        {"full",        SCENARIO_FULL},
        {"mixed",       SCENARIO_MIXED},
    };
    for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(SCENARIOS[0]); ++i)
        if (strcmp(name, SCENARIOS[i].name) == 0) {
            *canonical = SCENARIOS[i].name;
            return SCENARIOS[i].id;
        }
    *canonical = NULL;
    return SCENARIO_RPC_COUNT;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [options]\n"
        "  --host H              gateway host (default 127.0.0.1)\n"
        "  --port P              gateway port (default 8090)\n"
        "  --threads M           OS threads, each a loop+coroutine scheduler (default: ncpu)\n"
        "  --connections N       total virtual connections, spread across --threads (default 64)\n"
        "  --coros-per-thread K  coroutines per thread (overrides --connections; total = M×K)\n"
        "  --duration SECS       run for this many seconds (default 10)\n"
        "  --requests R          fixed requests PER CONNECTION (overrides --duration)\n"
        "  --scenario NAME       rpc_count|login|repo_create|register|full|mixed\n"
        "  --seed-users N        mixed: pre-create N account records (stage 1)\n"
        "  --repos-per-worker K  mixed: cap repos a connection creates (default 8)\n"
        "  --emulate             run the client machinery but make NO real calls\n"
        "                        (synthetic success responses) — measures how far\n"
        "                        the harness itself scales, not the gateway\n"
        "\n"
        "  Concurrency is M threads × K coroutines — C connections cost M threads, not C.\n",
        prog);
}

int main(int argc, char **argv)
{
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 4;

    struct perf_config cfg = {
        .host = "127.0.0.1",
        .port = 8090,
        .threads = (int)(ncpu > 32 ? 32 : ncpu),
        .connections = 64,
        .duration_secs = 10.0,
        .requests_per_conn = 0,
        .scenario = SCENARIO_RPC_COUNT,
        .scenario_name = "rpc_count",
        .run_nonce = (long)time(NULL),
        .seed_users = 0,
        .repos_per_conn = 8,
    };
    int coros_per_thread = 0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--host") && next) { cfg.host = next; i++; }
        else if (!strcmp(a, "--port") && next) { cfg.port = atoi(next); i++; }
        else if (!strcmp(a, "--threads") && next) { cfg.threads = atoi(next); i++; }
        else if (!strcmp(a, "--connections") && next) { cfg.connections = atoi(next); i++; }
        else if (!strcmp(a, "--coros-per-thread") && next) { coros_per_thread = atoi(next); i++; }
        else if (!strcmp(a, "--duration") && next) { cfg.duration_secs = atof(next); i++; }
        else if (!strcmp(a, "--requests") && next) { cfg.requests_per_conn = atol(next); i++; }
        else if (!strcmp(a, "--seed-users") && next) { cfg.seed_users = atol(next); i++; }
        else if (!strcmp(a, "--repos-per-worker") && next) { cfg.repos_per_conn = atoi(next); i++; }
        else if (!strcmp(a, "--emulate")) { cfg.emulate = 1; }
        else if (!strcmp(a, "--scenario") && next) {
            const char *canon = NULL;
            cfg.scenario = scenario_lookup(next, &canon);
            if (!canon) { fprintf(stderr, "unknown scenario '%s'\n", next); usage(argv[0]); return 2; }
            cfg.scenario_name = canon;
            i++;
        }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "unknown arg '%s'\n", a); usage(argv[0]); return 2; }
    }

    if (cfg.threads < 1) cfg.threads = 1;
    if (coros_per_thread > 0) cfg.connections = cfg.threads * coros_per_thread;
    if (cfg.connections < 1) cfg.connections = 1;
    if (cfg.threads > cfg.connections) cfg.threads = cfg.connections;
    cfg.total = cfg.connections;
    if (cfg.repos_per_conn < 1) cfg.repos_per_conn = 1;
    if ((long)cfg.total * cfg.repos_per_conn > 4000) {
        cfg.repos_per_conn = 4000 / cfg.total;
        if (cfg.repos_per_conn < 1) cfg.repos_per_conn = 1;
    }

    struct vconn *vconns = calloc((size_t)cfg.total, sizeof(*vconns));
    struct lt_thread *threads = calloc((size_t)cfg.threads, sizeof(*threads));
    if (!vconns || !threads) { fprintf(stderr, "oom\n"); return 1; }
    volatile int stop = 0;
    for (int i = 0; i < cfg.total; ++i) {
        vconns[i].id = i;
        vconns[i].cfg = &cfg;
        vconns[i].stop = &stop;
        vconns[i].rbuf = malloc(RBUF_CAP);
        if (!vconns[i].rbuf) { fprintf(stderr, "oom (rbuf)\n"); return 1; }
    }

    fprintf(stderr, "picoforge-perf — scenario=%s host=%s:%d threads=%d coros/thread=%d connections=%d ",
            cfg.scenario_name, cfg.host, cfg.port, cfg.threads,
            (cfg.total + cfg.threads - 1) / cfg.threads, cfg.total);
    if (cfg.requests_per_conn > 0)
        fprintf(stderr, "requests=%ld/conn\n", cfg.requests_per_conn);
    else
        fprintf(stderr, "duration=%.1fs\n", cfg.duration_secs);
    if (cfg.emulate)
        fprintf(stderr, "*** EMULATE MODE: no network, synthetic responses — "
                        "this measures the HARNESS's own ceiling, NOT the gateway ***\n");

    /* ---- stage 1: population seed (mixed only) ----------------------- */
    uint64_t seed_ok_total = 0, seed_err_total = 0;
    double seed_wall = 0.0;
    if (cfg.scenario == SCENARIO_MIXED && cfg.seed_users > 0) {
        fprintf(stderr, "seeding %ld account records (stage 1)…\n", cfg.seed_users);
        uint64_t seed0 = now_ns();
        run_phase(threads, cfg.threads, vconns, cfg.total, &cfg, &stop, 1);
        seed_wall = (double)(now_ns() - seed0) / 1e9;
        for (int i = 0; i < cfg.total; ++i) {
            seed_ok_total += vconns[i].seed_ok;
            seed_err_total += vconns[i].seed_err;
        }
        fprintf(stderr, "seed done: %llu accounts in %.2fs\n",
                (unsigned long long)seed_ok_total, seed_wall);
    }

    /* ---- stage 2: the load ------------------------------------------- */
    uint64_t wall0 = now_ns();
    run_phase(threads, cfg.threads, vconns, cfg.total, &cfg, &stop, 0);
    double wall_s = (double)(now_ns() - wall0) / 1e9;

    uint64_t total_ok = 0, total_err = 0;
    size_t total_samples = 0;
    int sessions_ok = 0;
    for (int i = 0; i < cfg.total; ++i) {
        total_ok += vconns[i].ok;
        total_err += vconns[i].errors;
        total_samples += vconns[i].lat.len;
        sessions_ok += vconns[i].session_ok ? 1 : 0;
    }
    uint64_t *all = malloc(total_samples * sizeof(*all) + 1);
    size_t off = 0;
    if (all) {
        for (int i = 0; i < cfg.total; ++i) {
            memcpy(all + off, vconns[i].lat.v, vconns[i].lat.len * sizeof(*all));
            off += vconns[i].lat.len;
        }
        qsort(all, off, sizeof(*all), cmp_u64);
    }

    double mean_ms = 0.0;
    if (off) {
        uint64_t sum = 0;
        for (size_t i = 0; i < off; ++i) sum += all[i];
        mean_ms = (double)sum / (double)off / 1e6;
    }
    uint64_t total_req = total_ok + total_err;
    double thr = wall_s > 0 ? (double)total_req / wall_s : 0.0;

    printf("\n");
    printf("scenario       : %s%s\n", cfg.scenario_name,
           cfg.emulate ? "  [EMULATE — harness-only ceiling, no real calls]" : "");
    printf("concurrency    : %d threads × ~%d coroutines = %d connections\n",
           cfg.threads, (cfg.total + cfg.threads - 1) / cfg.threads, cfg.total);
    if (cfg.scenario == SCENARIO_MIXED && cfg.seed_users > 0) {
        printf("seed stage     : %llu accounts in %.3f s (%.0f reg/s, %llu errors)\n",
               (unsigned long long)seed_ok_total, seed_wall,
               seed_wall > 0 ? (double)seed_ok_total / seed_wall : 0.0,
               (unsigned long long)seed_err_total);
    }
    printf("sessions       : %d / %d established\n", sessions_ok, cfg.total);
    printf("wall time      : %.3f s\n", wall_s);
    printf("requests       : %llu ok, %llu errors\n",
           (unsigned long long)total_ok, (unsigned long long)total_err);
    printf("throughput     : %.1f req/s\n", thr);
    if (all && off) {
        printf("latency (ms)   : mean=%.3f min=%.3f p50=%.3f p90=%.3f "
               "p99=%.3f p99.9=%.3f max=%.3f\n",
               mean_ms, (double)all[0] / 1e6,
               pctl_ms(all, off, 50), pctl_ms(all, off, 90),
               pctl_ms(all, off, 99), pctl_ms(all, off, 99.9),
               (double)all[off - 1] / 1e6);
    }
    if (cfg.scenario == SCENARIO_MIXED) {
        uint64_t op_ok_tot[OP__COUNT] = {0}, op_err_tot[OP__COUNT] = {0};
        for (int i = 0; i < cfg.total; ++i)
            for (int o = 0; o < OP__COUNT; ++o) {
                op_ok_tot[o] += vconns[i].op_ok[o];
                op_err_tot[o] += vconns[i].op_err[o];
            }
        printf("op breakdown   : (op = ok / err, share of total)\n");
        for (int o = 0; o < OP__COUNT; ++o) {
            uint64_t n = op_ok_tot[o] + op_err_tot[o];
            double share = total_req ? 100.0 * (double)n / (double)total_req : 0.0;
            printf("    %-12s : %8llu ok / %llu err  (%.1f%%, %.0f/s)\n",
                   OP_NAMES[o], (unsigned long long)op_ok_tot[o],
                   (unsigned long long)op_err_tot[o], share,
                   wall_s > 0 ? (double)n / wall_s : 0.0);
        }
    }
    printf("\n");

    int exit_code;
    if (cfg.scenario == SCENARIO_MIXED)
        exit_code = (total_ok == 0) ? 1 : 0;
    else
        exit_code = (total_err > 0 || total_ok == 0) ? 1 : 0;
    free(all);
    for (int i = 0; i < cfg.total; ++i) { free(vconns[i].lat.v); free(vconns[i].rbuf); }
    free(vconns);
    free(threads);
    return exit_code;
}
