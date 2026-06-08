/* accounts — the user registry, as RELATIONAL ROWS.
 *
 * A user is a ROW in the `users` table (real columns), stored in the
 * `relational_storage` service — not a scatter of `user:<uid>`, `name:<uid>`,
 * `groups:<uid>`, `balance:<uid>` keys in a KV store. The accounts plugin OWNS
 * this table: it runs `CREATE TABLE IF NOT EXISTS` itself, once per worker; the
 * consumer app does not define another plugin's schema.
 *
 *   users(uid PK, username UNIQUE, groups, balance, created_at)
 *
 *   register(uid, username)  1 newly created / 0 already exists
 *   exists(uid)              1 / 0
 *   set_balance(uid, n)      1 (error if uid unknown)
 *   balance(uid)             current balance (error if uid unknown)
 *   set_groups(uid, csv)     store "<account>:<role>,…" memberships
 *   groups(uid)              the groups CSV ("" if none)
 *   count()                  live user count
 *   list / list_all          [{"uid":…,"username":…}, …]
 */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yplatform/time.h>
#include <picomesh/ycore/idkey.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/secret.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/plugin/relational_storage/relational_sql.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Two clusters (docs/sharded-relational-storage.md):
 *   rstore_uid      — `users` rows, sharded by uid (the data cluster).
 *   rstore_username — `usernames` (username→uid) lookup, sharded by
 *                     hash(username); the SOLE authority for global username
 *                     uniqueness.
 * users.username is denormalized display data — no global UNIQUE here (a
 * shard-local UNIQUE could not enforce it across shards). */
#define ACCOUNTS_USERS_DDL \
    "CREATE TABLE IF NOT EXISTS users(" \
    "uid INTEGER PRIMARY KEY, username TEXT, " \
    "groups TEXT NOT NULL DEFAULT '', balance INTEGER NOT NULL DEFAULT 0, " \
    "created_at INTEGER NOT NULL DEFAULT 0)"
#define ACCOUNTS_NAMES_DDL \
    "CREATE TABLE IF NOT EXISTS usernames(" \
    "username TEXT PRIMARY KEY, uid INTEGER NOT NULL, " \
    "created_at INTEGER NOT NULL DEFAULT 0, confirmed INTEGER NOT NULL DEFAULT 0)"

/* Namespace authority (issue #30 / docs/security.md "Authorization Domain
 * Model"). A namespace is the ownership container for repos; a user gets a
 * personal namespace at register, groups are namespaces too, and groups may
 * nest into sub-namespaces. `path` is the full slash-joined namespace path
 * (`acme`, `acme/platform`); `id` is its deterministic FNV-1a hash so callers
 * agree on the id without a lookup round-trip (same trick git_repo uses for
 * repo_id). `parent_id` is the hash of the parent path (0 for a root namespace).
 *
 * Memberships carry the GitLab role ladder (guest < reporter < developer <
 * maintainer < owner). They are the canonical source for the JWT `groups`
 * claim: the token issuer reads a user's memberships at login and flattens them
 * to "<path>:<role>" strings. Role inheritance through parent namespaces is
 * resolved at AUTHORIZATION time (the authorizer walks the namespace path), so
 * only DIRECT grants are stored here. Both tables live in rstore_uid;
 * `namespaces` shards by hash(path) (point lookups by path), `namespace_members`
 * shards by uid (a user's memberships co-locate in one shard). */
#define ACCOUNTS_NS_DDL \
    "CREATE TABLE IF NOT EXISTS namespaces(" \
    "id INTEGER PRIMARY KEY, parent_id INTEGER NOT NULL DEFAULT 0, " \
    "slug TEXT NOT NULL, path TEXT NOT NULL, kind TEXT NOT NULL DEFAULT 'user', " \
    "owner_uid INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL DEFAULT 0, " \
    "UNIQUE(path))"
#define ACCOUNTS_NSMEMBER_DDL \
    "CREATE TABLE IF NOT EXISTS namespace_members(" \
    "namespace_id INTEGER NOT NULL, namespace_path TEXT NOT NULL, " \
    "uid INTEGER NOT NULL, role TEXT NOT NULL DEFAULT 'guest', " \
    "PRIMARY KEY(namespace_id, uid))"

#define ACCOUNTS_STORE_UID  "rstore_uid"      /* mesh service (remote routing) */
#define ACCOUNTS_STORE_NAME "rstore_username"
#define ACCOUNTS_DB_UID     "uid"             /* logical database within the instance */
#define ACCOUNTS_DB_NAME    "username"

struct PICOMESH_CLASS_ANNOTATE("class@accounts:accounts") accounts_accounts_data {
    int users_schema_ensured; /* per-worker: `users` created in rstore_uid */
    int names_schema_ensured; /* per-worker: `usernames` created in rstore_username */
    int ns_schema_ensured;    /* per-worker: namespace tables created in rstore_uid */
};

static struct accounts_accounts_data *acc(struct object *obj)
{
    return (struct accounts_accounts_data *)((char *)obj + sizeof(struct object));
}

/* Open the uid-sharded data cluster (users). Caller sets h->shard = uid. */
static struct picomesh_void_result accounts_open_uid(struct rel_handle *h, struct yheaders *hdrs, struct object *obj)
{
    struct picomesh_void_result o = rel_open(h, ACCOUNTS_STORE_UID, ACCOUNTS_DB_UID);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_void, "accounts: open rstore_uid failed", o);
    return rel_ensure_schema(h, hdrs, &acc(obj)->users_schema_ensured, ACCOUNTS_USERS_DDL);
}

/* Open the username→uid lookup cluster. Caller sets h->shard = hash(username). */
static struct picomesh_void_result accounts_open_names(struct rel_handle *h, struct yheaders *hdrs, struct object *obj)
{
    struct picomesh_void_result o = rel_open(h, ACCOUNTS_STORE_NAME, ACCOUNTS_DB_NAME);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_void, "accounts: open rstore_username failed", o);
    return rel_ensure_schema(h, hdrs, &acc(obj)->names_schema_ensured, ACCOUNTS_NAMES_DDL);
}

/* Ensure the two namespace tables exist on EVERY shard of rstore_uid (rows
 * spread by hash(path) / uid, so the tables must exist in each). Guarded by the
 * per-worker `ns_schema_ensured` flag so it runs at most once. Two CREATEs ⇒
 * two execs per shard (db_exec runs one statement at a time). */
static struct picomesh_void_result accounts_ensure_ns_schema(struct rel_handle *h, struct yheaders *hdrs, struct object *obj)
{
    if (acc(obj)->ns_schema_ensured) return PICOMESH_OK_VOID();
    struct picomesh_int_result sc = rel_handle_shard_count(h, hdrs);
    if (PICOMESH_IS_ERR(sc)) return PICOMESH_ERR(picomesh_void, "accounts: namespace schema shard count failed", sc);
    int shards = sc.value;
    uint32_t saved = h->shard;
    for (int i = 0; i < shards; ++i) {
        h->shard = (uint32_t)i;
        struct picomesh_json_result r1 = rel_exec(h, hdrs, ACCOUNTS_NS_DDL, "[]");
        if (PICOMESH_IS_ERR(r1)) { h->shard = saved; return PICOMESH_ERR(picomesh_void, "accounts: namespaces create failed", r1); }
        free(r1.value);
        struct picomesh_json_result r2 = rel_exec(h, hdrs, ACCOUNTS_NSMEMBER_DDL, "[]");
        if (PICOMESH_IS_ERR(r2)) { h->shard = saved; return PICOMESH_ERR(picomesh_void, "accounts: namespace_members create failed", r2); }
        free(r2.value);
    }
    h->shard = saved;
    acc(obj)->ns_schema_ensured = 1;
    return PICOMESH_OK_VOID();
}

