/* GENERATED — do not edit. */
/* Public interface for plugin `password_authn` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/password_authn/. */
#ifndef YAAFC_PLUGIN_PASSWORD_AUTHN_H
#define YAAFC_PLUGIN_PASSWORD_AUTHN_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result password_authn_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result password_authn_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int_result password_authn_store_register(struct ctx * ctx, struct object * obj, uint32_t user_id, int64_t hash);
struct yaafc_int_result password_authn_store_authenticate(struct ctx * ctx, struct object * obj, uint32_t user_id, int64_t hash);
struct yaafc_int_result password_authn_store_change_password(struct ctx * ctx, struct object * obj, uint32_t user_id, int64_t hash);
struct yaafc_size_result password_authn_store_count_registered(struct ctx * ctx, struct object * obj);

#endif
