# picoforge

The application built on top of **picomesh** — a small git forge (accounts,
repositories, issues, CI pipelines) running as a mesh of single-purpose
services. `picomesh` is the engine (mesh, plugins, RPC, discovery); `picoforge`
is the app. This directory holds the only browser-facing tier, the
[`webapp/`](webapp/) (binary `picoforge-webapp`).

## Start it

```sh
make build-desktop-release
./tools/picoforge/mesh-up.sh          # brings up the mesh, runs the smoke, stays live
./tools/picoforge/mesh-up.sh --help   # what it does + which ports it picks
```

`mesh-up.sh` starts the control parent, which spawns one child process per
service listed in `assets/picoforge/config/picoforge.yaml`, then runs an
end-to-end smoke through the gateway and leaves the stack running. Open the
webapp URL it prints (`http://127.0.0.1:<webapp>/login`). Stop the stack with
Ctrl-C — the mesh owns its children and reaps them; never `pkill` a child.

## Ports — almost everything is `port: auto`

Only **two** addresses are fixed per mesh instance: the **control parent** and
the **registry**. Their ports (plus the **gateway** and **webapp** front doors)
are *allocated from free ports* by `mesh-up.sh`, so repeated runs and several
co-resident instances never collide. Everything else sets `port: auto`:

- **portalloc** — hands a free TCP port to each `auto` node. It bind-probes
  every candidate before leasing, draws from a configured range
  (`portalloc.port_range`, default `8300-8799`), and is itself reached through
  the registry. Co-resident instances should use disjoint ranges.
- **registry** — the discovery service: nodes register their bound
  `name → host:port` and look each other up. Its fixed address is injected into
  every child's config so a node can find the registry — and through it,
  portalloc and any `port: auto` remote it consumes — before it knows where
  anything else lives.

A node boots, allocates its own port via portalloc (if `auto`), registers
itself, then resolves each `auto` remote through the registry. Allocation and
registration are internal to the framework — no application code is involved.
See [`docs/port-auto-discovery`](../../assets/picoforge/config/picoforge.yaml)
config comments and `src/picomesh/main/autoport.c` for the mechanism.

## Topology

| service        | listens | role                                                        |
| -------------- | ------- | ----------------------------------------------------------- |
| control parent | yhttp   | spawns/reaps children; not on the auth path                 |
| registry       | yrpc    | service discovery (fixed address)                           |
| portalloc      | yrpc    | port allocation for `auto` nodes                            |
| gateway        | yhttp   | the one auth boundary; external `/_rpc` + `/_describe`      |
| webapp         | yhttp   | browser pages; sources all data from the gateway over `/_rpc` |
| backends       | yrpc    | storage, accounts, session, git_repo, issues, pipelines, …  |

Browsers and API clients only ever talk to the **gateway** (and the webapp);
backends speak yrpc behind it and are never reached directly. See the top-level
`CLAUDE.md` for the full architecture invariants.
