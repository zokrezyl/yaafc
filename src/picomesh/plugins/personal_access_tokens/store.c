/* personal_access_tokens — long-lived bearer credentials.
 *
 * The gateway's `bearer_opaque_token` authenticator forwards
 * `Authorization: Bearer pat_*` headers here and trusts the returned
 * user_id. Tokens are uint32 surrogates on the wire — the real
 * implementation hashes the random bytes the user receives and
 * stores only the hash.
 *
 *   mint(user_id)         → pat_id  (server-owned, return + show once)
 *   lookup(pat_id)        → user_id (0 if absent / revoked)
 *   revoke(pat_id)        → 1 / 0
 *   list_for_user(uid)    → count owned
 *   count_active          → total live PATs */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yjson/yjson.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAT_MAX 512

struct pat_entry {
    uint32_t pat_id;
    uint32_t user_id;
    int used;
};

struct PICOMESH_CLASS_ANNOTATE("class@personal_access_tokens:personal_access_tokens") personal_access_tokens_personal_access_tokens_data {
    struct pat_entry entries[PAT_MAX];
    size_t count;
    uint32_t next_id;
};

static struct personal_access_tokens_personal_access_tokens_data *pat(struct object *obj)
{
    return (struct personal_access_tokens_personal_access_tokens_data *)
           ((char *)obj + sizeof(struct object));
}

PICOMESH_CLASS_ANNOTATE("override@personal_access_tokens:personal_access_tokens:personal_access_tokens_mint")
struct picomesh_uint32_result personal_access_tokens_personal_access_tokens_mint_impl(struct ctx *ctx,
                                                                  struct object *obj,
                                                                  struct yheaders *hdrs,
                                                                  uint32_t user_id)
{
    (void)ctx;
    struct personal_access_tokens_personal_access_tokens_data *data = pat(obj);
    if (data->next_id == 0) data->next_id = 1;
    for (size_t i = 0; i < PAT_MAX; ++i) {
        if (!data->entries[i].used) {
            data->entries[i].pat_id = data->next_id++;
            data->entries[i].user_id = user_id;
            data->entries[i].used = 1;
            data->count++;
            yinfo("pat: minted pat=%u user=%u", data->entries[i].pat_id, user_id);
            return PICOMESH_OK(picomesh_uint32, data->entries[i].pat_id);
        }
    }
    return PICOMESH_ERR(picomesh_uint32, "pat_mint: table full");
}

PICOMESH_CLASS_ANNOTATE("override@personal_access_tokens:personal_access_tokens:personal_access_tokens_lookup")
struct picomesh_uint32_result personal_access_tokens_personal_access_tokens_lookup_impl(struct ctx *ctx,
                                                                    struct object *obj,
                                                                    struct yheaders *hdrs,
                                                                    uint32_t pat_id)
{
    (void)ctx;
    struct personal_access_tokens_personal_access_tokens_data *data = pat(obj);
    for (size_t i = 0; i < PAT_MAX; ++i) {
        if (data->entries[i].used && data->entries[i].pat_id == pat_id) {
            return PICOMESH_OK(picomesh_uint32, data->entries[i].user_id);
        }
    }
    return PICOMESH_OK(picomesh_uint32, 0);
}

PICOMESH_CLASS_ANNOTATE("override@personal_access_tokens:personal_access_tokens:personal_access_tokens_revoke")
struct picomesh_int_result personal_access_tokens_personal_access_tokens_revoke_impl(struct ctx *ctx,
                                                                 struct object *obj,
                                                                 struct yheaders *hdrs,
                                                                 uint32_t pat_id)
{
    (void)ctx;
    struct personal_access_tokens_personal_access_tokens_data *data = pat(obj);
    for (size_t i = 0; i < PAT_MAX; ++i) {
        if (data->entries[i].used && data->entries[i].pat_id == pat_id) {
            data->entries[i].used = 0;
            data->count--;
            return PICOMESH_OK(picomesh_int, 1);
        }
    }
    return PICOMESH_OK(picomesh_int, 0);
}

