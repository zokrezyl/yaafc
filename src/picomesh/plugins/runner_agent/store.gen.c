/* GENERATED — do not edit. */
#include "runner_agent.internal.h"

__attribute__((unused))
static runner_agent_runner_agent_create_token_fn _runner_agent_runner_agent_runner_agent_runner_agent_create_token_check = runner_agent_runner_agent_create_token_impl;
__attribute__((unused))
static runner_agent_runner_agent_lookup_token_fn _runner_agent_runner_agent_runner_agent_runner_agent_lookup_token_check = runner_agent_runner_agent_lookup_token_impl;
__attribute__((unused))
static runner_agent_runner_agent_revoke_token_fn _runner_agent_runner_agent_runner_agent_runner_agent_revoke_token_check = runner_agent_runner_agent_revoke_token_impl;
__attribute__((unused))
static runner_agent_runner_agent_register_fn _runner_agent_runner_agent_runner_agent_runner_agent_register_check = runner_agent_runner_agent_register_impl;
__attribute__((unused))
static runner_agent_runner_agent_heartbeat_fn _runner_agent_runner_agent_runner_agent_runner_agent_heartbeat_check = runner_agent_runner_agent_heartbeat_impl;
__attribute__((unused))
static runner_agent_runner_agent_get_fn _runner_agent_runner_agent_runner_agent_runner_agent_get_check = runner_agent_runner_agent_get_impl;
__attribute__((unused))
static runner_agent_runner_agent_list_fn _runner_agent_runner_agent_runner_agent_runner_agent_list_check = runner_agent_runner_agent_list_impl;
__attribute__((unused))
static runner_agent_runner_agent_list_all_fn _runner_agent_runner_agent_runner_agent_runner_agent_list_all_check = runner_agent_runner_agent_list_all_impl;
__attribute__((unused))
static runner_agent_runner_agent_count_active_fn _runner_agent_runner_agent_runner_agent_runner_agent_count_active_check = runner_agent_runner_agent_count_active_impl;

struct class_ptr_result runner_agent_runner_agent_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return PICOMESH_OK(class_ptr, cls);
    ydebug("registering class=runner_agent_runner_agent");

    static const struct class_descriptor desc = {
        .name = "runner_agent_runner_agent",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct runner_agent_runner_agent_data),
    };
    static const struct op ops[] = {
        {"runner_agent", "runner_agent_create_token", (method_id_t)runner_agent_runner_agent_create_token, (impl_t)runner_agent_runner_agent_create_token_impl},
        {"runner_agent", "runner_agent_lookup_token", (method_id_t)runner_agent_runner_agent_lookup_token, (impl_t)runner_agent_runner_agent_lookup_token_impl},
        {"runner_agent", "runner_agent_revoke_token", (method_id_t)runner_agent_runner_agent_revoke_token, (impl_t)runner_agent_runner_agent_revoke_token_impl},
        {"runner_agent", "runner_agent_register", (method_id_t)runner_agent_runner_agent_register, (impl_t)runner_agent_runner_agent_register_impl},
        {"runner_agent", "runner_agent_heartbeat", (method_id_t)runner_agent_runner_agent_heartbeat, (impl_t)runner_agent_runner_agent_heartbeat_impl},
        {"runner_agent", "runner_agent_get", (method_id_t)runner_agent_runner_agent_get, (impl_t)runner_agent_runner_agent_get_impl},
        {"runner_agent", "runner_agent_list", (method_id_t)runner_agent_runner_agent_list, (impl_t)runner_agent_runner_agent_list_impl},
        {"runner_agent", "runner_agent_list_all", (method_id_t)runner_agent_runner_agent_list_all, (impl_t)runner_agent_runner_agent_list_all_impl},
        {"runner_agent", "runner_agent_count_active", (method_id_t)runner_agent_runner_agent_count_active, (impl_t)runner_agent_runner_agent_count_active_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (PICOMESH_IS_ERR(_r))
        return PICOMESH_ERR(class_ptr, "runner_agent_runner_agent_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
