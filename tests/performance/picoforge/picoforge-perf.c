/* picoforge-perf — load generator + latency/throughput harness for the
 * picoforge gateway.
 *
 * Drives the GATEWAY only (the production hot path, per CLAUDE.md): it
 * never touches a backend port directly.
 *
 * Concurrency model — HYBRID threads + coroutines (the same shape the
 * server uses): M OS threads, each running its OWN libuv event loop and
 * libco coroutine scheduler, hosting K coroutines. Every coroutine drives
 * one async keep-alive connection (loop_connect_tcp + loop_read_some +
 * loop_write, all coroutine-yielding). So C concurrent connections cost
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

#include <picomesh/picoco/coro.h>
#include <picomesh/core/result.h>
#include <picomesh/loop/loop.h>

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

/* One pre-established user in the multiplexed population (issue #12). The
 * pool-setup phase registers + logs in M of these once; during the timed run
 * each connection picks a random pool entry per request and acts as that user,
 * so C connections drive load on behalf of M users with C ≪ M. Read-only after
 * setup (every connection reads it; nobody writes during the load phase). */
struct perf_user {
    uint32_t uid;
    char uname[40];
    char sid[64];
    int ok;             /* 1 once register+login succeeded for this entry */
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
    /* --users M: user POPULATION multiplexed across the --connections (issue
     * #12). 0 == off (legacy 1 connection = 1 user). When > 0 the load phase
     * runs a multiplexed read workload attributed across `users` distinct
     * accounts held in `user_pool`, decoupling population size from the
     * connection-concurrency knob. */
    long users;
    struct perf_user *user_pool; /* [users]; filled by the pool-setup phase */
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
    struct loop *loop;          /* this coro's thread loop */
    struct loop_stream *stream; /* NULL until connected / after a drop */
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
    /* diagnostic: HTTP status distribution (mixed). Buckets:
     * 0=transport(<0/0), 1=2xx, 2=303, 3=401, 4=403, 5=5xx, 6=other */
    uint64_t status_hist[7];
};

static int vconn_connect(struct vconn *conn)
{
    if (conn->stream) return 0;
    if (conn->cfg->emulate) return 0; /* no server in emulate mode: stream stays NULL */
    struct loop_stream_ptr_result connect_res =
        loop_connect_tcp(conn->loop, conn->cfg->host, conn->cfg->port);
    if (PICOMESH_IS_ERR(connect_res)) {
        picomesh_error_destroy(connect_res.error);
        conn->stream = NULL;
        return -1;
    }
    conn->stream = connect_res.value;
    return 0;
}

static void vconn_close(struct vconn *conn)
{
    if (conn->stream) { loop_close(conn->stream); conn->stream = NULL; }
}

/* Issue one HTTP request on the keep-alive coroutine connection and consume
 * the full response (headers + Content-Length body). Returns the HTTP status
 * code, or -1 on transport failure. */
