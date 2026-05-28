/* github_authn — GitHub OAuth bridge.
 *
 * The full plugin trades an authorization `code` for a GitHub user
 * identity and stitches it back into the accounts plugin. We don't
 * speak HTTPS here (no libcurl yet), so the methods exposed are
 * minimum viable shape:
 *
 *   set_credentials(client_id, secret_id)  → 1
 *   register_code(code, user_id)           → 1
 *   resolve(code)                          → user_id (0 if absent)
 *   count_codes                            → size
 *
 * In production `register_code` would happen as a side effect of the
 * upstream OAuth callback; `resolve` is what the login flow uses to
 * trade the code for an internal user_id. */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>

#include <stdint.h>

#define GH_MAX_CODES 128

struct gh_code_entry {
    uint32_t code;
    uint32_t user_id;
    int used;
};

struct YAAFC_CLASS_ANNOTATE("class@github_authn:store") github_authn_store_data {
    uint32_t client_id;
    uint32_t secret_id;     /* opaque secret token id from yconfig substitution */
    struct gh_code_entry codes[GH_MAX_CODES];
    size_t count;
};

static struct github_authn_store_data *gh(struct object *obj)
{
    return (struct github_authn_store_data *)((char *)obj + sizeof(struct object));
}

YAAFC_CLASS_ANNOTATE("override@github_authn:store:store_set_credentials")
struct yaafc_int_result github_authn_store_set_credentials_impl(struct ctx *ctx,
                                                                struct object *obj,
                                                                uint32_t client_id,
                                                                uint32_t secret_id)
{
    (void)ctx;
    struct github_authn_store_data *d = gh(obj);
    d->client_id = client_id;
    d->secret_id = secret_id;
    yinfo("github_authn: credentials set (client_id=%u)", client_id);
    return YAAFC_OK(yaafc_int, 1);
}

YAAFC_CLASS_ANNOTATE("override@github_authn:store:store_register_code")
struct yaafc_int_result github_authn_store_register_code_impl(struct ctx *ctx,
                                                              struct object *obj,
                                                              uint32_t code, uint32_t user_id)
{
    (void)ctx;
    struct github_authn_store_data *d = gh(obj);
    for (size_t i = 0; i < GH_MAX_CODES; ++i) {
        if (!d->codes[i].used) {
            d->codes[i].code = code;
            d->codes[i].user_id = user_id;
            d->codes[i].used = 1;
            d->count++;
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    return YAAFC_ERR(yaafc_int, "github_authn_register_code: table full");
}

YAAFC_CLASS_ANNOTATE("override@github_authn:store:store_resolve")
struct yaafc_uint32_result github_authn_store_resolve_impl(struct ctx *ctx, struct object *obj,
                                                           uint32_t code)
{
    (void)ctx;
    struct github_authn_store_data *d = gh(obj);
    for (size_t i = 0; i < GH_MAX_CODES; ++i) {
        if (d->codes[i].used && d->codes[i].code == code) {
            return YAAFC_OK(yaafc_uint32, d->codes[i].user_id);
        }
    }
    return YAAFC_OK(yaafc_uint32, 0);
}

YAAFC_CLASS_ANNOTATE("override@github_authn:store:store_count_codes")
struct yaafc_size_result github_authn_store_count_codes_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    return YAAFC_OK(yaafc_size, gh(obj)->count);
}

#include "store.gen.c"
