/* GENERATED — do not edit. */
/* Public interface for plugin `session` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/session/. */
#ifndef PICOMESH_PLUGIN_SESSION_H
#define PICOMESH_PLUGIN_SESSION_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int_result;
struct picomesh_json_result;
struct picomesh_size_result;
struct picomesh_string_result;
struct picomesh_uint32_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result session_session_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result session_session_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_string_result session_session_start(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t user_id, const char * access_jwt, const char * refresh_token);
struct picomesh_string_result session_session_jwt(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * sid);
struct picomesh_uint32_result session_session_lookup(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * sid);
struct picomesh_int_result session_session_destroy(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * sid);
struct picomesh_size_result session_session_count_active(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_json_result session_session_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t offset, int64_t limit);
struct picomesh_json_result session_session_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_session_register(void);

#endif