static int http_request(struct vconn *conn, const char *method, const char *path,
                        const char *content_type, const char *cookie_sid,
                        const char *body, size_t body_len,
                        char *out_sid, size_t out_sid_cap)
{
    char hdr[1024];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: keep-alive\r\n",
        method, path, conn->cfg->host, conn->cfg->port);
    if (hdr_len <= 0 || (size_t)hdr_len >= sizeof(hdr)) return -1;
    if (content_type && body_len) {
        hdr_len += snprintf(hdr + hdr_len, sizeof(hdr) - hdr_len,
                      "Content-Type: %s\r\nContent-Length: %zu\r\n",
                      content_type, body_len);
    } else {
        hdr_len += snprintf(hdr + hdr_len, sizeof(hdr) - hdr_len, "Content-Length: 0\r\n");
    }
    if (cookie_sid && *cookie_sid)
        hdr_len += snprintf(hdr + hdr_len, sizeof(hdr) - hdr_len,
                            "Cookie: picomesh-sid=%s\r\n", cookie_sid);
    hdr_len += snprintf(hdr + hdr_len, sizeof(hdr) - hdr_len, "\r\n");
    if (hdr_len <= 0 || (size_t)hdr_len >= sizeof(hdr)) return -1;

    if (conn->cfg->emulate) {
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
        loop_sleep_ms(conn->loop, 0);
        if (out_sid && out_sid_cap)
            snprintf(out_sid, out_sid_cap, "emu%08xsid", (unsigned)conn->id);
        /* Match the status the real route returns on success so ok/err
         * accounting and the n_repos bookkeeping behave identically: the
         * form-POST routes redirect (303), everything else is 200. */
        int redirect = (strcmp(path, "/login") == 0 ||
                        strcmp(path, "/register") == 0 ||
                        strcmp(path, "/repos/new") == 0);
        return redirect ? 303 : 200;
    }

    struct picomesh_size_result hdr_write = loop_write(conn->stream, hdr, (size_t)hdr_len);
    if (PICOMESH_IS_ERR(hdr_write)) { picomesh_error_destroy(hdr_write.error); return -1; }
    if (body_len) {
        struct picomesh_size_result body_write = loop_write(conn->stream, body, body_len);
        if (PICOMESH_IS_ERR(body_write)) { picomesh_error_destroy(body_write.error); return -1; }
    }

    /* Read until the header terminator is in. */
    size_t total = 0;
    char *hdr_end = NULL;
    while (total < RBUF_CAP - 1) {
        struct picomesh_size_result read_res = loop_read_some(conn->stream, conn->rbuf + total, RBUF_CAP - 1 - total);
        if (PICOMESH_IS_ERR(read_res)) { picomesh_error_destroy(read_res.error); return -1; }
        if (read_res.value == 0) return -1; /* EOF */
        total += read_res.value;
        conn->rbuf[total] = 0;
        hdr_end = strstr(conn->rbuf, "\r\n\r\n");
        if (hdr_end) break;
    }
    if (!hdr_end) return -1;

    int status = 0;
    if (sscanf(conn->rbuf, "HTTP/1.%*d %d", &status) != 1) return -1;

    if (out_sid && out_sid_cap) {
        out_sid[0] = 0;
        for (char *p = conn->rbuf; p < hdr_end; ++p) {
            if ((p == conn->rbuf || p[-1] == '\n') &&
                strncasecmp(p, "set-cookie:", 11) == 0) {
                char *cookie_val = strstr(p, "picomesh-sid=");
                if (cookie_val && cookie_val < hdr_end) {
                    cookie_val = strchr(cookie_val, '=') + 1;
                    size_t i = 0;
                    while (cookie_val[i] && cookie_val[i] != ';' && cookie_val[i] != '\r'
                           && cookie_val[i] != '\n' && i < out_sid_cap - 1) {
                        out_sid[i] = cookie_val[i];
                        i++;
                    }
                    out_sid[i] = 0;
                }
                break;
            }
        }
    }

    long content_length = 0;
    for (char *p = conn->rbuf; p < hdr_end; ++p) {
        if ((p == conn->rbuf || p[-1] == '\n') &&
            strncasecmp(p, "content-length:", 15) == 0) {
            content_length = strtol(p + 15, NULL, 10);
            break;
        }
    }
    size_t header_len = (size_t)(hdr_end - conn->rbuf) + 4;
    size_t body_have = total - header_len;
    long remaining = content_length - (long)body_have;
    while (remaining > 0) {
        size_t want = (size_t)remaining < RBUF_CAP ? (size_t)remaining : RBUF_CAP;
        struct picomesh_size_result read_res = loop_read_some(conn->stream, conn->rbuf, want);
        if (PICOMESH_IS_ERR(read_res)) { picomesh_error_destroy(read_res.error); return -1; }
        if (read_res.value == 0) return -1; /* EOF */
        remaining -= (long)read_res.value;
    }
    return status;
}

