/* readlink + sigaction live behind _POSIX_C_SOURCE on glibc; the
 * codegen runs clang without that, so set the minimum at file-top
 * before any include pulls in <features.h>. */
#define _POSIX_C_SOURCE 200809L

/* mesh — orchestrator (skeleton).
 *
 * The yaapp mesh spawns each declared service as a child process and
 * brokers their addresses via portalloc. Subprocess management needs
 * libuv's `uv_spawn` plus shutdown/restart logic — wired in a follow-
 * up patch. For now the plugin exposes the inspection surface so the
 * gateway can call mesh.reconcile / mesh.status without crashing:
 *
 *   register_service(service_id, port)   → 1
 *   resolve(service_id)                  → port (0 if unknown)
 *   forget(service_id)                   → 1 / 0
 *   count_services                       → currently-registered count
 *   reconcile                            → stub: returns 1 */

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/engine/engine.h>
#include <picomesh/loop/loop.h>
#include <picomesh/config/config.h>
#include <picomesh/argv/argv.h>
#include <picomesh/plugin/storage/storage.h>
#include <picomesh/plugin/registry/registry.h>
#include <picomesh/plugin/portalloc/portalloc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define MESH_MAX_SERVICES 64
#define MESH_MAX_CHILDREN 64

struct mesh_service_entry {
    uint32_t service_id;
    uint32_t port;
    int used;
};

struct mesh_child_entry {
    int32_t pid;
    uint32_t port;     /* the port we asked the child to bind on */
    int exited;
    int exit_status;
};

struct PICOMESH_CLASS_ANNOTATE("class@mesh:mesh") mesh_mesh_data {
    struct mesh_service_entry entries[MESH_MAX_SERVICES];
    struct mesh_child_entry children[MESH_MAX_CHILDREN];
    size_t count;
    size_t child_count;
};

static struct mesh_mesh_data *ms(struct object *obj)
{
    return (struct mesh_mesh_data *)((char *)obj + sizeof(struct object));
}

/* ---- child lifecycle: the mesh owns and reaps what it spawned ---------- *
 *
 * Two layers, no flat pidfiles and no external pkill:
 *  1. Signal reaper — a SIGTERM/SIGINT handler SIGTERMs every live child this
 *     parent spawned, so `kill -TERM <parent>` takes the whole stack down. A
 *     handler may only touch async-signal-safe state, so live pids are
 *     mirrored in a file-scope table and reaped with kill(2) (which IS
 *     async-signal-safe). This — and the single process-lifetime storage
 *     handle below — are the one sanctioned file-scope use: signal state.
 *  2. Storage record — every spawned pid (and the parent's own pid) is
 *     written via the `storage` plugin under context "mesh". A fresh bring-up
 *     reaps any pid a previous run left alive (clearing restart port races);
 *     operators read the parent pid from storage to signal it, never by
 *     scanning the process table. */

static volatile sig_atomic_t g_mesh_reap_pids[MESH_MAX_CHILDREN];
static volatile sig_atomic_t g_mesh_reap_installed;
static struct object *g_mesh_store_db; /* process-lifetime KV handle for PIDs */

