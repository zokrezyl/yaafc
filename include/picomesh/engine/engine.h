/* picomesh engine — lifecycle owner.
 *
 * Mirrors yaapp's `Yaapp` + `YaappEngine`. One process owns one
 * `struct picomesh_engine`. The engine owns:
 *
 *   - the loop event loop (libuv).
 *   - the per-process RPC server state (object handles, skel cache).
 *
 * Plugins do NOT register with the engine at runtime — they're
 * compiled in and their `__attribute__((constructor))` hooks (emitted
 * by codegen) install the class accessor + skel lookup into the
 * global registry before main() runs. The engine just sees them via
 * `class_by_name`. */

#ifndef PICOMESH_ENGINE_ENGINE_H
#define PICOMESH_ENGINE_ENGINE_H

#include <picomesh/core/result.h>
#include <picomesh/picoclass/class.h>

struct picomesh_engine;
struct loop;
struct config;
struct config_node;
struct argv_chain;

PICOMESH_RESULT_DECLARE(picomesh_engine_ptr, struct picomesh_engine *);

/* Engine construction inputs. NULL → defaults. */
struct picomesh_engine_args {
    /* Pre-parsed CLI chain (owned by caller before create, owned by
     * the engine after — engine destroys it on picomesh_engine_destroy). */
    struct argv_chain *cli;

    /* Explicit config file path (highest precedence). NULL → check
     * the CLI chain for --config-file. */
    const char *config_file;

    /* App name → drives ~/.config/<name>/<name>.yaml etc. Default
     * "picomesh". */
    const char *app_name;

    /* Skip filesystem search (XDG / git-root / cwd). */
    int no_filesystem_search;
};

/* Create + initialize. Sets up tracing, the RPC server state, the
 * loop, and loads config. `args` may be NULL — then the engine
 * loads config from defaults + the standard filesystem search.
 *
 * When `args->cli` is non-NULL, the engine reads its `--config-file`,
 * `--config K=V`, and `--env K=V` options to drive config. The CLI
 * chain becomes owned by the engine. */
struct picomesh_engine_ptr_result picomesh_engine_create(const struct picomesh_engine_args *args);

void picomesh_engine_destroy(struct picomesh_engine *e);

/* Run until something stops the loop (frontend's shutdown, signal,
 * picomesh_engine_stop). Runs the single worker-0 loop on the calling
 * thread — used by the non-`serve` paths (mesh parent control, tests).
 * For the multi-worker serve path use picomesh_engine_run_workers. */
struct picomesh_void_result picomesh_engine_run(struct picomesh_engine *e);

/* ---- in-process multithreaded workers (gh#6) ---------------------- */

/* Per-worker setup hook. Runs ON the worker's own thread, after the
 * worker's loop has been created and registered as that thread's
 * current worker — so picomesh_engine_loop()/_remote()/_add_remote() and
 * picomesh_engine_open_remotes() all resolve to THIS worker. A typical
 * setup opens the service's remotes and installs the frontend listener
 * on picomesh_engine_loop(e) (which binds with SO_REUSEPORT, so all N
 * workers share the same port). `worker_index` is 0 for the worker that
 * runs on the calling thread, 1..N-1 for the spawned threads. */
typedef struct picomesh_void_result (*picomesh_worker_setup_fn)(struct picomesh_engine *e,
                                                          int worker_index, void *ud);

/* Spin up `workers` worker threads (clamped to >= 1) in ONE process,
 * each owning its own libuv event loop AND its own libco coroutine
 * scheduler. Worker 0 runs on the calling thread; workers 1..N-1 each
 * run on a spawned pthread. Every worker runs `setup` on its own thread,
 * then runs its loop. All workers serve the same port via SO_REUSEPORT;
 * the kernel load-balances connections across them, and each connection
 * is pinned to one worker for its lifetime. Warm state is shared per
 * process where it is read-only (class registry, skel lookup chain);
 * mutable per-request state (coroutine scheduler, RPC handle table,
 * dependency-proxy cache, backend connections) is per worker.
 *
 * Blocks until worker 0's loop exits, then joins the others. Worker 0's
 * setup runs before any thread is spawned, so a worker-0 setup failure
 * is reported directly; a failure in a spawned worker's setup is logged
 * and that worker bows out while the rest keep serving. */