/* A request with one transparent reconnect on transport failure. */
static int http_try(struct vconn *conn, const char *method, const char *path,
                    const char *ctype, const char *sid,
                    const char *body, size_t body_len,
                    char *out_sid, size_t out_sid_cap)
{
    int status = http_request(conn, method, path, ctype, sid, body, body_len,
                              out_sid, out_sid_cap);
    if (status >= 0) return status;
    vconn_close(conn);
    if (vconn_connect(conn) != 0) return -1;
    return http_request(conn, method, path, ctype, sid, body, body_len,
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

static int establish_session(struct vconn *conn)
{
    char user[40];
    make_user(user, sizeof(user), conn->cfg->run_nonce, conn->id, -1);
    snprintf(conn->uname, sizeof(conn->uname), "%s", user);
    char body[128];
    int body_len = snprintf(body, sizeof(body), "username=%s&password=x", user);

    http_try(conn, "POST", "/register",
             "application/x-www-form-urlencoded", NULL, body, (size_t)body_len, NULL, 0);
    for (int attempt = 0; attempt < 3; ++attempt) {
        conn->sid[0] = 0;
        int status = http_try(conn, "POST", "/login",
                          "application/x-www-form-urlencoded", NULL,
                          body, (size_t)body_len, conn->sid, sizeof(conn->sid));
        if ((status == 303 || status == 200) && conn->sid[0]) return 1;
    }
    return 0;
}

static int scenario_step(struct vconn *conn, long counter)
{
    switch (conn->cfg->scenario) {
    case SCENARIO_RPC_COUNT: {
        static const char body[] = "{\"path\":\"git_repo.git_repo.count_total\",\"args\":[]}";
        return http_try(conn, "POST", "/_rpc", "application/json", conn->sid,
                        body, sizeof(body) - 1, NULL, 0) == 200;
    }
    case SCENARIO_LOGIN: {
        char user[40], body[128];
        make_user(user, sizeof(user), conn->cfg->run_nonce, conn->id, -1);
        int body_len = snprintf(body, sizeof(body), "username=%s&password=x", user);
        char sid[64];
        int status = http_try(conn, "POST", "/login",
                          "application/x-www-form-urlencoded", NULL,
                          body, (size_t)body_len, sid, sizeof(sid));
        return status == 303 || status == 200;
    }
    case SCENARIO_REPO_CREATE: {
        if (!conn->sid[0] || !conn->uname[0]) return 0;
        char body[64];
        int body_len = snprintf(body, sizeof(body), "name=r%ld", counter);
        char cookie[128];
        snprintf(cookie, sizeof(cookie), "%s; picomesh-uname=%s", conn->sid, conn->uname);
        return http_try(conn, "POST", "/repos/new",
                        "application/x-www-form-urlencoded", cookie,
                        body, (size_t)body_len, NULL, 0) == 303;
    }
    case SCENARIO_REGISTER: {
        char user[40], body[128];
        make_user(user, sizeof(user), conn->cfg->run_nonce, conn->id, counter);
        int body_len = snprintf(body, sizeof(body), "username=%s&password=x", user);
        int status = http_try(conn, "POST", "/register",
                          "application/x-www-form-urlencoded", NULL,
                          body, (size_t)body_len, NULL, 0);
        return status == 303 || status == 200;
    }
    case SCENARIO_FULL: {
        char user[40], body[128], sid[64];
        make_user(user, sizeof(user), conn->cfg->run_nonce, conn->id, counter);
        int body_len = snprintf(body, sizeof(body), "username=%s&password=x", user);
        if (http_try(conn, "POST", "/register", "application/x-www-form-urlencoded",
                     NULL, body, (size_t)body_len, NULL, 0) < 0) return 0;
        sid[0] = 0;
        int status = http_try(conn, "POST", "/login", "application/x-www-form-urlencoded",
                          NULL, body, (size_t)body_len, sid, sizeof(sid));
        if (!(status == 303 || status == 200) || !sid[0]) return 0;
        char rbody[64];
        int rbody_len = snprintf(rbody, sizeof(rbody), "name=r%ld", counter);
        char cookie[128];
        snprintf(cookie, sizeof(cookie), "%s; picomesh-uname=%s", sid, user);
        if (http_try(conn, "POST", "/repos/new", "application/x-www-form-urlencoded",
                     cookie, rbody, (size_t)rbody_len, NULL, 0) < 0) return 0;
        status = http_try(conn, "GET", "/repos", NULL, cookie, "", 0, NULL, 0);
        return status == 200;
    }
    case SCENARIO_MIXED: {
        enum mixed_op op = pick_op(&conn->rng);
        enum mixed_op recorded_op = op;
        char body[512];
        int status = -1;
        if (op == OP_PUT_FILE || op == OP_OPEN_ISSUE || op == OP_ENQUEUE_RUN) {
            if (conn->n_repos <= 0) { op = OP_READ_COUNT; recorded_op = OP_READ_COUNT; }
        }
        if (op == OP_MAKE_REPO && conn->n_repos >= conn->cfg->repos_per_conn) {
            op = OP_READ_LIST; recorded_op = OP_READ_LIST;
        }
        /* Cap subgroup creation the same way repos are capped; once full,
         * read the subtree instead so the op still exercises the namespace. */
        if (op == OP_NS_CREATE && conn->n_groups >= conn->cfg->repos_per_conn) {
            op = OP_NS_SUBTREE; recorded_op = OP_NS_SUBTREE;
        }
        switch (op) {
        case OP_READ_COUNT: {
            static const char b[] = "{\"path\":\"git_repo.git_repo.count_total\",\"args\":[]}";
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, b, sizeof(b) - 1, NULL, 0);
            break;
        }
        case OP_READ_LIST: {
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"git_repo.git_repo.list_for_owner\",\"args\":[%u]}", conn->uid);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            break;
        }
        case OP_MAKE_REPO: {
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"git_repo.git_repo.make\",\"args\":[%u,\"%s\",\"r%d\"]}",
                conn->uid, conn->uname, conn->n_repos);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            if (status == 200) conn->n_repos++;
            break;
        }
        case OP_PUT_FILE: {
            uint32_t idx = rng_next(&conn->rng) % (uint32_t)conn->n_repos;
            char repo[16]; snprintf(repo, sizeof(repo), "r%u", idx);
            uint32_t repo_hash = hash_repo(conn->uname, repo);
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"git_repo.git_repo.put_file\",\"args\":"
                "[%u,\"f%ld.txt\",\"hello %ld\",\"commit %ld\",\"\",\"\"]}",
                repo_hash, counter, counter, counter);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            break;
        }
        case OP_OPEN_ISSUE: {
            uint32_t idx = rng_next(&conn->rng) % (uint32_t)conn->n_repos;
            char repo[16]; snprintf(repo, sizeof(repo), "r%u", idx);
            uint32_t repo_hash = hash_repo(conn->uname, repo);
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"issues.issues.open\",\"args\":[%u,%u]}", repo_hash, conn->uid);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            break;
        }
        case OP_ENQUEUE_RUN: {
            uint32_t idx = rng_next(&conn->rng) % (uint32_t)conn->n_repos;
            char repo[16]; snprintf(repo, sizeof(repo), "r%u", idx);
            uint32_t repo_hash = hash_repo(conn->uname, repo);
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"git_pipeline.git_pipeline.enqueue\",\"args\":[%u]}", repo_hash);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            break;
        }
        case OP_LOGIN: {
            char lbody[128];
            int body_len = snprintf(lbody, sizeof(lbody), "username=%s&password=x", conn->uname);
            char new_sid[64];
            status = http_try(conn, "POST", "/login", "application/x-www-form-urlencoded", NULL,
                          lbody, (size_t)body_len, new_sid, sizeof(new_sid));
            /* Adopt the fresh session. Otherwise the connection keeps using its
             * stale sid and the just-minted session is orphaned in the store —
             * over a long run that bloats the session lookup and drags throughput. */
            if ((status == 303 || status == 200) && new_sid[0])
                memcpy(conn->sid, new_sid, sizeof(conn->sid));
            break;
        }
        case OP_NS_SUBTREE: {
            /* The connection owns its personal namespace (path == its username),
             * so it has reporter+ on the whole subtree — the gate ns_subtree
             * requires. This is the call the webapp Projects page makes. */
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"accounts.accounts.ns_subtree\",\"args\":[\"%s\"]}", conn->uname);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            break;
        }
        case OP_NS_MEMBERS: {
            /* Owner of the namespace ⇒ maintainer, which ns_members requires. */
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"accounts.accounts.ns_members\",\"args\":[\"%s\"]}", conn->uname);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            break;
        }
        case OP_NS_CREATE: {
            /* A subgroup under the connection's own namespace; it is maintainer
             * of the parent, which is what subgroup creation requires (creating
             * a ROOT group is site-admin only, so we never do that here). */
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"accounts.accounts.ns_create\",\"args\":[%u,\"group\",\"g%d\",\"%s\"]}",
                conn->uid, conn->n_groups, conn->uname);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            if (status == 200) conn->n_groups++;
            break;
        }
        case OP_NS_ADD_MEMBER: {
            /* Grant a role on the connection's OWN namespace to ANOTHER
             * connection's user. The target must be a real registered account
             * (accounts.ns_add_member rejects unknown uids), and every
             * connection self-registers, so a sibling reliably exists — no
             * dependence on the (optional) seed population. Never target self:
             * INSERT OR REPLACE would overwrite our own owner row and downgrade
             * us out of the maintainer role the other ns ops need. */
            if (conn->cfg->total <= 1) {
                /* Degenerate single-connection run: no sibling to grant to —
                 * read members instead so the op still touches the namespace. */
                recorded_op = OP_NS_MEMBERS;
                int body_len = snprintf(body, sizeof(body),
                    "{\"path\":\"accounts.accounts.ns_members\",\"args\":[\"%s\"]}", conn->uname);
                status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
                break;
            }
            int other_id = (conn->id + 1 + (int)(rng_next(&conn->rng) % (uint32_t)(conn->cfg->total - 1)))
                      % conn->cfg->total;
            char other[40];
            make_user(other, sizeof(other), conn->cfg->run_nonce, other_id, -1);
            uint32_t member = hash_username(other);
            int body_len = snprintf(body, sizeof(body),
                "{\"path\":\"accounts.accounts.ns_add_member\",\"args\":[\"%s\",%u,\"developer\"]}",
                conn->uname, member);
            status = http_try(conn, "POST", "/_rpc", "application/json", conn->sid, body, (size_t)body_len, NULL, 0);
            break;
        }
        case OP__COUNT: break;
        }
        int bucket = status < 0 ? 0 : (status >= 200 && status < 300) ? 1 : status == 303 ? 2
                   : status == 401 ? 3 : status == 403 ? 4 : (status >= 500 && status < 600) ? 5 : 6;
        conn->status_hist[bucket]++;
        int is_ok = (op == OP_LOGIN) ? (status == 303 || status == 200) : (status == 200);
        if (is_ok) conn->op_ok[recorded_op]++; else conn->op_err[recorded_op]++;
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