static void mesh_reap_signal_handler(int sig)
{
    for (int i = 0; i < MESH_MAX_CHILDREN; ++i) {
        pid_t p = (pid_t)g_mesh_reap_pids[i];
        if (p > 0) kill(p, SIGTERM); /* async-signal-safe */
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void mesh_reap_install(void)
{
    if (g_mesh_reap_installed) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = mesh_reap_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    g_mesh_reap_installed = 1;
}

/* Create (once) the in-process storage object for PID bookkeeping. Resolves
 * locally because the parent activates the `storage` plugin. */
static struct object_ptr_result mesh_storage_db(void)
{
    if (g_mesh_store_db) return PICOMESH_OK(object_ptr, g_mesh_store_db);
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(object_ptr, "mesh_storage_db: no active engine");
    struct ctx service_ctx = picomesh_engine_service_ctx(engine, "storage");
    struct object_ptr_result create_res = storage_db_create(&service_ctx);
    PICOMESH_RETURN_IF_ERR(object_ptr, create_res, "mesh_storage_db: storage create failed");
    g_mesh_store_db = create_res.value;
    return PICOMESH_OK(object_ptr, g_mesh_store_db);
}

/* Persist a lifecycle row. Propagates the storage failure so callers that
 * MUST have it durable (e.g. the parent pid) can surface it rather than
 * pretend success. */
static struct picomesh_void_result mesh_storage_set(const char *key, const char *val)
{
    struct object_ptr_result db_res = mesh_storage_db();
    PICOMESH_RETURN_IF_ERR(picomesh_void, db_res, "mesh: storage unavailable");
    struct object *db = db_res.value;
    struct picomesh_engine *e = picomesh_active_engine();
    if (!db || !e) return PICOMESH_ERR(picomesh_void, "mesh: storage unavailable");
    struct ctx c = picomesh_engine_service_ctx(e, "storage");
    struct picomesh_int_result r = storage_set(&c, db, NULL, "mesh", key, val);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_void, "mesh: lifecycle write failed", r);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_storage_del(const char *key)
{
    struct object_ptr_result db_res = mesh_storage_db();
    PICOMESH_RETURN_IF_ERR(picomesh_void, db_res, "mesh: storage unavailable");
    struct object *db = db_res.value;
    struct picomesh_engine *e = picomesh_active_engine();
    if (!db || !e) return PICOMESH_ERR(picomesh_void, "mesh: storage unavailable");
    struct ctx c = picomesh_engine_service_ctx(e, "storage");
    struct picomesh_int_result r = storage_del(&c, db, NULL, "mesh", key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_void, "mesh: lifecycle delete failed", r);
    return PICOMESH_OK_VOID();
}

/* Record a freshly spawned child: signal-reaper mirror + storage row. The
 * in-memory mirror (set first) is what reaps the child on THIS run's
 * shutdown; the storage row is the cross-run backup (reaped after a crash).
 * A failed storage write is therefore non-fatal but must be LOUD, never
 * silent — the caller logs it (the cross-run record is missing). Returns the
 * write result so the spawn path can surface it. */
static struct picomesh_void_result mesh_child_track(int slot, int pid, const char *name)
{
    if (slot >= 0 && slot < MESH_MAX_CHILDREN) g_mesh_reap_pids[slot] = pid;
    char key[32], val[160];
    snprintf(key, sizeof(key), "child:%d", slot);
    snprintf(val, sizeof(val), "%d %s", pid, name ? name : "");
    return mesh_storage_set(key, val);
}

static struct picomesh_void_result mesh_child_untrack(int slot)
{
    if (slot >= 0 && slot < MESH_MAX_CHILDREN) g_mesh_reap_pids[slot] = 0;
    char key[32];
    snprintf(key, sizeof(key), "child:%d", slot);
    /* Best-effort cleanup of the cross-run record; log if it fails. */
    struct picomesh_void_result r = mesh_storage_del(key);
    if (PICOMESH_IS_ERR(r)) {
        ywarn("mesh: failed to clear child record slot=%d (%s)", slot, r.error.msg ? r.error.msg : "?");
        picomesh_error_destroy(r.error);
    }
    return PICOMESH_OK_VOID();
}

/* Reap children a PREVIOUS run left alive (its parent was SIGKILLed/crashed
 * before the handler ran) and clear the stale records. Called on bring-up. */
static struct picomesh_void_result mesh_reap_previous_run(void)
{
    struct object_ptr_result db_res = mesh_storage_db();
    PICOMESH_RETURN_IF_ERR(picomesh_void, db_res, "mesh_reap: storage unavailable");
    struct object *db = db_res.value;
    struct picomesh_engine *e = picomesh_active_engine();
    if (!db || !e) return PICOMESH_OK_VOID();
    struct ctx c = picomesh_engine_service_ctx(e, "storage");
    for (int slot = 0; slot < MESH_MAX_CHILDREN; ++slot) {
        char key[32];
        snprintf(key, sizeof(key), "child:%d", slot);
        struct picomesh_string_result g = storage_get(&c, db, NULL, "mesh", key);
        if (PICOMESH_IS_OK(g) && g.value && *g.value) {
            int pid = (int)strtol(g.value, NULL, 10);
            if (pid > 0 && kill(pid, 0) == 0) {
                kill(pid, SIGTERM);
                yinfo("mesh: reaped orphan pid=%d from a previous run", pid);
            }
        }
        if (PICOMESH_IS_OK(g)) free(g.value); else picomesh_error_destroy(g.error);
        /* Best-effort clear of the stale record; log if it fails. */
        struct picomesh_void_result d = mesh_storage_del(key);
        if (PICOMESH_IS_ERR(d)) {
            ywarn("mesh: failed to clear stale child record slot=%d (%s)", slot, d.error.msg ? d.error.msg : "?");
            picomesh_error_destroy(d.error);
        }
    }
    return PICOMESH_OK_VOID();
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_register_service")
struct picomesh_int_result mesh_mesh_register_service_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t service_id, uint32_t port)
{
    (void)ctx;
    struct mesh_mesh_data *d = ms(obj);
    /* idempotent: re-registering same id updates the port */
    for (size_t i = 0; i < MESH_MAX_SERVICES; ++i) {
        if (d->entries[i].used && d->entries[i].service_id == service_id) {
            d->entries[i].port = port;
            return PICOMESH_OK(picomesh_int, 1);
        }
    }
    for (size_t i = 0; i < MESH_MAX_SERVICES; ++i) {
        if (!d->entries[i].used) {
            d->entries[i].service_id = service_id;
            d->entries[i].port = port;
            d->entries[i].used = 1;
            d->count++;
            yinfo("mesh: registered service %u on port %u", service_id, port);
            return PICOMESH_OK(picomesh_int, 1);
        }
    }
    return PICOMESH_ERR(picomesh_int, "mesh_register_service: table full");
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_resolve")
struct picomesh_uint32_result mesh_mesh_resolve_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   uint32_t service_id)
{
    (void)ctx;
    struct mesh_mesh_data *d = ms(obj);
    for (size_t i = 0; i < MESH_MAX_SERVICES; ++i) {
        if (d->entries[i].used && d->entries[i].service_id == service_id) {
            return PICOMESH_OK(picomesh_uint32, d->entries[i].port);
        }
    }
    return PICOMESH_OK(picomesh_uint32, 0);
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_forget")
struct picomesh_int_result mesh_mesh_forget_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                               uint32_t service_id)
{
    (void)ctx;
    struct mesh_mesh_data *d = ms(obj);
    for (size_t i = 0; i < MESH_MAX_SERVICES; ++i) {
        if (d->entries[i].used && d->entries[i].service_id == service_id) {
            d->entries[i].used = 0;
            d->count--;
            return PICOMESH_OK(picomesh_int, 1);
        }
    }
    return PICOMESH_OK(picomesh_int, 0);
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_count_services")
struct picomesh_size_result mesh_mesh_count_services_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    return PICOMESH_OK(picomesh_size, ms(obj)->count);
}

/* uv_spawn-based subprocess management:
 *   spawn_picomesh(port)   → child pid (0 on failure)
 *   kill_pid(pid)       → 1 ok / 0 unknown
 *   count_children      → live (not-yet-reaped) child count
 *
 * spawn_picomesh(port) launches a fresh `<self> --frontend yttp --host
 * 127.0.0.1 --port <port> serve`. The child inherits stdin/stdout/
 * stderr from the parent so its trace lands in the parent's terminal.
 *
 * The exit callback flips the matching `exited` flag so kill_pid /
 * count_children stay accurate without a reap step.
 *
 * Path discovery uses `/proc/self/exe` on Linux. On macOS we'd use
 * `_NSGetExecutablePath` — TODO for the platform shim. */

static int find_child_slot(struct mesh_mesh_data *d, int32_t pid)
{
    for (int i = 0; i < MESH_MAX_CHILDREN; ++i) {
        if (!d->children[i].exited && d->children[i].pid == pid) return i;
    }
    return -1;
}

static void mesh_child_exit_cb(struct loop_process *p, int64_t exit_status,
                               int term_signal, void *ud)
{
    (void)p; (void)term_signal;
    struct object *obj = ud;
    if (!obj) return;
    struct mesh_mesh_data *d = ms(obj);
    /* libuv only knows the uv_process_t pointer; the pid we stashed
     * at spawn time matches the slot. Sweep for any non-exited child
     * whose pid matches the captured exit. Since uv passes us the
     * loop_process but not the pid directly, we walk all live
     * children and mark the first one matching this callback chain
     * — limitation: assumes one child exits per cb (true), and pid
     * uniqueness in the window. Good enough. */
    /* We can't recover the pid from `p` here without leaking the
     * loop_process internals, so we use `ud` to carry both obj +
     * pid in a pair. */
    (void)d; (void)exit_status;
}

struct mesh_child_cookie {
    struct object *obj;
    int32_t pid;
};

PICOMESH_EXTERNAL_CALLBACK
static void mesh_child_exit_cb_real(struct loop_process *p, int64_t exit_status,
                                    int term_signal, void *ud)
{
    (void)p;
    struct mesh_child_cookie *c = ud;
    if (!c) return;
    struct mesh_mesh_data *d = ms(c->obj);
    int slot = find_child_slot(d, c->pid);
    if (slot >= 0) {
        d->children[slot].exited = 1;
        d->children[slot].exit_status = (int)exit_status;
        d->child_count--;
        struct picomesh_void_result untrack_res = mesh_child_untrack(slot);
        if (PICOMESH_IS_ERR(untrack_res)) picomesh_error_destroy(untrack_res.error);
        yinfo("mesh: child pid=%d exited (status=%d, term_signal=%d)",
              c->pid, (int)exit_status, term_signal);
    }
    free(c);
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_spawn_picomesh")
struct picomesh_int_result mesh_mesh_spawn_picomesh_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                    uint32_t port)
{
    (void)ctx;
    struct mesh_mesh_data *d = ms(obj);
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(picomesh_int, "spawn_picomesh: no active engine");

    int slot = -1;
    for (int i = 0; i < MESH_MAX_CHILDREN; ++i) {
        if (d->children[i].exited || d->children[i].pid == 0) { slot = i; break; }
    }
    if (slot < 0) return PICOMESH_ERR(picomesh_int, "spawn_picomesh: child table full");

    /* /proc/self/exe is Linux-only; macOS needs _NSGetExecutablePath
     * (TODO via platform once a Mac build is exercised). */
    char self_exe[4096];
    ssize_t n = readlink("/proc/self/exe", self_exe, sizeof(self_exe) - 1);
    if (n <= 0) return PICOMESH_ERR(picomesh_int, "spawn_picomesh: readlink(/proc/self/exe) failed");
    self_exe[n] = 0;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    char *argv[] = {
        self_exe,
        (char *)"--frontend", (char *)"yhttp",
        (char *)"--host", (char *)"127.0.0.1",
        (char *)"--port", port_str,
        (char *)"serve",
        NULL,
    };

    struct mesh_child_cookie *c = calloc(1, sizeof(*c));
    if (!c) return PICOMESH_ERR(picomesh_int, "spawn_picomesh: calloc(cookie) failed");
    c->obj = obj;

    struct picomesh_int_result spawn_res = loop_spawn(picomesh_engine_loop(e), self_exe, argv,
                          mesh_child_exit_cb_real, c);
    if (PICOMESH_IS_ERR(spawn_res)) {
        free(c);
        return PICOMESH_ERR(picomesh_int, "spawn_picomesh: loop_spawn failed", spawn_res);
    }
    int pid = spawn_res.value;
    c->pid = pid;
    d->children[slot].pid = pid;
    d->children[slot].port = port;
    d->children[slot].exited = 0;
    d->children[slot].exit_status = 0;
    d->child_count++;
    mesh_reap_install();
    struct picomesh_void_result tr = mesh_child_track(slot, pid, "picomesh");
    if (PICOMESH_IS_ERR(tr)) {
        ywarn("mesh: spawned pid=%d but its cross-run reap record FAILED to persist (%s) — "
              "the in-memory reaper still covers this run's shutdown", pid, tr.error.msg ? tr.error.msg : "?");
        picomesh_error_destroy(tr.error);
    }
    yinfo("mesh: spawned pid=%d on port=%u", pid, port);
    return PICOMESH_OK(picomesh_int, pid);
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_kill_pid")
struct picomesh_int_result mesh_mesh_kill_pid_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                 int32_t pid)
{
    (void)ctx;
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(picomesh_int, "kill_pid: no active engine");
    struct mesh_mesh_data *d = ms(obj);
    if (find_child_slot(d, pid) < 0) {
        return PICOMESH_OK(picomesh_int, 0);
    }
    struct picomesh_void_result kill_res = loop_kill(picomesh_engine_loop(e), pid, SIGTERM);
    if (PICOMESH_IS_ERR(kill_res)) {
        /* Best-effort teardown: the child may already be gone (ESRCH). Log the
         * chain rather than dropping it, and report "not killed". */
        picomesh_error_print(stderr, "mesh: SIGTERM child", kill_res.error);
        picomesh_error_destroy(kill_res.error);
        return PICOMESH_OK(picomesh_int, 0);
    }
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_count_children")
struct picomesh_size_result mesh_mesh_count_children_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    return PICOMESH_OK(picomesh_size, ms(obj)->child_count);
}

/* for_each cb state: emit "<key>: <value>\n" for each entry of the node's
 * `config:` map at top level (column 0). `value` is serialized with the
 * shared node dumper, so nested maps/lists round-trip. */
struct split_emit { char *buf; size_t off; size_t cap; int overflow; };

static int split_config_kv_cb(const char *key, const struct config_node *val, void *ud)
{
    struct split_emit *se = ud;
    int kn = snprintf(se->buf + se->off, se->cap - se->off, "%s: ", key);
    if (kn < 0 || (size_t)kn >= se->cap - se->off) { se->overflow = 1; return 1; }
    se->off += (size_t)kn;
    size_t vn = config_node_dump(val, se->buf + se->off, se->cap - se->off);
    se->off += vn;
    if (se->off + 2 >= se->cap) { se->overflow = 1; return 1; }
    se->buf[se->off++] = '\n';
    se->buf[se->off] = 0;
    return 0;
}

/* Split one service's config out of the mesh config into a STANDALONE
 * per-node config file and return its path (written into `out`).
 *
 * The mesh config is the mesh plugin's OWN config: it lists nodes, their
 * ports, and each node's `config:` block. A node must NOT read the mesh
 * config — it gets its own file. So at spawn time we lift
 * `mesh.services.<name>.config` (the node's plugins + per-plugin config +
 * remotes) to the TOP LEVEL of a fresh file, plus the node's own `name:`,
 * `host:`, `port:`, `frontend:`. The child is launched with just
 * `--config-file <that file>` and finds everything at natural top-level
 * paths — no `--name` projection, no mesh.services lookup.
 *
 * Returns 0 on success. Files land under <dir>/nodes/<name>.yaml where
 * <dir> is the mesh's working area (derives from /tmp/picoforge). */
static struct picomesh_void_result mesh_write_node_config(struct picomesh_engine *e, const char *name,
                                  char *out, size_t out_cap)
{
    char base[256];
    snprintf(base, sizeof(base), "mesh.services.%s", name);

    /* Pull the per-node pieces from the mesh config. */
    char path[320];
    snprintf(path, sizeof(path), "%s.config", base);
    struct config_node_ptr_result cfgr = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_void, cfgr, "mesh_write_node_config: config block read failed");
    const struct config_node *cfg_node = cfgr.value ? cfgr.value : NULL;

    snprintf(path, sizeof(path), "%s.port", base);
    struct config_node_ptr_result pr = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_void, pr, "mesh_write_node_config: port read failed");
    const struct config_node *port_node = pr.value ? pr.value : NULL;
    int64_t port = -1;
    int port_auto = 0;
    if (port_node) {
        if (config_node_kind(port_node) == CONFIG_STRING) {
            const char *ps = config_node_as_string(port_node, NULL);
            port_auto = (ps && strcmp(ps, "auto") == 0);
        } else {
            port = config_node_as_int(port_node, -1);
        }
    }

    snprintf(path, sizeof(path), "%s.host", base);
    struct config_node_ptr_result hr = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_void, hr, "mesh_write_node_config: host read failed");
    const char *host = hr.value ? config_node_as_string(hr.value, NULL) : NULL;

    snprintf(path, sizeof(path), "%s.frontend", base);
    struct config_node_ptr_result fr = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_void, fr, "mesh_write_node_config: frontend read failed");
    const char *frontend = fr.value ? config_node_as_string(fr.value, NULL) : NULL;

    snprintf(path, sizeof(path), "%s.plugins", base);
    struct config_node_ptr_result plr = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_void, plr, "mesh_write_node_config: plugins read failed");
    const struct config_node *plugins_node = plr.value ? plr.value : NULL;

    snprintf(path, sizeof(path), "%s.workers", base);
    struct config_node_ptr_result wr = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_void, wr, "mesh_write_node_config: workers read failed");
    int64_t workers = wr.value ? config_node_as_int(wr.value, -1) : -1;

    /* Build the standalone node file as flow YAML (config parses it). */
    char body[16384];
    size_t off = 0;
    off += (size_t)snprintf(body + off, sizeof(body) - off, "name: \"%s\"\n", name);
    if (host)     off += (size_t)snprintf(body + off, sizeof(body) - off, "host: \"%s\"\n", host);
    /* `port: auto` is passed through verbatim — the child resolves it via
     * portalloc at serve time. A numeric port is baked in as before. */
    if (port_auto)    off += (size_t)snprintf(body + off, sizeof(body) - off, "port: auto\n");
    else if (port > 0) off += (size_t)snprintf(body + off, sizeof(body) - off, "port: %lld\n", (long long)port);
    if (frontend) off += (size_t)snprintf(body + off, sizeof(body) - off, "frontend: \"%s\"\n", frontend);
    if (workers > 0) off += (size_t)snprintf(body + off, sizeof(body) - off, "workers: %lld\n", (long long)workers);

    /* Inject the registry's fixed address as a global block so the child can
     * reach the registry (and, through it, portalloc and its auto remotes)
     * before it knows where anything else lives. Sourced from the registry
     * service's own config — the single place its port is pinned; overriding
     * that one key is how a second instance avoids colliding. */
    struct config_node_ptr_result reg_hr =
        config_get(picomesh_engine_config(e), "mesh.services.registry.host");
    PICOMESH_RETURN_IF_ERR(picomesh_void, reg_hr, "mesh_write_node_config: registry host read failed");
    struct config_node_ptr_result reg_pr =
        config_get(picomesh_engine_config(e), "mesh.services.registry.port");
    PICOMESH_RETURN_IF_ERR(picomesh_void, reg_pr, "mesh_write_node_config: registry port read failed");
    int64_t reg_port = reg_pr.value ? config_node_as_int(reg_pr.value, -1) : -1;
    if (reg_port > 0) {
        const char *reg_host = reg_hr.value ? config_node_as_string(reg_hr.value, NULL) : NULL;
        off += (size_t)snprintf(body + off, sizeof(body) - off,
                                "registry:\n  host: \"%s\"\n  port: %lld\n",
                                (reg_host && *reg_host) ? reg_host : "127.0.0.1",
                                (long long)reg_port);
    }
    if (plugins_node) {
        off += (size_t)snprintf(body + off, sizeof(body) - off, "plugins: ");
        off += config_node_dump(plugins_node, body + off, sizeof(body) - off);
        off += (size_t)snprintf(body + off, sizeof(body) - off, "\n");
    }
    /* The node's `config:` block — its per-plugin config + remotes —
     * lifted to top level: emit each of its keys at column 0 so plugins
     * read them at natural paths (`git_repo.repos_dir`, `remotes`, …). */
    if (cfg_node && config_node_kind(cfg_node) == CONFIG_MAP) {
        struct split_emit se = {body, off, sizeof(body), 0};
        config_node_for_each(cfg_node, split_config_kv_cb, &se);
        off = se.off;
        if (se.overflow) return PICOMESH_ERR(picomesh_void, "mesh_write_node_config: config body overflow");
    }

    if (off >= sizeof(body)) return PICOMESH_ERR(picomesh_void, "mesh_write_node_config: body buffer overflow");

    /* The generated per-node files land under `mesh.nodes_dir` — a REQUIRED
     * input-config key, never a baked-in path. Everything else in this file is
     * lifted from the input config; the OUTPUT location must be too, or a second
     * mesh instance (own input config → own state paths/ports) would still
     * clobber the first's node files here. Overridable like any config key:
     * `--config mesh.nodes_dir=/path`. No silent default. */
    struct config_node_ptr_result ndr =
        config_get(picomesh_engine_config(e), "mesh.nodes_dir");
    PICOMESH_RETURN_IF_ERR(picomesh_void, ndr, "mesh_write_node_config: nodes_dir read failed");
    const char *nodes_dir = ndr.value ? config_node_as_string(ndr.value, NULL) : NULL;
    if (!nodes_dir || !*nodes_dir) {
        ywarn("mesh.split: mesh.nodes_dir is REQUIRED (the directory the mesh "
              "writes per-node configs to); refusing to guess a path");
        return PICOMESH_ERR(picomesh_void, "mesh_write_node_config: mesh.nodes_dir is required");
    }
    if (snprintf(out, out_cap, "%s/%s.yaml", nodes_dir, name) >= (int)out_cap) {
        ywarn("mesh.split: mesh.nodes_dir path too long for node '%s'", name);
        return PICOMESH_ERR(picomesh_void, "mesh_write_node_config: nodes_dir path too long");
    }
    /* mkdir -p <nodes_dir> */
    char dir[256];
    if (snprintf(dir, sizeof(dir), "%s", nodes_dir) >= (int)sizeof(dir)) {
        ywarn("mesh.split: mesh.nodes_dir too long");
        return PICOMESH_ERR(picomesh_void, "mesh_write_node_config: nodes_dir too long");
    }
    char acc[256] = {0};
    for (size_t i = 1; dir[i]; ++i) {
        if (dir[i] == '/') { memcpy(acc, dir, i); acc[i] = 0; mkdir(acc, 0755); }
    }
    mkdir(dir, 0755);

    FILE *f = fopen(out, "w");
    if (!f) { ywarn("mesh.split: cannot write %s: %s", out, strerror(errno)); return PICOMESH_ERR(picomesh_void, "mesh_write_node_config: cannot open output file"); }
    fwrite(body, 1, off, f);
    fclose(f);
    return PICOMESH_OK_VOID();
}

