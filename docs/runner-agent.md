# Runner Agent Design

Picoforge pipeline execution is handled by external **runner agents**. A runner
agent is a process running outside the gateway that registers with Picomesh,
leases jobs, executes them locally, and reports status/logs/results back.

Use the term **runner agent** for the external process. A **runner** is the
logical execution identity/resource recorded by the platform. Avoid plain
`agent` because Picomesh may later have other agent types.

## MVP Choice

The MVP uses **runner-initiated polling/lease over the gateway**.

```text
runner agent -> gateway -> git_pipeline
```

The gateway never opens a connection to the runner agent in the MVP. The runner
agent is always the client. This keeps the design simple, works through NAT and
firewalls, and fits the existing Picomesh/yhttp/yrpc direction without adding
gRPC.

The initial polling interval should be short:

```text
idle poll: 1s +/- jitter
after repeated empty polls: optional backoff to 2s, 5s, max 15s
after any activity: return to 1s
on process start: register, heartbeat, immediate lease attempt
```

A one-second poll is acceptable for the MVP. It gives fast build pickup without
requiring persistent bidirectional RPC yet.

## Authentication

Runner agents authenticate to the gateway with an opaque Bearer token.

```http
Authorization: Bearer rnr_<secret>
```

The gateway should handle this through the same auth design described in
`docs/security.md`:

```text
opaque runner token
  -> bearer_opaque_token authenticator
  -> runner token lookup
  -> verified JWT with runner claims
  -> policy authorizer
  -> git_pipeline method invocation
```

Runner JWT claims should identify the runner and constrain what it can do.
Example:

```json
{
  "sub": "runner-agent-123",
  "kind": "runner_agent",
  "runner_id": 123,
  "groups": ["site:runner", "runner:123"],
  "capabilities": ["linux", "docker"],
  "exp": 1710000900
}
```

The runner token is a bearer secret. It must be random, revocable, and never
logged. It should be shown only once at registration/creation time.

## Registration

The runner agent registers itself with the gateway before leasing jobs.

MVP registration can be simple:

```text
runner_agent.register(name, labels, version, host_info)
  -> runner_id
```

Registration records:

- `runner_id`
- display name
- labels/capabilities, for example `linux`, `x86_64`, `docker`
- runner agent version
- last seen timestamp
- status: online/offline/disabled
- token id or token fingerprint, not the raw token

There are two acceptable provisioning models:

1. Admin creates a runner token in the UI, then starts the runner agent with
   that token. The agent calls `register` using the token.
2. Admin pre-creates a runner record and receives an enrollment token. The
   first agent process that uses it binds the token to a concrete runner id.

For MVP, prefer model 1 because it is simpler.

## Job Lifecycle

Current `git_pipeline` already has a minimal queue:

```text
enqueue(repo_id) -> job_id
lease(runner_id) -> job_id or 0
complete(job_id, status) -> ok
```

The MVP should extend this into a runner-agent-aware lifecycle:

```text
queued -> leased/running -> succeeded | failed | canceled | timed_out
```

Runner agent loop:

```text
register if needed
heartbeat
loop:
  lease next matching job
  if no job:
    sleep/poll
    continue
  fetch job payload
  mark/log started
  execute locally
  append logs periodically
  complete with status
```

A lease must have an expiry. If a runner agent disappears, the gateway/pipeline
service can requeue or mark the job timed out.

MVP job methods:

```text
runner_agent.register(name, labels, version, host_info) -> runner_id
runner_agent.heartbeat(runner_id, status) -> ok
git_pipeline.lease(runner_id, labels) -> job descriptor or none
git_pipeline.append_log(job_id, chunk, offset) -> ok
git_pipeline.complete(job_id, status, summary) -> ok
```

The current `lease(runner_id) -> job_id` can stay initially, but it will need to
return enough metadata for a runner to execute the job without extra guessing:

- `job_id`
- `repo_id`
- repo clone/fetch information
- commit/ref
- pipeline definition path
- environment metadata
- timeout

## Polling Semantics

Polling should be cheap and predictable.

MVP behavior:

- runner polls `lease` every 1 second while idle;
- add small jitter to avoid synchronized bursts;
- if no job is available for many polls, optionally back off;
- any successful lease or heartbeat activity resets the delay to 1 second;
- runner sends heartbeat at a slower interval, for example every 10-30 seconds.

`lease` should be idempotent from the runner's perspective. If the runner loses
the response after the server leased a job, the next request should either
return the same active lease or expose a `current_job(runner_id)` method.

## Logs and Results

The MVP should stream logs by append calls from runner agent to gateway:

```text
git_pipeline.append_log(job_id, offset, data)
```

The server should reject log writes from a runner that does not own the active
lease for that job.

Artifacts can be deferred. For MVP, store status, timestamps, and logs. Add
artifact upload later as either:

- chunked upload through gateway;
- direct upload to configured object storage with signed upload URLs.

## Authorization Rules

Runner agents should not have general user permissions. They need a narrow role
that allows only runner operations.

Example policy intent:

```yaml
runner_agent.runner_agent.register: runner_enroll
runner_agent.runner_agent.heartbeat: runner
git_pipeline.git_pipeline.lease: runner
git_pipeline.git_pipeline.append_log: runner
git_pipeline.git_pipeline.complete: runner
```

The service must also enforce resource ownership:

- a runner may complete only jobs currently leased to that runner;
- a runner may append logs only for jobs currently leased to that runner;
- a disabled runner cannot lease new jobs;
- a revoked token cannot authenticate.