/* Pool-setup phase (issue #12): register + log in this connection's slice of
 * the M-user population and pre-create one repo per user (so read ops have
 * data). Runs ONCE before the timed load; fills disjoint slices of the shared
 * user_pool, so no locking is needed. */
static void pool_setup_coro(void *arg)
{
    struct vconn *conn = arg;
    if (vconn_connect(conn) != 0) return;

    long total_users = conn->cfg->users;
    int conn_count = conn->cfg->total;
    long per = (total_users + conn_count - 1) / conn_count;
    long start = (long)conn->id * per;
    long end = start + per;
    if (end > total_users) end = total_users;

    for (long idx = start; idx < end && !*conn->stop; ++idx) {
        struct perf_user *user = &conn->cfg->user_pool[idx];
        snprintf(user->uname, sizeof(user->uname), "pu%ldn%ld", conn->cfg->run_nonce % 100000, idx);
        char body[128];
        int body_len = snprintf(body, sizeof(body), "username=%s&password=x", user->uname);
        http_try(conn, "POST", "/register", "application/x-www-form-urlencoded", NULL, body,
                 (size_t)body_len, NULL, 0);
        user->sid[0] = 0;
        int status = http_try(conn, "POST", "/login", "application/x-www-form-urlencoded", NULL,
                              body, (size_t)body_len, user->sid, sizeof(user->sid));
        if ((status == 303 || status == 200) && user->sid[0]) {
            user->uid = hash_username(user->uname);
            char repo_body[160];
            int repo_len = snprintf(repo_body, sizeof(repo_body),
                "{\"path\":\"git_repo.git_repo.make\",\"args\":[%u,\"%s\",\"r0\"]}",
                user->uid, user->uname);
            char cookie[128];
            snprintf(cookie, sizeof(cookie), "%s; picomesh-uname=%s", user->sid, user->uname);
            http_try(conn, "POST", "/_rpc", "application/json", cookie, repo_body, (size_t)repo_len,
                     NULL, 0);
            user->ok = 1;
            conn->seed_ok++;
        } else {
            conn->seed_err++;
        }
    }
    vconn_close(conn);
}