/* Spawn one child picomesh process for service `name`. The mesh SPLITS its
 * config into a standalone per-node file (mesh_write_node_config) and the
 * child is launched with just that file:
 *
 *   picomesh --config-file <nodes/<name>.yaml> --name <name> --frontend X serve
 *
 * The node file carries the node's own top-level config (plugins, per-
 * plugin config, remotes, port, host, frontend) — the child never reads
 * the mesh config. `--name` is kept only so the engine resolves bind
 * port/host from the node file's top-level keys uniformly. */
static struct picomesh_int_result mesh_internal_spawn(struct object *obj, const char *name)
{
    if (!name || !*name) return PICOMESH_OK(picomesh_int, 0);
    struct mesh_mesh_data *d = ms(obj);
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_OK(picomesh_int, 0);
    int slot = -1;
    for (int i = 0; i < MESH_MAX_CHILDREN; ++i) {
        if (d->children[i].exited || d->children[i].pid == 0) { slot = i; break; }
    }
    if (slot < 0) return PICOMESH_OK(picomesh_int, 0);
    char self_exe[4096];
    ssize_t n = readlink("/proc/self/exe", self_exe, sizeof(self_exe) - 1);
    if (n <= 0) return PICOMESH_OK(picomesh_int, 0);
    self_exe[n] = 0;

