/* GENERATED — do not edit. */
/* Public interface for plugin `accounts` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/accounts/. */
#ifndef PICOMESH_PLUGIN_ACCOUNTS_H
#define PICOMESH_PLUGIN_ACCOUNTS_H

#include <picomesh/yclass/class.h>
#include <picomesh/yclass/rpc.h>

struct picomesh_int64_result;
struct picomesh_int_result;
struct picomesh_json_result;
struct picomesh_size_result;
struct picomesh_string_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result accounts_accounts_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result accounts_accounts_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_int_result accounts_accounts_claim_username(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid, const char * username);
struct picomesh_int_result accounts_accounts_release_username(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid, const char * username);
struct picomesh_int_result accounts_accounts_register(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid, const char * username);
struct picomesh_int_result accounts_accounts_exists(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid);
struct picomesh_int_result accounts_accounts_set_balance(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid, int64_t n);
struct picomesh_int64_result accounts_accounts_balance(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid);
struct picomesh_size_result accounts_accounts_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_int_result accounts_accounts_set_groups(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid, const char * groups_csv);
struct picomesh_string_result accounts_accounts_groups(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid);
struct picomesh_string_result accounts_accounts_ns_create(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t owner_uid, const char * kind, const char * slug, const char * parent_path);
struct picomesh_int_result accounts_accounts_ns_add_member(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * path, uint32_t uid, const char * role);
struct picomesh_int64_result accounts_accounts_ns_resolve(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * path);
struct picomesh_json_result accounts_accounts_ns_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_json_result accounts_accounts_ns_members(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * path);
struct picomesh_int_result accounts_accounts_ns_remove_member(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * path, uint32_t uid);
struct picomesh_json_result accounts_accounts_ns_subtree(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * path);
struct picomesh_int_result accounts_accounts_ns_delete(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * path);
struct picomesh_json_result accounts_accounts_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t offset, int64_t limit);
struct picomesh_json_result accounts_accounts_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);

/* ---- activation ---- */
void picomesh_plugin_accounts_register(void);

#endif
