/* GENERATED — do not edit. */
/* Public interface for plugin `github_authn` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/github_authn/. */
#ifndef YAAFC_PLUGIN_GITHUB_AUTHN_H
#define YAAFC_PLUGIN_GITHUB_AUTHN_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result github_authn_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result github_authn_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int_result github_authn_store_set_credentials(struct ctx * ctx, struct object * obj, uint32_t client_id, uint32_t secret_id);
struct yaafc_int_result github_authn_store_register_code(struct ctx * ctx, struct object * obj, uint32_t code, uint32_t user_id);
struct yaafc_uint32_result github_authn_store_resolve(struct ctx * ctx, struct object * obj, uint32_t code);
struct yaafc_size_result github_authn_store_count_codes(struct ctx * ctx, struct object * obj);

#endif
