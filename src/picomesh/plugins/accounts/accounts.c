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

#define ACCOUNTS_STORE_UID  "rstore_uid"
#define ACCOUNTS_STORE_NAME "rstore_username"

struct PICOMESH_CLASS_ANNOTATE("class@accounts:accounts") accounts_accounts_data {
    int users_schema_ensured; /* per-worker: `users` created in rstore_uid */
    int names_schema_ensured; /* per-worker: `usernames` created in rstore_username */
};

static struct accounts_accounts_data *acc(struct object *obj)
{
    return (struct accounts_accounts_data *)((char *)obj + sizeof(struct object));
}

/* Open the uid-sharded data cluster (users). Caller sets h->shard = uid. */
static struct picomesh_void_result accounts_open_uid(struct rel_handle *h, struct yheaders *hdrs, struct object *obj)
{
    struct picomesh_void_result o = rel_open(h, ACCOUNTS_STORE_UID);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_void, "accounts: open rstore_uid failed", o);
    return rel_ensure_schema(h, hdrs, &acc(obj)->users_schema_ensured, ACCOUNTS_USERS_DDL);
}

/* Open the username→uid lookup cluster. Caller sets h->shard = hash(username). */
static struct picomesh_void_result accounts_open_names(struct rel_handle *h, struct yheaders *hdrs, struct object *obj)
{
    struct picomesh_void_result o = rel_open(h, ACCOUNTS_STORE_NAME);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_void, "accounts: open rstore_username failed", o);
    return rel_ensure_schema(h, hdrs, &acc(obj)->names_schema_ensured, ACCOUNTS_NAMES_DDL);
}

static int account_exists(struct rel_handle *h, struct yheaders *hdrs, uint32_t uid)
{
    h->shard = uid;
    char *args = rel_args1i((int64_t)uid);
    int found = 0;
    rel_query_int(h, hdrs, "SELECT uid FROM users WHERE uid=?", args, "uid", 0, &found);
    free(args);
    return found;
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
    return PICOMESH_OK(picomesh_int, account_exists(&h, hdrs, uid) ? 1 : 0);
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
    int64_t n = rel_query_int_all(&h, hdrs, "SELECT COUNT(*) AS n FROM users", "[]", "n");
    return PICOMESH_OK(picomesh_size, (size_t)(n < 0 ? 0 : n));
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

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_groups")
struct picomesh_string_result accounts_accounts_groups_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs, uint32_t uid)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_string, "accounts_groups: open failed", oh);
    h.shard = uid;
    char *args = rel_args1i((int64_t)uid);
    char *groups = rel_query_str(&h, hdrs, "SELECT groups FROM users WHERE uid=?", args, "groups");
    free(args);
    if (!groups) {
        char *empty = strdup("");
        return empty ? PICOMESH_OK(picomesh_string, empty) : PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory");
    }
    return PICOMESH_OK(picomesh_string, groups);
}

/* List registered users as `[{"uid":…,"username":…}, …]` — a plain SELECT. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_list")
struct picomesh_json_result accounts_accounts_list_impl(struct ctx *ctx, struct object *obj,
                                                        struct yheaders *hdrs,
                                                        int64_t offset, int64_t limit)
{
    (void)ctx;
    /* Cross-shard pagination needs a global merge/sort after fan-out, which is
     * not implemented; a per-shard LIMIT/OFFSET would over-return and mis-order.
     * Until then, list returns every shard's rows (shard-grouped order) — use
     * count() + client-side paging. */
    (void)offset; (void)limit;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "accounts_list: open failed", oh);
    return rel_query_all(&h, hdrs, "SELECT uid,username FROM users ORDER BY uid", "[]");
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_list_all")
struct picomesh_json_result accounts_accounts_list_all_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = accounts_open_uid(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "accounts_list_all: open failed", oh);
    return rel_query_all(&h, hdrs, "SELECT uid,username FROM users ORDER BY uid", "[]");
}

#include "accounts.gen.c"