struct picomesh_void_result picomesh_engine_run_workers(struct picomesh_engine *e,
                                                  size_t workers,
                                                  picomesh_worker_setup_fn setup,
                                                  void *ud);

/* Ask the engine to wind down — best effort across all worker loops. */
void picomesh_engine_stop(struct picomesh_engine *e);

/* Start config-driven perf profiling (gh#14) for the CURRENT worker.
 * Reads the projected `perf` config subtree (service projection has
 * already promoted `mesh.services.<name>.config.perf` onto the root) and,
 * when `perf.enabled` is true, opens the configured Linux perf counters
 * against this worker's own thread, sampling them on this worker's loop.
 * `service` (may be NULL) only tags the log lines.
 *
 * Must run ON the worker thread (typically from the per-worker setup hook),
 * before the worker's loop starts. Returns Ok when profiling is off or was
 * opened successfully; returns an error when the config asked for profiling
 * but the kernel refused it (permission / unknown event) — the caller
 * decides whether that is fatal. The opened sampler is owned by the worker
 * and torn down at engine destroy. */
struct picomesh_void_result picomesh_engine_perf_start(struct picomesh_engine *e,
                                                       const char *service);

/* Borrow the loop for frontend code (loop_listen_tcp etc.). Returns
 * the CURRENT worker thread's loop (worker 0 on the main/bootstrap
 * thread). The engine retains ownership; do not destroy. */
struct loop *picomesh_engine_loop(struct picomesh_engine *e);

/* Plugins reach the engine via a process-global accessor — the driver
 * calls `picomesh_active_engine_set` after `picomesh_engine_create` and the
 * plugins read it via `picomesh_active_engine`. Mirrors the role of
 * `yaapp_engine` being passed into every `__init__`. NULL is a valid
 * read (no engine yet) — plugins must handle it. */
void picomesh_active_engine_set(struct picomesh_engine *e);
struct picomesh_engine *picomesh_active_engine(void);

/* The engine's config tree. Owned by the engine; do not destroy. */
const struct config *picomesh_engine_config(struct picomesh_engine *e);

/* Per-worker cached state for a client-facing frontend's security pipeline
 * (the authenticator chain + authorizer). A frontend builds the pipeline once
 * on the worker's first gated request and stashes it here; subsequent requests
 * on the same worker reuse it. The slot is per-worker (thread-confined), so no
 * locking is needed. `set` stores `ptr` with `free_fn`, invoked at engine
 * destroy (and when replacing an existing value). `get` returns NULL if unset. */
void *picomesh_engine_worker_security(struct picomesh_engine *e);
void picomesh_engine_worker_set_security(struct picomesh_engine *e, void *ptr,
                                         void (*free_fn)(void *));

/* Plugin shortcut: return the top-level `<plugin>` subtree, or NULL.
 * Mirrors `yaapp_engine.get_config('<plugin>')`. */
const struct config_node *picomesh_engine_plugin_config(struct picomesh_engine *e, const char *plugin);

/* CLI chain — engine owns it, may be NULL. */
struct argv_chain *picomesh_engine_cli(struct picomesh_engine *e);

/* ---- inter-plugin RPC clients (the "remotes:" config edge) -------- */

/* Open a TCP RPC client connection to `host:port` and register it
 * under `name`, on the CURRENT worker thread. Each worker owns its own
 * backend connection set (the async client is bound to the worker's
 * loop), so this registers against the calling worker only. Subsequent
 * `picomesh_engine_remote(e, name)` on that same worker returns the
 * session. The engine owns every worker's sessions and destroys them at
 * shutdown. `host` defaults to 127.0.0.1 when NULL.
 *
 * Idempotent: a second call with the same name closes the previous
 * session before opening the new one — useful for reconnect logic. */
