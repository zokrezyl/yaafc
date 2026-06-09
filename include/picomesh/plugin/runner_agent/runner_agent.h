/* GENERATED — do not edit. */
/* Public interface for plugin `runner_agent` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/runner_agent/. */
#ifndef PICOMESH_PLUGIN_RUNNER_AGENT_H
#define PICOMESH_PLUGIN_RUNNER_AGENT_H

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
struct class_ptr_result runner_agent_runner_agent_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result runner_agent_runner_agent_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_json_result runner_agent_runner_agent_create_token(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * name, const char * labels);
struct picomesh_uint32_result runner_agent_runner_agent_lookup_token(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * token);
struct picomesh_string_result runner_agent_runner_agent_exchange(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * token);
struct picomesh_int_result runner_agent_runner_agent_revoke_token(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t runner_id);
struct picomesh_uint32_result runner_agent_runner_agent_register(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t runner_id, const char * name, const char * labels, const char * version, const char * host);
struct picomesh_int_result runner_agent_runner_agent_heartbeat(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t runner_id, const char * status);
struct picomesh_json_result runner_agent_runner_agent_get(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t runner_id);
struct picomesh_json_result runner_agent_runner_agent_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t offset, int64_t limit);
struct picomesh_json_result runner_agent_runner_agent_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_size_result runner_agent_runner_agent_count_active(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_runner_agent_register(void);

#endif
