# relational_storage — SQLite namespace shards (gh#18)

Picoforge product state (users, repos, memberships, issues, pipelines) is
relational. Forcing it into the generic KV engine (`sharded_storage`/mdbx)
means hand-rolling secondary indexes, uniqueness, joins and transactional
multi-row updates in application code — rebuilding a weak database above a KV
store. `relational_storage` uses **SQLite** as the engine instead, sharded at
the **namespace** boundary so relational power is preserved where Picoforge
needs it most.

## Three storage plugins, three jobs

| Plugin | Engine | Use for |
|---|---|---|
| `storage` | sqlite/mdbx, single env | the mesh control parent's private pid bookkeeping |
| `sharded_storage` | mdbx, hash-sharded | KV-shaped **runtime** state: sessions, tokens, PATs, OAuth codes, caches |
| `relational_storage` | **SQLite, namespace-sharded** | relational **product** state: users, repos, members, issues, pipelines |

`storage` and `sharded_storage` are unchanged. The split is deliberate: KV
where KV is the right abstraction, SQL where correctness is relational.

## Sharding model

A namespace is a user or org. Each namespace is assigned to a shard, and a
shard is one SQLite database holding the strongly-related state of every
namespace mapped to it:

```text
namespace_id  ->  shard = namespace_id % N  ->  <relational_storage.path>/shard_<i>.db
```

Inside one shard you get the full relational toolbox — joins, foreign keys,
`UNIQUE` constraints, indexes, transactions — and common workflows stay
**shard-local**:

- create a repo in a namespace, list a namespace's repos
- check repo membership / permissions
- create / list / update issues in a repo
- enqueue / list pipeline runs for a repo
- enforce local uniqueness like `(namespace_id, repo_name)`

Across shards you use IDs and service calls, not SQL joins — and materialized
read models for global dashboards/search. Cross-namespace operations (e.g.
repo transfer) are explicit workflows, never accidental multi-shard SQL.

## API

The plugin is a generic SQL engine, not a product API — the service layer
(accounts/git_repo/issues/git_pipeline) owns routing and correctness:

```text
relational_storage.db.exec(ns_id, sql, args_json)  -> {"changes":N,"last_insert_rowid":M}
relational_storage.db.query(ns_id, sql, args_json) -> [ {col: val, …}, … ]
```

- `ns_id` selects the shard.
- `sql` uses `?` placeholders only — values never get spliced into SQL.
- `args_json` is a JSON array of bind params (`[42,"diana",1717000000]`).
- DB work is offloaded to the worker pool (like `sharded_storage`) so the
  event loop never blocks on an fsync; each shard is serialized by a mutex.

## The engine carries NO data model

`relational_storage` is generic. It has **no hardcoded tables**. The per-shard
schema, the shard count, and the data directory all come from config; the
engine applies the configured DDL verbatim to each shard on open (plus
`PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;`). A node with no `schema`
gets empty shards and runs its own DDL through `exec`. picoforge's product
model lives in `picoforge.yaml`, never in the plugin.

## Config

```yaml
relational_storage:
  port: 8213
  workers: 4
  plugins: [relational_storage]
  config:
    relational_storage:
      path: /tmp/picoforge/relational   # REQUIRED — no silent default
      shards: 8                         # shard count (caller routes by shard_key % N)
      schema: |                         # applied verbatim to every shard
        CREATE TABLE IF NOT EXISTS users(uid INTEGER PRIMARY KEY, username TEXT UNIQUE, …);
        CREATE TABLE IF NOT EXISTS repos(id INTEGER PRIMARY KEY, namespace_id INTEGER,
          name TEXT, owner_uid INTEGER, …, UNIQUE(namespace_id, name));
        CREATE TABLE IF NOT EXISTS issues(…, UNIQUE(repo_id, number));
        CREATE TABLE IF NOT EXISTS pipeline_runs(…);
        -- + repo_members, namespaces, indexes
```

picoforge's full schema (users / namespaces / repos / repo_members / issues /
pipeline_runs + indexes) is configured under that `schema:` block. Swap it for
any other model — the engine doesn't care.

## Migration status (gh#18)

- [x] `relational_storage` plugin: SQLite, namespace shards, exec/query, schema.
- [x] accounts → relational (users / roster), in the global shard.
- [x] session → relational (`sessions` table), in the global shard.
- [x] token_issuer → relational (`refresh_tokens` table), in the global shard.
- [ ] git_repo metadata → relational (repos / repo_members).
- [x] issues → relational (`issues` table in `rstore_uid`, global shard): the
      hand-kept `open:<repo>` KV counter is now `COUNT(… status='open')` and the
      close CAS is `UPDATE … WHERE status='open'`, so the open count can't drift.
- [ ] git_pipeline → relational (pipeline_runs).
- [ ] global reads (e.g. roster, totals) via shard fan-out / read models.

### What is SQL-backed today, and why

`accounts`, `session`, and `token_issuer` are **intentionally** on
`relational_storage` now. Each owns a small, strongly-typed table with a real
primary key and `UNIQUE`/lookup semantics (`users(uid PK)`,
`sessions(sid PK)`, `refresh_tokens(token PK)`), and each query is a single
indexed row lookup. These are global, non-namespaced tables, so they live in
one shard — `REL_SHARD_GLOBAL` — and a lookup or count is one query with
nothing to scatter/gather. SQL is the better fit than faking columns behind
prefixed KV keys, so these moved first. This supersedes the earlier note that
"auth/runtime services stay on `sharded_storage`": only `password_authn`,
`personal_access_tokens`, and `github_authn` remain KV today.

### Product state — still KV (scope of the remaining gh#18 work)

`git_repo` (incl. repo membership/permission metadata) and `git_pipeline` still
store their state as prefixed keys in `sharded_storage`. `issues` has moved to
SQL on the global shard (see above); the remaining git_repo/git_pipeline work
is the open part of gh#18 and is **namespace-sharded**, not
global: a repo/issue/pipeline workflow should be shard-local
(`shard = namespace_id % N`) and transactional, enforcing local uniqueness such
as `(namespace_id, repo_name)` and `(repo_id, issue_number)`. That migration is
deliberately out of scope here — it is its own change — and these services keep
working on KV until then.
