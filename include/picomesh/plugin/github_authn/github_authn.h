/* GENERATED — do not edit. */
/* Public interface for plugin `github_authn` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/github_authn/. */
#ifndef PICOMESH_PLUGIN_GITHUB_AUTHN_H
#define PICOMESH_PLUGIN_GITHUB_AUTHN_H

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
struct class_ptr_result github_authn_github_authn_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result github_authn_github_authn_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_json_result github_authn_github_authn_exchange_code(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * code, const char * redirect_uri);
struct picomesh_int_result github_authn_github_authn_set_credentials(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t client_id, uint32_t secret_id);
struct picomesh_int_result github_authn_github_authn_register_code(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t code, uint32_t user_id);
struct picomesh_uint32_result github_authn_github_authn_resolve(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t code);
struct picomesh_size_result github_authn_github_authn_count_codes(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_json_result github_authn_github_authn_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t offset, int64_t limit);
struct picomesh_json_result github_authn_github_authn_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_github_authn_register(void);

#endif
