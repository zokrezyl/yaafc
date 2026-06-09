# Relational storage sharding

How picomesh shards relational (SQLite) state for throughput.

- Part 1 — generic design (app-agnostic).
- Part 2 — what the engine does **today**, and the gap to the design.
- Part 3 — picoforge target mapping.

**The decision this doc commits to:** one `relational_storage` *instance* = one
*cluster* = one shard set. Multiple clusters = multiple instances, declared in
config. Not a list of clusters baked into the engine.

## Storage plugins, storage jobs

Picomesh has multiple storage backends because the data shapes are different:

| Plugin | Engine | Use for |
|---|---|---|
| `storage` | sqlite/mdbx, single env | mesh control-parent/private bookkeeping |
| `sharded_storage` | mdbx, hash-sharded KV | simple KV-shaped state, counters, caches, remaining legacy credential stores |
| `relational_storage` | SQLite, caller-sharded SQL | rows that need relational constraints, secondary indexes, transactions, joins, and SQL queries inside one shard |

The split is intentional: KV stays KV where a key/value API is enough; SQL is
used where correctness depends on relational structure.

---

## Part 1 — Generic design

### The engine is a dumb single-cluster sharded SQL store

One running instance owns:

- a directory of `shard_0.db … shard_{N-1}.db` (SQLite, WAL, one connection +
  one mutex per shard),
- a shard count `N` (config),
- nothing else.

Every call carries an explicit `shard_key` (`uint32`). The engine routes
`shard = shard_key % N`. **It never knows what the key means.**

### The caller owns the key

Only the app knows which value identifies a row. The engine never derives the
key — not from an HTTP header, not from the yrpc auth context (that context is
absent on pre-auth lookups, so depending on it is unstable by construction). The
caller computes the key and passes it.

### Two kinds of cluster

1. **uid-sharded (data).** Key = owner `uid`. Co-locates everything a user owns
   in `uid % N` → per-user workflows are shard-local (single-shard txns/joins).
2. **lookup-sharded (identity resolution).** Key = `hash(external_id)`. Maps an
   identifier the client presents (username, session id, refresh token) to a
   `uid`. Needed because the request does not know the uid yet.

A lookup cluster is also the **authority for global uniqueness** of its key: the
external id is the PRIMARY KEY of the lookup row, so "one username → one uid" is
enforced in the single shard that key hashes to. A uid-sharded table **cannot**
enforce global uniqueness of a non-uid column — a `UNIQUE(username)` inside a
uid-sharded `users` table is only shard-local and lets the same name reappear on
another shard. Global uniqueness therefore lives in the lookup cluster, never in
the data cluster.

### Why a lookup is not "one global table"

`session_id → uid` runs on every authenticated request. A single un-sharded
table serializes every request behind one mutex — the bottleneck just moves. So
a lookup cluster shards by `hash(its key)`: random sid/token and hashed username
are already uniform, so rows spread and each lookup **self-routes** with the
value the client already holds. **No uid is embedded in the token.** The token
stays a random opaque secret; the lookup *row* carries the uid.

### Request flow: resolve, then route

1. take what the client gave you (username / session id / token);
2. hit the **lookup** cluster at `hash(key) % M` → get `uid`;
3. hit the **uid** cluster at `uid % N` for the data.

### Key contract: canonicalization + hash must be identical everywhere

The shard key handed to the engine is always a `uint32`. For a lookup cluster
that value is `hash(canonical_external_id)`. Because that hash picks the shard
that owns the authoritative PRIMARY KEY row, **every producer and consumer must
compute it identically** — same canonical form, same hash function. If the
gateway and a backend disagree by one byte, they route to different shards and
the lookup / uniqueness guarantee silently breaks. So the contract is part of
the on-disk schema:

- **Canonical form** — normalize the id *before* hashing and *before* storing it
  as the PK. No caller may disagree on case / trim / charset.
- **Hash function** — one named function, shared from a common header. Changing
  either the canonical form or the hash re-homes every row, so it is versioned
  with the schema.

### Schema and aggregates

- The owning plugin creates its table on **every shard** of its cluster (DDL
  broadcast, once per worker).
- Point reads/writes hit exactly one shard.
- Cross-shard aggregates (count, list) **fan out** over every shard and merge —
  rare / admin only, never the hot path.

