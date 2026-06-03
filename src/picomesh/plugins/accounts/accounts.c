/* accounts plugin — user registry backed by the storage service.
 *
 * Methods (uid is uint32 on the wire today; usernames are mapped to
 * uids client-side by the frontend, mirroring yaapp's accounts plugin
 * but without the string-key round-trip):
 *
 *   accounts_register(uid, name)  1 if newly created, 0 if already there
 *                                 (the username is stored as name:<uid>)
 *   accounts_exists(uid)          1 if present, 0 otherwise
 *   accounts_set_balance(uid, n)  set balance (errors if uid unknown)
 *   accounts_balance(uid)         current balance (errors if uid unknown)
 *   accounts_count()              live row count
 *
 * Storage layout in the `accounts` context:
 *
 *   user:<uid>      → 1 (registered marker)
 *   balance:<uid>   → int64 balance
 *   count           → number of registered users
 *
 * The plugin holds no in-memory state — every method delegates to the
 * storage service on the configured remote, using a single context
 * `accounts`. The storage service maps the context to either a sqlite
 * table or an mdbx DBI (transparent to this plugin).
 */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yjson/yjson.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PICOMESH_CLASS_ANNOTATE("class@accounts:accounts") accounts_accounts_data {
    char _unused;
};

struct acc_storage_handle {
    struct ctx c;
    struct object *obj;
};
PICOMESH_RESULT_DECLARE(acc_storage_handle, struct acc_storage_handle);

static struct acc_storage_handle_result open_storage(void)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(acc_storage_handle, "accounts: no active engine");
    struct acc_storage_handle h = {.c = picomesh_engine_service_ctx(e, "sharded_storage")};
    /* peer==NULL ⇒ storage is collocated in THIS process; create resolves it
     * as a local in-process object. A non-NULL peer is the remote-mesh path.
     * Both go through sharded_storage_db_create — don't bail on a missing peer. */
    struct object_ptr_result o = sharded_storage_db_create(&h.c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(acc_storage_handle, "accounts: storage_db_create failed", o);
    h.obj = o.value;
    return PICOMESH_OK(acc_storage_handle, h);
}

#define ACCOUNTS_CTX "accounts"

static void close_storage(struct acc_storage_handle *h)
{
    /* The storage object is a cached, service-lifetime dependency
     * (rpc_object_acquire owns it); nothing to release per call. */
    (void)h;
}

/* The store holds string values; account state is integer counters and
 * balances, so serialize as decimal strings and parse them back. */
/* A failed write is propagated, never swallowed. */
static struct picomesh_void_result kv_set_int(struct acc_storage_handle *h, struct yheaders *hdrs,
                                              const char *key, int64_t value)
{
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%lld", (long long)value);
    struct picomesh_int_result r = sharded_storage_db_set(&h->c, h->obj, hdrs, ACCOUNTS_CTX, key, vbuf);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_void, "accounts: storage write failed", r);
    return PICOMESH_OK_VOID();
}

/* Read an int. A real backend error is propagated; an absent/empty key
 * (db_get returns "") yields `fallback` — not conflated. */
static struct picomesh_int64_result kv_get_int(struct acc_storage_handle *h, struct yheaders *hdrs,
                                               const char *key, int64_t fallback)
{
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, ACCOUNTS_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "accounts: storage read failed", r);
    int64_t v = (r.value && r.value[0]) ? strtoll(r.value, NULL, 10) : fallback;
    free(r.value);
    return PICOMESH_OK(picomesh_int64, v);
}

/* Atomic counter bump — propagates a backend failure (OK value = count
 * after the add). */
static struct picomesh_int64_result kv_incr(struct acc_storage_handle *h, struct yheaders *hdrs,
                                            const char *key, int64_t delta)
{
    struct picomesh_int64_result r = sharded_storage_db_incr(&h->c, h->obj, hdrs, ACCOUNTS_CTX, key, delta);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "accounts: counter update failed", r);
    return r;
}

/* Existence check. A real backend error is propagated; OK carries 0/1. */
static struct picomesh_int_result kv_exists(struct acc_storage_handle *h, struct yheaders *hdrs, const char *key)
{
    struct picomesh_int_result r = sharded_storage_db_exists(&h->c, h->obj, hdrs, ACCOUNTS_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int, "accounts: storage exists failed", r);
    return r;
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_register")
struct picomesh_int_result accounts_accounts_register_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                    uint32_t uid, const char *username)
{
    (void)ctx; (void)obj;
    if (!username) username = "";
    struct acc_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "accounts_register: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;

    /* put_if_absent ELECTS one registrant for this uid atomically: two
     * concurrent registers can't both pass an exists-check and both bump
     * the count. Only the inserting caller increments the total and
     * reports "newly registered" (1); a loser reports "already exists" (0). */
    char k[64];
    snprintf(k, sizeof(k), "user:%u", uid);
    struct picomesh_int_result ins = sharded_storage_db_put_if_absent(&h.c, h.obj, hdrs, ACCOUNTS_CTX, k, "1");
    if (PICOMESH_IS_ERR(ins)) {
        close_storage(&h);
        return PICOMESH_ERR(picomesh_int, "accounts_register: storage write failed", ins);
    }
    if (ins.value == 0) {
        close_storage(&h);
        ydebug("accounts_register: uid=%u already exists", uid);
        return PICOMESH_OK(picomesh_int, 0);
    }