    /* Per CLAUDE.md: backends listen yrpc by default; only the gateway
     * opts into yhttp via `frontend: yhttp`. Read it from the mesh config
     * (we still have it here, in the parent). */
    char fe_path[256];
    snprintf(fe_path, sizeof(fe_path), "mesh.services.%s.frontend", name);
    const char *frontend = "yrpc";
    struct config_node_ptr_result fr = config_get(picomesh_engine_config(e), fe_path);
    PICOMESH_RETURN_IF_ERR(picomesh_int, fr, "mesh_internal_spawn: frontend config read failed");
    if (fr.value) {
        const char *s = config_node_as_string(fr.value, NULL);
        if (s && *s) frontend = s;
    }

    /* Split the mesh config → standalone per-node config file. */
    static char node_cfg[MESH_MAX_CHILDREN][256];
    struct picomesh_void_result write_res = mesh_write_node_config(e, name, node_cfg[slot], sizeof(node_cfg[slot]));
    if (PICOMESH_IS_ERR(write_res)) {
        ywarn("mesh.spawn: failed to write per-node config for '%s' (%s)", name,
              write_res.error.msg ? write_res.error.msg : "?");
        picomesh_error_destroy(write_res.error);
        return PICOMESH_OK(picomesh_int, 0);
    }
    const char *cfg = node_cfg[slot];