/* The timed multiplexed-read workload (issue #12): each iteration picks a
 * RANDOM user from the pre-established pool and issues a read on that user's
 * behalf, so the load is attributed across the whole population while the
 * connection count stays the concurrency knob. */
static void pool_load_coro(struct vconn *conn)
{
    conn->rng = (uint64_t)(conn->cfg->run_nonce) * 0x9e3779b97f4a7c15ull
              ^ ((uint64_t)(conn->id + 1) << 32) ^ now_ns();
    if (!conn->rng) conn->rng = 0x123456789abcdefull;
    long population = conn->cfg->users;

    long counter = 0;
    while (!*conn->stop &&
           (conn->cfg->requests_per_conn <= 0 || counter < conn->cfg->requests_per_conn)) {
        struct perf_user *user = &conn->cfg->user_pool[rng_next(&conn->rng) % (uint32_t)population];
        counter++;
        if (!user->ok) { conn->errors++; continue; }
        char cookie[128];
        snprintf(cookie, sizeof(cookie), "%s; picomesh-uname=%s", user->sid, user->uname);
        uint64_t started_ns = now_ns();
        int status;
        if (rng_next(&conn->rng) & 1u) {
            static const char count_body[] =
                "{\"path\":\"git_repo.git_repo.count_total\",\"args\":[]}";
            status = http_try(conn, "POST", "/_rpc", "application/json", cookie, count_body,
                              sizeof(count_body) - 1, NULL, 0);
        } else {
            char list_body[160];
            int list_len = snprintf(list_body, sizeof(list_body),
                "{\"path\":\"git_repo.git_repo.list_for_owner\",\"args\":[%u]}", user->uid);
            status = http_try(conn, "POST", "/_rpc", "application/json", cookie, list_body,
                              (size_t)list_len, NULL, 0);
        }
        lat_push(&conn->lat, now_ns() - started_ns);
        if (status == 200) conn->ok++; else conn->errors++;
    }
    vconn_close(conn);
}

