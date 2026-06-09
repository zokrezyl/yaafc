# Picomesh Architecture

Picomesh is the C runtime and framework. Picoforge is an application built on
top of it.

The important boundary is:

```text
picomesh  = core runtime, service model, plugins, transports
picoforge = product/application using picomesh services
```

Historically this repository was developed as a C counterpart to the Python
`yaapp` project. That lineage explains some names and protocol shapes, but the
current architecture should be described in Picomesh/Picoforge terms.

## System Shape

A normal Picoforge deployment has four roles:

```text
browser
  -> picoforge-webapp
       -> gateway yhttp API
            -> backend services over yrpc
```

The roles are separate even when some of them are collocated in one process for
deployment or emulation.

### Picomesh Core

Picomesh owns:

- result/error types
- class and method registry
- generated method stubs
- generated RPC skeletons
- coroutine runtime
- libuv-backed event loop
- configuration loading
- service context resolution
- protocol frontends such as `yhttp`, `yttp`, `yrpc`, and `cli`

Picomesh code lives mostly under:

```text
src/picomesh/
include/picomesh/
```

Protocol frontends live under:

```text
src/picomesh/frontends/
include/picomesh/frontends/
```

These are framework transport adapters. They are not the Picoforge browser UI.

### Picoforge Application

Picoforge owns the product-level services and browser UI.

The browser page tier is:

```text
src/picoforge/webapp/
```

The binary is:

```text
picoforge-webapp
```

The webapp is intentionally separate from the Picomesh transport frontends. It
renders HTML pages and serves static assets. It does not link plugins, does not
open backend ports, and does not dispatch picoclass methods directly.

The webapp gets data from the gateway through:

```text
POST /_rpc
GET|POST /_describe
```

## Process Roles

### Gateway

The gateway is the public API and authentication boundary.

It listens through the `yhttp` frontend and exposes:

```text
POST /_rpc
GET|POST /_describe
POST /login
POST /register
POST /logout
```

The gateway should not render browser pages. It should not serve static files.
HTML routes belong to `picoforge-webapp`.

The gateway accepts external credentials such as cookies or opaque bearer
tokens, resolves them to internal caller context, and forwards service calls to
backends.

### Webapp

The webapp is the browser-facing page tier.

It owns routes such as:

```text
/login
/register
/repos
/<account>
/<account>/<repo>
/<account>/<repo>/issues
/<account>/<repo>/runs
/admin/users
```

The webapp renders HTML and submits forms. For data and mutations, it calls the
gateway. It must not call backend service ports directly.

### Backends

Backends are Picomesh services such as:

```text
storage
session
accounts
password_authn
token_issuer
issues
git_repo
git_pipeline
personal_access_tokens
mesh
```

In the split mesh, backends listen on `yrpc` and stay behind the gateway. They
do not serve HTML and do not expose public HTTP APIs.

### Mesh Control Parent

The mesh parent is a bootstrap/control process. It can spawn child services
from configuration through the `mesh` plugin.

This process is not the user-facing gateway. Its HTTP surface is for control
and bootstrap tasks, not for browser traffic.

## Service Model

Picomesh exposes C structs and functions through generated class metadata.

A plugin source file annotates a class and method implementations. Codegen then
emits:

- class accessors
- public typed stubs
- binary RPC skeletons
- JSON invokers
- method lookup tables

Conceptually:

```text
annotated C source
  -> codegen
      -> typed local calls
      -> yrpc skeletons
      -> JSON invoke support
      -> class/method registration
```

The important rule is that user-facing service invocation should be resolved
through the active service model, not by blindly accepting any compiled global
class/method name.

## Active Services

An active service is one of:

- a local service activated for this process
- a configured remote service reachable through a peer connection

Gateway discovery should describe the services this process can actually route
to. The webapp uses `/_describe` to decide which pages are available.

The desired model is:

```text
transport request
  -> gateway active service tree
      -> local service
      -> remote service over yrpc
```

The undesired model is:

```text
transport request
  -> parse service.class.method
      -> fall through to any globally registered class
```

Global class registration is useful internally, but it must not define the
public gateway surface by itself.

## Frontends And Webapp

The word "frontend" has two meanings in many codebases, so this repository
uses stricter language:

```text
Picomesh frontend = transport adapter
Picoforge webapp  = browser UI
```

Picomesh frontends:

```text
src/picomesh/frontends/yhttp
src/picomesh/frontends/yttp
src/picomesh/frontends/yrpc
src/picomesh/frontends/alpine   (the generic /_alpine service console — see docs/service-console.md)
src/picomesh/frontends/cli
```

Generic service tooling — a yrpc->yhttp transport bridge and the standalone
`/_alpine` service console — is documented in `docs/service-console.md`.
Both are application-agnostic and separate from the picoforge webapp.

Picoforge webapp:

```text
src/picoforge/webapp
```