    /* `cfg`, `name`, and `frontend` outlive the spawn call (cfg is in the
     * process-lifetime node_cfg table), so dropping them into argv[] is safe. */
    char *argv[] = {
        self_exe,
        (char *)"--config-file", (char *)cfg,
        (char *)"--name",        (char *)name,
        (char *)"--frontend",    (char *)frontend,
        (char *)"serve",
        NULL,
    };

    struct mesh_child_cookie *c = calloc(1, sizeof(*c));
    if (!c) return PICOMESH_ERR(picomesh_int, "mesh_internal_spawn: child cookie alloc failed");
    c->obj = obj;
    struct picomesh_int_result spawn_res = loop_spawn(picomesh_engine_loop(e), self_exe, argv,
                          mesh_child_exit_cb_real, c);
    if (PICOMESH_IS_ERR(spawn_res)) { free(c); return PICOMESH_ERR(picomesh_int, "mesh_internal_spawn: loop_spawn failed", spawn_res); }
    int pid = spawn_res.value;
    c->pid = pid;
    d->children[slot].pid = pid;
    d->children[slot].port = 0; /* port is in the YAML; child resolves */
    d->children[slot].exited = 0;
    d->child_count++;
    mesh_reap_install();
    struct picomesh_void_result tr = mesh_child_track(slot, pid, name);
    if (PICOMESH_IS_ERR(tr)) {
        ywarn("mesh: spawned service='%s' pid=%d but its cross-run reap record FAILED to persist (%s) — "
              "the in-memory reaper still covers this run's shutdown", name, pid, tr.error.msg ? tr.error.msg : "?");
        picomesh_error_destroy(tr.error);
    }
    yinfo("mesh: spawned pid=%d service='%s'", pid, name);
    return PICOMESH_OK(picomesh_int, pid);
}


static void mesh_nap_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static int mesh_tcp_connect(const char *host, int port)
{
    if (!host || !*host) host = "127.0.0.1";
    const char *dial = (strcmp(host, "0.0.0.0") == 0) ? "127.0.0.1" : host;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, dial, &addr.sin_addr) != 1) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

static int mesh_probe_bind(const char *host, int port)
{
    if (!host || !*host) host = "127.0.0.1";
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) { close(fd); return 0; }
    int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return rc == 0;
}

static struct picomesh_int_result mesh_registry_addr(struct picomesh_engine *e, char *host_out, size_t cap, int *port_out)
{
    struct config_node_ptr_result hr = config_get(picomesh_engine_config(e), "mesh.services.registry.host");
    PICOMESH_RETURN_IF_ERR(picomesh_int, hr, "mesh_registry_addr: registry host config read failed");
    struct config_node_ptr_result pr = config_get(picomesh_engine_config(e), "mesh.services.registry.port");
    PICOMESH_RETURN_IF_ERR(picomesh_int, pr, "mesh_registry_addr: registry port config read failed");
    int64_t port = pr.value ? config_node_as_int(pr.value, -1) : -1;
    if (port <= 0) return PICOMESH_OK(picomesh_int, 0);
    const char *host = hr.value ? config_node_as_string(hr.value, NULL) : NULL;
    snprintf(host_out, cap, "%s", (host && *host) ? host : "127.0.0.1");
    *port_out = (int)port;
    return PICOMESH_OK(picomesh_int, 1);
}

struct mesh_reg_session {
    struct peer_channel *channel;
    struct object *obj;
    struct ctx ctx;
};

