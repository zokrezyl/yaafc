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

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/engine/engine.h>
#include <picomesh/platform/time.h>
#include <picomesh/core/idkey.h>
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
        ssize_t read_len = getrandom(raw + got, sizeof(raw) - got, 0);
        if (read_len < 0) { if (errno == EINTR) continue; return 0; }
        got += (size_t)read_len;
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
static struct picomesh_void_result session_open(struct rel_handle *rel_handle, struct yheaders *hdrs, struct object *obj)
{
    struct picomesh_void_result open_res = rel_open(rel_handle, "rstore_session", "session");
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_void, "session: open relational_storage failed", open_res);
    return rel_ensure_schema(rel_handle, hdrs, &sess(obj)->schema_ensured, SESSION_DDL);
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

    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = session_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_string, "session_start: open failed", open_res);
    rel_handle.shard = picomesh_fnv1a32(sid); /* lookup cluster: shard by the sid we issue */

    struct json_writer *args_writer = json_writer_new();
    json_writer_begin_array(args_writer);
    json_writer_string(args_writer, sid);
    json_writer_int(args_writer, (int64_t)user_id);
    json_writer_string(args_writer, access_jwt);
    json_writer_string(args_writer, refresh_token);
    json_writer_int(args_writer, picomesh_platform_time_wall_ms() / 1000);
    char *args = rel_args_take(args_writer);
    struct picomesh_int_result changes_res = rel_exec_changes(&rel_handle, hdrs,
        "INSERT INTO sessions(sid,uid,access_jwt,refresh_token,created_at) VALUES(?,?,?,?,?)", args);
    free(args);
    PICOMESH_RETURN_IF_ERR(picomesh_string, changes_res, "session_start: insert failed");
    if (changes_res.value < 1) return PICOMESH_ERR(picomesh_string, "session_start: insert failed");

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
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = session_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_string, "session_jwt: open failed", open_res);
    rel_handle.shard = picomesh_fnv1a32(sid ? sid : "");
    char *args = rel_args1s(sid ? sid : "");
    struct picomesh_string_result jwt_res = rel_query_str(&rel_handle, hdrs, "SELECT access_jwt FROM sessions WHERE sid=?", args, "access_jwt");
    free(args);
    PICOMESH_RETURN_IF_ERR(picomesh_string, jwt_res, "session_jwt: query failed");
    char *jwt = jwt_res.value;
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
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = session_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_uint32, "session_lookup: open failed", open_res);
    rel_handle.shard = picomesh_fnv1a32(sid);
    char *args = rel_args1s(sid);
    struct picomesh_int64_result uid_res = rel_query_int(&rel_handle, hdrs, "SELECT uid FROM sessions WHERE sid=?", args, "uid", 0, NULL);
    free(args);
    PICOMESH_RETURN_IF_ERR(picomesh_uint32, uid_res, "session_lookup: query failed");
    return PICOMESH_OK(picomesh_uint32, (uint32_t)uid_res.value);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_destroy")
struct picomesh_int_result session_session_destroy_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   const char *sid)
{
    (void)ctx;
    if (!sid || !*sid) return PICOMESH_OK(picomesh_int, 0);
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = session_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_int, "session_destroy: open failed", open_res);
    rel_handle.shard = picomesh_fnv1a32(sid);
    char *args = rel_args1s(sid);
    struct picomesh_int_result changes_res = rel_exec_changes(&rel_handle, hdrs, "DELETE FROM sessions WHERE sid=?", args);
    free(args);
    PICOMESH_RETURN_IF_ERR(picomesh_int, changes_res, "session_destroy: delete failed");
    return PICOMESH_OK(picomesh_int, changes_res.value > 0 ? 1 : 0);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_count_active")
struct picomesh_size_result session_session_count_active_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = session_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_size, "session_count: open failed", open_res);
    struct picomesh_int64_result count_res = rel_query_int_all(&rel_handle, hdrs, "SELECT COUNT(*) AS n FROM sessions", "[]", "n");
    if (PICOMESH_IS_ERR(count_res)) return PICOMESH_ERR(picomesh_size, "session_count: aggregate failed", count_res);
    return PICOMESH_OK(picomesh_size, (size_t)(count_res.value < 0 ? 0 : count_res.value));
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
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = session_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "session_list: open failed", open_res);
    /* Globally ordered + paginated across shards by created_at. */
    return rel_query_page(&rel_handle, hdrs, "SELECT uid,created_at FROM sessions", "[]", "created_at", 0, offset, limit);
}

PICOMESH_CLASS_ANNOTATE("override@session:session:session_list_all")
struct picomesh_json_result session_session_list_all_impl(struct ctx *ctx, struct object *obj,
                                                          struct yheaders *hdrs)
{
    (void)ctx;
    struct rel_handle rel_handle;
    struct picomesh_void_result open_res = session_open(&rel_handle, hdrs, obj);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "session_list_all: open failed", open_res);
    /* Unbounded but GLOBALLY ordered by created_at (limit<=0). */
    return rel_query_page(&rel_handle, hdrs, "SELECT uid,created_at FROM sessions", "[]", "created_at", 0, 0, 0);
}

#include "session.gen.c"
