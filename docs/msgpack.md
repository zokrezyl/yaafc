# picomesh MessagePack RPC — implementation status

Tracks GitHub issue **#17** (refines **#9**). This is the working status /
handoff document; the canonical wire contract is
[`docs/msgpack-rpc.md`](./msgpack-rpc.md).

**Goal:** let services in any language talk to picomesh, and let picomesh C
call foreign services, over a MessagePack wire — **without FFI / C-ABI
binding**. It is wire RPC (msgpack over TCP): decoupled, cross-process,
language-agnostic. The single source of truth is the codegen model
(`model.yaml`), which is both a codegen **output** (from annotated C) and a
hand-authorable **input** (an IDL).

## Pipeline — one source of truth

```
annotated C (calc.c, [[clang::annotate]])
   │  src/picomesh/yclass/gen/codegen.py   (clang AST)
   ▼
model.yaml   ← language-neutral IR, regenerated every build
   │                                         \
   │  codegen.py (C glue)                      \  tools/msgpack-codegen/gen.py
   ▼                                            ▼
C skel + C client (rpc/methods.gen.c)         foreign-language CLIENTS
                                              (python / go / rust / cpp / lua)
```

`model.yaml` is the interchange. The C codegen needs clang; the polyglot
generators need only the YAML — no clang, no C sources.

---

## Implemented

| Part | What | Status |
|---|---|---|
| 0 | Shared active-service resolver + gate | ✅ verified |
| 1a | Vendored cmp codec + adapter | ✅ builds |
| 1b | Codegen `minvoke` (inbound dispatch) | ✅ builds |
| 1c | Inbound `msgpack` frontend | ✅ verified |
| 1d | Reference client + inbound smoke | ✅ **9/9** |
| 2 | Outbound transport, C **caller** side | ✅ **3/3** |
| 3 | Polyglot **client** generators + docs | ✅ py e2e, go/cpp compile |

### Part 0 — shared active-service resolver

- `include/picomesh/yengine/resolve.h`, `src/picomesh/yengine/resolve.c`
  - `picomesh_resolve_service_call(engine, "service.class.method")` — gates:
    the service must be an activated plugin **or** a configured remote on this
    node (registration == activation) **before** any class/method lookup; never
    reaches a global method table ungated. Returns `{ctx, obj (acquired),
    class_qname, method_qname, params}`.
  - `picomesh_service_call_release(...)`.
- `src/picomesh/frontends/yhttp/frontend.c` — `route_json_rpc()` refactored
  onto the resolver (dropped the local `path_to_qnames` + raw object-create +
  `jinvoke_for`). This **also closed the pre-existing global-exposure hole** in
  the gateway `/_rpc`.
- **Verified:** collocated `yhttp.serve_app` + calculator on `:8099` —
  `calculator.calc.add(2,3)` → 200 `{result:5}`; `nope.calc.add` → 404
  `service_not_active`; malformed → 400; unknown method → 404.

#### Resolver scope: service-path vs object-handle transports

The active-service resolver gate applies **only** to inbound transports that
accept a `service.class.method` **path** — today the gateway `/_rpc` and the
`msgpack` frontend. Those are the boundaries where an ungated path could reach
a globally-registered class, so the gate lives there.