static struct picomesh_int_result mesh_reg_session_open(struct picomesh_engine *e, struct mesh_reg_session *session)
{
    char host[64];
    int port = 0;
    struct picomesh_int_result addr_res = mesh_registry_addr(e, host, sizeof(host), &port);
    PICOMESH_RETURN_IF_ERR(picomesh_int, addr_res, "mesh_reg_session_open: registry addr lookup failed");
    if (!addr_res.value) return PICOMESH_OK(picomesh_int, 0);
    int fd = -1;
    for (int attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        fd = mesh_tcp_connect(host, port);
        if (fd < 0) mesh_nap_ms(100);
    }
    if (fd < 0) return PICOMESH_OK(picomesh_int, 0);
    session->channel = peer_channel_create(fd);
    if (!session->channel) { close(fd); return PICOMESH_OK(picomesh_int, 0); }
    session->ctx = (struct ctx){.peer = session->channel};
    struct object_ptr_result obj_r = registry_registry_create(&session->ctx);
    if (PICOMESH_IS_ERR(obj_r)) {
        peer_channel_destroy(session->channel);
        session->channel = NULL;
        return PICOMESH_ERR(picomesh_int, "mesh_reg_session_open: registry create failed", obj_r);
    }
    session->obj = obj_r.value;
    return PICOMESH_OK(picomesh_int, 1);
}

static void mesh_reg_session_close(struct mesh_reg_session *session)
{
    if (!session || !session->channel) return;
    object_release_in_ctx(&session->ctx, session->obj);
    peer_channel_destroy(session->channel);
    session->channel = NULL;
    session->obj = NULL;
}

static struct picomesh_int_result mesh_reg_resolve(struct mesh_reg_session *session, const char *service, int wait,
                            char *host_out, size_t cap, int *port_out)
{
    int tries = wait ? 150 : 1;
    for (int attempt = 0; attempt < tries; ++attempt) {
        struct picomesh_string_result r = registry_registry_resolve(&session->ctx, session->obj, NULL, service);
        if (PICOMESH_IS_ERR(r)) {
            /* Transient — the registry may still be starting; retry. */
            picomesh_error_destroy(r.error);
        } else if (r.value && *r.value) {
            const char *colon = strrchr(r.value, ':');
            int ok = 0;
            if (colon && colon != r.value) {
                size_t hlen = (size_t)(colon - r.value);
                if (hlen < cap) {
                    memcpy(host_out, r.value, hlen);
                    host_out[hlen] = 0;
                    *port_out = atoi(colon + 1);
                    ok = (*port_out > 0);
                }
            }
            free(r.value);
            if (ok) return PICOMESH_OK(picomesh_int, 1);
        } else {
            free(r.value);
        }
        if (wait) mesh_nap_ms(100);
    }
    return PICOMESH_OK(picomesh_int, 0);
}

static struct picomesh_int_result mesh_allocate_webapp_port(struct picomesh_engine *e, const char *name, const char *host)
{
    struct mesh_reg_session reg = {0};
    struct picomesh_int_result session_res = mesh_reg_session_open(e, &reg);
    PICOMESH_RETURN_IF_ERR(picomesh_int, session_res, "mesh_allocate_webapp_port: registry session failed");
    if (!session_res.value) return PICOMESH_ERR(picomesh_int, "mesh_allocate_webapp_port: registry unreachable");
    char ph[64];
    int pp = 0;
    struct picomesh_int_result resolve_res = mesh_reg_resolve(&reg, "portalloc", 1, ph, sizeof(ph), &pp);
    mesh_reg_session_close(&reg);
    PICOMESH_RETURN_IF_ERR(picomesh_int, resolve_res, "mesh_allocate_webapp_port: portalloc resolve failed");
    if (!resolve_res.value) return PICOMESH_ERR(picomesh_int, "mesh_allocate_webapp_port: portalloc not discoverable");

    int fd = -1;
    for (int attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        fd = mesh_tcp_connect(ph, pp);
        if (fd < 0) mesh_nap_ms(100);
    }
    if (fd < 0) return PICOMESH_ERR(picomesh_int, "mesh_allocate_webapp_port: portalloc unreachable");
    struct peer_channel *channel = peer_channel_create(fd);
    if (!channel) { close(fd); return PICOMESH_ERR(picomesh_int, "mesh_allocate_webapp_port: peer channel alloc failed"); }
    struct ctx pctx = {.peer = channel};
    struct object_ptr_result obj_r = portalloc_portalloc_create(&pctx);
    if (PICOMESH_IS_ERR(obj_r)) {
        peer_channel_destroy(channel);
        return PICOMESH_ERR(picomesh_int, "mesh_allocate_webapp_port: portalloc create failed", obj_r);
    }
    struct object *pobj = obj_r.value;
    int port = -1;
    for (int attempt = 0; attempt < 64; ++attempt) {
        struct picomesh_uint32_result r = portalloc_portalloc_allocate(&pctx, pobj, NULL, name, host);
        if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); break; }
        uint32_t candidate = r.value;
        if (mesh_probe_bind(host, (int)candidate)) { port = (int)candidate; break; }
        struct picomesh_int_result rel = portalloc_portalloc_release(&pctx, pobj, NULL, candidate);
        if (PICOMESH_IS_ERR(rel)) picomesh_error_destroy(rel.error);
        mesh_nap_ms(20);
    }
    object_release_in_ctx(&pctx, pobj);
    peer_channel_destroy(channel);
    if (port <= 0) return PICOMESH_ERR(picomesh_int, "mesh_allocate_webapp_port: no bindable port from portalloc");
    return PICOMESH_OK(picomesh_int, port);
}

static struct picomesh_int_result mesh_register_webapp(struct picomesh_engine *e, const char *name, const char *host, int port)
{
    struct mesh_reg_session reg = {0};
    struct picomesh_int_result session_res = mesh_reg_session_open(e, &reg);
    PICOMESH_RETURN_IF_ERR(picomesh_int, session_res, "mesh_register_webapp: registry session failed");
    if (!session_res.value) return PICOMESH_OK(picomesh_int, 0);
    const char *reg_host = (strcmp(host, "0.0.0.0") == 0) ? "127.0.0.1" : host;
    struct picomesh_int_result r =
        registry_registry_register_service(&reg.ctx, reg.obj, NULL, name, name, reg_host, (uint32_t)port);
    int ok = PICOMESH_IS_OK(r);
    if (!ok) picomesh_error_destroy(r.error);
    mesh_reg_session_close(&reg);
    return PICOMESH_OK(picomesh_int, ok ? 1 : 0);
}

static int mesh_sibling_exe(const char *app, char *out, size_t cap)
{
    if (!app || !*app) return 0;
    if (strchr(app, '/')) return snprintf(out, cap, "%s", app) < (int)cap;
    char self_exe[4096];
    ssize_t n = readlink("/proc/self/exe", self_exe, sizeof(self_exe) - 1);
    if (n <= 0) return 0;
    self_exe[n] = 0;
    char *slash = strrchr(self_exe, '/');
    if (!slash) return snprintf(out, cap, "%s", app) < (int)cap;
    *slash = 0;
    return snprintf(out, cap, "%s/%s", self_exe, app) < (int)cap;
}

