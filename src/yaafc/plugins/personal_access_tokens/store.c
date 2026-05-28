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

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>

#include <stdint.h>

#define PAT_MAX 512

struct pat_entry {
    uint32_t pat_id;
    uint32_t user_id;
    int used;
};

struct YAAFC_CLASS_ANNOTATE("class@personal_access_tokens:store") personal_access_tokens_store_data {
    struct pat_entry entries[PAT_MAX];
    size_t count;
    uint32_t next_id;
};

static struct personal_access_tokens_store_data *pat(struct object *obj)
{
    return (struct personal_access_tokens_store_data *)
           ((char *)obj + sizeof(struct object));
}

YAAFC_CLASS_ANNOTATE("override@personal_access_tokens:store:store_mint")
struct yaafc_uint32_result personal_access_tokens_store_mint_impl(struct ctx *ctx,
                                                                  struct object *obj,
                                                                  uint32_t user_id)
{
    (void)ctx;
    struct personal_access_tokens_store_data *d = pat(obj);
    if (d->next_id == 0) d->next_id = 1;
    for (size_t i = 0; i < PAT_MAX; ++i) {
        if (!d->entries[i].used) {
            d->entries[i].pat_id = d->next_id++;
            d->entries[i].user_id = user_id;
            d->entries[i].used = 1;
            d->count++;
            yinfo("pat: minted pat=%u user=%u", d->entries[i].pat_id, user_id);
            return YAAFC_OK(yaafc_uint32, d->entries[i].pat_id);
        }
    }
    return YAAFC_ERR(yaafc_uint32, "pat_mint: table full");
}

YAAFC_CLASS_ANNOTATE("override@personal_access_tokens:store:store_lookup")
struct yaafc_uint32_result personal_access_tokens_store_lookup_impl(struct ctx *ctx,
                                                                    struct object *obj,
                                                                    uint32_t pat_id)
{
    (void)ctx;
    struct personal_access_tokens_store_data *d = pat(obj);
    for (size_t i = 0; i < PAT_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].pat_id == pat_id) {
            return YAAFC_OK(yaafc_uint32, d->entries[i].user_id);
        }
    }
    return YAAFC_OK(yaafc_uint32, 0);
}

YAAFC_CLASS_ANNOTATE("override@personal_access_tokens:store:store_revoke")
struct yaafc_int_result personal_access_tokens_store_revoke_impl(struct ctx *ctx,
                                                                 struct object *obj,
                                                                 uint32_t pat_id)
{
    (void)ctx;
    struct personal_access_tokens_store_data *d = pat(obj);
    for (size_t i = 0; i < PAT_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].pat_id == pat_id) {
            d->entries[i].used = 0;
            d->count--;
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    return YAAFC_OK(yaafc_int, 0);
}

YAAFC_CLASS_ANNOTATE("override@personal_access_tokens:store:store_list_for_user")
struct yaafc_size_result personal_access_tokens_store_list_for_user_impl(
    struct ctx *ctx, struct object *obj, uint32_t user_id)
{
    (void)ctx;
    struct personal_access_tokens_store_data *d = pat(obj);
    size_t n = 0;
    for (size_t i = 0; i < PAT_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].user_id == user_id) n++;
    }
    return YAAFC_OK(yaafc_size, n);
}

YAAFC_CLASS_ANNOTATE("override@personal_access_tokens:store:store_count_active")
struct yaafc_size_result personal_access_tokens_store_count_active_impl(struct ctx *ctx,
                                                                        struct object *obj)
{
    (void)ctx;
    return YAAFC_OK(yaafc_size, pat(obj)->count);
}

#include "store.gen.c"
