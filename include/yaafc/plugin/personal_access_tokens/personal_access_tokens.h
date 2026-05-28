/* GENERATED — do not edit. */
/* Public interface for plugin `personal_access_tokens` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/personal_access_tokens/. */
#ifndef YAAFC_PLUGIN_PERSONAL_ACCESS_TOKENS_H
#define YAAFC_PLUGIN_PERSONAL_ACCESS_TOKENS_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result personal_access_tokens_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result personal_access_tokens_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_uint32_result personal_access_tokens_store_mint(struct ctx * ctx, struct object * obj, uint32_t user_id);
struct yaafc_uint32_result personal_access_tokens_store_lookup(struct ctx * ctx, struct object * obj, uint32_t pat_id);
struct yaafc_int_result personal_access_tokens_store_revoke(struct ctx * ctx, struct object * obj, uint32_t pat_id);
struct yaafc_size_result personal_access_tokens_store_list_for_user(struct ctx * ctx, struct object * obj, uint32_t user_id);
struct yaafc_size_result personal_access_tokens_store_count_active(struct ctx * ctx, struct object * obj);

#endif