static struct picomesh_int_result mesh_spawn_webapp(struct object *obj, const char *name)
{
    if (!name || !*name) return PICOMESH_OK(picomesh_int, 0);
    struct mesh_mesh_data *d = ms(obj);
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_OK(picomesh_int, 0);

    int slot = -1;
    for (int i = 0; i < MESH_MAX_CHILDREN; ++i) {
        if (d->children[i].exited || d->children[i].pid == 0) { slot = i; break; }
    }
    if (slot < 0) return PICOMESH_OK(picomesh_int, 0);

    char base[256], path[320];
    snprintf(base, sizeof(base), "mesh.webapps.%s", name);
    snprintf(path, sizeof(path), "%s.app", base);
    struct config_node_ptr_result ar = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_int, ar, "mesh_spawn_webapp: app config read failed");
    const char *app = ar.value ? config_node_as_string(ar.value, NULL) : NULL;
    if (!app || !*app) app = name;

    snprintf(path, sizeof(path), "%s.host", base);
    struct config_node_ptr_result hr = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_int, hr, "mesh_spawn_webapp: host config read failed");
    const char *host = hr.value ? config_node_as_string(hr.value, NULL) : NULL;
    if (!host || !*host) host = "127.0.0.1";

    snprintf(path, sizeof(path), "%s.port", base);
    struct config_node_ptr_result pr = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_int, pr, "mesh_spawn_webapp: port config read failed");
    const struct config_node *port_node = pr.value ? pr.value : NULL;
    int port = -1;
    int port_auto = 0;
    if (port_node && config_node_kind(port_node) == CONFIG_STRING) {
        const char *ps = config_node_as_string(port_node, NULL);
        port_auto = ps && strcmp(ps, "auto") == 0;
    } else if (port_node) {
        port = (int)config_node_as_int(port_node, -1);
    }
    if (port_auto) {
        struct picomesh_int_result alloc_res = mesh_allocate_webapp_port(e, name, host);
        PICOMESH_RETURN_IF_ERR(picomesh_int, alloc_res, "mesh_spawn_webapp: webapp port allocation failed");
        port = alloc_res.value;
    }
    if (port <= 0) return PICOMESH_OK(picomesh_int, 0);

    snprintf(path, sizeof(path), "%s.upstream.service", base);
    struct config_node_ptr_result ur = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_int, ur, "mesh_spawn_webapp: upstream config read failed");
    const char *upstream = ur.value ? config_node_as_string(ur.value, NULL) : NULL;
    if (!upstream || !*upstream) return PICOMESH_OK(picomesh_int, 0);

    struct mesh_reg_session reg = {0};
    struct picomesh_int_result session_res = mesh_reg_session_open(e, &reg);
    PICOMESH_RETURN_IF_ERR(picomesh_int, session_res, "mesh_spawn_webapp: registry session failed");
    if (!session_res.value) return PICOMESH_OK(picomesh_int, 0);
    char up_host[64];
    int up_port = 0;
    struct picomesh_int_result resolve_res = mesh_reg_resolve(&reg, upstream, 1, up_host, sizeof(up_host), &up_port);
    mesh_reg_session_close(&reg);
    PICOMESH_RETURN_IF_ERR(picomesh_int, resolve_res, "mesh_spawn_webapp: upstream resolve failed");
    if (!resolve_res.value) return PICOMESH_OK(picomesh_int, 0);

    char exe[4096];
    if (!mesh_sibling_exe(app, exe, sizeof(exe))) return PICOMESH_OK(picomesh_int, 0);
    char port_str[16], up_port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    snprintf(up_port_str, sizeof(up_port_str), "%d", up_port);

    char *argv[] = {
        exe,
        (char *)"--host", (char *)host,
        (char *)"--port", port_str,
        (char *)"--upstream-service", (char *)upstream,
        (char *)"--upstream-host", up_host,
        (char *)"--upstream-port", up_port_str,
        NULL,
    };

    struct mesh_child_cookie *c = calloc(1, sizeof(*c));
    if (!c) return PICOMESH_ERR(picomesh_int, "mesh_spawn_webapp: child cookie alloc failed");
    c->obj = obj;
    struct picomesh_int_result spawn_res = loop_spawn(picomesh_engine_loop(e), exe, argv, mesh_child_exit_cb_real, c);
    if (PICOMESH_IS_ERR(spawn_res)) { free(c); return PICOMESH_ERR(picomesh_int, "mesh_spawn_webapp: loop_spawn failed", spawn_res); }
    int pid = spawn_res.value;
    c->pid = pid;
    d->children[slot].pid = pid;
    d->children[slot].port = (uint32_t)port;
    d->children[slot].exited = 0;
    d->children[slot].exit_status = 0;
    d->child_count++;
    mesh_reap_install();
    struct picomesh_void_result tr = mesh_child_track(slot, pid, name);
    if (PICOMESH_IS_ERR(tr)) { picomesh_error_destroy(tr.error); }
    struct picomesh_int_result register_res = mesh_register_webapp(e, name, host, port);
    if (PICOMESH_IS_ERR(register_res)) {
        ywarn("mesh.webapps: spawned '%s' pid=%d but registry registration failed", name, pid);
        picomesh_error_destroy(register_res.error);
    }
    yinfo("mesh.webapps: spawned '%s' pid=%d listen=%s:%d upstream=%s:%s:%d",
          name, pid, host, port, upstream, up_host, up_port);
    return PICOMESH_OK(picomesh_int, pid);
}

/* Walk callback for the services map. Each iteration sees one
 * (service_name, service_node) pair — we always spawn a child for
 * each service (the child looks up its own port/host/remotes by name
 * via the inherited --config-file). */
struct reconcile_ctx {
    struct object *obj;
    int spawned;
};

/* The discovery plane comes up first and is spawned explicitly, so the walk
 * must not spawn these a second time. */
static int reconcile_is_discovery_plane(const char *service_name)
{
    return strcmp(service_name, "registry") == 0 || strcmp(service_name, "portalloc") == 0;
}

/* Config-walk callback — fixed (name, node, ud) signature dictated by
 * config_node_for_each, so a spawn failure is absorbed (logged) here. */
PICOMESH_EXTERNAL_CALLBACK
static int reconcile_walk_cb(const char *service_name,
                             const struct config_node *val, void *ud)
{
    struct reconcile_ctx *rc = ud;
    if (config_node_kind(val) != CONFIG_MAP) return 0;
    if (reconcile_is_discovery_plane(service_name)) return 0; /* already spawned */

    struct picomesh_int_result spawn_res = mesh_internal_spawn(rc->obj, service_name);
    if (PICOMESH_IS_ERR(spawn_res)) {
        ywarn("mesh.reconcile: failed to spawn '%s' (%s)", service_name,
              spawn_res.error.msg ? spawn_res.error.msg : "?");
        picomesh_error_destroy(spawn_res.error);
        return 0;
    }
    if (spawn_res.value > 0) {
        rc->spawned++;
        yinfo("mesh.reconcile: '%s' → pid=%d", service_name, spawn_res.value);
    } else {
        ywarn("mesh.reconcile: failed to spawn '%s'", service_name);
    }
    return 0;
}