/* Deterministic namespace id: FNV-1a of the full path (0 nudged to 1). The id
 * is derivable from the path so every node agrees on it without a lookup. */
static int64_t ns_id_of(const char *path)
{
    uint32_t hash = picomesh_fnv1a32(path ? path : "");
    return (int64_t)(hash ? hash : 1u);
}

/* Canonical existence check: the namespace_id recorded in the `namespaces`
 * table for `path`, or 0 if no such namespace exists. The namespaces table —
 * NOT a derived hash — is the authority, so a grant or a repo can be rejected
 * when its target namespace was never created. Shards by hash(path). */
static int64_t ns_lookup(struct rel_handle *h, struct yheaders *hdrs, const char *path)
{
    h->shard = picomesh_fnv1a32(path);
    char *args = rel_args1s(path);
    int found = 0;
    int64_t id = rel_query_int(h, hdrs, "SELECT id FROM namespaces WHERE path=?", args, "id", 0, &found);
    free(args);
    return found ? id : 0;
}

/* A single namespace path SEGMENT, validated to a STRICT grammar:
 * `[A-Za-z0-9._-]`, length 1..63, no leading dot. Identical to git_repo's
 * path_segment_ok. This is security-critical: namespace paths are serialized
 * into the comma/colon-delimited JWT `groups` claim ("<path>:<role>,..."), so a
 * slug containing ',' ':' or ';' could otherwise smuggle a second membership
 * (e.g. a slug "x,site" would inject "site:owner" into the claim and grant the
 * site-admin bypass). Restricting the charset to the same safe segment grammar
 * the repo paths use closes that injection before any namespace can enter a
 * token claim. */
static int ns_slug_ok(const char *slug)
{
    if (!slug || !*slug || slug[0] == '.') return 0;
    /* "-" is the GitLab-style routing sentinel ("/-/<command>", "<repo>/-/<verb>")
     * in the webapp, so a namespace segment may never be exactly "-". */
    if (strcmp(slug, "-") == 0) return 0;
    size_t n = 0;
    for (const char *p = slug; *p; ++p, ++n) {
        if (n >= 63) return 0;
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            return 0;
    }
    /* Reject names that collide with webapp URL route words — both the repo
     * verbs (so `<ns>/<repo>/<verb>` stays unambiguous) and the top-level page
     * routes (so a personal namespace can't shadow /admin, /groups, …). A
     * namespace slug IS a user's path, so this also reserves those usernames
     * (the GitLab convention). */
    static const char *const reserved[] = {
        "issues", "runs", "edit", "new", "settings",
        "repos", "admin", "groups", "dashboard", "search",
        "login", "register", "logout", "static",
    };
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (strcmp(slug, reserved[i]) == 0) return 0;
    return 1;
}

/* Existence of the completed `users` row for `uid`, AS A RESULT. A backend/query
 * failure PROPAGATES (it must not collapse to "not found"): callers use this as
 * a correctness gate — registration's collision/takeover guard and login —
 * which must fail closed on an outage, not silently treat the account as absent.
 * COUNT(*) on the PK is always one row, so there is no found/fallback ambiguity. */
static struct picomesh_int_result account_exists(struct rel_handle *h, struct yheaders *hdrs, uint32_t uid)
{
    h->shard = uid;
    char *args = rel_args1i((int64_t)uid);
    struct picomesh_int64_result r =
        rel_query_int_result(h, hdrs, "SELECT COUNT(*) AS n FROM users WHERE uid=?", args, "n", 0);
    free(args);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int, "account_exists: query failed", r);
    return PICOMESH_OK(picomesh_int, r.value > 0 ? 1 : 0);
}

/* Claim a username in the lookup cluster — the FIRST step of registration and
 * the uniqueness authority. Returns 1 iff THIS call won a FRESH claim (the
 * INSERT OR IGNORE actually inserted the row), 0 if the name was already claimed
 * (by a completed account, an in-flight registration, or an abandoned one).
 *
 * The caller MUST NOT write or overwrite the credential unless it won the claim.
 * That is what serializes concurrent registrations of the same name: exactly
 * one INSERT OR IGNORE inserts (changes==1), so only one registrant is the
 * winner, and a loser can never reach the credential to overwrite the winner's
 * password. A `users` row (the completion marker) is still written last, after
 * the credential, by accounts_register. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_claim_username")
struct picomesh_int_result accounts_accounts_claim_username_impl(struct ctx *ctx, struct object *obj,
                                                                 struct yheaders *hdrs,
                                                                 uint32_t uid, const char *username)
{
    (void)ctx;
    if (!username) username = "";
    struct rel_handle nh;
    struct picomesh_void_result on = accounts_open_names(&nh, hdrs, obj);
    if (PICOMESH_IS_ERR(on)) return PICOMESH_ERR(picomesh_int, "accounts_claim_username: open names failed", on);
    nh.shard = picomesh_fnv1a32(username);
    struct yjson_writer *cw = yjson_writer_new();
    yjson_writer_begin_array(cw);
    yjson_writer_string(cw, username);
    yjson_writer_int(cw, (int64_t)uid);
    yjson_writer_int(cw, picomesh_yplatform_time_wall_ms() / 1000);
    char *cargs = rel_args_take(cw);
    int claimed = rel_exec_changes(&nh, hdrs,
        "INSERT OR IGNORE INTO usernames(username,uid,created_at) VALUES(?,?,?)", cargs);
    free(cargs);
    if (claimed < 0) return PICOMESH_ERR(picomesh_int, "accounts_claim_username: name claim failed");
    /* changes==1 ⇒ we inserted the row (and its uid is ours) ⇒ we won.
     * changes==0 ⇒ the row already existed ⇒ someone else holds the name. */
    return PICOMESH_OK(picomesh_int, claimed == 1 ? 1 : 0);
}

