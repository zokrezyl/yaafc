/* GENERATED — do not edit. */
#include "mesh.internal.h"

__attribute__((unused))
static mesh_store_register_service_fn _mesh_store_mesh_store_register_service_check = mesh_store_register_service_impl;
__attribute__((unused))
static mesh_store_resolve_fn _mesh_store_mesh_store_resolve_check = mesh_store_resolve_impl;
__attribute__((unused))
static mesh_store_forget_fn _mesh_store_mesh_store_forget_check = mesh_store_forget_impl;
__attribute__((unused))
static mesh_store_count_services_fn _mesh_store_mesh_store_count_services_check = mesh_store_count_services_impl;
__attribute__((unused))
static mesh_store_spawn_yaafc_fn _mesh_store_mesh_store_spawn_yaafc_check = mesh_store_spawn_yaafc_impl;
__attribute__((unused))
static mesh_store_kill_pid_fn _mesh_store_mesh_store_kill_pid_check = mesh_store_kill_pid_impl;
__attribute__((unused))
static mesh_store_count_children_fn _mesh_store_mesh_store_count_children_check = mesh_store_count_children_impl;
__attribute__((unused))
static mesh_store_reconcile_from_config_fn _mesh_store_mesh_store_reconcile_from_config_check = mesh_store_reconcile_from_config_impl;
__attribute__((unused))
static mesh_store_reconcile_fn _mesh_store_mesh_store_reconcile_check = mesh_store_reconcile_impl;

struct class_ptr_result mesh_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=mesh_store");

    static const struct class_descriptor desc = {
        .name = "mesh_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct mesh_store_data),
    };
    static const struct op ops[] = {
        {"mesh", "store_register_service", (method_id_t)mesh_store_register_service, (impl_t)mesh_store_register_service_impl},
        {"mesh", "store_resolve", (method_id_t)mesh_store_resolve, (impl_t)mesh_store_resolve_impl},
        {"mesh", "store_forget", (method_id_t)mesh_store_forget, (impl_t)mesh_store_forget_impl},
        {"mesh", "store_count_services", (method_id_t)mesh_store_count_services, (impl_t)mesh_store_count_services_impl},
        {"mesh", "store_spawn_yaafc", (method_id_t)mesh_store_spawn_yaafc, (impl_t)mesh_store_spawn_yaafc_impl},
        {"mesh", "store_kill_pid", (method_id_t)mesh_store_kill_pid, (impl_t)mesh_store_kill_pid_impl},
        {"mesh", "store_count_children", (method_id_t)mesh_store_count_children, (impl_t)mesh_store_count_children_impl},
        {"mesh", "store_reconcile_from_config", (method_id_t)mesh_store_reconcile_from_config, (impl_t)mesh_store_reconcile_from_config_impl},
        {"mesh", "store_reconcile", (method_id_t)mesh_store_reconcile, (impl_t)mesh_store_reconcile_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "mesh_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