static void load_coro(void *arg)
{
    struct vconn *conn = arg;
    if (vconn_connect(conn) != 0) return;

    /* Multiplexed population mode (issue #12): no per-connection login — drive
     * reads across the pre-established user pool instead. */
    if (conn->cfg->users > 0 && conn->cfg->user_pool) {
        pool_load_coro(conn);
        return;
    }

    conn->session_ok = 1;
    if (scenario_needs_session(conn->cfg->scenario)) {
        conn->session_ok = establish_session(conn);
        if (!conn->session_ok) { vconn_close(conn); return; }
    } else {
        establish_session(conn);
    }

    if (conn->cfg->scenario == SCENARIO_MIXED) {
        conn->uid = hash_username(conn->uname);
        conn->rng = (uint64_t)(conn->cfg->run_nonce) * 0x9e3779b97f4a7c15ull
                ^ ((uint64_t)(conn->id + 1) << 32) ^ now_ns();
        if (!conn->rng) conn->rng = 0x123456789abcdefull;
        char body[160];
        int body_len = snprintf(body, sizeof(body),
            "{\"path\":\"git_repo.git_repo.make\",\"args\":[%u,\"%s\",\"r0\"]}",
            conn->uid, conn->uname);
        if (http_try(conn, "POST", "/_rpc", "application/json", conn->sid,
                     body, (size_t)body_len, NULL, 0) == 200)
            conn->n_repos = 1;
    }

    long counter = 0;
    if (conn->cfg->requests_per_conn > 0) {
        for (long i = 0; i < conn->cfg->requests_per_conn && !*conn->stop; ++i) {
            uint64_t started_ns = now_ns();
            int ok = scenario_step(conn, counter++);
            lat_push(&conn->lat, now_ns() - started_ns);
            if (ok) conn->ok++; else conn->errors++;
        }
    } else {
        while (!*conn->stop) {
            uint64_t started_ns = now_ns();
            int ok = scenario_step(conn, counter++);
            lat_push(&conn->lat, now_ns() - started_ns);
            if (ok) conn->ok++; else conn->errors++;
        }
    }
    vconn_close(conn);
}

static void seed_coro(void *arg)
{
    struct vconn *conn = arg;
    if (vconn_connect(conn) != 0) return;

    long total_users = conn->cfg->seed_users;
    int conn_count = conn->cfg->total;
    long per = (total_users + conn_count - 1) / conn_count;
    long start = (long)conn->id * per;
    long end = start + per;
    if (end > total_users) end = total_users;
    for (long i = start; i < end && !*conn->stop; ++i) {
        uint32_t uid = (uint32_t)(i + 1);
        char body[96];
        int body_len = snprintf(body, sizeof(body),
            "{\"path\":\"accounts.accounts.register\",\"args\":[%u,\"u%u\"]}", uid, uid);
        int status = http_try(conn, "POST", "/_rpc", "application/json", NULL,
                          body, (size_t)body_len, NULL, 0);
        if (status == 200) conn->seed_ok++; else conn->seed_err++;
    }
    vconn_close(conn);
}

struct deadline_arg {
    struct loop *loop;
    volatile int *stop;
    unsigned int ms;
};

static void deadline_coro(void *arg)
{
    struct deadline_arg *deadline = arg;
    loop_sleep_ms(deadline->loop, deadline->ms);
    *deadline->stop = 1;
    loop_stop(deadline->loop);
}

/* ---- one OS thread: a loop + K coroutines ---------------------------- */

struct lt_thread {
    pthread_t thread;
    const struct perf_config *cfg;
    volatile int *stop;
    struct vconn *vconns;  /* slice into the shared vconn array */
    int lo, hi;            /* this thread owns vconns[lo..hi) */
    int phase;             /* PHASE_LOAD | PHASE_SEED | PHASE_POOL_SETUP */
};

