/* GENERATED — do not edit. */
/* Public interface for plugin `password_authn` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/password_authn/. */
#ifndef PICOMESH_PLUGIN_PASSWORD_AUTHN_H
#define PICOMESH_PLUGIN_PASSWORD_AUTHN_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int_result;
struct picomesh_json_result;
struct picomesh_size_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result password_authn_password_authn_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result password_authn_password_authn_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_int_result password_authn_password_authn_register(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t user_id, int64_t hash);
struct picomesh_int_result password_authn_password_authn_authenticate(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t user_id, int64_t hash);
struct picomesh_int_result password_authn_password_authn_change_password(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t user_id, int64_t hash);
struct picomesh_size_result password_authn_password_authn_count_registered(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_json_result password_authn_password_authn_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t offset, int64_t limit);
struct picomesh_json_result password_authn_password_authn_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_password_authn_register(void);

#endif