PICOMESH_CLASS_ANNOTATE("override@personal_access_tokens:personal_access_tokens:personal_access_tokens_list_for_user")
struct picomesh_size_result personal_access_tokens_personal_access_tokens_list_for_user_impl(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs, uint32_t user_id)
{
    (void)ctx;
    struct personal_access_tokens_personal_access_tokens_data *data = pat(obj);
    size_t count = 0;
    for (size_t i = 0; i < PAT_MAX; ++i) {
        if (data->entries[i].used && data->entries[i].user_id == user_id) count++;
    }
    return PICOMESH_OK(picomesh_size, count);
}

PICOMESH_CLASS_ANNOTATE("override@personal_access_tokens:personal_access_tokens:personal_access_tokens_count_active")
struct picomesh_size_result personal_access_tokens_personal_access_tokens_count_active_impl(struct ctx *ctx,
                                                                        struct object *obj,
                                                                        struct yheaders *hdrs)
{
    (void)ctx;
    return PICOMESH_OK(picomesh_size, pat(obj)->count);
}

/* List ALL personal access tokens this service manages, as a JSON array
 * `[{"pat_id":…,"user_id":…}, …]` (gh#15) — every token, not per user, not
 * a count. State is the in-memory entry table. */
/* Build the PAT list as JSON, skipping `offset` and stopping after `limit`
 * (< 0 == unbounded). Shared by the paginated list + list_all. */
static struct picomesh_json_result pat_list_window(struct object *obj, int64_t offset, int64_t limit)
{
    struct personal_access_tokens_personal_access_tokens_data *data = pat(obj);
    struct yjson_writer *writer = yjson_writer_new();
    if (!writer) return PICOMESH_ERR(picomesh_json, "pat_list: writer alloc failed");
    yjson_writer_begin_array(writer);
    int64_t skip = offset > 0 ? offset : 0, emitted = 0;
    for (size_t i = 0; i < PAT_MAX && (limit < 0 || emitted < limit); ++i) {
        if (!data->entries[i].used) continue;
        if (skip > 0) { --skip; continue; }
        yjson_writer_begin_object(writer);
        yjson_writer_key(writer, "pat_id");  yjson_writer_int(writer, (int64_t)data->entries[i].pat_id);
        yjson_writer_key(writer, "user_id"); yjson_writer_int(writer, (int64_t)data->entries[i].user_id);
        yjson_writer_end_object(writer);
        ++emitted;
    }
    yjson_writer_end_array(writer);
    size_t len = 0;
    const char *json_data = yjson_writer_data(writer, &len);
    char *out = strdup(json_data ? json_data : "[]");
    yjson_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_json, "pat_list: strdup failed");
    return PICOMESH_OK(picomesh_json, out);
}

/* List ALL personal access tokens as a JSON array, paginated (gh#15). */
PICOMESH_CLASS_ANNOTATE("override@personal_access_tokens:personal_access_tokens:personal_access_tokens_list")
struct picomesh_json_result personal_access_tokens_personal_access_tokens_list_impl(struct ctx *ctx,
                                                                   struct object *obj,
                                                                   struct yheaders *hdrs,
                                                                   int64_t offset, int64_t limit)
{
    (void)ctx; (void)hdrs;
    if (limit <= 0) limit = 100;
    return pat_list_window(obj, offset, limit);
}

/* Unbounded variant — every token (the in-memory table is bounded anyway). */
PICOMESH_CLASS_ANNOTATE("override@personal_access_tokens:personal_access_tokens:personal_access_tokens_list_all")
struct picomesh_json_result personal_access_tokens_personal_access_tokens_list_all_impl(struct ctx *ctx,
                                                                       struct object *obj,
                                                                       struct yheaders *hdrs)
{
    (void)ctx; (void)hdrs;
    return pat_list_window(obj, 0, -1);
}

#include "store.gen.c"