`yttp` and `cli` are **object-handle dispatchers**: a client first creates (or
is handed) an object handle and then calls ops against that handle. They do not
resolve a dotted service path, so the same service-path gate does not map onto
them — this is a deliberate scope boundary, not an omission. Earlier wording
(gh#17) that listed `yttp`/`cli` alongside the service-path transports referred
to the shared *dispatch* plumbing, not the service-path gate. If `yttp`/`cli`
ever grow a service-path entry point, they must adopt `picomesh_resolve_service_call`
at that point.

### Part 1a — vendored codec

- `vendor/cmp/{cmp.c,cmp.h,LICENSE}` — cmp (MIT, single-file, zero-alloc).
- `include/picomesh/msgpack/msgpack.h`, `src/picomesh/msgpack/msgpack.c` —
  fixed-buffer adapter over cmp (`picomesh_msgpack_{reader,writer}_init`).
- CMake: `cmp.c` + `msgpack.c` in `picomesh_runtime`; `vendor/cmp` as a SYSTEM
  include; `cmp.c` compiled `-w`. The native `yrpc` path is untouched.

### Part 1b — codegen minvoke (inbound dispatch)

- `include/picomesh/yclass/minvoke.h`, `src/picomesh/yclass/minvoke.c` —
  `minvoke_fn` / `minvoke_for` / `minvoke_add_lookup`, the msgpack twin of
  jinvoke.
- `src/picomesh/yclass/gen/codegen.py`: `emit_minvoke` / `emit_munpack_arg` /
  `emit_minvoke_write_result` / `emit_minvoke_table` — per method, decode
  positional msgpack args (with per-type width/signedness/range checks) → call
  the public stub → encode the result. Emitted into every plugin's
  `rpc.gen.c`.

### Part 1c — inbound msgpack frontend

- `include/picomesh/frontends/msgpack/msgpack.h`,
  `src/picomesh/frontends/msgpack/msgpack.c` — `--frontend msgpack` (default
  port 7900). Strict serial, `u32` big-endian length prefix, 1 MiB cap.
  Decodes the `{v,op,path,args,kwargs,headers}` envelope, resolves via
  `picomesh_resolve_service_call`, dispatches via `minvoke_for`, encodes
  `{v,ok,result|error}`. Implements `op=invoke` + a minimal `op=describe`.
- `ymain.c`: `--frontend msgpack` wired into the serve dispatch + validation +
  usage.

### Part 1d — reference client + inbound smoke

- `tools/msgpack-client/picomesh_msgpack.py` — reference client.
- `tools/picoforge/msgpack-smoke.sh` — **9/9**: add/mul, gate
  (`service_not_active`), `no_such_method`, `kwargs_unsupported`, bad-arg-type
  (`call_error`), `frame_too_large`, `describe`. Python's **reference msgpack
  lib** round-trips against our cmp codec.

### Part 2 — outbound transport (C → foreign), caller side

- `src/picomesh/yclass/rpc.h` (decls); `src/picomesh/yclass/rpc.c`:
  `RPC_MODE_MSGPACK`; `peer_channel_create_msgpack(fd)`;
  `peer_channel_is_msgpack(s)`; `peer_channel_msgpack_call(...)` — wraps a
  pre-encoded args array in the envelope, big-endian length-frames it, blocking
  round-trip, returns the response's `result` value bytes.
- `src/picomesh/yclass/gen/codegen.py`: `emit_msgpack_remote_branch` /
  `emit_mpack_write_arg` / `emit_mpack_read_result` / `dotted_path` — when
  `ctx->peer` is a msgpack channel, the `methods.gen.c` stub encodes args,
  calls `peer_channel_msgpack_call`, decodes the result.
- `ymain.c`: `client --transport msgpack`.
- `tools/msgpack-client/calculator_server_impl.py` — foreign service built on the
  **generated** server skeleton (only the four arithmetic bodies are hand-written;
  `tools/msgpack-client/echo_server.py` is kept as the fully-hand-written
  reference).
- `tools/picoforge/msgpack-outbound-smoke.sh` — **3/3**: picomesh C → foreign
  Python service: `6+7=13`, `6*7=42`.

### Part 3 — polyglot client generators + docs

- `docs/msgpack-rpc.md` — wire contract (framing, envelope, type map, error
  codes, kwargs).
- `tools/msgpack-codegen/`:
  - `model.py` — load `model.yaml` → normalized IR (dotted path, arg kinds, ret
    kind); C-type → neutral-kind maps.
  - `gen.py` — CLI: `gen.py --role <client|server> --lang <L> --out DIR --module
    NAME model.yaml...`.
  - clients: `emit_python.py emit_go.py emit_rust.py emit_cpp.py emit_lua.py`.
  - servers: `emit_python_server.py` (foreign-service skeleton — framing,
    envelope validation, path dispatch, describe, structured errors; typed
    handler stubs the developer fills in; gh#23).
- `bindings/{python,go,rust,cpp,lua}/calculator_client.*` — generated demo.
- `bindings/python/calculator_server.py` — generated server skeleton.
- **Verified:** python client end-to-end vs a live frontend (add 5 / mul 42 /
  sub 6); go parses (`gofmt -e`); c++ compiles vs cmp (`g++ -fsyntax-only`).
  rust + lua are generate-only here (rustc libz broken, lua absent).

---

## Wire contract (summary)

```
request : u32 BE len | msgpack({v, op, path, args, kwargs, headers})
reply   : u32 BE len | msgpack({v, ok:true,  result: <value>})
                     | msgpack({v, ok:false, error: {message, code}})
```

Picomesh envelope, **not** standard msgpack-rpc. Length-prefixed, strict
serial, 1 MiB cap. `kwargs` is positional-only in v1 (non-empty →
`kwargs_unsupported`). Full detail in [`docs/msgpack-rpc.md`](./msgpack-rpc.md).

## Verify

```sh
make build-desktop-release
bash tools/picoforge/msgpack-smoke.sh            # inbound  9/9
bash tools/picoforge/msgpack-outbound-smoke.sh   # outbound 3/3
# polyglot: tools/msgpack-codegen/gen.py per lang, then run the python client
```

---

## Missing / deferred

1. **Foreign server-skeleton generators — DONE for Python (gh#23).**
   `gen.py --role server --lang python` emits `<module>_server.py`: framing,
   envelope validation (incl. v1 kwargs rejection), dispatch-on-path, `describe`,
   structured errors, and typed handler stubs the developer fills in. The outbound
   smoke now runs against the generated skeleton
   (`tools/msgpack-client/calculator_server_impl.py`), not a hand-written server.
   Remaining: `emit_go_server` / `emit_rust_server` / … follow the same model.

2. **C client stubs from a hand-authored `model.yaml` (no-C-class / IDL
   case).** For a foreign service picomesh has no annotated C class for, you
   hand-author `model.yaml` as the IDL. Today `codegen.py` goes annotated-C →
   `model.yaml` → C stubs and needs impl functions; it cannot consume a
   hand-written `model.yaml` to emit C *client* stubs. Need either a
   `model.yaml` → C-client emitter, or a "thin annotated-C interface header"
   convention. (The foreign *client* generators already consume `model.yaml`
   directly, so only the C-client side of this case is missing.)

3. **Engine config `transport: msgpack` for remotes — DONE (gh#22).**
   `remotes: [{service, host, port, transport: msgpack}]` now opens a yloop-aware
   async MessagePack transport per worker (`msgpack_async_client` in `engine.c`):
   lazy connect, framed coroutine-yielding read/write (never blocks the loop),
   reconnect on drop, and a cooperative serial gate (the envelope carries no
   req_id, so calls on one channel are serialised rather than multiplexed). The
   envelope build/parse stays in `rpc.c`; the framed I/O is injected via
   `peer_channel_set_msgpack_async`. Because the msgpack wire is stateless
   path-invoke, `rpc_object_acquire` hands back a handle-less proxy (no
   `RPC_OP_CREATE`). Verified: `tools/picoforge/msgpack-config-remote-smoke.sh` —
   a C node forwards `/_rpc calculator.calc.*` to a foreign Python msgpack server
   declared purely as a config remote (4/4).

4. **Build wiring for bindings (automation).** `model.yaml` regenerates with
   the build, but the polyglot clients do **not** — `gen.py` is invoked by
   hand. Need a `make bindings` / CMake target that runs `gen.py` per plugin ×
   language from each `model.yaml` on model change.

5. **Bindings only generated for `calculator` (coverage).** 18 plugins have a
   `model.yaml` (accounts, git_repo, session, storage, …); only calculator
   clients were generated as the verified demo. Generating the rest is just
   looping `gen.py` (no new code).

6. **yttp / cli not on the resolver (scope).** Only the gateway `/_rpc` and the
   msgpack frontend go through `picomesh_resolve_service_call`. yttp / cli are
   local object-handle dispatchers (the caller already holds a handle) — not
   service-path boundaries, so the gate doesn't map to them. Intentional; noted
   for completeness.

7. **`describe` is minimal (inbound).** The msgpack frontend's `op=describe`
   returns a method's positional param signature only — now gated through
   `picomesh_resolve_service_call` (uses `call.params`), same as invoke. No
   service-tree / class enumeration like `/_describe_tree` yet.

8. **Object-handle args not crossed (type surface).** v1 targets flat
   value/string methods. struct / object-handle args are process-local and not
   represented on the msgpack wire (cross-language object proxying is out of
   scope).

---

## Housekeeping

- Nothing committed (no commit without an explicit ask).
- Pre-merge formatter (`qa-tools/refactoring/code-format/apply-format.py`) not
  run yet.
- The frontend is named `msgpack` (like `alpine` / `cli`), **not** `ymsgpack`.
- Decisions: vendored **cmp** (not mpack); **Picomesh envelope** (not standard
  msgpack-rpc); kwargs positional-only.
