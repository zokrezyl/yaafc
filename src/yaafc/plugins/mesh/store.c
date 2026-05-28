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

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yengine/engine.h>
#include <yaafc/yloop/yloop.h>
#include <yaafc/yconfig/yconfig.h>
#include <yaafc/yargv/yargv.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MESH_MAX_SERVICES 64
#define MESH_MAX_CHILDREN 32

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

struct YAAFC_CLASS_ANNOTATE("class@mesh:store") mesh_store_data {
    struct mesh_service_entry entries[MESH_MAX_SERVICES];
    struct mesh_child_entry children[MESH_MAX_CHILDREN];
    size_t count;
    size_t child_count;
};

static struct mesh_store_data *ms(struct object *obj)
{
    return (struct mesh_store_data *)((char *)obj + sizeof(struct object));
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_register_service")
struct yaafc_int_result mesh_store_register_service_impl(struct ctx *ctx, struct object *obj,
                                                         uint32_t service_id, uint32_t port)
{
    (void)ctx;
    struct mesh_store_data *d = ms(obj);
    /* idempotent: re-registering same id updates the port */
    for (size_t i = 0; i < MESH_MAX_SERVICES; ++i) {
        if (d->entries[i].used && d->entries[i].service_id == service_id) {
            d->entries[i].port = port;
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    for (size_t i = 0; i < MESH_MAX_SERVICES; ++i) {
        if (!d->entries[i].used) {
            d->entries[i].service_id = service_id;
            d->entries[i].port = port;
            d->entries[i].used = 1;
            d->count++;
            yinfo("mesh: registered service %u on port %u", service_id, port);
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    return YAAFC_ERR(yaafc_int, "mesh_register_service: table full");
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_resolve")
struct yaafc_uint32_result mesh_store_resolve_impl(struct ctx *ctx, struct object *obj,
                                                   uint32_t service_id)
{
    (void)ctx;
    struct mesh_store_data *d = ms(obj);
    for (size_t i = 0; i < MESH_MAX_SERVICES; ++i) {
        if (d->entries[i].used && d->entries[i].service_id == service_id) {
            return YAAFC_OK(yaafc_uint32, d->entries[i].port);
        }
    }
    return YAAFC_OK(yaafc_uint32, 0);
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_forget")
struct yaafc_int_result mesh_store_forget_impl(struct ctx *ctx, struct object *obj,
                                               uint32_t service_id)
{
    (void)ctx;
    struct mesh_store_data *d = ms(obj);
    for (size_t i = 0; i < MESH_MAX_SERVICES; ++i) {
        if (d->entries[i].used && d->entries[i].service_id == service_id) {
            d->entries[i].used = 0;
            d->count--;
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    return YAAFC_OK(yaafc_int, 0);
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_count_services")
struct yaafc_size_result mesh_store_count_services_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    return YAAFC_OK(yaafc_size, ms(obj)->count);
}

/* uv_spawn-based subprocess management:
 *   spawn_yaafc(port)   → child pid (0 on failure)
 *   kill_pid(pid)       → 1 ok / 0 unknown
 *   count_children      → live (not-yet-reaped) child count
 *
 * spawn_yaafc(port) launches a fresh `<self> --frontend yttp --host
 * 127.0.0.1 --port <port> serve`. The child inherits stdin/stdout/
 * stderr from the parent so its trace lands in the parent's terminal.
 *
 * The exit callback flips the matching `exited` flag so kill_pid /
 * count_children stay accurate without a reap step.
 *
 * Path discovery uses `/proc/self/exe` on Linux. On macOS we'd use
 * `_NSGetExecutablePath` — TODO for the platform shim. */

static int find_child_slot(struct mesh_store_data *d, int32_t pid)
{
    for (int i = 0; i < MESH_MAX_CHILDREN; ++i) {
        if (!d->children[i].exited && d->children[i].pid == pid) return i;
    }
    return -1;
}

static void mesh_child_exit_cb(struct yloop_process *p, int64_t exit_status,
                               int term_signal, void *ud)
{
    (void)p; (void)term_signal;
    struct object *obj = ud;
    if (!obj) return;
    struct mesh_store_data *d = ms(obj);
    /* libuv only knows the uv_process_t pointer; the pid we stashed
     * at spawn time matches the slot. Sweep for any non-exited child
     * whose pid matches the captured exit. Since uv passes us the
     * yloop_process but not the pid directly, we walk all live
     * children and mark the first one matching this callback chain
     * — limitation: assumes one child exits per cb (true), and pid
     * uniqueness in the window. Good enough. */
    /* We can't recover the pid from `p` here without leaking the
     * yloop_process internals, so we use `ud` to carry both obj +
     * pid in a pair. */
    (void)d; (void)exit_status;
}

struct mesh_child_cookie {
    struct object *obj;
    int32_t pid;
};

static void mesh_child_exit_cb_real(struct yloop_process *p, int64_t exit_status,
                                    int term_signal, void *ud)
{
    (void)p; (void)term_signal;
    struct mesh_child_cookie *c = ud;
    if (!c) return;
    struct mesh_store_data *d = ms(c->obj);
    int slot = find_child_slot(d, c->pid);
    if (slot >= 0) {
        d->children[slot].exited = 1;
        d->children[slot].exit_status = (int)exit_status;
        d->child_count--;
        yinfo("mesh: child pid=%d exited (status=%d)", c->pid, (int)exit_status);
    }
    free(c);
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_spawn_yaafc")
struct yaafc_int_result mesh_store_spawn_yaafc_impl(struct ctx *ctx, struct object *obj,
                                                    uint32_t port)
{
    (void)ctx;
    struct mesh_store_data *d = ms(obj);
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return YAAFC_ERR(yaafc_int, "spawn_yaafc: no active engine");

    int slot = -1;
    for (int i = 0; i < MESH_MAX_CHILDREN; ++i) {
        if (d->children[i].exited || d->children[i].pid == 0) { slot = i; break; }
    }
    if (slot < 0) return YAAFC_ERR(yaafc_int, "spawn_yaafc: child table full");

    /* /proc/self/exe is Linux-only; macOS needs _NSGetExecutablePath
     * (TODO via yplatform once a Mac build is exercised). */
    char self_exe[4096];
    ssize_t n = readlink("/proc/self/exe", self_exe, sizeof(self_exe) - 1);
    if (n <= 0) return YAAFC_ERR(yaafc_int, "spawn_yaafc: readlink(/proc/self/exe) failed");
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
    if (!c) return YAAFC_ERR(yaafc_int, "spawn_yaafc: calloc(cookie) failed");
    c->obj = obj;

    int pid = yloop_spawn(yaafc_engine_loop(e), self_exe, argv,
                          mesh_child_exit_cb_real, c);
    if (pid <= 0) {
        free(c);
        return YAAFC_ERR(yaafc_int, "spawn_yaafc: yloop_spawn failed");
    }
    c->pid = pid;
    d->children[slot].pid = pid;
    d->children[slot].port = port;
    d->children[slot].exited = 0;
    d->children[slot].exit_status = 0;
    d->child_count++;
    yinfo("mesh: spawned pid=%d on port=%u", pid, port);
    return YAAFC_OK(yaafc_int, pid);
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_kill_pid")
struct yaafc_int_result mesh_store_kill_pid_impl(struct ctx *ctx, struct object *obj,
                                                 int32_t pid)
{
    (void)ctx;
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return YAAFC_ERR(yaafc_int, "kill_pid: no active engine");
    struct mesh_store_data *d = ms(obj);
    if (find_child_slot(d, pid) < 0) {
        return YAAFC_OK(yaafc_int, 0);
    }
    int rc = yloop_kill(yaafc_engine_loop(e), pid, SIGTERM);
    return YAAFC_OK(yaafc_int, rc == 0 ? 1 : 0);
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_count_children")
struct yaafc_size_result mesh_store_count_children_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    return YAAFC_OK(yaafc_size, ms(obj)->child_count);
}

/* Spawn one child yaafc process for service `name`. The child's
 * argv is IDENTICAL to what a human would type by hand:
 *
 *   yaafc --config-file <yaml> --name <service> --frontend yttp serve
 *
 * The child's `--name` drives every config lookup the engine does on
 * its own: bind port (mesh.services.<name>.port), bind host, AND
 * mesh.services.<name>.config.remotes[] (opened automatically by
 * `cmd_serve` before the frontend listens). Reconcile passes ONLY
 * the name and inherits the parent's --config-file. */
static int mesh_internal_spawn(struct object *obj, const char *name)
{
    if (!name || !*name) return 0;
    struct mesh_store_data *d = ms(obj);
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return 0;
    int slot = -1;
    for (int i = 0; i < MESH_MAX_CHILDREN; ++i) {
        if (d->children[i].exited || d->children[i].pid == 0) { slot = i; break; }
    }
    if (slot < 0) return 0;
    char self_exe[4096];
    ssize_t n = readlink("/proc/self/exe", self_exe, sizeof(self_exe) - 1);
    if (n <= 0) return 0;
    self_exe[n] = 0;

    const char *cfg = NULL;
    if (yaafc_engine_cli(e)) {
        cfg = yargv_get_string(yaafc_engine_cli(e), "config_file", NULL);
    }
    if (!cfg) {
        ywarn("mesh.spawn: parent has no --config-file — child won't know its config");
        return 0;
    }

    /* Per CLAUDE.md: backends listen yrpc by default (binary, fast,
     * the perf-critical inter-service path). Only the gateway opts
     * into yhttp via an explicit `frontend: yhttp` in its yaml block.
     * yrpc on every backend is the throughput invariant — do not
     * change the default without updating CLAUDE.md. */
    char fe_path[256];
    snprintf(fe_path, sizeof(fe_path), "mesh.services.%s.frontend", name);
    const char *frontend = "yrpc";
    struct yconfig_node_ptr_result fr = yconfig_get(yaafc_engine_config(e), fe_path);
    if (YAAFC_IS_OK(fr) && fr.value) {
        const char *s = yconfig_node_as_string(fr.value, NULL);
        if (s && *s) frontend = s;
    }

    /* `cfg`, `name`, and `frontend` come from the parent's CLI/config
     * and outlive the spawn call, so dropping them into argv[] is safe. */
    char *argv[] = {
        self_exe,
        (char *)"--config-file", (char *)cfg,
        (char *)"--name",        (char *)name,
        (char *)"--frontend",    (char *)frontend,
        (char *)"serve",
        NULL,
    };

    struct mesh_child_cookie *c = calloc(1, sizeof(*c));
    if (!c) return 0;
    c->obj = obj;
    int pid = yloop_spawn(yaafc_engine_loop(e), self_exe, argv,
                          mesh_child_exit_cb_real, c);
    if (pid <= 0) { free(c); return 0; }
    c->pid = pid;
    d->children[slot].pid = pid;
    d->children[slot].port = 0; /* port is in the YAML; child resolves */
    d->children[slot].exited = 0;
    d->child_count++;
    yinfo("mesh: spawned pid=%d service='%s'", pid, name);
    return pid;
}

/* Walk callback for the services map. Each iteration sees one
 * (service_name, service_node) pair — we always spawn a child for
 * each service (the child looks up its own port/host/remotes by name
 * via the inherited --config-file). */
struct reconcile_ctx {
    struct object *obj;
    int spawned;
};

static int reconcile_walk_cb(const char *service_name,
                             const struct yconfig_node *val, void *ud)
{
    struct reconcile_ctx *rc = ud;
    if (yconfig_node_kind(val) != YCONFIG_MAP) return 0;

    int pid = mesh_internal_spawn(rc->obj, service_name);
    if (pid > 0) {
        rc->spawned++;
        yinfo("mesh.reconcile: '%s' → pid=%d", service_name, pid);
    } else {
        ywarn("mesh.reconcile: failed to spawn '%s'", service_name);
    }
    return 0;
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_reconcile_from_config")
struct yaafc_int_result mesh_store_reconcile_from_config_impl(struct ctx *ctx,
                                                              struct object *obj)
{
    (void)ctx;
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return YAAFC_ERR(yaafc_int, "reconcile_from_config: no active engine");

    /* `mesh.services` — a map keyed by service name. */
    struct yconfig_node_ptr_result r =
        yconfig_get(yaafc_engine_config(e), "mesh.services");
    if (YAAFC_IS_ERR(r) || !r.value) {
        return YAAFC_ERR(yaafc_int, "reconcile_from_config: mesh.services missing");
    }
    if (yconfig_node_kind(r.value) != YCONFIG_MAP) {
        return YAAFC_ERR(yaafc_int, "reconcile_from_config: mesh.services not a map");
    }
    struct reconcile_ctx rc = {.obj = obj, .spawned = 0};
    yconfig_node_for_each(r.value, reconcile_walk_cb, &rc);
    yinfo("mesh.reconcile_from_config: spawned %d services", rc.spawned);
    return YAAFC_OK(yaafc_int, rc.spawned);
}

YAAFC_CLASS_ANNOTATE("override@mesh:store:store_reconcile")
struct yaafc_int_result mesh_store_reconcile_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    /* No actual orchestration yet — placeholder for the spawn loop. */
    yinfo("mesh: reconcile (count=%zu)", ms(obj)->count);
    return YAAFC_OK(yaafc_int, 1);
}

#include "store.gen.c"
