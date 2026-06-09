# registry

In-memory service registry for mesh discovery. Nodes register their bound
`{host, port}` under a service name; consumers look a service up by name to
discover where it is listening. Pure data store — no health checks, no
persistence (state is lost on restart by design; the mesh re-spawns and every
node re-registers on boot).

The registry is one of only **two** fixed-address nodes in the mesh (the other
is the mesh control parent). Its address is injected into every child's config
as a global `registry: {host, port}` block, so a freshly spawned node can reach
the registry — and through it `portalloc` and its `port: auto` remotes — before
it knows where anything else lives.

## State is process-wide

The yrpc server allocates a fresh receiver object for **every** client's
`CREATE`, so a per-object table would be invisible to the next client. The
registrations therefore live in one mutex-guarded process-wide singleton
(`registry_state()`), the same shape the trace collector uses for its span
store. The mutex is uncontended under the single worker the registry runs with
and stays correct if it ever scales out.

## Methods

| method                                                  | purpose                                   |
| ------------------------------------------------------- | ----------------------------------------- |
| `register_service(name, instance_id, host, port)`       | add or refresh an instance                |
| `deregister_service(name, instance_id)`                 | remove an instance                        |
| `resolve(name)`                                         | `"host:port"` of the live instance, or `""` |
| `discover_service(name)`                                | JSON `{service_name, instances:[…]}`      |
| `list_services()`                                       | JSON `[{service_name, instances:[…]}]`    |
| `count()`                                               | number of live instances                  |

`resolve` is the framework's `port: auto` edge: a node opening a remote with no
explicit port calls it to turn a service name into a concrete address. An
unknown service returns the empty string (the caller retries until the producer
registers), never an error. The richer JSON methods mirror yaapp's `registry`
for the describe surface.

## Registration is internal to each node

Nodes do not call `register_service` from application code — the framework
registers a node automatically on the serve path (see
`src/picomesh/main/autoport.c`). Only `port: auto` discovery and the
inspection methods are exposed to callers.