    /* The username is an account detail this plugin OWNS: store it under
     * name:<uid> — one O(1) write per account, no shared roster index to
     * RMW (that was O(N²) + an ever-growing value, and it forced the gateway
     * to reach around into our storage). `list` enumerates by scanning the
     * name:<uid> keys instead. */
    snprintf(k, sizeof(k), "name:%u", uid);
    struct picomesh_int_result nw =
        sharded_storage_db_set(&h.c, h.obj, hdrs, ACCOUNTS_CTX, k, username);
    if (PICOMESH_IS_ERR(nw)) { close_storage(&h); return PICOMESH_ERR(picomesh_int, "accounts_register: name write failed", nw); }

    struct picomesh_int64_result cinc = kv_incr(&h, hdrs, "count", 1);
    if (PICOMESH_IS_ERR(cinc)) { close_storage(&h); return PICOMESH_ERR(picomesh_int, "accounts_register: bump count failed", cinc); }
    close_storage(&h);
    yinfo("accounts_register: uid=%u name=%s", uid, username);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_exists")
struct picomesh_int_result accounts_accounts_exists_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                  uint32_t uid)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "accounts_exists: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "user:%u", uid);
    struct picomesh_int_result ex = kv_exists(&h, hdrs, k);
    close_storage(&h);
    if (PICOMESH_IS_ERR(ex)) return PICOMESH_ERR(picomesh_int, "accounts_exists: read failed", ex);
    return PICOMESH_OK(picomesh_int, ex.value ? 1 : 0);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_set_balance")
struct picomesh_int_result accounts_accounts_set_balance_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                        uint32_t uid, int64_t n)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "accounts_set_balance: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "user:%u", uid);
    struct picomesh_int_result ex = kv_exists(&h, hdrs, k);
    if (PICOMESH_IS_ERR(ex)) { close_storage(&h); return PICOMESH_ERR(picomesh_int, "accounts_set_balance: existence check failed", ex); }
    if (!ex.value) {
        close_storage(&h);
        return PICOMESH_ERR(picomesh_int, "accounts_set_balance: unknown uid");
    }
    snprintf(k, sizeof(k), "balance:%u", uid);
    struct picomesh_void_result w = kv_set_int(&h, hdrs, k, n);
    if (PICOMESH_IS_ERR(w)) { close_storage(&h); return PICOMESH_ERR(picomesh_int, "accounts_set_balance: write failed", w); }
    close_storage(&h);
    ydebug("accounts_set_balance: uid=%u balance=%lld", uid, (long long)n);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_balance")
struct picomesh_int64_result accounts_accounts_balance_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                      uint32_t uid)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int64, "accounts_balance: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "user:%u", uid);
    struct picomesh_int_result ex = kv_exists(&h, hdrs, k);
    if (PICOMESH_IS_ERR(ex)) { close_storage(&h); return PICOMESH_ERR(picomesh_int64, "accounts_balance: existence check failed", ex); }
    if (!ex.value) {
        close_storage(&h);
        return PICOMESH_ERR(picomesh_int64, "accounts_balance: unknown uid");
    }
    snprintf(k, sizeof(k), "balance:%u", uid);
    struct picomesh_int64_result balr = kv_get_int(&h, hdrs, k, 0);
    close_storage(&h);
    if (PICOMESH_IS_ERR(balr)) return PICOMESH_ERR(picomesh_int64, "accounts_balance: read failed", balr);
    return PICOMESH_OK(picomesh_int64, balr.value);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_count")
struct picomesh_size_result accounts_accounts_count_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "accounts_count: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    struct picomesh_int64_result cr = kv_get_int(&h, hdrs, "count", 0);
    close_storage(&h);
    if (PICOMESH_IS_ERR(cr)) return PICOMESH_ERR(picomesh_size, "accounts_count: read failed", cr);
    return PICOMESH_OK(picomesh_size, (size_t)(cr.value < 0 ? 0 : cr.value));
}