### Generic config + API shape

```yaml
mesh:
  services:
    <cluster_name>:                 # one instance per cluster
      plugins: [relational_storage]
      config:
        relational_storage:
          shards: <N>
          path:   <dir for this cluster's shard files>
```

A consumer **binds the instance at open** — `rel_open(&h, "<instance>")`
resolves that service's ctx/object — then runs ops on the handle. The per-call
stub signature is unchanged: `db_query(ctx, obj, hdrs, shard_key, sql, args)`.
The instance is **not** a `db_query` argument; it is whichever service the
handle was opened against. (A config-resolved `rel_open_purpose(&h, "users")`
that maps a logical purpose → instance name is fine sugar on top.)

The runtime SQL API is intentionally small:

```text
relational_storage.db.exec(shard_key, sql, args_json)  -> {"changes":N,"last_insert_rowid":M}
relational_storage.db.query(shard_key, sql, args_json) -> [ {col: val, ...}, ... ]
```

- `shard_key` selects the shard through `shard_key % N`.
- `sql` uses `?` placeholders; values must be bound, not spliced into SQL.
- `args_json` is a JSON array of bind params, e.g. `[42,"diana",1717000000]`.
- DB work is offloaded to the worker pool so the event loop does not block on
  SQLite fsync/IO; each SQLite shard is still serialized by its shard mutex.
- The engine is generic: it has no hardcoded tables. Config schema is applied to
  each shard on open, and plugin-owned tables can also be created by the owning
  plugin through DDL broadcast.

### What the engine must never do

- read the shard key from the auth context / yrpc prefix;
- know app concepts (uid, username, tenant);
- hold a list of clusters in code.

---

## Part 2 — Current implementation

This is what the code does today. The design in Part 1 is **built**; this part
records the decisions and the few deliberate limits.

### Engine

- `relational_storage` keeps a **process-global** shard set
  (`rel_set()` returns a `static struct rel_set`, `relational_storage.c`).
  **Committed decision:** one process == one cluster == one shard set. Multiple
  clusters are multiple instances == multiple processes, which is exactly how
  the split mesh deploys — each `rstore_*` runs as its own process and gets its
  own `shards`/`path` via the per-service config projection
  (`mesh.services.<self>.config` → top-level). **Collocated / all-in-one mode
  with two relational clusters in one process is NOT supported** (one config
  projection + one shard set per process). Keying the shard set by config path
  would lift that, but it is unneeded while the mesh spawns a process per
  service. The `static` is the one sanctioned file-scope datum here — a
  process-lifetime subsystem singleton behind its own init mutex.
- The engine reads `relational_storage.path/shards/schema` for ITS process.
- It exposes its real opened shard count via `db_shard_count()` so consumers
  fan out over the instance's shards, never their own local config (below).
- WAL + FK PRAGMAs are set **and read back** on open (a PRAGMA can "succeed"
  yet not take effect). Explicit policy: **WAL is best-effort** — a filesystem
  that rejects it falls back to a rollback journal, still ACID, so it is logged
  and the open continues; **FK enforcement is required** — `foreign_keys` that
  cannot be confirmed `ON` **fails `rel_init()`** rather than silently running a
  schema's foreign keys unenforced.

### Helper (`relational_sql.h`)

- `rel_open(&h, "<instance>")` opens a **named** instance —
  `picomesh_engine_service_ctx(engine, instance)` — so a consumer opens
  `rstore_uid`, `rstore_session`, … . The per-call stubs are unchanged.
- `h.shard` routing + DDL broadcast + `rel_query_int_all` / `rel_query_all`
  fan-out are present.
- **Fan-out is instance-bound.** `rel_handle_shard_count()` fetches the OPENED
  instance's shard count from the instance itself (`db_shard_count`, cached on
  the handle), instead of reading the caller process's local
  `relational_storage.shards`. A consumer wired to a remote cluster whose shard
  count differs from its own config would otherwise miss shards (undercount) or
  wrap onto already-queried shards (double-count). The hot path (point
  reads/writes) never calls it.
