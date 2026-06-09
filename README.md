# picomesh — yet another application framework in C

A C port of [yaapp](../yaapp) (Python).  yaapp's pitch is "decorate any
object with `@expose`; it becomes callable over CLI, HTTP, JSON-RPC,
etc.".  picomesh keeps the same idea but in C, with:

- **Annotated source as source of truth** — you mark a struct as a class
  and one or more functions as method overrides with
  `[[clang::annotate("class@<domain>:<name>")]]` /
  `override@<domain>:<class>:<slot>`.
- **Generated public stubs + RPC skels** — `gen/codegen.py` reads the
  annotated `.c`, parses it with `clang -ast-dump=json`, and emits the
  public method stubs, the class accessor, and the RPC skel/lookup
  tables.  No hand-written boilerplate.
- **Result-typed everything** — every fallible entry point returns a
  `struct *_result` with an `ok` flag and an error chain.  Same shape
  the yetty PoC defined.
- **libco-backed coroutines** — `picomesh_coro_spawn / yield / resume`
  thin wrapper around higan-emu/libco.
- **libuv-backed event loop with coroutine streams** — `loop_listen_tcp`
  accepts connections, spawns one coroutine per peer, and provides
  `loop_read / loop_write` that look blocking but yield on EAGAIN.
- **Transparent RPC** — the same generated stub dispatches locally when
  `ctx->session == NULL` and falls through to `rpc_call(RPC_OP_CALL, …)`
  when a session is attached.  Caller code is identical either way.

The class/RPC model is lifted from
`yetty-optimize-ygui/poc/class-object-model/`.  The coroutine layer is
adapted from the same project's `picoco/` + `platform/coroutine/`.

## Build

```
make build-desktop-release
```

The Makefile wraps cmake/ninja.  3rd-party deps (libco, libuv) are built
locally from source on first configure by
`build-tools/3rdparty/<lib>/_build.sh` — no system packages required, no
network beyond fetching upstream tarballs into
`~/.cache/picomesh-3rdparty/`.

To add another dep:

1. `mkdir build-tools/3rdparty/<libname>/`
2. drop a `version` (upstream tag / commit) and an `_build.sh` (compiles
   for `$TARGET_PLATFORM`, writes `<libname>-<platform>-<version>.tar.gz`
   into `$OUTPUT_DIR` with `lib/lib<n>.a` + `include/...`).
3. add `build-tools/picomesh/libs/<libname>.cmake` that calls
   `picomesh_3rdparty_fetch(<libname> _DIR)` and creates an IMPORTED static
   target.

## Try it

```
# Single driver binary with every linked-in plugin exposed.
./build-desktop-release/picomesh serve  --port 7777
./build-desktop-release/picomesh client --port 7777
```

Run two clients in parallel against the same server to see the
server-side trace timeline interleave their requests — one coroutine per
peer, all on the libuv loop thread.

## Layout

```
build-tools/
  3rdparty/<lib>/{version,_build.sh}   per-lib upstream build recipe
  picomesh/3rdparty-fetch.cmake           generic "build-and-install" helper
  picomesh/libs/<lib>.cmake               per-lib IMPORTED target stub

include/picomesh/
  core/{result,ytrace}.h              error chain + switchable trace
  picoclass/{class,rpc}.h                 class registry + dispatch + RPC wire
  picoco/coro.h                           libco-backed coroutines
  loop/loop.h                        libuv event loop + coro streams
  engine/engine.h                     engine lifecycle (init/run/stop)
  platform/time.h                     cross-platform time + sleep
  frontends/yrpc/yrpc.h                binary RPC frontend

src/picomesh/
  core/ picoclass/ picoco/ loop/           runtime implementation
  engine/                             engine lifecycle
  platform/time/{posix,windows}.c     per-platform backends
  frontends/yrpc/                      yrpc frontend
  plugins/
    storage/    — KV store: kv_set / kv_get / kv_count
    calculator/ — add / sub / mul / div (div-by-zero → Result error)
    accounts/   — register / set_balance / balance / count
    time/       — now_ms / sleep_ms (coroutine-yielding via uv_timer_t)

gen/codegen.py                         annotated-C → public stubs / skels
bin/picomesh_main.c                       driver binary (serve | client)
include/uthash/                        vendored uthash header
include/picohttpparser/                vendored picohttpparser header
src/picohttpparser/                    vendored picohttpparser source
```

## Adding a plugin

Drop a new directory under `src/picomesh/plugins/<name>/`. The CMake
helper `picomesh_add_plugin(<name> SOURCES <file>.c)` (in the top-level
CMakeLists.txt) reads the annotated sources, runs the codegen, and
emits an OBJECT library `picomesh_plugin_<name>`. Add it to the
`picomesh` executable's `$<TARGET_OBJECTS:…>` list and the constructor
hooks installed by codegen register the plugin's classes with the
runtime before `main()` runs.