/* Group memberships drive authorization (issue #19). Each user's groups are a
 * comma-separated list of "<account>:<role>" slugs stored at groups:<uid>; the
 * token issuer mints them into the JWT `groups` claim at login. This replaces
 * the gateway poking a raw role:<uid> storage key. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_set_groups")
struct picomesh_int_result accounts_accounts_set_groups_impl(struct ctx *ctx, struct object *obj,
                                                             struct yheaders *hdrs,
                                                             uint32_t uid, const char *groups_csv)
{
    (void)ctx; (void)obj;
    if (!groups_csv) groups_csv = "";
    struct acc_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "accounts_set_groups: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "groups:%u", uid);
    struct picomesh_int_result w = sharded_storage_db_set(&h.c, h.obj, hdrs, ACCOUNTS_CTX, k, groups_csv);
    close_storage(&h);
    if (PICOMESH_IS_ERR(w)) return PICOMESH_ERR(picomesh_int, "accounts_set_groups: write failed", w);
    yinfo("accounts_set_groups: uid=%u groups=%s", uid, groups_csv);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_groups")
struct picomesh_string_result accounts_accounts_groups_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs, uint32_t uid)
{
    (void)ctx; (void)obj;
    struct acc_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "accounts_groups: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    char k[64];
    snprintf(k, sizeof(k), "groups:%u", uid);
    struct picomesh_string_result r = sharded_storage_db_get(&h.c, h.obj, hdrs, ACCOUNTS_CTX, k);
    close_storage(&h);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_string, "accounts_groups: read failed", r);
    if (!r.value) {
        char *empty = strdup("");
        if (!empty) return PICOMESH_ERR(picomesh_string, "accounts_groups: out of memory");
        return PICOMESH_OK(picomesh_string, empty);
    }
    return PICOMESH_OK(picomesh_string, r.value);
}

/* List the registered users as a JSON array `[{"uid":<n>,"name":"<s>"}, …]`.
 * State is the `index` key, stored at registration as newline-separated
 * "<uid>\t<username>" rows (the reverse map the frontend needs); this parses
 * that index into a real JSON list so an RPC consumer gets structured data,
 * not a delimited blob. Empty array when no users have registered yet. */
/* Parse the TSV `index` into a JSON array of {uid,name}, skipping `offset`
 * rows and stopping after `limit` (< 0 == all). Shared by list + list_all. */
static struct picomesh_json_result acc_list_window(struct yheaders *hdrs, int64_t offset, int64_t limit)
{
    struct acc_storage_handle_result sr = open_storage();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "accounts_list: open_storage failed", sr);
    struct acc_storage_handle h = sr.value;
    /* Enumerate the per-account name:<uid> keys via the storage scan
     * (paginated). No shared roster index — this scales with the account
     * table instead of an ever-growing denormalized value. */
    struct picomesh_json_result raw =
        (limit < 0) ? sharded_storage_db_list_all(&h.c, h.obj, hdrs, ACCOUNTS_CTX, "name:")
                    : sharded_storage_db_list(&h.c, h.obj, hdrs, ACCOUNTS_CTX, "name:", offset, limit);
    close_storage(&h);
    if (PICOMESH_IS_ERR(raw)) return PICOMESH_ERR(picomesh_json, "accounts_list: scan failed", raw);

    /* raw.value is [{"key":"name:<uid>","value":"<username>"}, …] — reshape
     * to clean [{"uid":<n>,"name":"<username>"}, …] records. */
    struct yjson_doc *doc = yjson_parse(raw.value ? raw.value : "[]", raw.value ? strlen(raw.value) : 2);
    struct yjson_writer *w = yjson_writer_new();
    if (!doc || !w) {
        if (doc) yjson_doc_free(doc);
        if (w) yjson_writer_free(w);
        free(raw.value);
        return PICOMESH_ERR(picomesh_json, "accounts_list: reshape alloc failed");
    }
    yjson_writer_begin_array(w);
    const struct yjson_value *arr = yjson_doc_root(doc);
    size_t n = arr ? yjson_array_size(arr) : 0;
    for (size_t i = 0; i < n; ++i) {
        const struct yjson_value *e = yjson_array_at(arr, i);
        const char *key = yjson_as_string(yjson_object_get(e, "key"), "");
        const char *val = yjson_as_string(yjson_object_get(e, "value"), "");
        const char *uid_s = strncmp(key, "name:", 5) == 0 ? key + 5 : key;
        yjson_writer_begin_object(w);
        yjson_writer_key(w, "uid");  yjson_writer_int(w, (int64_t)strtoull(uid_s, NULL, 10));
        yjson_writer_key(w, "name"); yjson_writer_string(w, val);
        yjson_writer_end_object(w);
    }
    yjson_writer_end_array(w);
    yjson_doc_free(doc);
    free(raw.value);

    size_t len = 0;
    const char *data = yjson_writer_data(w, &len);
    char *out = strdup(data ? data : "[]");
    yjson_writer_free(w);
    if (!out) return PICOMESH_ERR(picomesh_json, "accounts_list: strdup failed");
    return PICOMESH_OK(picomesh_json, out);
}

/* List the registered users as a JSON array `[{"uid":…,"name":…}]`,
 * paginated by offset/limit (gh#15). */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_list")
struct picomesh_json_result accounts_accounts_list_impl(struct ctx *ctx, struct object *obj,
                                                        struct yheaders *hdrs,
                                                        int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    if (limit <= 0) limit = 100;
    return acc_list_window(hdrs, offset, limit);
}

/* Unbounded variant — every registered user. */
PICOMESH_CLASS_ANNOTATE("override@accounts:accounts:accounts_list_all")
struct picomesh_json_result accounts_accounts_list_all_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    return acc_list_window(hdrs, 0, -1);
}

#include "accounts.gen.c"
