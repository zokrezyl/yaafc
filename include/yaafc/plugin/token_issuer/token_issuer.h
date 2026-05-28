/* GENERATED — do not edit. */
/* Public interface for plugin `token_issuer` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/token_issuer/. */
#ifndef YAAFC_PLUGIN_TOKEN_ISSUER_H
#define YAAFC_PLUGIN_TOKEN_ISSUER_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result token_issuer_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result token_issuer_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_uint32_result token_issuer_store_login(struct ctx * ctx, struct object * obj, uint32_t user_id, uint32_t provider_id);
struct yaafc_uint32_result token_issuer_store_validate(struct ctx * ctx, struct object * obj, uint32_t token_id);
struct yaafc_uint32_result token_issuer_store_refresh(struct ctx * ctx, struct object * obj, uint32_t token_id);
struct yaafc_int_result token_issuer_store_revoke(struct ctx * ctx, struct object * obj, uint32_t token_id);
struct yaafc_size_result token_issuer_store_count_active(struct ctx * ctx, struct object * obj);

#endif