Policy answers whether the caller is a runner. The pipeline service still owns
job-state correctness.

## Why Not Gateway-to-Runner Callbacks for MVP

The alternative is a callback model:

```text
runner registers callback address/token
gateway later calls runner to start work
```

Do not use this for the MVP. It creates avoidable problems:

- runner must expose an inbound network listener;
- NAT/firewall setups become harder;
- gateway stores credentials that can command the runner;
- retries and runner availability are more complex;
- this is not needed for fast startup with 1s polling.

## Later: Persistent Connection

A future version can use a runner-initiated persistent connection:

```text
runner agent dials gateway
authenticates once
gateway binds connection to runner identity
gateway sends job/cancel messages over that existing connection
runner streams logs/results back
```

This gives Woodpecker-style bidirectional behavior without gRPC, using
Picomesh's own yrpc/msgpack transport. Defer it until the basic runner-agent
model is working.

## MVP Scope

Implement first:

- runner token creation/revocation;
- runner agent registration;
- heartbeat and online/offline state;
- 1s polling lease loop with jitter;
- lease expiry and requeue/timeout behavior;
- job descriptor returned by lease;
- log append;
- complete/fail status update;
- authorization through the gateway using opaque runner Bearer tokens.

Defer:

- gateway-to-runner callbacks;
- persistent bidirectional runner channel;
- artifact upload;
- matrix builds;
- autoscaling runner pools;
- interactive debug shells;
- advanced scheduling beyond labels/capabilities.

## Implementation status (MVP — implemented)

The MVP above is implemented and exercised end-to-end by
`tools/picoforge/mesh-up.sh` (the `[5d] runner agent` block).

- **`runner_agent` plugin** (`src/picomesh/plugins/runner_agent/`, yrpc backend
  on port 8214) owns runner identity. State lives in `sharded_storage` (context
  `runner_agent`); the opaque token is stored only as its SHA-256 hash. Methods:
  `create_token(name, labels) → {runner_id, token}` (token shown once),
  `lookup_token(token) → runner_id` (gateway-internal credential exchange),
  `revoke_token(runner_id)`, `register(runner_id, name, labels, version, host)`,
  `heartbeat(runner_id, status)`, `get`, `list`, `list_all`, `count_active`.
- **Runner identity rides the existing JWT model.** The gateway's
  `bearer_opaque_token` authenticator recognises the `rnr_` prefix, resolves it
  via `lookup_token`, and mints a JWT with `sub = runner_id`,
  `username = "runner-<id>"`, `groups = "site:runner,runner:<id>"`. Policy gates
  runner-only methods with a new `required_group: "site:runner"` rule
  (`src/picomesh/frontends/yhttp/authz.c`). No JWT or runner claims struct was
  added — it reuses `picomesh_jwt_build_claims` / `picomesh_authctx`.
- **`git_pipeline` job lifecycle** (`src/picomesh/plugins/git_pipeline/store.c`)
  gained, additively: `enqueue_job(repo_id, ref, pipeline_path, timeout)`,
  `lease_job(runner_id, labels, wait_ms) → descriptor|null` (the runner's lease
  entry point — `wait_ms>0` long-polls; legacy `lease` stays),
  `job_descriptor(job_id)`,
  `append_log(job_id, offset, chunk)` and `complete_job(job_id, status,
  summary)` (both reject a caller that does not own the active lease),
  `read_log(job_id)`, and `requeue_expired()` (sweeps timed-out leases back to
  queued). Every lease records a `lease_expiry`.
- **Runner agent process**: `tools/picoforge/runner-agent/runner-agent.py`
  (stdlib only) — registers, runs the lease loop, executes the job (a stub
  build when no real `pipeline_path` is present), streams `append_log`, reports
  `complete_job`, and heartbeats. `--once` runs a single job and exits.

### Long polling + curl transport (implemented)

`lease_job` gained an optional `wait_ms` argument so the runner can long-poll,
GitLab/GitHub-runner style, instead of busy-polling every second:

```text
lease_job(runner_id, labels, wait_ms)
  wait_ms == 0  → classic immediate poll (claim once, reply null if empty)
  wait_ms  > 0  → HOLD the call open, re-claiming every ~500ms, until a job is
                  claimed or wait_ms elapses (capped server-side at 30s)
```

Server side (`git_pipeline/store.c`): when the queue is empty and `wait_ms>0`,
`lease_job` yields its coroutine in slices via `loop_sleep_ms` — the libuv loop
keeps leasing to and enqueuing from every other connection meanwhile, so a job
enqueued during the wait is delivered on the *same held call*, not a later
poll. The handle is re-opened per slice so nothing relational is held across a
yield. This is the runner-initiated approximation of the "Later: Persistent
Connection" section: one in-flight request, near-instant pickup, still no
inbound listener on the runner.

The agent is **long-poll by default** (`--lease-wait 25`, `0` falls back to the
1s-jitter / 2-5-15s-backoff short poll). Its HTTP transport is now **curl**,
isolated in a `CurlTransport` class kept separate from the RPC envelope and
from job execution — so an `https://` gateway (or mTLS / proxy / custom CA)
needs only curl flags, no code change. The lease call uses an HTTP budget that
outlasts the server's hold so the held connection is never cut client-side.

Policy (gateway `security.policy`): admins (`site:owner`) mint/revoke/list
tokens; runners (`site:runner`) register/heartbeat/get and
lease_job/append_log/complete_job; `enqueue_job`/`job_descriptor`/`read_log` are
`authenticated`; `requeue_expired` is owner-gated.
