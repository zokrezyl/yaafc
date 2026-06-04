/* session — opaque session id ↔ stored access JWT, as RELATIONAL ROWS.
 *
 * A session is a ROW in the `sessions` table (one row per sid, with real
 * columns), stored in the `relational_storage` service — not three prefixed
 * keys faked into a KV store. The session plugin OWNS this table: it runs
 * `CREATE TABLE IF NOT EXISTS` itself, once per worker; the consumer app does
 * not define another plugin's schema.
 *
 *   sessions(sid PK, uid, access_jwt, refresh_token, created_at)
 *
 *   start(uid, access_jwt, refresh_token) → opaque sid
 *   jwt(sid)                              → stored access JWT ("" if absent)
 *   lookup(sid)                           → uid (0 if absent)        — back-compat
 *   destroy(sid)                          → 1 if removed, 0 if unknown
 *   count_active                          → live session count
 *   list / list_all                       → [{"uid":…,"created_at":…}, …]
 *
 * The sid is a 128-bit random hex bearer secret, never logged. The sid, access
 * JWT and refresh token are server-side secrets and never appear in listings —
 * a listing returns only non-secret metadata (uid, created_at), so a caller
 * with list access cannot harvest a live session credential.
 */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yplatform/time.h>
#include <picomesh/ycore/idkey.h>
#include <picomesh/plugin/relational_storage/relational_sql.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#define SESSION_DDL \
    "CREATE TABLE IF NOT EXISTS sessions(" \
    "sid TEXT PRIMARY KEY, uid INTEGER NOT NULL, access_jwt TEXT, " \
    "refresh_token TEXT, created_at INTEGER NOT NULL DEFAULT 0)"

struct PICOMESH_CLASS_ANNOTATE("class@session:session") session_session_data {
    int schema_ensured; /* per-worker: set once this worker has created the table */
};

static struct session_session_data *sess(struct object *obj)
{
    return (struct session_session_data *)((char *)obj + sizeof(struct object));
}

/* Allocate a fresh session id: 128 bits of secure randomness, lowercase hex.
 * Opaque and unguessable. FAILS CLOSED if the kernel cannot give secure bytes —
 * an sid is a bearer secret, never a predictable value. Returns 1 on success. */
static int alloc_token(char *out, size_t cap)
{
    uint8_t raw[16];
    size_t got = 0;
    while (got < sizeof(raw)) {
        ssize_t n = getrandom(raw + got, sizeof(raw) - got, 0);
        if (n < 0) { if (errno == EINTR) continue; return 0; }
        got += (size_t)n;
    }
    static const char hex[] = "0123456789abcdef";
    size_t k = 0;
    for (size_t i = 0; i < sizeof(raw) && k + 2 < cap; ++i) {
        out[k++] = hex[raw[i] >> 4];
        out[k++] = hex[raw[i] & 0x0f];
    }
    out[k < cap ? k : cap - 1] = 0;
    return 1;
}

/* Open relational_storage and ensure this plugin's table exists. */
static struct picomesh_void_result session_open(struct rel_handle *h, struct yheaders *hdrs, struct object *obj)
{
    struct picomesh_void_result o = rel_open(h, "rstore_session");
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_void, "session: open relational_storage failed", o);
    return rel_ensure_schema(h, hdrs, &sess(obj)->schema_ensured, SESSION_DDL);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_start")
struct picomesh_string_result session_session_start_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                    uint32_t user_id, const char *access_jwt,
                                                    const char *refresh_token)
{
    (void)ctx;
    if (!access_jwt) access_jwt = "";
    if (!refresh_token) refresh_token = "";
    char sid[40];
    if (!alloc_token(sid, sizeof(sid)))
        return PICOMESH_ERR(picomesh_string, "session_start: secure random unavailable");

    struct rel_handle h;
    struct picomesh_void_result oh = session_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_string, "session_start: open failed", oh);
    h.shard = picomesh_fnv1a32(sid); /* lookup cluster: shard by the sid we issue */

    struct yjson_writer *aw = yjson_writer_new();
    yjson_writer_begin_array(aw);
    yjson_writer_string(aw, sid);
    yjson_writer_int(aw, (int64_t)user_id);
    yjson_writer_string(aw, access_jwt);
    yjson_writer_string(aw, refresh_token);
    yjson_writer_int(aw, picomesh_yplatform_time_wall_ms() / 1000);
    char *args = rel_args_take(aw);
    int changes = rel_exec_changes(&h, hdrs,
        "INSERT INTO sessions(sid,uid,access_jwt,refresh_token,created_at) VALUES(?,?,?,?,?)", args);
    free(args);
    if (changes < 1) return PICOMESH_ERR(picomesh_string, "session_start: insert failed");

    yinfo("session: started user=%u", user_id); /* never log the sid */
    char *out = strdup(sid);
    return out ? PICOMESH_OK(picomesh_string, out) : PICOMESH_ERR(picomesh_string, "session_start: out of memory");
}

