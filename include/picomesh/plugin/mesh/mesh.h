/* GENERATED — do not edit. */
/* Public interface for plugin `mesh` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/mesh/. */
#ifndef PICOMESH_PLUGIN_MESH_H
#define PICOMESH_PLUGIN_MESH_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int_result;
struct picomesh_size_result;
struct picomesh_uint32_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result mesh_mesh_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result mesh_mesh_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_int_result mesh_mesh_register_service(struct ctx *ctx,
                                                      struct object *obj,
                                                      struct yheaders *hdrs,
                                                      uint32_t service_id,
                                                      uint32_t port);
struct picomesh_uint32_result mesh_mesh_resolve(struct ctx *ctx,
                                                struct object *obj,
                                                struct yheaders *hdrs,
                                                uint32_t service_id);
struct picomesh_int_result mesh_mesh_forget(struct ctx *ctx, struct object *obj,
                                            struct yheaders *hdrs,
                                            uint32_t service_id);
struct picomesh_size_result mesh_mesh_count_services(struct ctx *ctx,
                                                     struct object *obj,
                                                     struct yheaders *hdrs);
struct picomesh_int_result mesh_mesh_spawn_picomesh(struct ctx *ctx,
                                                    struct object *obj,
                                                    struct yheaders *hdrs,
                                                    uint32_t port);
struct picomesh_int_result mesh_mesh_kill_pid(struct ctx *ctx,
                                              struct object *obj,
                                              struct yheaders *hdrs,
                                              int32_t pid);
struct picomesh_size_result mesh_mesh_count_children(struct ctx *ctx,
                                                     struct object *obj,
                                                     struct yheaders *hdrs);
struct picomesh_int_result
mesh_mesh_reconcile_from_config(struct ctx *ctx, struct object *obj,
                                struct yheaders *hdrs);
struct picomesh_int_result
mesh_mesh_reconcile(struct ctx *ctx, struct object *obj, struct yheaders *hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_mesh_register(void);

#endif