enum { PHASE_LOAD = 0, PHASE_SEED = 1, PHASE_POOL_SETUP = 2 };

static void *lt_thread_main(void *arg)
{
    struct lt_thread *thread = arg;
    struct loop_ptr_result loop_res = loop_create();
    if (PICOMESH_IS_ERR(loop_res)) { picomesh_error_destroy(loop_res.error); return NULL; }
    struct loop *loop = loop_res.value;

    int coro_count = thread->hi - thread->lo;
    struct picomesh_coro **coros = calloc((size_t)coro_count + 1, sizeof(*coros));
    if (!coros) { loop_destroy(loop); return NULL; }

    for (int i = 0; i < coro_count; ++i) {
        struct vconn *conn = &thread->vconns[thread->lo + i];
        conn->loop = loop;
        void (*coro_entry)(void *) = thread->phase == PHASE_SEED ? seed_coro
                                   : thread->phase == PHASE_POOL_SETUP ? pool_setup_coro
                                                                       : load_coro;
        struct picomesh_coro_ptr_result spawn_res =
            picomesh_coro_spawn(coro_entry, conn, CORO_STACK_HINT, "vc");
        if (PICOMESH_IS_ERR(spawn_res)) { picomesh_error_destroy(spawn_res.error); coros[i] = NULL; continue; }
        coros[i] = spawn_res.value;
        picomesh_coro_resume(coros[i]); /* run to first park (connect) */
    }

    /* Load phase, duration mode: a timer coro that ends the run. */
    struct deadline_arg deadline = {.loop = loop, .stop = thread->stop,
        .ms = (unsigned int)(thread->cfg->duration_secs * 1000.0)};
    struct picomesh_coro *deadline_coro_handle = NULL;
    if (thread->phase == PHASE_LOAD && thread->cfg->requests_per_conn <= 0) {
        struct picomesh_coro_ptr_result deadline_res =
            picomesh_coro_spawn(deadline_coro, &deadline, CORO_STACK_HINT, "deadline");
        if (PICOMESH_IS_OK(deadline_res)) {
            deadline_coro_handle = deadline_res.value;
            picomesh_coro_resume(deadline_coro_handle);
        }
    }

    loop_run(loop);

    /* Reap finished coros; parked ones (abandoned mid-request at the
     * deadline) are left for process exit — destroying a suspended coro
     * is unsafe (its resume callback would touch freed memory). */
    for (int i = 0; i < coro_count; ++i)
        if (coros[i] && picomesh_coro_is_finished(coros[i]))
            picomesh_coro_destroy(coros[i]);
    if (deadline_coro_handle && picomesh_coro_is_finished(deadline_coro_handle))
        picomesh_coro_destroy(deadline_coro_handle);
    free(coros);
    loop_destroy(loop);
    return NULL;
}

