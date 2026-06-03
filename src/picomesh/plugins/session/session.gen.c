/* GENERATED — do not edit. */
#include "session.internal.h"

__attribute__((unused))
static session_session_start_fn _session_session_session_session_start_check = session_session_start_impl;
__attribute__((unused))
static session_session_jwt_fn _session_session_session_session_jwt_check = session_session_jwt_impl;
__attribute__((unused))
static session_session_lookup_fn _session_session_session_session_lookup_check = session_session_lookup_impl;
__attribute__((unused))
static session_session_destroy_fn _session_session_session_session_destroy_check = session_session_destroy_impl;
__attribute__((unused))
static session_session_count_active_fn _session_session_session_session_count_active_check = session_session_count_active_impl;
__attribute__((unused))
static session_session_list_fn _session_session_session_session_list_check = session_session_list_impl;
__attribute__((unused))
static session_session_list_all_fn _session_session_session_session_list_all_check = session_session_list_all_impl;

struct class_ptr_result session_session_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return PICOMESH_OK(class_ptr, cls);
    ydebug("registering class=session_session");

    static const struct class_descriptor desc = {
        .name = "session_session",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct session_session_data),
    };
    static const struct op ops[] = {
        {"session", "session_start", (method_id_t)session_session_start, (impl_t)session_session_start_impl},
        {"session", "session_jwt", (method_id_t)session_session_jwt, (impl_t)session_session_jwt_impl},
        {"session", "session_lookup", (method_id_t)session_session_lookup, (impl_t)session_session_lookup_impl},
        {"session", "session_destroy", (method_id_t)session_session_destroy, (impl_t)session_session_destroy_impl},
        {"session", "session_count_active", (method_id_t)session_session_count_active, (impl_t)session_session_count_active_impl},
        {"session", "session_list", (method_id_t)session_session_list, (impl_t)session_session_list_impl},
        {"session", "session_list_all", (method_id_t)session_session_list_all, (impl_t)session_session_list_all_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (PICOMESH_IS_ERR(_r))
        return PICOMESH_ERR(class_ptr, "session_session_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