struct picomesh_void_result picomesh_engine_add_remote(struct picomesh_engine *e,
                                                  const char *name,
                                                  const char *host, int port);

/* As above, but `transport` selects the outbound wire: NULL/"yrpc" = native
 * async binary RPC; "msgpack" = async MessagePack-envelope transport, for a
 * foreign service reached via `remotes: [{transport: msgpack}]` (issue #22). */
struct picomesh_void_result picomesh_engine_add_remote_transport(struct picomesh_engine *e,
                                                                 const char *name, const char *host,
                                                                 int port, const char *transport);

/* Borrow the current worker's session registered under `name`, or NULL
 * if unknown. The session stays owned by the engine. */
struct peer_channel *picomesh_engine_remote(struct picomesh_engine *e, const char *name);

/* Auto-dispatch helper: produce a `struct ctx` for talking to `service`.
 * If the engine has a registered remote with that name, `ctx.peer`
 * is filled in — generated method stubs will RPC. If not (e.g. the
 * service lives in this same process), `ctx.peer` stays NULL and
 * the generated stubs dispatch locally. Either way callers write the
 * same code:
 *
 *     struct ctx c = picomesh_engine_service_ctx(e, "storage");
 *     struct object_ptr_result o = storage_db_create(&c);
 *
 * which removes the manual `c.peer = picomesh_engine_remote(...)`
 * boilerplate at every call site (gh#2). */
struct ctx picomesh_engine_service_ctx(struct picomesh_engine *e, const char *service);

/* Resolve a backend service's bind address from config
 * (mesh.services.<svc>.host/port). Returns 1 and fills host_out/port_out
 * on success, 0 otherwise. Used to open a plain blocking connection to a
 * backend from a worker-pool thread (where the loop-bound async mux can't
 * be used). */
struct picomesh_int_result picomesh_engine_service_addr(struct picomesh_engine *e, const char *service,
                              char *host_out, size_t host_cap, int *port_out);

/* Walk the engine's config for `<plugin>.remotes.<idx>.{service,host,port}`
 * entries and call `picomesh_engine_add_remote` for each. Returns the
 * number of remotes opened. Silently skips entries that have no
 * `service:` field or that fail to connect (logged via ywarn).
 *
 * If `host`/`port` are absent in the config, the function looks up
 * `<service>.bind.host` / `<service>.bind.port` at the config root —
 * matching the scenario YAML's `mesh.services.<svc>.host/port` shape. */
struct picomesh_size_result picomesh_engine_open_remotes(struct picomesh_engine *e, const char *plugin);

/* ---- resolved-remote side table (port: auto discovery) ----------- *
 *
 * A node that consumes a remote with `port: auto` cannot read the
 * producer's port from config — it discovers it through the registry at
 * boot. The discovery (an RPC) is driven once on the main thread by the
 * driver, which records each (name → host:port) here BEFORE the worker
 * threads spin. `picomesh_engine_open_remotes` then consults this table
 * for any remote that lacks an explicit port. The table is write-once at
 * bootstrap and read-only thereafter, so worker reads need no locking. */
void picomesh_engine_set_resolved_remote(struct picomesh_engine *e, const char *name,
                                         const char *host, int port);
int picomesh_engine_get_resolved_remote(struct picomesh_engine *e, const char *name,
                                        char *host_out, size_t host_cap, int *port_out);

/* Iterate every registered plugin class. The engine walks the class
 * registry's accessor-lookup chain by name pattern — by default,
 * everything registered as a `class@*:*` annotation. Returns names
 * suitable for passing to `class_by_name`. */
struct picomesh_void_result picomesh_engine_for_each_plugin(struct picomesh_engine *e,
                                                     void (*cb)(const char *qname, void *ud),
                                                     void *ud);

#endif /* PICOMESH_ENGINE_ENGINE_H */
