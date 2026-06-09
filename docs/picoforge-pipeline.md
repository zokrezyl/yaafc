# Picoforge CI/CD — pipeline file contract and roadmap (issue #33)

This documents the **`.picoforge.yml`** pipeline-file contract (the first
sequencing step of issue #33) and records which CI/CD foundations are landed
versus tracked.

The architecture boundary is unchanged (CLAUDE.md): the webapp stays
browser-facing, the gateway stays the public API/auth boundary, and runners stay
**external clients polling the gateway** — never inbound mesh services.

## `.picoforge.yml` — the pipeline file contract

A repository describes its pipeline in a `.picoforge.yml` at the repo root. The
runner fetches it for the leased commit and executes it. The contract is
intentionally small for the MVP and forward-compatible with stages, matrices,
and retries.

```yaml
# .picoforge.yml
version: 1                      # contract version (required)

env:                            # pipeline-wide environment (optional)
  CI: "true"

stages: [build, test]           # ordered stage names (optional; default: one
                                # implicit stage running every job in file order)

jobs:
  build:
    stage: build                # which stage (must be in `stages` if present)
    image: ""                   # execution image/toolchain hint (optional)
    labels: [linux, amd64]      # runner-selection labels (optional) — a runner
                                # leases a job only if it advertises ALL labels
    timeout: 600                # seconds; overrides the per-job lease TTL
    env:                        # job-scoped env (merged over pipeline env)
      BUILD_TYPE: release
    commands:                   # the shell commands, run in order, from a clean
      - make build              # checkout of the leased commit
    artifacts:                  # paths to upload after success (optional)
      - build/out/
  test:
    stage: test
    needs: [build]              # intra-pipeline ordering beyond stages (optional)
    commands:
      - make test

secrets_policy: none            # none | inherit | named — how repo/namespace
                                # secrets are exposed to job env (default: none)
```

Field rules:

- `version` is required and must be `1`.
- A job MUST have `commands` (non-empty list of strings).
- `timeout` is seconds; absent → the enqueue-time per-job timeout (default 300).
- `labels` are matched against the runner's advertised labels: a runner leases a
  job only when it advertises every label the job requires (label matching is on
  the roadmap below; today `labels` is recorded and echoed but not yet enforced
  at lease time).
- Unknown top-level keys are rejected (fail fast on typos), unknown job keys are
  reserved for forward compatibility and ignored with a warning.

## Execution-ready job descriptor

`git_pipeline.lease_job(runner_id, labels)` returns an **immutable descriptor**
for the claimed job (or JSON `null` when the queue is empty), authenticated by
the runner's token. Current fields:

```json
{
  "job_id": 12, "repo_id": 99, "runner_id": 7, "status": 1,
  "ref": "refs/heads/main", "pipeline_path": ".picoforge.yml",
  "attempt": 1, "timeout": 600, "lease_expiry": 1718050000
}
```

- `attempt` (gh#33) is the run-attempt counter: it starts at 1 and is
  incremented every time `requeue_expired` reclaims a timed-out lease, so retries
  are distinguishable and a future max-attempts policy can fail a job that keeps
  expiring instead of requeuing it forever.
- Still to enrich (tracked below): the resolved **commit SHA** for `ref`, an
  explicit **repo clone location**, the **pipeline content / blob SHA**, and the
  **env/secrets policy** resolved for the run.

## Runner authorization (enforced today)

Runner identity is the verified token JWT (`runner:<id>` + the runner group), and
every lifecycle method is gated on it — there is no `uid==0` or
client-supplied-id bypass:

- `lease` / `lease_job` require the caller's runner token to match the
  `runner_id` it leases as (`gp_caller_owns`).
- `append_log` / `complete_job` / `read_log` / `job_descriptor` require the
  caller to **actively own the job's lease** (or reporter+ on the repo namespace
  for reads) — a runner cannot append to or complete a job it does not hold.
- The transitions are single atomic SQL UPDATEs, so a double-complete or a
  racing requeue affects zero rows the second time.

## Roadmap status

Landed (this issue's foundations):

- [x] `.picoforge.yml` contract defined (this document).
- [x] `lease_job` returns an immutable descriptor; `attempt` (run-attempt
      counter) added to the data model + descriptor; `requeue_expired` bumps it.
- [x] Runner authorization: lease/append/complete are owner-gated on the
      verified runner token (issue #30 carried this; documented here).

Tracked / sequenced (not yet implemented):

1. `.picoforge.yml` parser + validation in the runner; reject malformed files.
2. Descriptor enrichment: resolved commit SHA, repo clone location, pipeline
   blob SHA/content, resolved env/secrets policy.
3. Isolated per-job workspace: fetch/checkout the exact commit, run from a clean
   dir, archive/clean up afterward.
4. Artifact storage: gateway-mediated chunked upload/download, keyed by job/run.
5. Data model: evolve `pipeline_runs` toward run/job/step before matrix + retries.
6. Label matching enforced at lease time (a runner leases only jobs whose labels
   it advertises).
7. Runner admin UI: create/revoke tokens, runner list, labels, last heartbeat,
   current job, disabled status.
8. Triggers beyond manual enqueue: push-triggered, then scheduled and MR runs.
9. Deployment primitives: environments, deployment records, approvals, history.
10. CI observability: queue depth, lease latency, job duration, failure rate,
    heartbeat age, requeue count, log-append errors.