The annotated source uses C23 attributes that map onto the codegen's
schema (`class@`, `override@`, `parent@`, `uses@`):

```c
struct [[clang::annotate("class@<plugin>:<class>")]] data { … };

[[clang::annotate("override@<plugin>:<class>:<slot>")]]
struct picomesh_<retty>_result <plugin>_<class>_<slot>_impl(
    struct ctx *ctx, struct object *obj, <args>) { … }

#include "<class>.gen.c"  /* slot table + class accessor */
```

The codegen invariant is that the *file stem must equal the class
name* — multiple classes per file are not supported yet.

## Configuration

The engine loads `config` at startup. Precedence matches yaapp's
`Config.create(defaults, path)` exactly — lowest to highest:

1. `--config KEY=VALUE` CLI overrides (lowest; treated as *defaults*).
2. `$HOME/.config/<app>/<app>.yaml` (XDG).
3. `<git-repo-root>/<app>.yaml`.
4. `./<app>.yaml`.
5. `--config-file PATH` (highest).

Every layer is deep-merged into the previous one (nested maps merge,
scalars + lists replace). After merging, every string value gets
`${VAR}` / `${VAR:default}` environment-variable substitution applied
recursively.

CLI shape (mirrors yaapp's `option_defs` / `CmdChain`):

```
picomesh [--config-file PATH] [--config KEY=VALUE]... [--env KEY=VALUE]...
      [--host H] [--port P] [--verbose] [--app-name N]
      (serve | client | config-dump)
```

`--env KEY=VALUE` is applied as `setenv()` before config runs, so any
`${KEY}` substitutions in config strings pick them up.

`picomesh config-dump` prints the fully-resolved config tree — handy for
debugging precedence.

Plugins read their own subtree:

```c
const struct config_node *cfg = picomesh_engine_plugin_config(e, "storage");
const char *backend = config_node_as_string(
    map_get(cfg, "backend"), "memory");
```

Dot-path access with parent-chain inheritance works too:

```c
struct config_node_ptr_result r = config_get(cfg, "storage.host");
/* falls back to top-level `host` when `storage.host` is absent */
```

## Status

What's in:

- Foundation runtime (Result, ytrace, class/RPC, coroutines, loop).
- Cross-platform time (`platform/time`) with POSIX + Win32 backends.
- Codegen: classes, mixins, inheritance, same-domain and cross-domain
  overrides.
- TCP RPC server using libuv + per-connection coroutines.
- Single driver binary (`picomesh serve|client|config-dump`) that links
  four plugins end-to-end (storage, calculator, accounts, time) and
  exercises each one over the wire.
- Coroutine-yielding `time.sleep_ms` — the libuv loop keeps servicing
  other peers while the calling coro is parked.
- YAML config (libyaml-backed) with full yaapp-style precedence,
  env-var substitution, dotted-path lookup, parent-chain inheritance,
  and `--config KEY=VAL` / `--config-file` / `--env` CLI overrides.
- Three frontends:
  - `yrpc` — binary packed-header RPC over TCP (per-connection coro).
  - `yttp` — JSON-RPC 2.0 over TCP, Content-Length framed (LSP/MCP
    shape). Methods: `create`, `invoke`, `describe`. Parsed via
    simdjson; responses built via a tiny hand-rolled writer.
  - `cli` — `picomesh invoke <plugin>_<class>_<method> [args...]` runs
    locally on a fresh instance, prints the JSON-encoded result.
- Codegen emits a per-method JSON invoker alongside the binary skel;
  the same impl is callable from yrpc, yttp, and cli with no extra
  plumbing.
- **String args on the wire**: codegen emits `[u32 len | bytes]` for
  `const char *` parameters; existing scalar-only methods stay
  byte-compatible.
- **SQLite-backed storage** (`storage_sql` class): set/get/del/exists/
  count with string keys + int64 values, persisted to the path from
  config (`storage.db_path`). libsqlite3 is built locally via the
  3rdparty pattern.
- **Inter-plugin RPC clients**: `picomesh_engine_add_remote(name, host,
  port)` and `picomesh_engine_remote(name)`; auto-open via
  `picomesh_engine_open_remotes(plugin)` which walks `mesh.services.
  <plugin>.config.remotes[]` from the scenario YAML.
- **uv_spawn-based subprocess management** (`mesh` plugin): the parent
  can `spawn_picomesh(port)` to fork another picomesh serving on a different
  port, `kill_pid(pid)` to take it down, `count_children` to introspect.

What isn't yet (next):
- Plugin discovery + lifecycle hooks (the `add_plugin` / lifecycle
  surface of `yaapp.Yaapp`).
- Windows backend for libuv build (libuv supports it; the
  `_build.sh` script doesn't drive the MSVC path yet).
