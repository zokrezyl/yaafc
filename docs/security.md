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

## Authorization Domain Model

Picoforge authorization should be role-based over namespaces, not only
owner-id checks on individual repos. The GitLab-compatible model is:

```text
namespace:
  id
  parent_id nullable
  slug
  kind: user | group

repo:
  id
  namespace_id
  name

namespace_member:
  namespace_id
  uid
  role: guest | reporter | developer | maintainer | owner
```

A namespace is the ownership container for repos. A user gets a personal
namespace when the account is created:

```text
alice/demo
```

Groups are also namespaces:

```text
acme/api
```

Group namespaces may contain sub-namespaces:

```text
acme/platform/api
acme/platform/backend/auth-service
```

So the repo's full path is:

```text
<namespace-path>/<repo-name>
```

GitHub-style ownership is a constrained case of the same model: repos usually
attach only to a root user/org namespace (`owner/repo`), while teams provide
permission grouping. GitLab-style ownership allows nested group namespaces in
the repo path. Both shapes can use the same RBAC evaluator.

Role inheritance follows the namespace tree. To authorize an action on a repo,
resolve the repo's namespace, walk from that namespace to its parents, and
compute the caller's effective role as the highest applicable membership. A
direct membership on a child namespace may grant a more specific role than an
ancestor. The first implementation can use monotonic max-role semantics; if
deny rules or inheritance locks are needed later, they must be added explicitly.

```text
effective_role(uid, namespace):
  max(
    direct membership on namespace,
    inherited membership on parent namespaces,
    explicit site bypass when configured
  )
```

Examples:

```text
alice owns namespace alice
alice/demo requires developer to push
alice has alice:owner
owner >= developer
=> allowed

bob is acme:developer
acme/platform/api requires reporter to read
developer >= reporter
=> allowed through inherited acme membership

carol has no role on acme or acme/platform
acme/platform/api requires reporter to read
=> denied
```

Site-level administration is separate from namespace membership. A deployment
may choose to let `site:maintainer` or `site:owner` bypass normal namespace
checks, but that bypass must be explicit in the authorizer and should be used
only for administrative operations.

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

      # Repo content reads require at least reporter on the repo namespace.
      # The authorizer should resolve repo_id -> namespace_id -> namespace path
      # and compare the caller's effective namespace role.
      git_repo.git_repo.read_tree:
        required_role: reporter
        resource_from: "{kwargs.repo_id}"
        role_scope: repo_namespace

      # Repo writes require at least developer on the repo namespace.
      git_repo.git_repo.put_file:
        required_role: developer
        resource_from: "{kwargs.repo_id}"
        role_scope: repo_namespace

      mesh.mesh.reconcile:
        required_role: owner
        account_from: site
