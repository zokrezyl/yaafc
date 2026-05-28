/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `mesh`.
 * NEVER include this from outside src/yaafc/plugins/mesh/. */
#ifndef YAAFC_MESH_INTERNAL_H
#define YAAFC_MESH_INTERNAL_H

#include <yaafc/plugin/mesh/mesh.h>

typedef struct yaafc_int_result (*mesh_store_register_service_fn)(struct ctx *, struct object *, uint32_t, uint32_t);
typedef struct yaafc_uint32_result (*mesh_store_resolve_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*mesh_store_forget_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*mesh_store_count_services_fn)(struct ctx *, struct object *);
typedef struct yaafc_int_result (*mesh_store_spawn_yaafc_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*mesh_store_kill_pid_fn)(struct ctx *, struct object *, int32_t);
typedef struct yaafc_size_result (*mesh_store_count_children_fn)(struct ctx *, struct object *);
typedef struct yaafc_int_result (*mesh_store_reconcile_from_config_fn)(struct ctx *, struct object *);
typedef struct yaafc_int_result (*mesh_store_reconcile_fn)(struct ctx *, struct object *);

#endif
