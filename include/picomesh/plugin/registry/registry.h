/* GENERATED — do not edit. */
/* Public interface for plugin `registry` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/registry/. */
#ifndef PICOMESH_PLUGIN_REGISTRY_H
#define PICOMESH_PLUGIN_REGISTRY_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int_result;
struct picomesh_json_result;
struct picomesh_size_result;
struct picomesh_string_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result registry_registry_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result registry_registry_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_int_result registry_registry_register_service(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * name, const char * instance_id, const char * host, uint32_t port);
struct picomesh_int_result registry_registry_deregister_service(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * name, const char * instance_id);
struct picomesh_string_result registry_registry_resolve(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * name);
struct picomesh_json_result registry_registry_discover_service(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * name);
struct picomesh_json_result registry_registry_list_services(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_size_result registry_registry_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_registry_register(void);

#endif