/* Best-effort release of a username claim that THIS registration won but could
 * not complete (a later step failed). Deletes ONLY an UNCONFIRMED claim for this
 * uid — never a confirmed one, so a completed account's name can never be freed
 * by a stray release. Returns 1 if a row was removed, 0 otherwise. This unstrands
 * the name so a retry can re-claim it, turning the "permanent until reaped" DoS
 * into a transient one; a background reaper still covers the rare case where
 * this compensating delete itself fails. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_release_username")
struct picomesh_int_result accounts_accounts_release_username_impl(struct ctx *ctx, struct object *obj,
                                                                   struct yheaders *hdrs,
                                                                   uint32_t uid, const char *username)
{
    (void)ctx;
    if (!username) username = "";
    struct rel_handle nh;
    struct picomesh_void_result on = accounts_open_names(&nh, hdrs, obj);
    if (PICOMESH_IS_ERR(on)) return PICOMESH_ERR(picomesh_int, "accounts_release_username: open names failed", on);
    nh.shard = picomesh_fnv1a32(username);
    struct yjson_writer *aw = yjson_writer_new();
    yjson_writer_begin_array(aw);
    yjson_writer_string(aw, username);
    yjson_writer_int(aw, (int64_t)uid);
    char *args = rel_args_take(aw);
    int changes = rel_exec_changes(&nh, hdrs,
        "DELETE FROM usernames WHERE username=? AND uid=? AND confirmed=0", args);
    free(args);
    if (changes < 0) return PICOMESH_ERR(picomesh_int, "accounts_release_username: delete failed");
    return PICOMESH_OK(picomesh_int, changes > 0 ? 1 : 0);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_register")
struct picomesh_int_result accounts_accounts_register_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                    uint32_t uid, const char *username)
{
    (void)ctx;
    if (!username) username = "";

    /* 1) Claim the name in the lookup cluster — the uniqueness authority.
     * INSERT OR IGNORE elects one winner; read it back and require it to be OUR
     * uid (else the name is taken by another). */
    struct rel_handle nh;
    struct picomesh_void_result on = accounts_open_names(&nh, hdrs, obj);
    if (PICOMESH_IS_ERR(on)) return PICOMESH_ERR(picomesh_int, "accounts_register: open names failed", on);
    nh.shard = picomesh_fnv1a32(username);
    struct yjson_writer *cw = yjson_writer_new();
    yjson_writer_begin_array(cw);
    yjson_writer_string(cw, username);
    yjson_writer_int(cw, (int64_t)uid);
    yjson_writer_int(cw, picomesh_yplatform_time_wall_ms() / 1000);
    char *cargs = rel_args_take(cw);
    int claimed = rel_exec_changes(&nh, hdrs,
        "INSERT OR IGNORE INTO usernames(username,uid,created_at) VALUES(?,?,?)", cargs);
    free(cargs);
    if (claimed < 0) return PICOMESH_ERR(picomesh_int, "accounts_register: name claim failed");
    char *qa = rel_args1s(username);
    int found = 0;
    int64_t owner = rel_query_int(&nh, hdrs, "SELECT uid FROM usernames WHERE username=?", qa, "uid", 0, &found);
    free(qa);
    if (!found) return PICOMESH_ERR(picomesh_int, "accounts_register: claim readback failed");
    if ((uint32_t)owner != uid) { ydebug("accounts_register: name taken uid=%u", uid); return PICOMESH_OK(picomesh_int, 0); }

    /* 2) Write the user row in the data cluster — the completion marker. The
     * gateway wrote the credential BEFORE this call, so a users row implies a
     * credential exists. */
    struct rel_handle uh;
    struct picomesh_void_result ou = accounts_open_uid(&uh, hdrs, obj);
    if (PICOMESH_IS_ERR(ou)) return PICOMESH_ERR(picomesh_int, "accounts_register: open uid failed", ou);
    uh.shard = uid;
    struct yjson_writer *uw = yjson_writer_new();
    yjson_writer_begin_array(uw);
    yjson_writer_int(uw, (int64_t)uid);
    yjson_writer_string(uw, username);
    yjson_writer_int(uw, picomesh_yplatform_time_wall_ms() / 1000);
    char *uargs = rel_args_take(uw);
    int changes = rel_exec_changes(&uh, hdrs,
        "INSERT OR IGNORE INTO users(uid,username,created_at) VALUES(?,?,?)", uargs);
    free(uargs);
    if (changes < 0) return PICOMESH_ERR(picomesh_int, "accounts_register: user insert failed");

    /* 3) Confirm the claim now that the users row exists. */
    nh.shard = picomesh_fnv1a32(username);
    char *ca = rel_args1s(username);
    (void)rel_exec_changes(&nh, hdrs, "UPDATE usernames SET confirmed=1 WHERE username=?", ca);
    free(ca);

    if (changes == 0) { ydebug("accounts_register: uid=%u already exists", uid); return PICOMESH_OK(picomesh_int, 0); }
    yinfo("accounts_register: uid=%u name=%s", uid, username);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_exists")
struct picomesh_int_result accounts_accounts_exists_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                  uint32_t uid)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int, "accounts_exists: open failed", oh);
    return account_exists(&h, hdrs, uid);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_set_balance")
struct picomesh_int_result accounts_accounts_set_balance_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                        uint32_t uid, int64_t n)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int, "accounts_set_balance: open failed", oh);
    h.shard = uid;
    char *args = rel_args2i(n, (int64_t)uid);
    int changes = rel_exec_changes(&h, hdrs, "UPDATE users SET balance=? WHERE uid=?", args);
    free(args);
    if (changes < 0) return PICOMESH_ERR(picomesh_int, "accounts_set_balance: write failed");
    if (changes == 0) return PICOMESH_ERR(picomesh_int, "accounts_set_balance: unknown uid");
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_balance")
struct picomesh_int64_result accounts_accounts_balance_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                      uint32_t uid)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int64, "accounts_balance: open failed", oh);
    h.shard = uid;
    char *args = rel_args1i((int64_t)uid);
    int found = 0;
    int64_t bal = rel_query_int(&h, hdrs, "SELECT balance FROM users WHERE uid=?", args, "balance", 0, &found);
    free(args);
    if (!found) return PICOMESH_ERR(picomesh_int64, "accounts_balance: unknown uid");
    return PICOMESH_OK(picomesh_int64, bal);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_count")
struct picomesh_size_result accounts_accounts_count_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_size, "accounts_count: open failed", oh);
    /* Users spread across shards by uid → sum the per-shard counts. */
    struct picomesh_int64_result n = rel_query_int_all(&h, hdrs, "SELECT COUNT(*) AS n FROM users", "[]", "n");
    if (PICOMESH_IS_ERR(n)) return PICOMESH_ERR(picomesh_size, "accounts_count: aggregate failed", n);
    return PICOMESH_OK(picomesh_size, (size_t)(n.value < 0 ? 0 : n.value));
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_set_groups")
struct picomesh_int_result accounts_accounts_set_groups_impl(struct ctx *ctx, struct object *obj,
                                                             struct yheaders *hdrs,
                                                             uint32_t uid, const char *groups_csv)
{
    (void)ctx;
    if (!groups_csv) groups_csv = "";
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int, "accounts_set_groups: open failed", oh);
    h.shard = uid;
    struct yjson_writer *aw = yjson_writer_new();
    yjson_writer_begin_array(aw);
    yjson_writer_string(aw, groups_csv);
    yjson_writer_int(aw, (int64_t)uid);
    char *args = rel_args_take(aw);
    int changes = rel_exec_changes(&h, hdrs, "UPDATE users SET groups=? WHERE uid=?", args);
    free(args);
    if (changes < 0) return PICOMESH_ERR(picomesh_int, "accounts_set_groups: write failed");
    if (changes == 0) return PICOMESH_ERR(picomesh_int, "accounts_set_groups: unknown uid");
    yinfo("accounts_set_groups: uid=%u groups=%s", uid, groups_csv);
    return PICOMESH_OK(picomesh_int, 1);
}