/* Config-walk callback — fixed (name, node, ud) signature dictated by
 * config_node_for_each, so a spawn failure is absorbed (logged) here. */
PICOMESH_EXTERNAL_CALLBACK
static int reconcile_webapp_walk_cb(const char *webapp_name,
                                    const struct config_node *val, void *ud)
{
    struct reconcile_ctx *rc = ud;
    if (config_node_kind(val) != CONFIG_MAP) return 0;
    struct picomesh_int_result spawn_res = mesh_spawn_webapp(rc->obj, webapp_name);
    if (PICOMESH_IS_ERR(spawn_res)) {
        ywarn("mesh.reconcile: failed to spawn webapp '%s' (%s)", webapp_name,
              spawn_res.error.msg ? spawn_res.error.msg : "?");
        picomesh_error_destroy(spawn_res.error);
        return 0;
    }
    if (spawn_res.value > 0) {
        rc->spawned++;
        yinfo("mesh.reconcile: webapp '%s' -> pid=%d", webapp_name, spawn_res.value);
    } else {
        ywarn("mesh.reconcile: failed to spawn webapp '%s'", webapp_name);
    }
    return 0;
}

/* Spawn one named service if it exists in mesh.services. Used to bring the
 * discovery plane (registry, then portalloc) up before everything else. */
static struct picomesh_void_result reconcile_spawn_named(struct reconcile_ctx *rc, struct picomesh_engine *e,
                                  const char *name)
{
    char path[160];
    snprintf(path, sizeof(path), "mesh.services.%s", name);
    struct config_node_ptr_result r = config_get(picomesh_engine_config(e), path);
    PICOMESH_RETURN_IF_ERR(picomesh_void, r, "reconcile_spawn_named: service config read failed");
    if (!r.value || config_node_kind(r.value) != CONFIG_MAP) return PICOMESH_OK_VOID();
    struct picomesh_int_result spawn_res = mesh_internal_spawn(rc->obj, name);
    if (PICOMESH_IS_ERR(spawn_res)) {
        ywarn("mesh.reconcile: failed to spawn discovery-plane service '%s' (%s)", name,
              spawn_res.error.msg ? spawn_res.error.msg : "?");
        picomesh_error_destroy(spawn_res.error);
        return PICOMESH_OK_VOID();
    }
    if (spawn_res.value > 0) {
        rc->spawned++;
        yinfo("mesh.reconcile: '%s' (discovery plane) → pid=%d", name, spawn_res.value);
    } else {
        ywarn("mesh.reconcile: failed to spawn discovery-plane service '%s'", name);
    }
    return PICOMESH_OK_VOID();
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_reconcile_from_config")
struct picomesh_int_result mesh_mesh_reconcile_from_config_impl(struct ctx *ctx,
                                                              struct object *obj,
                                                              struct yheaders *hdrs)
{
    (void)ctx;
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(picomesh_int, "reconcile_from_config: no active engine");

    /* Own the lifecycle: install the SIGTERM/SIGINT reaper, take down any
     * children a previous run left alive, and record this parent's pid in
     * storage so the stack can be stopped by signalling it (no pkill). */
    mesh_reap_install();
    struct picomesh_void_result reap_res = mesh_reap_previous_run();
    PICOMESH_RETURN_IF_ERR(picomesh_int, reap_res, "reconcile_from_config: reap previous run failed");
    char parent_pid[16];
    snprintf(parent_pid, sizeof(parent_pid), "%d", (int)getpid());
    /* The parent pid MUST be durable: it is how the stack is stopped (signal
     * the recorded parent; no pkill). If we can't persist it, refuse to spawn
     * — a running stack with no recorded parent can't be cleanly taken down. */
    struct picomesh_void_result pp = mesh_storage_set("parent", parent_pid);
    if (PICOMESH_IS_ERR(pp))
        return PICOMESH_ERR(picomesh_int, "reconcile_from_config: cannot persist parent pid", pp);

    /* `mesh.services` — a map keyed by service name. */
    struct config_node_ptr_result r =
        config_get(picomesh_engine_config(e), "mesh.services");
    if (PICOMESH_IS_ERR(r) || !r.value) {
        return PICOMESH_ERR(picomesh_int, "reconcile_from_config: mesh.services missing");
    }
    if (config_node_kind(r.value) != CONFIG_MAP) {
        return PICOMESH_ERR(picomesh_int, "reconcile_from_config: mesh.services not a map");
    }
    struct reconcile_ctx rc = {.obj = obj, .spawned = 0};
    /* Discovery plane first: the registry (fixed address) so nodes can
     * register/discover, then portalloc so `port: auto` nodes can allocate.
     * Children retry-connect while these finish binding, so no barrier is
     * needed — just a head start. The walk then skips both. */
    struct picomesh_void_result registry_spawn = reconcile_spawn_named(&rc, e, "registry");
    PICOMESH_RETURN_IF_ERR(picomesh_int, registry_spawn, "reconcile_from_config: spawn registry");
    struct picomesh_void_result portalloc_spawn = reconcile_spawn_named(&rc, e, "portalloc");
    PICOMESH_RETURN_IF_ERR(picomesh_int, portalloc_spawn, "reconcile_from_config: spawn portalloc");
    config_node_for_each(r.value, reconcile_walk_cb, &rc);

    struct config_node_ptr_result wr = config_get(picomesh_engine_config(e), "mesh.webapps");
    if (PICOMESH_IS_OK(wr) && wr.value) {
        if (config_node_kind(wr.value) != CONFIG_MAP) {
            return PICOMESH_ERR(picomesh_int, "reconcile_from_config: mesh.webapps not a map");
        }
        config_node_for_each(wr.value, reconcile_webapp_walk_cb, &rc);
    }
    yinfo("mesh.reconcile_from_config: spawned %d children", rc.spawned);
    return PICOMESH_OK(picomesh_int, rc.spawned);
}

PICOMESH_CLASS_ANNOTATE("override@mesh:mesh:mesh_reconcile")
struct picomesh_int_result mesh_mesh_reconcile_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    /* No actual orchestration yet — placeholder for the spawn loop. */
    yinfo("mesh: reconcile (count=%zu)", ms(obj)->count);
    return PICOMESH_OK(picomesh_int, 1);
}

#include "store.gen.c"
