/* GENERATED — do not edit. */
#include "storage.internal.h"

__attribute__((unused))
static storage_kv_set_fn _storage_kv_storage_kv_set_check = storage_kv_set_impl;
__attribute__((unused))
static storage_kv_get_fn _storage_kv_storage_kv_get_check = storage_kv_get_impl;
__attribute__((unused))
static storage_kv_count_fn _storage_kv_storage_kv_count_check = storage_kv_count_impl;

struct class_ptr_result storage_kv_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=storage_kv");

    static const struct class_descriptor desc = {
        .name = "storage_kv",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct storage_kv_data),
    };
    static const struct op ops[] = {
        {"storage", "kv_set", (method_id_t)storage_kv_set, (impl_t)storage_kv_set_impl},
        {"storage", "kv_get", (method_id_t)storage_kv_get, (impl_t)storage_kv_get_impl},
        {"storage", "kv_count", (method_id_t)storage_kv_count, (impl_t)storage_kv_count_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "storage_kv_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