/* --- pluggable external group resolution (issue #31) ----------------------
 *
 * `accounts.accounts.groups(uid)` stays the single facade the rest of the mesh
 * (token_issuer → JWT, repo/namespace authz) consumes. To let a customer's
 * external directory (LDAP/AD, …) grant Picoforge roles WITHOUT replacing the
 * whole accounts plugin, the local `namespace_members` result is merged with an
 * optional external provider's groups, mapped to `namespace:role` by config, with
 * the highest role winning per namespace.
 *
 * Config shape (all optional; absent/disabled == today's local-only behavior):
 *
 *   accounts:
 *     external_groups:
 *       enabled: true
 *       fail_closed: true          # provider/config error → fail groups() (default)
 *       members:                   # FIRST-PASS directory source: uid -> [group].
 *         "1001": [acme-platform-devs, picoforge-admins]   # a real LDAP backend
 *                                  # replaces ONLY this membership lookup.
 *       mappings:                  # external group -> Picoforge namespace:role
 *         - { external_group: acme-platform-devs, namespace: acme/platform, role: developer }
 *         - { external_group: picoforge-admins,   namespace: site,          role: maintainer }
 */
struct accounts_ns_role {
    char namespace_path[160];
    int rank;
};

/* Keep the highest role rank per namespace. Returns -1 on OOM, else 1. */
static int accounts_ns_role_upsert(struct accounts_ns_role **arr, size_t *count, size_t *cap,
                                   const char *namespace_path, int rank)
{
    if (rank < 0 || !namespace_path || !*namespace_path) return 1;
    for (size_t i = 0; i < *count; ++i) {
        if (strcmp((*arr)[i].namespace_path, namespace_path) == 0) {
            if (rank > (*arr)[i].rank) (*arr)[i].rank = rank;
            return 1;
        }
    }
    if (*count == *cap) {
        size_t new_cap = *cap ? *cap * 2 : 8;
        struct accounts_ns_role *grown = realloc(*arr, new_cap * sizeof(**arr));
        if (!grown) return -1;
        *arr = grown;
        *cap = new_cap;
    }
    snprintf((*arr)[*count].namespace_path, sizeof((*arr)[*count].namespace_path), "%s",
             namespace_path);
    (*arr)[*count].rank = rank;
    (*count)++;
    return 1;
}

/* Fold a "<path>:<role>,..." CSV into the per-namespace highest-role map. */
static int accounts_ns_role_add_csv(struct accounts_ns_role **arr, size_t *count, size_t *cap,
                                    const char *csv)
{
    for (const char *cursor = csv; cursor && *cursor;) {
        const char *comma = strchr(cursor, ',');
        size_t seg = comma ? (size_t)(comma - cursor) : strlen(cursor);
        const char *colon = memchr(cursor, ':', seg);
        if (colon) {
            size_t path_len = (size_t)(colon - cursor);
            size_t role_len = seg - path_len - 1;
            char namespace_path[160], role[32];
            if (path_len < sizeof(namespace_path) && role_len < sizeof(role)) {
                memcpy(namespace_path, cursor, path_len);
                namespace_path[path_len] = 0;
                memcpy(role, colon + 1, role_len);
                role[role_len] = 0;
                if (accounts_ns_role_upsert(arr, count, cap, namespace_path,
                                            picomesh_role_rank(role)) < 0)
                    return -1;
            }
        }
        if (!comma) break;
        cursor = comma + 1;
    }
    return 0;
}

/* Merge `local_csv` (direct namespace_members grants) with an optional external
 * directory provider's mapped roles, highest-role-wins per namespace, and return
 * the flattened CSV. When `accounts.external_groups` is absent/disabled this is
 * exactly `strdup(local_csv)` — local-only behavior is unchanged. */
static struct picomesh_string_result accounts_merge_external_groups(uint32_t uid,
                                                                    const char *username,
                                                                    const char *local_csv)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    const struct yconfig *cfg = engine ? picomesh_engine_config(engine) : NULL;
    const struct yconfig_node *root = NULL;
    if (cfg) {
        struct yconfig_node_ptr_result config_node_res =
            yconfig_get(cfg, "accounts.external_groups");
        if (PICOMESH_IS_ERR(config_node_res)) {
            yerror("accounts: reading 'accounts.external_groups' failed: %s",
                   config_node_res.error.msg ? config_node_res.error.msg : "?");
            picomesh_error_destroy(config_node_res.error);
        } else {
            root = config_node_res.value;
        }
    }
    int enabled = root && yconfig_node_as_bool(yconfig_node_get(root, "enabled"), 0);
    if (!enabled) {
        char *copy = strdup(local_csv ? local_csv : "");
        if (!copy) return PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory");
        return PICOMESH_OK(picomesh_string, copy);
    }
    int fail_closed = yconfig_node_as_bool(yconfig_node_get(root, "fail_closed"), 1);

    struct accounts_ns_role *roles = NULL;
    size_t count = 0, cap = 0;
    if (accounts_ns_role_add_csv(&roles, &count, &cap, local_csv) < 0) {
        free(roles);
        return PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory");
    }

    /* Resolve THIS user's external group names. First-pass directory source: a
     * config map keyed by username (LDAP's `uid={username}`), with a uid-string
     * fallback. A real LDAP/AD provider replaces only this lookup (bind + group
     * search) — the mapping + merge below is unchanged. */
    const struct yconfig_node *members = yconfig_node_get(root, "members");
    const struct yconfig_node *user_external_groups = NULL;
    if (members) {
        if (username && *username)
            user_external_groups = yconfig_node_get(members, username);
        if (!user_external_groups) {
            char uid_key[16];
            snprintf(uid_key, sizeof(uid_key), "%u", uid);
            user_external_groups = yconfig_node_get(members, uid_key);
        }
    }
    const struct yconfig_node *mappings = yconfig_node_get(root, "mappings");

    int provider_ok = 1;
    if (user_external_groups && yconfig_node_kind(user_external_groups) == YCONFIG_LIST &&
        mappings && yconfig_node_kind(mappings) == YCONFIG_LIST) {
        size_t group_count = yconfig_node_size(user_external_groups);
        size_t mapping_count = yconfig_node_size(mappings);
        for (size_t group_idx = 0; group_idx < group_count; ++group_idx) {
            const char *external_group =
                yconfig_node_as_string(yconfig_node_at(user_external_groups, group_idx), NULL);
            if (!external_group || !*external_group) continue;
            for (size_t mapping_idx = 0; mapping_idx < mapping_count; ++mapping_idx) {
                const struct yconfig_node *mapping_node = yconfig_node_at(mappings, mapping_idx);
                const char *mapped_external_group =
                    yconfig_node_as_string(yconfig_node_get(mapping_node, "external_group"), NULL);
                const char *namespace_path =
                    yconfig_node_as_string(yconfig_node_get(mapping_node, "namespace"), NULL);
                const char *role =
                    yconfig_node_as_string(yconfig_node_get(mapping_node, "role"), NULL);
                if (!mapped_external_group || strcmp(mapped_external_group, external_group) != 0)
                    continue;
                if (!namespace_path || !role) {
                    yerror("accounts: external_groups mapping for '%s' missing namespace/role",
                           external_group);
                    provider_ok = 0;
                    continue;
                }
                int rank = picomesh_role_rank(role);
                if (rank < 0) {
                    yerror("accounts: external_groups mapping has unknown role '%s'", role);
                    provider_ok = 0;
                    continue;
                }
                if (accounts_ns_role_upsert(&roles, &count, &cap, namespace_path, rank) < 0) {
                    free(roles);
                    return PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory");
                }
            }
        }
    }
    if (!provider_ok && fail_closed) {
        free(roles);
        return PICOMESH_ERR(picomesh_string,
                            "accounts_groups: external group provider error (fail-closed)");
    }

    size_t out_cap = 64, len = 0;
    char *out = malloc(out_cap);
    if (!out) {
        free(roles);
        return PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory");
    }
    out[0] = 0;
    for (size_t i = 0; i < count; ++i) {
        const char *role_name = picomesh_role_name(roles[i].rank);
        if (!role_name) continue;
        size_t need = len + (len ? 1 : 0) + strlen(roles[i].namespace_path) + 1 +
                      strlen(role_name) + 1;
        if (need > out_cap) {
            while (out_cap < need) out_cap *= 2;
            char *grown = realloc(out, out_cap);
            if (!grown) {
                free(out);
                free(roles);
                return PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory");
            }
            out = grown;
        }
        len += (size_t)snprintf(out + len, out_cap - len, "%s%s:%s", len ? "," : "",
                                roles[i].namespace_path, role_name);
    }
    free(roles);
    return PICOMESH_OK(picomesh_string, out);
}