```

Rule evaluation:

1. `anonymous`: allow without a JWT.
2. No JWT and rule is not anonymous: deny.
3. Verify JWT signature and expiry.
4. `authenticated`: allow any valid JWT.
5. Role-gated rule: resolve the target namespace/resource, compute the caller's
   effective role there, and compare it with the required role.

Roles use a monotonic ladder:

```text
guest < reporter < developer < maintainer < owner
```

The JWT `groups` claim stores namespace memberships as strings:

```text
<namespace-path>:<role>
```

Examples:

```text
site:owner
alice:owner
acme:maintainer
acme/platform:developer
```

Site-level administrators can be represented by `site:maintainer` or
`site:owner`, depending on deployment policy. The site-level bypass must be
explicit in the authorizer.

The yaapp policy authorizer supports account resolution against kwargs. Picoforge
should generalize this to resource resolution, because most repo APIs take a
`repo_id`, not a namespace path:

| Form | Resolves to |
|---|---|
| `site` | literal `site` |
| `{kwargs.name}` | the whole value of `kwargs["name"]` |
| `{kwargs.name:account}` | namespace/account part before `/` |
| `{kwargs.repo_id}` + `role_scope: repo_namespace` | repo's owning namespace |

Picomesh may also support positional `args` while its JSON API remains
positional, but the design target should preserve the yaapp-style named
argument form as the cleaner policy language. The final policy language should
name resources precisely enough that endpoint policy does not have to fall back
to ad hoc service-local owner checks.

## Where Groups Come From

Groups are minted into the JWT at login and refresh time. The token issuer
must call the canonical accounts/users service to load namespace memberships.

This implies point-in-time claims:

- role changes take effect when the user logs in again or refreshes the access
  token;
- short access-token TTL limits role staleness;
- deleting a session or refresh token is immediate for future exchanges;
- already-issued access JWTs remain valid until expiry unless JWT revocation is
  added.

For Picoforge, the current `accounts` plugin is only a partial user/namespace
authority. The target accounts/users service must own users, namespaces,
namespace memberships, roles, and credential links.

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
- `src/picomesh/ysecurity/secret.c` must remain low-level helper code. It now
  fails closed on absent/invalid JWTs; keep it that way and do not reintroduce
  any `yheaders["uid"]` fallback or other policy decision there.
- Browser login/session composition is currently partly in gateway routes. The
  yaapp shape puts server-side login composition in `session.start`, which calls
  `token_issuer.login`, stores the JWT, and returns only the opaque cookie.
- Session rows do not yet enforce the full idle timeout plus absolute lifetime
  model on every gateway mutation path.
- Some legacy yhttp mutation routes still use `session.lookup -> uid` instead
  of the JWT/authz pipeline.
- The policy language currently supports only a subset of yaapp account
  resolution.
- Namespace RBAC is implemented (issue #30). The `accounts` service owns the
  canonical namespace tree (`namespaces`) and membership authority
  (`namespace_members`) with the GitLab role ladder; a personal namespace is
  created per user at register, group and nested sub-namespaces via
  `accounts.ns_create`, and grants via `accounts.ns_add_member`. The token
  issuer mints memberships into the JWT `groups` claim. The policy authorizer
  resolves a repo to its owning namespace (`role_scope: repo_namespace`) or a
  namespace path (`role_scope: namespace_path`) and compares the caller's
  INHERITED role (parent-namespace grants apply to child resources), with the
  explicit `site:maintainer+` bypass. git_repo's resource-level checks were
  narrowed to the same namespace effective-role model. Picoforge policy
  role-gates repo reads AND writes (`read_tree`/`read_file` at `reporter`,
  `put_file`/`make` at `developer`, `set_public` at `maintainer`) over the
  repo's namespace. Public repos are readable by anyone — including anonymous
  callers — via an explicit `allow_public` rule flag. The site-admin bypass is
  opt-in per policy rule (`site_bypass: true`), never implicit for every
  role-gated endpoint. `accounts.ns_create` is hardened against privilege
  escalation (owner is the verified caller; the owner grant happens only when
  the namespace is freshly created; the reserved `site` namespace and subgroups
  the caller can't administer are refused). `accounts.ns_add_member` and
  `git_repo.make` reject targets that do not exist in the canonical namespaces
  table, and a repo row records its `namespace_id`. Namespace slugs are
  validated to the strict `[A-Za-z0-9._-]` segment grammar so a slug can never
  smuggle a second `<path>:<role>` into the comma/colon-delimited groups claim.
  Root-namespace creation by an external caller is a site-admin operation
  (anti-squatting); a personal namespace whose name is already a namespace is
  refused at register so an account is never stranded without ownership;
  subgroups require maintainer+ on the parent. `git_repo.make` and `delete`
  carry service-local namespace checks. Issue and pipeline operations are
  namespace-scoped too: `issues.open`/`count_open_in_repo` and
  `git_pipeline.enqueue*` resolve repo_id→namespace at the gate; `issues.close`
  resolves issue_id→repo→namespace; and `git_pipeline.job_descriptor`/`read_log`
  are enforced service-local (the leasing runner OR reporter+ on the repo
  namespace), since that compound rule can't be expressed in policy alone.
  `git_pipeline.lease`/`lease_job` are runner-token-only (policy group + a
  service-local owner check). Every repo-scoped write (`issues.open`/`close`,
  `git_pipeline.enqueue`/`enqueue_job`, `git_repo.make`/`put_file`/`set_public`/
  `delete`) carries a service-local namespace-role check in addition to the
  gateway policy gate, so a backend reached off the gateway path still enforces
  RBAC; `issues.open` records the authenticated caller as the issue author,
  never a client-supplied id. The personal namespace is created (mandatorily,
  fail-closed, idempotent-for-owner) BEFORE the account-completion marker at
  register, so a completed account always owns its `<username>` namespace.
  `/repos/new` creates the repo first and only mirrors/redirects on success,
  never leaving stale registry metadata.

  Trust is FAIL-CLOSED, not "no JWT means trusted". Every repo/issue/pipeline/
  namespace mutation denies a credential-less caller. The gateway's own
  bootstrap operations (creating a new user's personal namespace, the first-user
  `site` namespace, the `/repos/new` repo) present an explicit, gateway-signed
  `system:internal` capability JWT (`PICOMESH_GROUP_SYSTEM`) that backends
  recognise as the trusted-internal marker — it is never issued to a client and
  never derived from a user's memberships. The first-user `site` bootstrap is
  fail-closed (registration fails if it can't be created). Per-owner repo
  listings (`count_for_owner`/`list_for_owner`) are scoped to the caller's own
  namespace or a site admin, so private repo names/counts don't leak across
  namespaces. Note: the namespace authz checks make a nested `namespace_of`
  resolve RPC; under the pre-existing intermittent mesh "short RPC response"
  transport flake a legitimate caller can be transiently denied (fail-closed) —
  the transport flake itself is tracked separately. `git_pipeline.complete` is
  guarded service-local (the leasing runner OR maintainer+ on the repo
  namespace), and `git_repo.delete` is policy-gated (maintainer on
  repo_namespace, site_bypass) plus the service-local check.

  Operator/console surfaces (`*.list`/`*.list_all`, `issues.status`) are NOT in
  the gateway policy, so they are default-DENIED on the public gateway. They are
  reachable only on the loopback operator console/bridge — an intentionally
  network-isolated, app-agnostic tool — and by trusted intra-mesh calls. They
  are scoped by that network boundary rather than per-resource RBAC, because
  gating the generic console with namespace roles would break it; the public
  edge stays fail-closed.

  The low-level signing oracle is closed: `token_issuer.mint` (used only by the
  runner-token exchange, and fronted unauthenticated by the loopback operator
  bridge) REFUSES to mint any privilege-granting claim — `system:internal`, a
  `site:<ladder-role>` admin membership, or any `<namespace>:<role>` ladder
  membership — so a caller reaching the bridge cannot forge a signed JWT that
  bypasses RBAC. Non-privileged runner tokens (`site:runner,runner:<id>`) stay
  mintable.

  The first-user `site` bootstrap is created BEFORE the account-completion
  marker (`accounts_register`), keyed on `count == 0` (no completed accounts
  yet), and is FAIL-CLOSED with rollback. A backend failure creating `site`
  aborts the registration with nothing committed. If `site` is created but the
  subsequent `accounts_register` fails, the just-created `site` namespace is
  rolled back via `accounts.ns_delete` (an internal-only method, gated to the
  namespace owner / site admin / `system:internal` and refused at the gateway),
  so a failed first registration can never strand `site` under a phantom owner
  with no completed account. A concurrent first registrant that loses the race
  for `site` (it already exists, owned by another) did not create it and
  continues as a regular user. The net invariant: `site:owner` is always backed
  by a completed account, and the first deployment registrant becomes that owner.

  Repo path lengths are bounded to prevent silent truncation: a namespace path
  is stored verbatim in git_repo's fixed-size `owner_name` field and is the
  basis of the deterministic repo id, so both `git_repo.make` (`path_ok`) and
  `accounts.ns_create` reject any path that would not fit (identical limits),
  rather than truncating and desyncing the id from the stored path. The repo
  creator uid recorded by `git_repo.make` comes from the VERIFIED auth context,
  never the caller-supplied `owner_id` argument (except for the trusted
  `system:internal` capability), so a developer on a group cannot poison another
  user's creator index by passing that user's uid.

  Projects-page repo discovery (the webapp `/repos`) is ROLE-based, not
  creator-index-based. For each direct membership the caller holds, the webapp
  expands the namespace to its full subtree via `accounts.ns_subtree` (a
  reporter+ role inherits down the whole subtree) and lists each namespace's
  repos. This lists repos in subgroups reached only by INHERITED role (e.g. a
  developer on `acme` sees `acme/platform/svc` with no direct subgroup grant),
  and — because the creator index is no longer consulted for discovery — it does
  NOT leak repo paths in namespaces the caller created in but has since been
  revoked from. `list_for_owner`/`count_for_owner` remain (caller-or-site-admin
  scoped) but are not part of access-based discovery.

  Acceptance tests for issue #30 live in two places: the integration pytest
  suite `tests/integration/picoforge/test_rbac.py` (18 tests over an isolated
  mesh — the six issue acceptance cases, plus the bypass-class regressions, the
  mint-forgery guard, and first-user bootstrap) and the `[5e]`/`[5f]` blocks of
  `tools/picoforge/mesh-up.sh` (the full end-to-end smoke).

  The Picoforge webapp is namespace-based, not owner-uid-based. Repos carry a
  per-namespace-path index ("ns:<path>") so a namespace page lists its repos —
  group repos included — via `git_repo.list_for_namespace`/`count_for_namespace`
  (reporter+ on the namespace). `/_whoami` returns the caller's namespace
  memberships (path+role, never a JWT), which drives the repo-creation namespace
  picker (any namespace the user can push to) and a non-admin `/groups` area
  where namespace owners/maintainers manage members and subgroups; site admins
  use the same UI under `/admin/namespaces`. RBAC mutation forms surface the
  gateway's error instead of silently redirecting, and `accounts.ns_add_member`
  refuses a uid with no registered account (so a grant can't pre-authorize a
  future username). Nested namespaces are addressable: the webapp repo-path
  parser resolves `<namespace-path>/<repo>[/<verb>]` from the right (a trailing
  known verb delimits the repo from a multi-segment namespace), so
  `/acme/platform/svc` is the repo `svc` in `acme/platform`. The repo-creation
  namespace field is free text (with the user's pushable namespaces as
  suggestions) so an INHERITED subgroup can be entered; `/groups` likewise lets a
  maintainer jump to an inherited subgroup by path. `accounts.ns_list` and
  `accounts.ns_members` are fail-closed in-service (site-admin / maintainer-on-
  namespace), not only at the gateway; `ns_members` joins the username so the
  management UI works for non-admin maintainers without an admin roster fetch.
  `git_repo.make` idempotently repairs the owner/namespace indexes when a
  duplicate is detected, so a partial-failure retry still lists the repo. The
  owner index stores each repo's FULL PATH (`<namespace>/<repo>`), so a group
  repo created by a user links to its namespace URL (e.g. `/acme/bobweb`) on
  `/repos`, search, and the issue dashboard — not `/<username>/<repo>`. Repo
  names and namespace slugs may not be URL route words (`issues`, `runs`,
  `edit`, `new`, `settings`; namespaces also reserve `repos`/`admin`/`groups`/…),
  so the `<namespace>/<repo>/<verb>` parse stays unambiguous and a personal
  namespace can't shadow a top-level route. The repo-scoped issue reads
  (`count_open_in_repo`, `status`) are namespace-role-gated service-local, and
  the unbounded cross-repo scans (`issues`/`git_pipeline` `list`/`list_all`) are
  site-admin-only service-local (fail-closed) — they are operator/bridge
  surfaces, default-denied at the public gateway.
- PAT support is not yet real opaque-token auth. Sequential numeric ids are not
  acceptable bearer credentials.
- Password verification remains demo-grade and must be replaced before treating
  this as production security.

## Implementation Plan

1. Keep `ysecurity` as a low-level library only. Remove policy decisions and
   any invalid-JWT fallback behavior from it.
2. Implement the namespace tree and namespace membership authority: user
   namespaces, group namespaces, optional nested sub-namespaces, repo ownership
   by namespace, and effective-role resolution through parent namespaces.
3. Move the auth pipeline into the CORE invoke boundary (not a frontend), so
   yhttp, yrpc/msgpack, bridges, and future frontends share it by carrying
   headers and invoking — no per-frontend auth code. Boundary vs internal vs
   trusting is each node's own `security` config.
4. Implement configured authenticator modules:
   `session_cookie`, `bearer_jwt_token`, and generic
   `bearer_opaque_token`.
5. Make opaque authenticators call configured lookup service paths and verify
   returned JWTs.
6. Implement a configured authorizer. The first target should be the
   yaapp-compatible `policy` authorizer, extended with resource resolution such
   as `repo_id -> namespace -> effective role`. If Picomesh wants authz as a
   mesh plugin/service instead, define that contract explicitly.
7. Move browser login/session composition to the `session` service shape:
   `session.start -> token_issuer.login -> store JWT under sid -> Set-Cookie`.
8. Redesign `session` rows with idle timeout, absolute lifetime, refresh token,
   and logout deletion.
9. Replace numeric PATs with random opaque tokens stored by hash and exchanged
   for JWTs by the PAT service.
10. Pass verified JWT/auth context through local and remote calls. Backend
    resource checks must use verified auth context or fail closed.
11. Replace demo password hashing with a real password KDF and per-password
    salt.
12. Add tests for both success and failure: missing credential, malformed
    credential, expired session/JWT, absent policy entry, anonymous endpoint,
    namespace role inheritance, and valid cookie/PAT/runner token flows.

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
