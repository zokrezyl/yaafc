/* autoport — the `port: auto` machinery driven from the serve path.
 *
 * A node with `port: auto` allocates its listen port through `portalloc`
 * (discovered via the `registry`), registers itself in the registry so
 * consumers can find it, and discovers any remote it consumes whose port
 * is `auto`. All of this runs ONCE on the main thread before the worker
 * threads spin, over short-lived synchronous RPC channels — the same wire
 * the `client` subcommand uses, which needs no running event loop.
 *
 * Only the `registry` and the mesh control parent carry fixed addresses;
 * the registry address is injected into every node's config as a global
 * `registry: {host, port}` block by the mesh parent at spawn. */
#ifndef PICOMESH_MAIN_AUTOPORT_H
#define PICOMESH_MAIN_AUTOPORT_H

#include <picomesh/engine/engine.h>

/* True iff this node's own bind port is configured as the string "auto"
 * (and not overridden by an explicit numeric --port on the CLI). */
struct picomesh_int_result picomesh_serve_port_is_auto(struct picomesh_engine *engine, const char *name);

/* Allocate this node's listen port via portalloc. portalloc is reached
 * locally when THIS node provides it (bootstrap), otherwise discovered
 * through the registry. The returned port has been bind-probed by this
 * node; returns <= 0 only if allocation failed outright. `host` is the
 * node's bind host (probed for real availability). */
struct picomesh_int_result picomesh_autoport_allocate(struct picomesh_engine *engine, const char *name,
                               const char *host);

/* Register (name -> host:port) in the registry so `port: auto` consumers
 * can discover this node. Best-effort: a failure is logged, not fatal.
 * No-op when `name` is empty or no registry is configured. */
struct picomesh_void_result picomesh_autoport_register_self(struct picomesh_engine *engine, const char *name,
                                     const char *host, int port);

/* Discover the address of every remote this node consumes with `port: auto`
 * and record it in the engine's resolved-remote table (consumed by
 * picomesh_engine_open_remotes). No-op when no registry is configured. */
struct picomesh_void_result picomesh_autoport_resolve_remotes(struct picomesh_engine *engine, const char *name);

#endif /* PICOMESH_MAIN_AUTOPORT_H */
