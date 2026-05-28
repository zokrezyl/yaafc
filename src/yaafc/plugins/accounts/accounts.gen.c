/* GENERATED — do not edit. */
#include "accounts.internal.h"

__attribute__((unused))
static accounts_store_register_fn _accounts_store_accounts_store_register_check = accounts_store_register_impl;
__attribute__((unused))
static accounts_store_exists_fn _accounts_store_accounts_store_exists_check = accounts_store_exists_impl;
__attribute__((unused))
static accounts_store_set_balance_fn _accounts_store_accounts_store_set_balance_check = accounts_store_set_balance_impl;
__attribute__((unused))
static accounts_store_balance_fn _accounts_store_accounts_store_balance_check = accounts_store_balance_impl;
__attribute__((unused))
static accounts_store_count_fn _accounts_store_accounts_store_count_check = accounts_store_count_impl;

struct class_ptr_result accounts_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=accounts_store");

    static const struct class_descriptor desc = {
        .name = "accounts_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct accounts_store_data),
    };
    static const struct op ops[] = {
        {"accounts", "store_register", (method_id_t)accounts_store_register, (impl_t)accounts_store_register_impl},
        {"accounts", "store_exists", (method_id_t)accounts_store_exists, (impl_t)accounts_store_exists_impl},
        {"accounts", "store_set_balance", (method_id_t)accounts_store_set_balance, (impl_t)accounts_store_set_balance_impl},
        {"accounts", "store_balance", (method_id_t)accounts_store_balance, (impl_t)accounts_store_balance_impl},
        {"accounts", "store_count", (method_id_t)accounts_store_count, (impl_t)accounts_store_count_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "accounts_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
