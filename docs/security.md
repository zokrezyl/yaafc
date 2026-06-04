# Authentication and Authorization

This document defines the Picomesh security design to port from yaapp. It is a
target design, not a statement that the current C implementation already
matches it.

The core idea: authentication/authorization is a **core picomesh mechanism run
at the service-invoke boundary**, selected per node by config — not logic baked
into any frontend. Every protocol carries headers, so a frontend's only auth job
is to fold its wire headers into `yheaders` and invoke the target method with
them; the core then runs the configured authn chain + authorizer off those
headers before the method body executes:

```text
request (any frontend: yhttp / yrpc / msgpack)
  -> frontend folds protocol headers into yheaders, invokes the method
  -> CORE auth-at-invoke (only when this node's config enables it):
       authenticator chain   -> JWT or anonymous
       authorizer(endpoint, args/kwargs, JWT)
  -> verified auth context (JWT claims) replaces the credential in yheaders
  -> the resolved service method runs
```

Because the mechanism is core and reads `yheaders` (which every protocol
carries), the same auth works under any frontend with zero per-frontend auth
code. Which authenticators/authorizer a node runs — and therefore whether it is
an auth boundary at all — is that node's own config.

Services such as `session`, `token_issuer`, `accounts`, `password_authn`, and
`personal_access_tokens` are mesh plugins/services. They can run in the same
node as the boundary or in another node reached through the mesh. The auth
mechanism must call them through configured service paths; it must not hardcode
their storage details.

## Yaapp Reference Model

In yaapp, auth is split like this:

| Concern | Yaapp location | Role |
|---|---|---|
| Per-request frontend pipeline | `src/yaapp/frontends/fastapi/plugin.py`, `src/yaapp/frontends/yttp/plugin.py` | Runs authn/authz before service invocation |
| Authenticator framework | `src/yaapp/authenticators/base.py`, `registry.py` | Builds and runs ordered authenticators from config |
| Built-in authenticators | `src/yaapp/authenticators/session_cookie/`, `bearer_jwt_token/`, `bearer_opaque_token/` | Convert cookie/bearer credentials to a JWT |
| Authorizer framework | `src/yaapp/authorizers/` | Builds and runs an authorizer from config |
| Policy authorizer | `src/yaapp/authorizers/policy/` | Default-deny endpoint policy, role ladder, account resolution |
| JWT verifier helper | `src/yaapp/lib/jwt_verifier.py` | Fetches signing key from `token_issuer`, verifies JWT |
| Credential services | `session`, `token_issuer`, `accounts`, `password_authn`, `personal_access_tokens` plugins | Own sessions, tokens, credentials, groups |

Yaapp's current policy authorizer is framework code, not a remote `authz`
service call on every request. It was ported from an older `authz` plugin.
Picomesh may keep that framework-authorizer shape or deliberately implement an
`authz` plugin/service, but the choice must be explicit.

## Plugin and Framework Boundary

In Picomesh, "plugin" means a mesh feature/service that can be activated in a
specific node or run in a remote node. Security must preserve that boundary.

Orchestration is a CORE step at the invoke boundary, not a frontend
responsibility. The frontend only carries headers and invokes; the core, when
this node's config enables auth, does:

- read the credential from `yheaders` (the frontend already folded the wire
  headers in — cookie / bearer / forwarded header);
- run the configured authenticator chain;
- receive or verify an access JWT;
- run the configured authorizer;
- invoke the target service method only after allow;
- replace the credential in `yheaders` with the verified JWT/auth context and
  forward only that downstream.

Neither the frontend nor the core auth step may know how PATs are stored, how
session rows are laid out, or how account memberships are stored. Those are
plugin/service contracts the configured authenticators reach over RPC.

Low-level library code may exist for crypto/JWT primitives, but it must stay
boring:

- base64url;
- HMAC/JWT encode/decode;
- constant-time compare;
- claims parsing;
- maybe a helper to fetch/verify signing material.

It must not contain policy decisions such as "fall back to uid if JWT
verification fails".

## Boundary Pattern (config-selected, not a special frontend)

A deployment may have several frontends: `yhttp`, `yrpc`, future bridge
frontends, and service-console frontends. **Whether a node is an auth boundary
is decided by that node's config, not by which frontend it runs.** The
"gateway" is simply a node whose config declares opaque-resolving authenticators
plus a policy authorizer; the same yhttp binary on another node with no
`security` block is a dumb transport carrier. One core mechanism, three config
shapes:

- **boundary** — opaque-resolving authenticators (`session_cookie`,
  `bearer_opaque_token`) + an authorizer: resolves the external credential to a
  JWT, scrubs the opaque credential, authorizes, forwards JWT-only;