/* Exchange an opaque sid for the stored access JWT. Authenticator-internal
 * credential exchange — not a public /_rpc call. */
PICOMESH_CLASS_ANNOTATE("override@session:session:session_jwt")
struct picomesh_string_result session_session_jwt_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                       const char *sid)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = session_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_string, "session_jwt: open failed", oh);
    h.shard = picomesh_fnv1a32(sid ? sid : "");
    char *args = rel_args1s(sid ? sid : "");
    char *jwt = rel_query_str(&h, hdrs, "SELECT access_jwt FROM sessions WHERE sid=?", args, "access_jwt");
    free(args);
    if (!jwt) {
        char *empty = strdup("");
        return empty ? PICOMESH_OK(picomesh_string, empty) : PICOMESH_ERR(picomesh_string, "session_jwt: out of memory");
    }
    return PICOMESH_OK(picomesh_string, jwt);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_lookup")
struct picomesh_uint32_result session_session_lookup_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                     const char *sid)
{
    (void)ctx;
    if (!sid || !*sid) return PICOMESH_OK(picomesh_uint32, 0);
    struct rel_handle h;
    struct picomesh_void_result oh = session_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_uint32, "session_lookup: open failed", oh);
    h.shard = picomesh_fnv1a32(sid);
    char *args = rel_args1s(sid);
    int64_t uid = rel_query_int(&h, hdrs, "SELECT uid FROM sessions WHERE sid=?", args, "uid", 0, NULL);
    free(args);
    return PICOMESH_OK(picomesh_uint32, (uint32_t)uid);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_destroy")
struct picomesh_int_result session_session_destroy_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   const char *sid)
{
    (void)ctx;
    if (!sid || !*sid) return PICOMESH_OK(picomesh_int, 0);
    struct rel_handle h;
    struct picomesh_void_result oh = session_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_int, "session_destroy: open failed", oh);
    h.shard = picomesh_fnv1a32(sid);
    char *args = rel_args1s(sid);
    int changes = rel_exec_changes(&h, hdrs, "DELETE FROM sessions WHERE sid=?", args);
    free(args);
    if (changes < 0) return PICOMESH_ERR(picomesh_int, "session_destroy: delete failed");
    return PICOMESH_OK(picomesh_int, changes > 0 ? 1 : 0);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_count_active")
struct picomesh_size_result session_session_count_active_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = session_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_size, "session_count: open failed", oh);
    int64_t n = rel_query_int_all(&h, hdrs, "SELECT COUNT(*) AS n FROM sessions", "[]", "n");
    return PICOMESH_OK(picomesh_size, (size_t)(n < 0 ? 0 : n));
}

/* List sessions as `[{"uid":…,"created_at":…}, …]` — non-secret metadata only.
 * The sid is a bearer secret: selecting it here would let any caller with list
 * access harvest live session credentials, so it (and the stored jwt/refresh)
 * are never returned. */
PICOMESH_CLASS_ANNOTATE("override@session:session:session_list")
struct picomesh_json_result session_session_list_impl(struct ctx *ctx, struct object *obj,
                                                    struct yheaders *hdrs,
                                                    int64_t offset, int64_t limit)
{
    (void)ctx;
    /* No cross-shard pagination yet (needs a global merge after fan-out); return
     * all shards' rows shard-grouped. Use count_active() + client-side paging. */
    (void)offset; (void)limit;
    struct rel_handle h;
    struct picomesh_void_result oh = session_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "session_list: open failed", oh);
    return rel_query_all(&h, hdrs, "SELECT uid,created_at FROM sessions ORDER BY created_at", "[]");
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_list_all")
struct picomesh_json_result session_session_list_all_impl(struct ctx *ctx, struct object *obj,
                                                          struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle h;
    struct picomesh_void_result oh = session_open(&h, hdrs, obj);
    if (PICOMESH_IS_ERR(oh)) return PICOMESH_ERR(picomesh_json, "session_list_all: open failed", oh);
    return rel_query_all(&h, hdrs, "SELECT uid,created_at FROM sessions ORDER BY created_at", "[]");
}

#include "session.gen.c"