/* The user's namespace memberships, flattened to the "<path>:<role>,..." CSV
 * the token issuer mints into the JWT `groups` claim. Sourced from the canonical
 * `namespace_members` table (issue #30) — NOT the legacy `users.groups` column,
 * which is no longer the authority. Inheritance through parent namespaces is
 * applied later, at authorization time; this returns only direct grants.
 *
 * When `accounts.external_groups` is configured, the local grants are merged
 * with an external directory provider's mapped roles (issue #31). */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_groups")
struct picomesh_string_result accounts_accounts_groups_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs, uint32_t uid)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_string, "accounts_groups: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_string, "accounts_groups: namespace schema failed", ens);
    h.shard = uid;
    char *args = rel_args1i((int64_t)uid);
    struct picomesh_json_result rows = rel_query(&h, hdrs,
        "SELECT namespace_path, role FROM namespace_members WHERE uid=?", args);
    free(args);
    if (PICOMESH_IS_ERR(rows)) return PICOMESH_ERR(picomesh_string, "accounts_groups: query failed", rows);

    struct yjson_doc *doc = yjson_parse(rows.value ? rows.value : "[]", rows.value ? strlen(rows.value) : 2);
    free(rows.value);
    if (!doc) return PICOMESH_ERR(picomesh_string, "accounts_groups: malformed members result");
    const struct yjson_value *arr = yjson_doc_root(doc);
    size_t n = (arr && yjson_is_array(arr)) ? yjson_array_size(arr) : 0;
    size_t cap = 64, len = 0;
    char *out = malloc(cap);
    if (!out) { yjson_doc_free(doc); return PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory"); }
    out[0] = 0;
    for (size_t i = 0; i < n; ++i) {
        const struct yjson_value *row = yjson_array_at(arr, i);
        const char *path = yjson_as_string(yjson_object_get(row, "namespace_path"), NULL);
        const char *role = yjson_as_string(yjson_object_get(row, "role"), NULL);
        if (!path || !*path || !role || !*role) continue;
        size_t need = len + (len ? 1 : 0) + strlen(path) + 1 + strlen(role) + 1;
        if (need > cap) {
            while (cap < need) cap *= 2;
            char *grown = realloc(out, cap);
            if (!grown) { free(out); yjson_doc_free(doc); return PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory"); }
            out = grown;
        }
        len += (size_t)snprintf(out + len, cap - len, "%s%s:%s", len ? "," : "", path, role);
    }
    yjson_doc_free(doc);

    /* Merge in any configured external directory provider's roles (issue #31).
     * No-op (returns a copy of `out`) when no external provider is configured.
     * The username keys the external directory (LDAP `uid={username}`); read it
     * from the same uid cluster handle. */
    char *uname_args = rel_args1i((int64_t)uid);
    char *username = rel_query_str(&h, hdrs, "SELECT username FROM users WHERE uid=?", uname_args,
                                   "username");
    free(uname_args);
    struct picomesh_string_result merged = accounts_merge_external_groups(uid, username, out);
    free(username);
    free(out);
    return merged;
}

/* Read a boolean from this service's own yconfig (e.g. "accounts.<key>"),
 * returning `fallback` when the key is absent or the engine is unavailable. */
static int accounts_cfg_bool(const char *path, int fallback)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return fallback;
    struct yconfig_node_ptr_result r = yconfig_get(picomesh_engine_config(e), path);
    int out = (PICOMESH_IS_OK(r) && r.value) ? yconfig_node_as_bool(r.value, fallback) : fallback;
    if (PICOMESH_IS_ERR(r)) picomesh_error_destroy(r.error);
    return out;
}

/* The caller's verified identity + roles from the JWT in `hdrs`, or
 * authenticated=0 for an in-process (NULL/empty-JWT) caller. */
static struct picomesh_authctx ns_caller(struct yheaders *hdrs)
{
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    return caller;
}

/* 1 if `caller` may administer `path` (grant roles / create subgroups under it):
 * site admin, or maintainer+ on `path` (inherited). An unauthenticated caller is
 * an in-process trusted caller (e.g. the register flow) and is allowed. */
static int ns_caller_admins(const struct picomesh_authctx *caller, const char *path)
{
    if (!caller->authenticated) return 0; /* fail closed: no credential, no admin */
    if (picomesh_groups_contains(caller->groups_csv, PICOMESH_GROUP_SYSTEM)) return 1; /* trusted internal */
    if (picomesh_groups_max_role(caller->groups_csv, "site") >= picomesh_role_rank("maintainer")) return 1;
    return picomesh_groups_effective_role(caller->groups_csv, path) >= picomesh_role_rank("maintainer");
}

/* Create a namespace (issue #30). `parent_path` "" makes a root namespace;
 * otherwise the new path is `<parent_path>/<slug>` and a subgroup is recorded
 * with `parent_id` set. `kind` is "user" or "group". Returns the full path.
 *
 * Security (privilege-escalation hardening):
 *   - The owner is the VERIFIED CALLER, never the `owner_uid` argument, except
 *     for the in-process register flow (no JWT) which passes the new user's uid.
 *   - The owner membership is granted ONLY when THIS call actually created the
 *     namespace row (INSERT changes==1). Re-creating an existing namespace
 *     (e.g. `site`, or another user's namespace) is rejected and grants nothing,
 *     so a caller cannot mint themselves owner on a namespace they don't own.
 *   - The reserved `site` namespace cannot be created by an external caller.
 *   - A subgroup requires its parent to exist AND the caller to be maintainer+
 *     on the parent (or a site admin). */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_ns_create")
