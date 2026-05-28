/* GENERATED — do not edit. */
#include "portalloc.internal.h"

__attribute__((unused))
static portalloc_store_allocate_fn _portalloc_store_portalloc_store_allocate_check = portalloc_store_allocate_impl;
__attribute__((unused))
static portalloc_store_release_fn _portalloc_store_portalloc_store_release_check = portalloc_store_release_impl;
__attribute__((unused))
static portalloc_store_count_used_fn _portalloc_store_portalloc_store_count_used_check = portalloc_store_count_used_impl;

struct class_ptr_result portalloc_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=portalloc_store");

    static const struct class_descriptor desc = {
        .name = "portalloc_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct portalloc_store_data),
    };
    static const struct op ops[] = {
        {"portalloc", "store_allocate", (method_id_t)portalloc_store_allocate, (impl_t)portalloc_store_allocate_impl},
        {"portalloc", "store_release", (method_id_t)portalloc_store_release, (impl_t)portalloc_store_release_impl},
        {"portalloc", "store_count_used", (method_id_t)portalloc_store_count_used, (impl_t)portalloc_store_count_used_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "portalloc_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