- **Cross-shard pagination** is `rel_query_page(&h, hdrs, base_sql, args,
  order_col, descending, offset, limit)`: it appends `ORDER BY/LIMIT` itself,
  fans out a per-shard top-K (`offset+limit`), then merges, sorts globally by
  the integer `order_col`, and applies the global offset/limit. A per-shard
  `LIMIT/OFFSET` cannot work (it limits/offsets each shard independently and
  orders only shard-locally), so callers pass the BASE query without
  `ORDER BY/LIMIT`. `rel_query_all` remains for unordered "dump everything".
- `rel_token_uid()` / `rel_token_prefix()` (from an earlier "embed uid in the
  token" idea) were **removed** — tokens stay opaque; lookups shard by
  `hash(key)`.

### Consumers

- `accounts` → `rstore_uid` (key `uid`) for `users`; `rstore_username`
  (key `hash(username)`) for the `usernames(username → uid)` uniqueness
  authority. Every user op routes by uid; `count` fans out; `list` paginates
  globally via `rel_query_page`.
- `session` → `rstore_session` (key `hash(sid)`); `token_issuer` →
  `rstore_token` (key `hash(refresh_token)`). Both shard by the opaque secret
  they issue, so per-request lookups self-route and spread across shards.

### Remaining design follow-ups (not regressions)

- **Assigned, never-reused uid** (Part 3, "Assigned uid"). `uid` is still
  `hash(username)`; the credential-orphan hazard it creates is now closed at the
  gateway (below), but assigned uids remain the cleaner end state.

---

## Part 3 — picoforge target mapping

Four clusters = four `relational_storage` instances.

| instance         | role   | tables                                                                                          | shard key             |
|------------------|--------|-------------------------------------------------------------------------------------------------|-----------------------|
| `rstore_uid`     | data   | users, user_profile, user_settings, repos, repo_members, issues, pipeline_runs, jobs, `pat_metadata`, audit | `uid`                 |
| `rstore_username`| lookup | `usernames(username → uid)`                                                                     | `hash(username)`      |
| `rstore_session` | lookup | `sessions(session_id → uid, access_jwt)`                                                        | `hash(session_id)`    |
| `rstore_token`   | lookup | `refresh_tokens(token → uid)`                                                                    | `hash(refresh_token)` |

**Tokens, split by role.** The bearer *secret* → uid lookup rows live in the
**lookup** clusters keyed by `hash(secret)` (`rstore_token` for refresh tokens).
PAT / runner secrets would each get their own lookup cluster the same way, but
that is **out of scope for this migration** — it ships the four clusters in the
table above. The per-user token *metadata* a user lists and manages (id, name,
scopes, created_at, a hash for revocation — **never the raw secret**) lives in
`rstore_uid` as `pat_metadata`, keyed by `uid`. So `rstore_uid` never holds a
usable bearer credential.

### Registration spans two clusters — make it idempotent

The name claim (`usernames` in `rstore_username`) and the `users` row (in
`rstore_uid`) are different SQLite clusters, so there is **no atomic transaction
across them**. A naive "claim then write" can strand a username if the second
write fails. Protocol:

The writes touch three stores — the lookup cluster, the credential backend
(`password_authn`), and the data cluster — so order them so each step implies
all prior ones, and make the **last** write the single completion marker:

1. **Claim** — `INSERT OR IGNORE INTO usernames(username, uid, created_at)` in
   `rstore_username` @ `hash(username)`. Existing row, *different* uid → name
   taken → reject. Fresh insert, or *same* uid (a retry) → continue.
2. **Credential** — write the password credential in `password_authn` (keyed by
   uid). NOTE: `password_authn.register` is **put-if-absent**
   (`sharded_storage_db_put_if_absent`, `store.c`) — it does **not** overwrite an
   existing credential; it returns 0. So step 2 is "create if absent", never
   "replace". That is the root of the credential-orphan hazard below.
3. **Complete** — `INSERT OR IGNORE INTO users(uid, …)` in `rstore_uid` @ `uid`,
   then `UPDATE usernames SET confirmed=1`. The `users` row is written **after**
   the credential, so its presence *implies the credential already exists*.
4. **Login requires the `users` row** — login resolves `username → uid` in
   `rstore_username`, then **requires** the `users` row in `rstore_uid`. Because
   that row is written last, "present" means name **and** credential **and**
   account are all there, so authentication can always succeed. A claim with no
   `users` row is "not registered yet".

Failure handling:

- **Any step fails before the `users` row exists:** re-running register with the
  same identity replays the idempotent steps and finishes → self-heals. No half
  state ever logs in, so there is no security window. (This is why the credential
  goes *before* the `users` row — a `users` row can never exist without a
  credential.)
- **Abandoned claim — credential-orphan hazard (the hard case):** if a
  credential was written but the `users` row never was, the credential is
  orphaned. Since `password_authn.register` is put-if-absent, a *later*
  registration of the same username gets "already exists" (0) for the
  credential. **Closed (gh#29) by claiming the name BEFORE any credential
  write.** The gateway's `route_register_post` (`frontend.c`) runs the protocol
  as:
  0. **Collision gate** — `accounts.exists(uid)` (the `users` completion marker).
     Because `uid = FNV(username)`, a *different* username can hash to an
     existing user's uid (a chosen 32-bit collision); that different name would
     win a fresh claim in step 1, so without this gate the step-2 overwrite would
     stomp the victim's password — takeover under the colliding name. If a
     completed account already holds this uid, refuse and NEVER touch the
     credential. The read **fails closed** (an `accounts`/`rstore_uid` outage is
     not read as "no account"), which is why `accounts.exists` propagates backend
     errors instead of collapsing them to 0.
  1. **Claim** — `accounts.claim_username(uid, username)` does the
     `INSERT OR IGNORE` and returns 1 ONLY to the request whose insert actually
     landed (SQLite `changes == 1`). Exactly one registrant wins; everyone else
     gets "username already taken". This is the serialization point for
     concurrent same-name registrations.
  2. **Credential — winner only.** Only the claim winner writes the credential.
     `password_authn.register` is put-if-absent; a 0 return is only possible as a
     *legacy* orphan (an older flow wrote the credential before the claim), and
     overwriting it with `password_authn.change_password` is safe because step 0
     proved no completed account holds this uid **and** we hold the fresh claim —
     so no concurrent registrant can be here to have its password stomped.
  3. **Complete** — `accounts.register` writes the `users` completion marker
     after the credential, then confirms the claim.

  Why both step 0 AND step 1: the claim alone stops two registrations of the
  *same name string*, but a *colliding different name* wins a fresh claim, so the
  uid-keyed `exists` gate is what stops the cross-name takeover. The earlier
  "exists-gate then UNCONDITIONAL overwrite" was insufficient the other way —
  two concurrent same-name registrations both passed the gate and the loser
  overwrote the winner; tying the overwrite to *winning the claim* closes that.

  Residual (inherent to hash-derived uid, NOT closed by the stopgap): this gate
  stops takeover of an *already-completed* victim. It cannot stop two colliding
  names registered in the same instant — both pass step 0 (no `users` row yet),
  both win their own (different-string) claims, and the credential keyed by the
  shared uid races. That is not a targeted takeover (the attacker cannot force a
  victim to register in that window) and only the assigned-uid fix below removes
  it, because the credential is keyed by uid while the claim is keyed by name —
  they only become 1:1 once the uid is assigned per account rather than hashed
  from the name.

  Failure handling: on any failure AFTER winning the claim (credential backend
  down, completion fails), the gateway calls `accounts.release_username` — a
  best-effort delete of the still-`confirmed=0` claim — so the name is not
  stranded and a retry can re-claim it. It **fails closed** throughout (nothing
  can log in without the `users` row). The only residual strand is the rare case
  where that compensating delete *also* fails; that (and any pre-release crash)
  is the abandoned-claim reaper's job — it must verify **both** no `users` row
  **and** no credential for that uid before freeing the name. Self-heal-on-retry
  via plain idempotency is given up in exchange for closing the takeover.
  - **(preferred end state) assigned, never-reused uid.** Assign the uid at
    claim time (sequence / snowflake), never `FNV(username)`, never reuse. A
    failed registration's uid is dead; a retry gets a *fresh* uid with an empty
    credential slot; the orphan under the old uid is unreachable garbage (lazy
    GC, never inherited). Restores clean idempotent self-heal AND makes the
    reaper safe by construction — see "assigned uid" below.
- **No cross-cluster compensation** (delete-the-claim-on-failure): the
  compensating delete can itself fail. Prefer idempotent-retry + the
  written-last completion marker.

### Flows

- **login(username, pw):** `rstore_username` @ `hash(username)` → uid → verify
  credential via `password_authn` → `rstore_uid` @ `uid` for groups → mint jwt +
  refresh → write `rstore_session` @ `hash(sid)`, `rstore_token` @ `hash(token)`.
- **request w/ cookie:** `rstore_session` @ `hash(sid)` → uid (+jwt) →
  `rstore_uid` @ `uid`.
- **GET /alice/project:** `rstore_username` @ `hash("alice")` → uid →
  `rstore_uid` @ `uid`.

### Config (mesh.services)

```yaml
rstore_uid:      { plugins:[relational_storage], config:{ relational_storage:{ shards:8, path:/var/picoforge/rel/uid } } }
rstore_username: { plugins:[relational_storage], config:{ relational_storage:{ shards:8, path:/var/picoforge/rel/username } } }
rstore_session:  { plugins:[relational_storage], config:{ relational_storage:{ shards:8, path:/var/picoforge/rel/session } } }
rstore_token:    { plugins:[relational_storage], config:{ relational_storage:{ shards:8, path:/var/picoforge/rel/token } } }
```

### Consumer wiring

- **accounts** → `rstore_uid` (key `uid`) for users; maintains `username → uid`
  in `rstore_username` (key `hash(username)`).
- **session** → `rstore_session` (key `hash(sid)`).
- **token_issuer** → `rstore_token` (key `hash(token)`).

**Username uniqueness is owned by `rstore_username`.** Its row
`usernames(username PRIMARY KEY, uid, created_at, confirmed)` is the *only*
enforcement of "one username → one uid"; `created_at`/`confirmed` drive the
idempotent registration protocol and the abandoned-claim reaper above.
`users.username` in `rstore_uid` is **denormalized display data**, not a
uniqueness mechanism — its `UNIQUE` constraint there is shard-local and must not
be relied on. (Today `accounts` DDL still carries a shard-local
`username TEXT UNIQUE` in `users` — that becomes denormalized once split.)

**Password credentials are not in the relational clusters.** They live in the
`password_authn` backend, keyed by uid (KV today, 8-shard parallel). Login
verifies via `password_authn.authenticate`; registration writes via
`password_authn.register` (step 2 of the protocol). If ever migrated they become
a `password_credentials` table in `rstore_uid` keyed by uid — out of scope here.

Each backend lists the instances it needs as remotes + a config mapping
purpose → instance name.

### Hash contract (picoforge)

- **username** — canonical form is the existing `username_ok` rule: lowercased,
  charset `[a-z0-9._-]`, 1–32 chars; reject anything else before hashing or
  storing. Hash the canonical form.
- **session_id / refresh_token** — already canonical (lowercase hex from a
  CSPRNG); hashed verbatim.
- **hash** — FNV-1a 32-bit, promoted to the shared header
  `picomesh/core/idkey.h` as `picomesh_fnv1a32`. The gateway, webapp, accounts,
  session and token_issuer all call this one symbol, so the shard key cannot
  drift between producer and consumer. Shard key = `picomesh_fnv1a32(canonical_key)`;
  the engine then does `% N`. (The gateway/webapp `hash_username` keep only the
  local `uid==0 → 1` anonymous reservation on top of the shared primitive; the
  hash itself is no longer re-implemented per site.)

### Gateway note (corrected)

The gateway is the auth boundary. It resolves the opaque sid → uid via
`rstore_session`, then forwards downstream the **verified JWT** (claims) plus the
resolved `uid` in the yrpc `yheaders` (the per-call auth-context bag — `uid` +
`jwt`, set in `frontend.c`). Backends trust the **signed JWT claims**, not a bare
uid. The resolved `uid` is used purely as the **routing key** for the
uid cluster — it is not the security mechanism, and nothing is encoded into the
token for routing.

### Assigned uid — a requirement, not just a bonus

Today `uid = FNV(username)`: it collides at scale **and** makes failed
registrations dangerous — a reused uid inherits an orphaned credential (see the
registration hazard). The authoritative `username → uid` lookup lets `uid` become
a real assigned, never-reused id (sequence / snowflake), the lookup mapping
name → id. This removes hash collisions, enables rename, and makes
partial-registration orphans inert. Treat it as part of this migration, not a
later nicety.

### Issue #18

Product tables (repos / issues / pipeline_runs / jobs) move into `rstore_uid`,
keyed by `uid`.