Do not put Picoforge page routes into `src/picomesh/frontends/yhttp`. That
mixes application UI with framework transport code.

## Request Flows

### Browser Page Load

```text
browser
  -> GET /alice/demo
  -> picoforge-webapp
  -> POST gateway /_rpc {"path":"git_repo.git_repo.read_tree", ...}
  -> gateway resolves auth and service
  -> git_repo backend
  -> response back to webapp
  -> rendered HTML back to browser
```

### Programmatic API Call

```text
curl or external client
  -> POST gateway /_rpc {"path":"service.class.method","args":[],"kwargs":{}}
  -> gateway validates caller context
  -> active local or remote service
  -> JSON result/error
```

### Gateway To Backend

```text
gateway
  -> yrpc frame
      header: operation and request id
      body: caller context + packed args
  -> backend skeleton
  -> typed C method implementation
  -> packed response
```

## Wire Protocols

### yhttp

`yhttp` is the HTTP gateway transport. Its public gateway API is:

```text
POST /_rpc
GET|POST /_describe
```

The `/_rpc` JSON envelope is dynamic:

```json
{
  "path": "git_repo.git_repo.count_total",
  "args": [],
  "kwargs": {}
}
```

### yrpc

`yrpc` is the binary service-to-service transport.

It uses a compact frame with operation, request id, body length, and body
bytes. Generated skeletons unpack arguments and call the typed implementation.

This is the production backend path in the split mesh:

```text
gateway -> yrpc -> backend
backend -> yrpc -> backend
```

### yttp

`yttp` is a JSON-RPC style transport over TCP with Content-Length framing. It
is useful for tools and protocol experiments, but it is not the browser page
tier.

### cli

The CLI frontend invokes services locally for development and diagnostics.

## Configuration

Configuration is layered and deep-merged. The intended shape is:

```yaml
mesh:
  services:
    gateway:
      frontend: yhttp
      config:
        remotes:
          - service: session
            host: 127.0.0.1
            port: 8203

    session:
      frontend: yrpc
      plugins:
        - session
```

For a named process, service-specific configuration is projected onto the
runtime config so the process sees its own settings.

Split mesh:

```text
gateway process
  frontend: yhttp
  remotes: session, accounts, git_repo, ...

backend process
  frontend: yrpc
  plugins: one or more backend services
```

Collocated deployment:

```text
single process
  frontend: yhttp
  plugins: all services
  remotes: none
```

The collocated mode is useful for constrained environments such as the in-VM or
WebAssembly demo, but it should preserve the same logical gateway/service
boundary.

## Picoforge Deployment Modes

### Split Mesh

The development scenario starts a parent process that spawns one child per
service. The gateway talks to those services over `yrpc`.

This mode is best for validating the real process topology:

```text
picoforge-webapp :8080
gateway          :8090
session          :8203
accounts         :8204
git_repo         :8209
...
```

### Collocated App

The deploy/YEMU path may use one process with all services linked and active.
This avoids many process hops inside slow emulated environments.

Even in this mode, the architecture should remain:

```text
browser pages -> webapp
API/auth      -> gateway
business data -> services
```

Do not let collocation turn into a reason to move browser pages back into the
gateway frontend.

## Authentication Boundary

External clients receive opaque session credentials, not internal JWTs or
backend credentials.

Typical browser flow:

```text
browser submits login form to webapp
webapp forwards POST /login to gateway
gateway validates credentials and starts session
gateway returns opaque picomesh-sid cookie
webapp relays cookie to browser
future webapp calls forward that sid to gateway
gateway resolves sid to uid/sid caller context
```

Internal service calls receive caller context through the backend transport.
The browser should never see internal service credentials.

## Repository Layout

High-level layout:

```text
include/picomesh/              public framework headers
src/picomesh/                  framework/runtime implementation
src/picomesh/frontends/        transport adapters
src/picomesh/plugins/          framework/application service plugins
src/picomesh/picoclass/gen/       code generator
src/picomesh/main/            picomesh main command

src/picoforge/webapp/          browser-facing Picoforge webapp
assets/picoforge/              Picoforge config/ + static/ assets
tools/picoforge/               Picoforge operator + build/deploy tooling
tests/integration/picoforge/   browser-driven Picoforge tests
tests/performance/picoforge/   gateway performance harness
```

The top-level README should stay short and point here for details.

## Architectural Rules

- Keep `picomesh` framework code separate from `picoforge` application code.
- Keep transport frontends separate from browser UI.
- Gateway is API/auth boundary, not page renderer.
- Webapp renders pages and serves static assets.
- Webapp calls only gateway `/_rpc` and `/_describe`.
- Backends stay behind the gateway.
- Active services come from configured local services plus configured remotes.
- Do not expose arbitrary compiled classes merely because they are registered.
- Prefer generated glue for service calls instead of handwritten dispatch.
- Keep deployment shortcuts logically equivalent to the split architecture.

