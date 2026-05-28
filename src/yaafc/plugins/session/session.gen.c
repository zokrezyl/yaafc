/* GENERATED — do not edit. */
#include "session.internal.h"

__attribute__((unused))
static session_store_start_fn _session_store_session_store_start_check = session_store_start_impl;
__attribute__((unused))
static session_store_lookup_fn _session_store_session_store_lookup_check = session_store_lookup_impl;
__attribute__((unused))
static session_store_destroy_fn _session_store_session_store_destroy_check = session_store_destroy_impl;
__attribute__((unused))
static session_store_count_active_fn _session_store_session_store_count_active_check = session_store_count_active_impl;

struct class_ptr_result session_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=session_store");

    static const struct class_descriptor desc = {
        .name = "session_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct session_store_data),
    };
    static const struct op ops[] = {
        {"session", "store_start", (method_id_t)session_store_start, (impl_t)session_store_start_impl},
        {"session", "store_lookup", (method_id_t)session_store_lookup, (impl_t)session_store_lookup_impl},
        {"session", "store_destroy", (method_id_t)session_store_destroy, (impl_t)session_store_destroy_impl},
        {"session", "store_count_active", (method_id_t)session_store_count_active, (impl_t)session_store_count_active_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "session_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