struct picomesh_string_result accounts_accounts_ns_create_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                               uint32_t owner_uid, const char *kind,
                                                               const char *slug, const char *parent_path)
{
    (void)ctx;
    if (!ns_slug_ok(slug)) return PICOMESH_ERR(picomesh_string, "accounts_ns_create: invalid slug");
    if (!kind || !*kind) kind = "group";
    if (!parent_path) parent_path = "";

    struct picomesh_authctx caller = ns_caller(hdrs);
    /* FAIL CLOSED: a credential-less caller is never trusted. The gateway's
     * register/bootstrap path presents an explicit signed `system:internal`
     * capability instead of relying on the absence of a JWT (issue #30). */
    if (!caller.authenticated)
        return PICOMESH_ERR(picomesh_string, "accounts_ns_create: authentication required");
    int is_system = picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM);

    /* ROOT namespace creation. GitLab-style: any authenticated user MAY create a
     * top-level group by default, but the deployment can restrict it to site
     * admins via `accounts.allow_user_root_groups: false` (default true). The
     * site admin and the trusted internal capability (the personal-namespace
     * bootstrap at register) may always create one. The reserved `site`
     * namespace stays mintable ONLY by the internal capability (the first-user
     * bootstrap) regardless of the flag. A regular user may also create
     * SUBGROUPS under a namespace they administer (checked below). */
    if (!parent_path[0]) {
        if (!is_system && strcmp(slug, "site") == 0)
            return PICOMESH_ERR(picomesh_string, "accounts_ns_create: reserved namespace");
        int is_site_admin = picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer");
        if (!is_system && !is_site_admin && !accounts_cfg_bool("accounts.allow_user_root_groups", 1))
            return PICOMESH_ERR(picomesh_string, "accounts_ns_create: forbidden (this deployment restricts root group creation to site admins)");
    }

    /* The owner is the caller's own uid, EXCEPT for the trusted internal
     * capability, which acts on behalf of `owner_uid` (a new user at register). */
    uint32_t owner = is_system ? owner_uid : caller.uid;

    char path[256];
    int written = parent_path[0] ? snprintf(path, sizeof(path), "%s/%s", parent_path, slug)
                                 : snprintf(path, sizeof(path), "%s", slug);
    /* Cross-service invariant: a namespace path is stored VERBATIM in
     * git_repo's repo_rec.owner_name (a 64-byte field) and is the basis of the
     * deterministic repo id. Reject any path that would not fit there, so a
     * namespace can never be created that no repo could reference (git_repo's
     * path_ok rejects the same length — keep the two limits identical). */
    if (written <= 0 || (size_t)written >= 64)
        return PICOMESH_ERR(picomesh_string, "accounts_ns_create: namespace path too long");
    int64_t nsid = ns_id_of(path);
    int64_t parent_id = parent_path[0] ? ns_id_of(parent_path) : 0;

    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_string, "accounts_ns_create: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_string, "accounts_ns_create: namespace schema failed", ens);

    if (parent_path[0]) {
        if (!ns_lookup(&h, hdrs, parent_path))
            return PICOMESH_ERR(picomesh_string, "accounts_ns_create: parent namespace does not exist");
        if (!ns_caller_admins(&caller, parent_path))
            return PICOMESH_ERR(picomesh_string, "accounts_ns_create: forbidden (need maintainer on parent)");
    }

    /* Create the namespace row. INSERT OR IGNORE elects exactly one creator: a
     * losing caller (the row already existed) gets changes==0 and is rejected
     * WITHOUT a membership grant — the escalation guard. */
    h.shard = picomesh_fnv1a32(path);
    struct yjson_writer *nw = yjson_writer_new();
    yjson_writer_begin_array(nw);
    yjson_writer_int(nw, nsid);
    yjson_writer_int(nw, parent_id);
    yjson_writer_string(nw, slug);
    yjson_writer_string(nw, path);
    yjson_writer_string(nw, kind);
    yjson_writer_int(nw, (int64_t)owner);
    yjson_writer_int(nw, picomesh_yplatform_time_wall_ms() / 1000);
    char *nargs = rel_args_take(nw);
    int nc = rel_exec_changes(&h, hdrs,
        "INSERT OR IGNORE INTO namespaces(id,parent_id,slug,path,kind,owner_uid,created_at) VALUES(?,?,?,?,?,?,?)", nargs);
    free(nargs);
    if (nc < 0) return PICOMESH_ERR(picomesh_string, "accounts_ns_create: namespace insert failed");
    if (nc == 0) {
        /* The namespace already exists. IDEMPOTENT for the SAME owner — a retry
         * of a partially-completed create, or re-ensuring a user's personal
         * namespace — so it (re)grants the owner membership below. A DIFFERENT
         * owner means the name is taken by someone else → reject (this is what
         * makes a registration whose personal-namespace name was grabbed by a
         * group fail cleanly instead of stranding the account). */
        h.shard = picomesh_fnv1a32(path);
        char *qa = rel_args1s(path);
        int found = 0;
        int64_t existing_owner =
            rel_query_int(&h, hdrs, "SELECT owner_uid FROM namespaces WHERE path=?", qa, "owner_uid", -1, &found);
        free(qa);
        if (!found) return PICOMESH_ERR(picomesh_string, "accounts_ns_create: namespace owner lookup failed");
        if ((uint32_t)existing_owner != owner)
            return PICOMESH_ERR(picomesh_string, "accounts_ns_create: namespace already owned by another");
        /* same owner → fall through to (re)grant the owner membership */
    }

    /* The creator (or the same owner re-ensuring) reaches here; the owner grant
     * is safe and idempotent. */
    h.shard = owner;
    struct yjson_writer *mw = yjson_writer_new();
    yjson_writer_begin_array(mw);
    yjson_writer_int(mw, nsid);
    yjson_writer_string(mw, path);
    yjson_writer_int(mw, (int64_t)owner);
    yjson_writer_string(mw, "owner");
    char *margs = rel_args_take(mw);
    int mc = rel_exec_changes(&h, hdrs,
        "INSERT OR REPLACE INTO namespace_members(namespace_id,namespace_path,uid,role) VALUES(?,?,?,?)", margs);
    free(margs);
    if (mc < 0) return PICOMESH_ERR(picomesh_string, "accounts_ns_create: owner membership insert failed");

    yinfo("accounts_ns_create: %s kind=%s owner=%u", path, kind, owner);
    char *result = strdup(path);
    if (!result) return PICOMESH_ERR(picomesh_string, "accounts_ns_create: out of memory");
    return PICOMESH_OK(picomesh_string, result);
}

