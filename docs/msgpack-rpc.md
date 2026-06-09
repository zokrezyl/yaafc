# Picomesh MessagePack RPC

The `msgpack` frontend lets clients in any language call picomesh services
without binding to the native binary `yrpc` wire or any generated C symbol.
It is a sibling transport to `yrpc` (`--frontend msgpack`), not a replacement —
the native binary path stays the default and is untouched.

This is a **Picomesh envelope**, deliberately *not* standard msgpack-rpc: it
keeps the dynamic `op/path/args/kwargs/headers` shape (issue #9) and is
length-prefixed, so off-the-shelf msgpack-rpc array clients do not interoperate.
Clients are generated (`tools/msgpack-codegen/`) or hand-written against this
contract (`tools/msgpack-client/picomesh_msgpack.py` is the reference).

## Framing

Strict serial request/response, one in-flight request per connection (no
multiplexing in v1):

```
u32 frame_len (big-endian) | msgpack(envelope)
```

`frame_len` is the byte length of the msgpack payload that follows. A frame
whose declared length is zero or exceeds the **1 MiB** cap is answered with a
`frame_too_large` error and the connection is closed. EOF mid-frame closes the
connection; a complete-but-undecodable frame yields a `bad_envelope` error and
the connection stays open.

## Request envelope

A msgpack **map**:

```
{
  "v":       1,                                  # protocol version (int)
  "op":      "invoke" | "describe",              # default "invoke"
  "path":    "service.class.method",             # dotted; str
  "args":    [ ... ],                            # positional args (array)
  "kwargs":  { },                                # MUST be empty in v1 (see below)
  "headers": { "uid": N, "sid": N, "trace_id": "..." }   # optional
}
```

- `path` resolves through the **active-service gate**: the service must be an
  activated plugin or a configured remote on the target node. A path naming an
  inactive service is rejected (`service_not_active`) before any class/method
  lookup — no global method table is reached.
- `headers` is the msgpack analogue of the internal header bag. `uid`/`sid` are
  ints; `trace_id` is a string. An anonymous caller sends `{}` (or omits it).
- `kwargs` is **positional-only in v1**: a non-empty `kwargs` is rejected with
  `kwargs_unsupported` (never silently ignored). This holds for **every** `op`,
  including `describe` — the check runs before the op is dispatched. The key is
  reserved for a future keyword-binding version.

## Response envelope

```
{ "v": 1, "ok": true,  "result": <value> }
{ "v": 1, "ok": false, "error": { "message": "...", "code": "..." } }
```

`describe` (op) returns, as its `result`, the method's positional parameter
signature: `{ "path": "...", "params": [ { "name": "...", "type": "<C type>" } ] }`.

## Type mapping

Each value carries its width/signedness; the generated C unpack validates the
range and rejects out-of-range values (`call_error`).

| C type | msgpack | python | go | rust | c++ | lua |
|---|---|---|---|---|---|---|
| `int` / `int64_t` / `int32_t` | int | `int` | `int64` | `i64` | `int64_t` | integer |
| `uint32_t` / `size_t` / `uint64_t` | uint | `int` | `uint64` | `u64` | `uint64_t` | integer |
| `double` / `float` | float64 | `float` | `float64` | `f64` | `double` | number |
| `const char *` / `char *` | str | `str` | `string` | `String` | `std::string` | string |
| `void` (return) | nil | `None` | `nil` | `()` | `void` | `nil` |

Object-handle args are process-local and do not cross a language boundary; the
v1 surface targets flat value/string methods.

## Error codes

| code | meaning |
|---|---|
| `bad_envelope` | the frame is not a well-formed envelope map |
| `bad_path` | `path` missing or malformed (want `service.class.method`) |
| `service_not_active` | the service is not active/configured on this node |
| `no_such_class` | the receiver class is not registered/reachable (or backend create failed) |
| `no_such_method` | the method does not exist on the (active) service |
| `kwargs_unsupported` | non-empty `kwargs` in a v1 request |
| `call_error` | the method ran and failed (bad arg type/range, or impl error) |
| `frame_too_large` | frame length zero or over the 1 MiB cap |
| `response_too_large` | the result exceeded the frame cap |

## Outbound — picomesh as a client

picomesh C can also call a **foreign** msgpack service that implements this same
contract. A `peer_channel` created with `peer_channel_create_msgpack(fd)` makes
every codegen-emitted method stub encode its call as this envelope (the args
array + a `path` derived from the method) and decode the `result`. The CLI
`--transport msgpack` exercises it; `tools/msgpack-client/echo_server.py` is a
reference foreign service.

> Status: the outbound transport + per-method client glue are implemented and
> tested over a blocking connection (CLI / worker-pool use). Selecting it for an
> engine *config remote* (`remotes: [{transport: msgpack}]`) over the gateway's
> async loop mux is the remaining integration.

## Try it

```sh
make build-desktop-release
# inbound: other languages → picomesh
./build-desktop-release/picomesh --plugins calculator --frontend msgpack --port 7900 serve &
./tools/msgpack-client/picomesh_msgpack.py invoke 127.0.0.1 7900 calculator.calc.add '[2,3]'
#   -> {"v": 1, "ok": true, "result": 5}

# the smokes drive both directions end to end:
bash tools/picoforge/msgpack-smoke.sh           # inbound (Python client → frontend)
bash tools/picoforge/msgpack-outbound-smoke.sh  # outbound (picomesh C → foreign Python service)
```
