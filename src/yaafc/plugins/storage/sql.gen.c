/* GENERATED — do not edit. */
#include "storage.internal.h"

__attribute__((unused))
static storage_sql_set_fn _storage_sql_storage_sql_set_check = storage_sql_set_impl;
__attribute__((unused))
static storage_sql_get_fn _storage_sql_storage_sql_get_check = storage_sql_get_impl;
__attribute__((unused))
static storage_sql_exists_fn _storage_sql_storage_sql_exists_check = storage_sql_exists_impl;
__attribute__((unused))
static storage_sql_del_fn _storage_sql_storage_sql_del_check = storage_sql_del_impl;
__attribute__((unused))
static storage_sql_count_fn _storage_sql_storage_sql_count_check = storage_sql_count_impl;

struct class_ptr_result storage_sql_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=storage_sql");

    static const struct class_descriptor desc = {
        .name = "storage_sql",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct storage_sql_data),
    };
    static const struct op ops[] = {
        {"storage", "sql_set", (method_id_t)storage_sql_set, (impl_t)storage_sql_set_impl},
        {"storage", "sql_get", (method_id_t)storage_sql_get, (impl_t)storage_sql_get_impl},
        {"storage", "sql_exists", (method_id_t)storage_sql_exists, (impl_t)storage_sql_exists_impl},
        {"storage", "sql_del", (method_id_t)storage_sql_del, (impl_t)storage_sql_del_impl},
        {"storage", "sql_count", (method_id_t)storage_sql_count, (impl_t)storage_sql_count_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "storage_sql_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