/* Run one phase (load, seed, or pool-setup) across all threads, then join. */
static void run_phase(struct lt_thread *threads, int nthreads, struct vconn *vconns,
                      int total, const struct perf_config *cfg, volatile int *stop,
                      int phase)
{
    int per = (total + nthreads - 1) / nthreads;
    for (int thread_idx = 0; thread_idx < nthreads; ++thread_idx) {
        threads[thread_idx].cfg = cfg;
        threads[thread_idx].stop = stop;
        threads[thread_idx].vconns = vconns;
        threads[thread_idx].lo = thread_idx * per;
        threads[thread_idx].hi = (thread_idx + 1) * per < total ? (thread_idx + 1) * per : total;
        threads[thread_idx].phase = phase;
        if (threads[thread_idx].lo >= threads[thread_idx].hi) {
            threads[thread_idx].hi = threads[thread_idx].lo;
        }
        pthread_create(&threads[thread_idx].thread, NULL, lt_thread_main, &threads[thread_idx]);
    }
    for (int thread_idx = 0; thread_idx < nthreads; ++thread_idx)
        pthread_join(threads[thread_idx].thread, NULL);
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
        "  --users M             population of M users multiplexed across --connections\n"
        "                        (C ≪ M): pre-establish M sessions once, then each\n"
        "                        connection acts as a RANDOM user per request — a\n"
        "                        read workload attributed across the whole population.\n"
        "                        Decouples user count from the connection concurrency knob.\n"
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
        const char *arg = argv[i];
        const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(arg, "--host") && next) { cfg.host = next; i++; }
        else if (!strcmp(arg, "--port") && next) { cfg.port = atoi(next); i++; }
        else if (!strcmp(arg, "--threads") && next) { cfg.threads = atoi(next); i++; }
        else if (!strcmp(arg, "--connections") && next) { cfg.connections = atoi(next); i++; }
        else if (!strcmp(arg, "--coros-per-thread") && next) { coros_per_thread = atoi(next); i++; }
        else if (!strcmp(arg, "--duration") && next) { cfg.duration_secs = atof(next); i++; }
        else if (!strcmp(arg, "--requests") && next) { cfg.requests_per_conn = atol(next); i++; }
        else if (!strcmp(arg, "--seed-users") && next) { cfg.seed_users = atol(next); i++; }
        else if (!strcmp(arg, "--users") && next) { cfg.users = atol(next); i++; }
        else if (!strcmp(arg, "--repos-per-worker") && next) { cfg.repos_per_conn = atoi(next); i++; }
        else if (!strcmp(arg, "--emulate")) { cfg.emulate = 1; }
        else if (!strcmp(arg, "--scenario") && next) {
            const char *canon = NULL;
            cfg.scenario = scenario_lookup(next, &canon);
            if (!canon) { fprintf(stderr, "unknown scenario '%s'\n", next); usage(argv[0]); return 2; }
            cfg.scenario_name = canon;
            i++;
        }
        else if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "unknown arg '%s'\n", arg); usage(argv[0]); return 2; }
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

    if (cfg.users > 0) {
        cfg.user_pool = calloc((size_t)cfg.users, sizeof(*cfg.user_pool));
        if (!cfg.user_pool) { fprintf(stderr, "oom (user_pool)\n"); return 1; }
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

    /* ---- stage 1b: pre-establish the user population (issue #12) ------ */
    if (cfg.users > 0) {
        uint64_t pool_ok = 0, pool_err = 0;
        fprintf(stderr, "establishing %ld-user population across %d connections (issue #12)…\n",
                cfg.users, cfg.total);
        uint64_t pool0 = now_ns();
        run_phase(threads, cfg.threads, vconns, cfg.total, &cfg, &stop, PHASE_POOL_SETUP);
        double pool_wall = (double)(now_ns() - pool0) / 1e9;
        for (int i = 0; i < cfg.total; ++i) {
            pool_ok += vconns[i].seed_ok;
            pool_err += vconns[i].seed_err;
            vconns[i].seed_ok = vconns[i].seed_err = 0; /* reset before the load phase */
        }
        fprintf(stderr, "population ready: %llu/%ld users in %.2fs (%llu failed)\n",
                (unsigned long long)pool_ok, cfg.users, pool_wall, (unsigned long long)pool_err);
    }

    /* ---- stage 2: the load ------------------------------------------- */
    uint64_t wall0 = now_ns();
    run_phase(threads, cfg.threads, vconns, cfg.total, &cfg, &stop, PHASE_LOAD);
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
    double throughput = wall_s > 0 ? (double)total_req / wall_s : 0.0;

    printf("\n");
    printf("scenario       : %s%s\n", cfg.scenario_name,
           cfg.emulate ? "  [EMULATE — harness-only ceiling, no real calls]" : "");
    printf("concurrency    : %d threads × ~%d coroutines = %d connections\n",
           cfg.threads, (cfg.total + cfg.threads - 1) / cfg.threads, cfg.total);
    if (cfg.users > 0)
        printf("population     : %ld users multiplexed across %d connections (issue #12)\n",
               cfg.users, cfg.total);
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
    printf("throughput     : %.1f req/s\n", throughput);
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
            uint64_t op_total = op_ok_tot[o] + op_err_tot[o];
            double share = total_req ? 100.0 * (double)op_total / (double)total_req : 0.0;
            printf("    %-12s : %8llu ok / %llu err  (%.1f%%, %.0f/s)\n",
                   OP_NAMES[o], (unsigned long long)op_ok_tot[o],
                   (unsigned long long)op_err_tot[o], share,
                   wall_s > 0 ? (double)op_total / wall_s : 0.0);
        }
        uint64_t status_totals[7] = {0};
        for (int i = 0; i < cfg.total; ++i)
            for (int b = 0; b < 7; ++b) status_totals[b] += vconns[i].status_hist[b];
        static const char *const SB[7] = {
            "transport(<0)", "2xx", "303", "401", "403", "5xx", "other"};
        printf("status codes   : (what the errors actually were)\n");
        for (int b = 0; b < 7; ++b)
            if (status_totals[b])
                printf("    %-13s : %llu\n", SB[b], (unsigned long long)status_totals[b]);
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