/* Grant (or change) `uid`'s role on the namespace `path` (issue #30). The
 * namespace MUST already exist in the canonical `namespaces` table — a grant
 * cannot conjure a namespace into being. The caller must be maintainer+ on the
 * namespace (or a site admin); the in-process path is trusted. Stores a direct
 * membership (inheritance is resolved at authz time). Returns 1. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_ns_add_member")
struct picomesh_int_result accounts_accounts_ns_add_member_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                                const char *path, uint32_t uid, const char *role)
{
    (void)ctx;
    if (!path || !*path) return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: path required");
    if (!role || !*role) role = "guest";
    if (picomesh_role_rank(role) < 0) return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: unknown role");
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: namespace schema failed", ens);

    int64_t nsid = ns_lookup(&h, hdrs, path);
    if (!nsid) return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: namespace does not exist");
    struct picomesh_authctx caller = ns_caller(hdrs);
    if (!ns_caller_admins(&caller, path))
        return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: forbidden (need maintainer on namespace)");

    /* The target must be a REAL registered user — uids are the deterministic
     * hash of a username, so without this a grant could pre-authorize an
     * arbitrary future uid that silently takes effect when that name registers. */
    struct picomesh_int_result exists = account_exists(&h, hdrs, uid);
    if (PICOMESH_IS_ERR(exists)) return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: account check failed", exists);
    if (exists.value == 0) return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: no such user");

    h.shard = uid;
    struct yjson_writer *mw = yjson_writer_new();
    yjson_writer_begin_array(mw);
    yjson_writer_int(mw, nsid);
    yjson_writer_string(mw, path);
    yjson_writer_int(mw, (int64_t)uid);
    yjson_writer_string(mw, role);
    char *margs = rel_args_take(mw);
    int mc = rel_exec_changes(&h, hdrs,
        "INSERT OR REPLACE INTO namespace_members(namespace_id,namespace_path,uid,role) VALUES(?,?,?,?)", margs);
    free(margs);
    if (mc < 0) return PICOMESH_ERR(picomesh_int, "accounts_ns_add_member: membership insert failed");
    yinfo("accounts_ns_add_member: %s uid=%u role=%s", path, uid, role);
    return PICOMESH_OK(picomesh_int, 1);
}

/* Resolve a namespace path to its canonical namespace_id (0 if it does not
 * exist). git_repo calls this at make time to verify the owning namespace
 * exists and to store the repo's namespace_id reference (issue #30). */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_ns_resolve")
struct picomesh_int64_result accounts_accounts_ns_resolve_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                               const char *path)
{
    (void)ctx;
    if (!path || !*path) return PICOMESH_OK(picomesh_int64, 0);
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int64, "accounts_ns_resolve: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_int64, "accounts_ns_resolve: namespace schema failed", ens);
    return PICOMESH_OK(picomesh_int64, ns_lookup(&h, hdrs, path));
}

/* Every namespace as `[{"id","parent_id","slug","path","kind","owner_uid"}, …]`,
 * globally ordered by id. Drives the admin RBAC page (issue #30). Namespaces
 * shard by hash(path), so this fans out across every shard. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_ns_list")
struct picomesh_json_result accounts_accounts_ns_list_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    /* FAIL CLOSED: enumerating the whole tree is a site-admin view, re-checked
     * here so a non-gateway/bridge path can't read it without auth (issue #30). */
    struct picomesh_authctx caller = ns_caller(hdrs);
    int is_system = caller.authenticated && picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM);
    if (!caller.authenticated ||
        (!is_system && picomesh_groups_max_role(caller.groups_csv, "site") < picomesh_role_rank("maintainer")))
        return PICOMESH_ERR(picomesh_json, "accounts_ns_list: forbidden (site admin only)");
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "accounts_ns_list: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_json, "accounts_ns_list: namespace schema failed", ens);
    return rel_query_page(&h, hdrs,
        "SELECT id,parent_id,slug,path,kind,owner_uid FROM namespaces", "[]", "id", 0, 0, 0);
}

/* The members of a namespace as `[{"uid","role"}, …]`. Memberships shard by uid,
 * so a by-path query fans out across every shard. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_ns_members")
struct picomesh_json_result accounts_accounts_ns_members_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                              const char *path)
{
    (void)ctx;
    if (!path || !*path) return PICOMESH_ERR(picomesh_json, "accounts_ns_members: path required");
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "accounts_ns_members: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_json, "accounts_ns_members: namespace schema failed", ens);
    /* FAIL CLOSED: a namespace's members are visible to a maintainer of it (or a
     * site admin / internal capability) — re-checked here, not just at the
     * gateway (issue #30). */
    struct picomesh_authctx caller = ns_caller(hdrs);
    if (!ns_caller_admins(&caller, path))
        return PICOMESH_ERR(picomesh_json, "accounts_ns_members: forbidden (need maintainer on namespace)");
    char *args = rel_args1s(path);
    /* Join the users table (same uid shard) so the row carries the username —
     * the management UI can then show members without a separate, admin-only
     * roster fetch, which is what made the page useless for non-admin
     * maintainers. */
    struct picomesh_json_result r = rel_query_page(&h, hdrs,
        "SELECT m.uid AS uid, m.role AS role, u.username AS username "
        "FROM namespace_members m LEFT JOIN users u ON u.uid = m.uid "
        "WHERE m.namespace_path=?", args, "uid", 0, 0, 0);
    free(args);
    return r;
}

/* Revoke `uid`'s membership on the namespace `path` (issue #30). Maintainer+ on
 * the namespace (or site admin) only — same gate as ns_add_member. Returns 1 if
 * a membership was removed, 0 if there was none. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_ns_remove_member")
struct picomesh_int_result accounts_accounts_ns_remove_member_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                                   const char *path, uint32_t uid)
{
    (void)ctx;
    if (!path || !*path) return PICOMESH_ERR(picomesh_int, "accounts_ns_remove_member: path required");
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int, "accounts_ns_remove_member: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_int, "accounts_ns_remove_member: namespace schema failed", ens);
    struct picomesh_authctx caller = ns_caller(hdrs);
    if (!ns_caller_admins(&caller, path))
        return PICOMESH_ERR(picomesh_int, "accounts_ns_remove_member: forbidden (need maintainer on namespace)");
    h.shard = uid;
    struct yjson_writer *aw = yjson_writer_new();
    yjson_writer_begin_array(aw);
    yjson_writer_string(aw, path);
    yjson_writer_int(aw, (int64_t)uid);
    char *args = rel_args_take(aw);
    int changes = rel_exec_changes(&h, hdrs,
        "DELETE FROM namespace_members WHERE namespace_path=? AND uid=?", args);
    free(args);
    if (changes < 0) return PICOMESH_ERR(picomesh_int, "accounts_ns_remove_member: delete failed");
    yinfo("accounts_ns_remove_member: %s uid=%u removed=%d", path, uid, changes > 0 ? 1 : 0);
    return PICOMESH_OK(picomesh_int, changes > 0 ? 1 : 0);
}

/* 1 if `caller` may READ the namespace `path` (reporter+ inherited, site admin,
 * or trusted internal). Fail closed for an unauthenticated caller. */
static int ns_caller_can_read(const struct picomesh_authctx *caller, const char *path)
{
    if (!caller->authenticated) return 0; /* fail closed */
    if (picomesh_groups_contains(caller->groups_csv, PICOMESH_GROUP_SYSTEM)) return 1;
    if (picomesh_groups_max_role(caller->groups_csv, "site") >= picomesh_role_rank("maintainer")) return 1;
    return picomesh_groups_effective_role(caller->groups_csv, path) >= picomesh_role_rank("reporter");
}

