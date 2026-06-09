/* GENERATED — do not edit. */
/* Public interface for plugin `personal_access_tokens` — GENERATED.
 * Edit the annotated sources under
 * src/picomesh/plugins/personal_access_tokens/. */
#ifndef PICOMESH_PLUGIN_PERSONAL_ACCESS_TOKENS_H
#define PICOMESH_PLUGIN_PERSONAL_ACCESS_TOKENS_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int_result;
struct picomesh_json_result;
struct picomesh_size_result;
struct picomesh_uint32_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result
personal_access_tokens_personal_access_tokens_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result
personal_access_tokens_personal_access_tokens_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_uint32_result
personal_access_tokens_personal_access_tokens_mint(struct ctx *ctx,
                                                   struct object *obj,
                                                   struct yheaders *hdrs,
                                                   uint32_t user_id);
struct picomesh_uint32_result
personal_access_tokens_personal_access_tokens_lookup(struct ctx *ctx,
                                                     struct object *obj,
                                                     struct yheaders *hdrs,
                                                     uint32_t pat_id);
struct picomesh_int_result personal_access_tokens_personal_access_tokens_revoke(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    uint32_t pat_id);
struct picomesh_size_result
personal_access_tokens_personal_access_tokens_list_for_user(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    uint32_t user_id);
struct picomesh_size_result
personal_access_tokens_personal_access_tokens_count_active(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs);
struct picomesh_json_result personal_access_tokens_personal_access_tokens_list(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs, int64_t offset,
    int64_t limit);
struct picomesh_json_result
personal_access_tokens_personal_access_tokens_list_all(struct ctx *ctx,
                                                       struct object *obj,
                                                       struct yheaders *hdrs);

/* ---- activation ---- */
struct picomesh_void_result
picomesh_plugin_personal_access_tokens_register(void);

#endif
