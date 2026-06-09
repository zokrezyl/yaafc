/* GENERATED — do not edit. */
/* Public interface for plugin `token_issuer` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/token_issuer/. */
#ifndef PICOMESH_PLUGIN_TOKEN_ISSUER_H
#define PICOMESH_PLUGIN_TOKEN_ISSUER_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_json_result;
struct picomesh_size_result;
struct picomesh_string_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result token_issuer_token_issuer_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result token_issuer_token_issuer_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_json_result token_issuer_token_issuer_login(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    const char *method, uint32_t uid, const char *username, int64_t pw_hash);
struct picomesh_json_result
token_issuer_token_issuer_refresh(struct ctx *ctx, struct object *obj,
                                  struct yheaders *hdrs,
                                  const char *refresh_token);
struct picomesh_string_result token_issuer_token_issuer_mint(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs, uint32_t uid,
    const char *username, const char *groups_csv, int64_t ttl_seconds);
struct picomesh_size_result
token_issuer_token_issuer_count_active(struct ctx *ctx, struct object *obj,
                                       struct yheaders *hdrs);
struct picomesh_json_result
token_issuer_token_issuer_list(struct ctx *ctx, struct object *obj,
                               struct yheaders *hdrs, int64_t offset,
                               int64_t limit);
struct picomesh_json_result
token_issuer_token_issuer_list_all(struct ctx *ctx, struct object *obj,
                                   struct yheaders *hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_token_issuer_register(void);

#endif