/* The namespace `path` and ALL of its descendants, as `[{"path":…}, …]` ordered
 * by path (issue #30). A reporter+ on `path` inherits that role to every
 * descendant, so the whole subtree is returned in one call — this is what lets
 * the webapp's Projects page list repos in subgroups the caller can reach by
 * INHERITED role (e.g. a developer on `acme` seeing `acme/platform/svc`) without
 * a direct membership on the subgroup. FAIL CLOSED: a caller without reporter+
 * on `path` gets nothing. Namespaces shard by hash(path), so the prefix query
 * fans out across every shard. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_ns_subtree")
struct picomesh_json_result accounts_accounts_ns_subtree_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                              const char *path)
{
    (void)ctx;
    if (!path || !*path) return PICOMESH_ERR(picomesh_json, "accounts_ns_subtree: path required");
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "accounts_ns_subtree: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_json, "accounts_ns_subtree: namespace schema failed", ens);
    struct picomesh_authctx caller = ns_caller(hdrs);
    if (!ns_caller_can_read(&caller, path))
        return PICOMESH_ERR(picomesh_json, "accounts_ns_subtree: forbidden (need reporter on namespace)");
    /* `path` itself OR any descendant `path/...`. Match the descendant prefix
     * with an EXACT substring compare, NOT a LIKE pattern: the slug grammar
     * permits `_`, which SQLite LIKE treats as a single-character wildcard, so a
     * `LIKE 'a_b/%'` would also match `axb/...` and disclose sibling namespace
     * names. `substr(path,1,N)=prefix` has no wildcard semantics. */
    char prefix[128];
    int prefix_len = snprintf(prefix, sizeof(prefix), "%s/", path);
    struct yjson_writer *qw = yjson_writer_new();
    yjson_writer_begin_array(qw);
    yjson_writer_string(qw, path);
    yjson_writer_int(qw, prefix_len);
    yjson_writer_string(qw, prefix);
    char *args = rel_args_take(qw);
    struct picomesh_json_result r = rel_query_page(&h, hdrs,
        "SELECT path FROM namespaces WHERE path=? OR substr(path,1,?)=?", args, "path", 0, 0, 0);
    free(args);
    return r;
}

/* Delete the namespace `path` (its row + all memberships) (issue #30). Gated to
 * the namespace OWNER, a site admin, or the trusted internal capability. Refuses
 * if the namespace has CHILD namespaces (deleting a parent would orphan the
 * subtree). Returns 1 if a row was removed, 0 if there was none. Used for the
 * first-user `site` bootstrap rollback (remove the just-created site when the
 * account it was created for fails to complete), so a failed first registration
 * can never strand `site` under a phantom owner. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_ns_delete")
struct picomesh_int_result accounts_accounts_ns_delete_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                            const char *path)
{
    (void)ctx;
    if (!path || !*path) return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: path required");
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: open failed", oh);
    struct picomesh_void_result ens = accounts_ensure_ns_schema(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(ens)) return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: namespace schema failed", ens);

    /* Authorize against the recorded owner (NOT inherited maintainer — deletion
     * is an owner/site-admin/system operation). */
    h.shard = picomesh_fnv1a32(path);
    char *qa = rel_args1s(path);
    int found = 0;
    int64_t owner_uid =
        rel_query_int(&h, hdrs, "SELECT owner_uid FROM namespaces WHERE path=?", qa, "owner_uid", -1, &found);
    free(qa);
    if (!found) return PICOMESH_OK(picomesh_int, 0); /* nothing to delete */
    struct picomesh_authctx caller = ns_caller(hdrs);
    int is_system = caller.authenticated && picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM);
    int is_site_admin = caller.authenticated &&
        picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer");
    int is_owner = caller.authenticated && (uint32_t)owner_uid == caller.uid;
    if (!is_system && !is_site_admin && !is_owner)
        return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: forbidden (owner or site admin only)");

    /* Refuse to orphan a subtree: a namespace with children cannot be deleted.
     * EXACT prefix match (not LIKE — `_` in a slug is a SQLite wildcard that
     * would miscount unrelated siblings as children), and a cross-shard
     * aggregate (child namespaces hash to other shards than `path`). */
    char cprefix[128];
    int cprefix_len = snprintf(cprefix, sizeof(cprefix), "%s/", path);
    struct yjson_writer *cw = yjson_writer_new();
    yjson_writer_begin_array(cw);
    yjson_writer_int(cw, cprefix_len);
    yjson_writer_string(cw, cprefix);
    char *ca = rel_args_take(cw);
    struct picomesh_int64_result children = rel_query_int_all(&h, hdrs,
        "SELECT COUNT(*) AS n FROM namespaces WHERE substr(path,1,?)=?", ca, "n");
    free(ca);
    if (PICOMESH_IS_ERR(children)) return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: child check failed", children);
    if (children.value > 0)
        return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: namespace has child namespaces");

    /* Remove the memberships. They shard by uid, so the row for each member
     * lives on a different shard — run the delete on EVERY shard (a single
     * rel_exec_changes only touches h.shard). */
    struct picomesh_int_result sc = rel_handle_shard_count(&h, hdrs);
    if (PICOMESH_IS_ERR(sc)) return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: shard count failed", sc);
    for (int shard = 0; shard < sc.value; ++shard) {
        h.shard = (uint32_t)shard;
        char *ma = rel_args1s(path);
        int mc = rel_exec_changes(&h, hdrs, "DELETE FROM namespace_members WHERE namespace_path=?", ma);
        free(ma);
        if (mc < 0) return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: membership delete failed");
    }
    h.shard = picomesh_fnv1a32(path);
    char *na = rel_args1s(path);
    int nc = rel_exec_changes(&h, hdrs, "DELETE FROM namespaces WHERE path=?", na);
    free(na);
    if (nc < 0) return PICOMESH_ERR(picomesh_int, "accounts_ns_delete: namespace delete failed");
    yinfo("accounts_ns_delete: %s removed=%d", path, nc > 0 ? 1 : 0);
    return PICOMESH_OK(picomesh_int, nc > 0 ? 1 : 0);
}

/* List registered users as `[{"uid":…,"username":…}, …]` — a plain SELECT. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_list")
struct picomesh_json_result accounts_accounts_list_impl(struct ctx *ctx, struct object *obj,
                                                        struct yheaders *hdrs,
                                                        int64_t offset, int64_t limit)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "accounts_list: open failed", oh);
    /* Globally ordered, globally paginated across shards (uid is the order key). */
    return rel_query_page(&h, hdrs, "SELECT uid,username FROM users", "[]", "uid", 0, offset, limit);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_list_all")
struct picomesh_json_result accounts_accounts_list_all_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "accounts_list_all: open failed", oh);
    /* Unbounded but GLOBALLY ordered (limit<=0): a plain shard-concat would
     * only be shard-locally ordered despite the ORDER BY. */
    return rel_query_page(&h, hdrs, "SELECT uid,username FROM users", "[]", "uid", 0, 0, 0);
}

#include "accounts.gen.c"
