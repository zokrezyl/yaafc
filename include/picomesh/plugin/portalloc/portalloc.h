/* GENERATED — do not edit. */
/* Public interface for plugin `portalloc` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/portalloc/. */
#ifndef PICOMESH_PLUGIN_PORTALLOC_H
#define PICOMESH_PLUGIN_PORTALLOC_H

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
struct class_ptr_result portalloc_portalloc_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result portalloc_portalloc_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_uint32_result portalloc_portalloc_allocate(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * service_name, const char * host);
struct picomesh_int_result portalloc_portalloc_release(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t port);
struct picomesh_size_result portalloc_portalloc_count_used(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_json_result portalloc_portalloc_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t offset, int64_t limit);
struct picomesh_json_result portalloc_portalloc_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_portalloc_register(void);

#endif