- **internal verifier** — a `bearer_jwt_token` authenticator only: verifies the
  JWT already in the prefix and rejects if absent/invalid;
- **trusting internal** — no `security` block: runs no auth-at-invoke and trusts
  the upstream-stamped context (safe only behind a boundary).

Typical Picoforge shape:

```text
browser or API client            (presents an OPAQUE cookie/bearer)
  -> Picoforge webapp or direct client
  -> a node configured as a BOUNDARY
       core auth-at-invoke:
         1. resolve cookie/bearer -> JWT (or anonymous), SCRUB the opaque
         2. authorize endpoint + arguments
         3. invoke active local or remote service with JWT-only context
  -> backend service nodes              (see only the internal JWT)
```

Internally only the JWT travels. The opaque cookie/bearer crosses only the
external edge into the boundary node and is scrubbed there; every hop after that
carries the verified JWT in `yheaders`, never the opaque secret. Backend nodes
without their own auth config are safe only behind a boundary; their RPC surface
must not be reachable by untrusted clients. The external edge stays opaque-only:
a boundary must reject a client-presented raw JWT, so `bearer_jwt_token` is an
internal-verification authenticator, never enabled on the external edge.

## Per-Request Pipeline

When a node has security configured, the core gates every service invocation at
the invoke boundary — independent of frontend. The frontend has already folded
its wire headers into `yheaders` and called the method; the core auth step runs
before the method body. Surfaces gated this way include `/_rpc`, `/_describe`,
`/_describe_tree`, and any yrpc/msgpack service-call entrypoint. HTML/static/debug
routes may pass through only if they do not invoke backend service methods.

For each service invocation:

```text
invoke(method, yheaders, args)        (yheaders already carries the credential)
  -> run authenticator chain, reading the credential from yheaders
       no credential shape matched: jwt = none
       credential shape matched and valid: jwt = verified JWT
       credential shape matched and invalid: fail 401
  -> run authorizer(endpoint, args/kwargs, jwt)
       allowed: continue
       denied: fail 403
  -> replace the credential in yheaders with the verified JWT/auth context
  -> run the resolved active service method
```

The failure rule is security-critical: if a credential of a known shape is
present but invalid, the chain stops with 401. It must not fall through to a
weaker scheme or to a raw uid header.

The endpoint is the fully qualified service method name, for example:

```text
git_repo.git_repo.make
git_repo.git_repo.put_file
accounts.accounts.get_user
mesh.mesh.reconcile
```

## Authenticators

Authentication answers "who is this caller?" It does not decide whether the
caller may perform the requested operation.

Authenticators are configured by type. Each authenticator owns only one
credential shape, and reads it from `yheaders` — the frontend has already folded
its protocol's cookie / `Authorization` / forwarded headers into the bag, so an
authenticator never parses a wire format and behaves identically under any
frontend.

| Type | Credential read (from `yheaders`) | How it yields a JWT |
|---|---|---|
| `session_cookie` | configured cookie or same-named forwarded header | calls configured `lookup` RPC, usually `session.session.jwt`, and verifies the returned JWT |
| `bearer_jwt_token` | `Authorization: Bearer <jwt>` | verifies the JWT signature and expiry directly |
| `bearer_opaque_token` | `Authorization: Bearer <prefix>...` | calls configured `lookup` RPC, such as PAT or runner lookup, and verifies the returned JWT |

Example shape:

```yaml
security:
  authenticators:
    - type: session_cookie
      cookie: picomesh-sid
      header: picomesh-sid
      lookup: session.session.jwt

    - type: bearer_jwt_token
      header: Authorization
      reject_prefixes: ["pat_", "rnr_"]

    - type: bearer_opaque_token
      prefix: "pat_"
      lookup: personal_access_tokens.personal_access_tokens.lookup

    - type: bearer_opaque_token
      prefix: "rnr_"
      lookup: runner_agent.runner_agent.lookup_token
```

The opaque-token authenticator must not parse PAT ids or runner ids itself. It
passes the opaque token to the configured lookup service. The lookup service
returns a JWT or a payload containing a JWT. The authenticator verifies that JWT
before forwarding it.

## Token Issuer

`token_issuer` is a plugin/service. It owns framework JWT issuance and refresh
tokens. Authn plugins verify credentials; they do not mint framework
authorization tokens themselves.

Login flow:

```text
token_issuer.login(method, credentials)
  -> call <method>_authn.verify(credentials)
  -> receive identity: user id, username, optional email
  -> load groups from accounts/users service
  -> mint short-lived access JWT
  -> mint longer-lived opaque refresh token
  -> return token pair and public user metadata
```

The access JWT should contain at least:

```json
{
  "iss": "picomesh",
  "sub": "user-id",
  "username": "alice",
  "groups": ["site:owner", "alice:owner", "team:developer"],
  "iat": 1710000000,
  "exp": 1710000900,
  "jti": "unique-token-id"
}
```

The signing key must come from `token_issuer` or an explicitly configured key
source. HS256 is acceptable only inside a trusted mesh because verifiers need
the symmetric secret. For untrusted edges, prefer an asymmetric scheme such as
RS256 or EdDSA so verifiers only need public key material.

## Sessions

Browser sessions use opaque cookies. In the normal cookie flow the browser must
not receive the internal JWT.

The `session` plugin/service stores:

```text
session_id -> access JWT, refresh token, user metadata, expiry metadata
```

Browser login should be composed by the session service, matching yaapp:

```text
POST login request
  -> session.session.start(method="password", credentials)
  -> session.start calls token_issuer.login(...)
  -> session.start stores access JWT under a fresh opaque sid
  -> response sets HttpOnly picomesh-sid cookie
```

Later requests:

```text
browser sends picomesh-sid cookie
  -> session_cookie authenticator calls session.session.jwt(session_id)
  -> jwt() returns the stored access JWT if the session exists
  -> authenticator verifies the JWT (signature + expiry)
  -> authorizer decides endpoint access
```

Session ids are bearer secrets. They must be high-entropy random values and
must never be derived from user ids, counters, clocks, or storage row numbers.

Because the sid is a bearer secret, it never appears in a listing.
`session.session.list` / `list_all` return only non-secret metadata
(`uid`, `created_at`) — never the sid, the stored access JWT, or the refresh
token. The same rule holds for every credential store: `token_issuer.list`
returns `uid`/`username`/`created_at` but never the refresh token, and a
runner/PAT list must never return its opaque token. One-time token material is
handed out only at issuance, not by a list call. A caller with list access must
not be able to harvest a live credential.

Session rows should support:

- idle timeout, extended on activity;
- absolute max lifetime;
- logout deletion;
- optional cookie/session rotation on login, privilege elevation, or suspicious
  activity.

The cookie value does not need to rotate on every request. Rotating every
request can create races with parallel browser requests.

## Credential-Exchange Endpoints

Some methods consume credentials as their arguments:

- `session.session.jwt(session_id)` exchanges an opaque session id for the
  stored access JWT (the `session_cookie` authenticator's `lookup`).
- `token_issuer.token_issuer.refresh(refresh_token)` exchanges a refresh token
  for a fresh access JWT and rotated refresh token.
- `personal_access_tokens.personal_access_tokens.lookup(token)` exchanges an
  opaque PAT for a JWT.
- `runner_agent.runner_agent.lookup_token(token)` exchanges an opaque runner
  token for a runner JWT.

These are not open data endpoints. They exist so authenticators can turn an
otherwise unauthenticated request into a JWT-bearing request.

Expose them carefully. If they are reachable through public `/_rpc`, then any
holder of the opaque secret can retrieve a JWT. Prefer one of these designs:

- hide credential-exchange methods from normal public service invocation;
- expose them only on an internal frontend used by the authenticator pipeline;
- allow public invocation only if the deployment explicitly accepts that
  behavior.

## Authorization

Authorization answers: "may this caller invoke this endpoint with these
arguments?"

Picomesh runs a config-selected authorizer at the core invoke boundary (per
node, from that node's `security` config). The yaapp-compatible default is a
policy authorizer keyed by endpoint name. Endpoints absent from policy are
denied by default.

Policy example:

```yaml
security:
  authorizer:
    type: policy
    policy:
      _describe: anonymous
      _describe_tree: anonymous

      token_issuer.token_issuer.login: anonymous
      token_issuer.token_issuer.refresh: anonymous
      session.session.start: anonymous

      accounts.accounts.get_user: authenticated
      accounts.accounts.list_users:
        required_role: owner
        account_from: site

      git_repo.git_repo.read_tree:
        required_role: reporter
        account_from: "{kwargs.name:account}"

      git_repo.git_repo.put_file:
        required_role: developer
        account_from: "{kwargs.name:account}"

      mesh.mesh.reconcile:
        required_role: owner
        account_from: site
```

Rule evaluation:

1. `anonymous`: allow without a JWT.
2. No JWT and rule is not anonymous: deny.
3. Verify JWT signature and expiry.
4. `authenticated`: allow any valid JWT.
5. Role-gated rule: compare JWT groups against required role and account.

Roles use a monotonic ladder:

```text
guest < reporter < developer < maintainer < owner
```

The JWT `groups` claim stores memberships as strings:

```text
<account-slug>:<role>
```

Examples:

```text
site:owner
alice:owner
acme:maintainer
project-team:developer
```

Site-level administrators can be represented by `site:maintainer` or
`site:owner`, depending on deployment policy. The site-level bypass must be
explicit in the authorizer.

The yaapp policy authorizer supports account resolution against kwargs:

| Form | Resolves to |
|---|---|
| `site` | literal `site` |
| `{kwargs.name}` | the whole value of `kwargs["name"]` |
| `{kwargs.name:account}` | account part before `/` |

Picomesh may also support positional `args` while its JSON API remains
positional, but the design target should preserve the yaapp-style named
argument form as the cleaner policy language.

## Where Groups Come From

Groups are minted into the JWT at login and refresh time. The token issuer
must call the canonical accounts/users service to load memberships.

This implies point-in-time claims:

- role changes take effect when the user logs in again or refreshes the access
  token;
- short access-token TTL limits role staleness;
- deleting a session or refresh token is immediate for future exchanges;
- already-issued access JWTs remain valid until expiry unless JWT revocation is
  added.

For Picoforge, the current `accounts` plugin is only a partial user/account
authority. The target accounts/users service must own users, account
memberships, groups, roles, and credential links.

## Current Picomesh Implementation Gaps

The current implementation only partially matches this design.

Known mismatches:

- `src/picomesh/frontends/yhttp/frontend.c` builds and runs the authenticator
  chain + authorizer (`gateway_security`, the auth block in `route_json_rpc`)
  inside the yhttp frontend, and legacy mutation routes resolve identity
  themselves. The target moves that mechanism into the CORE invoke boundary,
  config-selected per node, so every frontend gets it by carrying headers and
  invoking — no per-frontend auth code.
- The yhttp authenticator code knows PAT and runner internals instead of using
  generic `bearer_opaque_token` entries with configured `lookup` service paths.
- `src/picomesh/ysecurity/secret.c` is low-level helper code but currently
  contains a dangerous policy behavior: an invalid JWT can fall back to
  `yheaders["uid"]`. Invalid JWT must fail closed.
- Browser login/session composition is currently partly in gateway routes. The
  yaapp shape puts server-side login composition in `session.start`, which calls
  `token_issuer.login`, stores the JWT, and returns only the opaque cookie.
- Session rows do not yet enforce the full idle timeout plus absolute lifetime
  model on every gateway mutation path.
- Some legacy yhttp mutation routes still use `session.lookup -> uid` instead
  of the JWT/authz pipeline.
- The policy language currently supports only a subset of yaapp account
  resolution.
- PAT support is not yet real opaque-token auth. Sequential numeric ids are not
  acceptable bearer credentials.
- Password verification remains demo-grade and must be replaced before treating
  this as production security.

## Implementation Plan

1. Keep `ysecurity` as a low-level library only. Remove policy decisions and
   any invalid-JWT fallback behavior from it.
2. Move the auth pipeline into the CORE invoke boundary (not a frontend), so
   yhttp, yrpc/msgpack, bridges, and future frontends share it by carrying
   headers and invoking — no per-frontend auth code. Boundary vs internal vs
   trusting is each node's own `security` config.
3. Implement configured authenticator modules:
   `session_cookie`, `bearer_jwt_token`, and generic
   `bearer_opaque_token`.
4. Make opaque authenticators call configured lookup service paths and verify
   returned JWTs.
5. Implement a configured authorizer. The first target should be the
   yaapp-compatible `policy` authorizer. If Picomesh wants authz as a mesh
   plugin/service instead, define that contract explicitly.
6. Move browser login/session composition to the `session` service shape:
   `session.start -> token_issuer.login -> store JWT under sid -> Set-Cookie`.
7. Redesign `session` rows with idle timeout, absolute lifetime, refresh token,
   and logout deletion.
8. Replace numeric PATs with random opaque tokens stored by hash and exchanged
   for JWTs by the PAT service.
9. Pass verified JWT/auth context through local and remote calls. Backend
   resource checks must use verified auth context or fail closed.
10. Replace demo password hashing with a real password KDF and per-password
    salt.
11. Add tests for both success and failure: missing credential, malformed
    credential, expired session/JWT, absent policy entry, anonymous endpoint,
    and valid cookie/PAT/runner token flows.

## Security Properties

The target design provides:

- default deny for methods absent from policy;
- no credential downgrade on malformed tokens;
- clear separation of authn and authz;
- clear separation between the core auth mechanism and the mesh service plugins;
- one core policy decision at the invoke boundary before service invocation;
- short-lived JWT claims with refresh/login updates;
- browser sessions that expose only opaque HttpOnly cookies;
- support for API clients using bearer JWTs or bearer opaque tokens;
- explicit deployment boundary for backend services without their own auth.
