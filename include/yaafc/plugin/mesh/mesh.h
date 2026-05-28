/* GENERATED — do not edit. */
/* Public interface for plugin `mesh` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/mesh/. */
#ifndef YAAFC_PLUGIN_MESH_H
#define YAAFC_PLUGIN_MESH_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result mesh_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result mesh_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int_result mesh_store_register_service(struct ctx * ctx, struct object * obj, uint32_t service_id, uint32_t port);
struct yaafc_uint32_result mesh_store_resolve(struct ctx * ctx, struct object * obj, uint32_t service_id);
struct yaafc_int_result mesh_store_forget(struct ctx * ctx, struct object * obj, uint32_t service_id);
struct yaafc_size_result mesh_store_count_services(struct ctx * ctx, struct object * obj);
struct yaafc_int_result mesh_store_spawn_yaafc(struct ctx * ctx, struct object * obj, uint32_t port);
struct yaafc_int_result mesh_store_kill_pid(struct ctx * ctx, struct object * obj, int32_t pid);
struct yaafc_size_result mesh_store_count_children(struct ctx * ctx, struct object * obj);
struct yaafc_int_result mesh_store_reconcile_from_config(struct ctx * ctx, struct object * obj);
struct yaafc_int_result mesh_store_reconcile(struct ctx * ctx, struct object * obj);

#endif
