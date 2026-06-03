/* GENERATED — do not edit. */
#include "accounts.internal.h"

__attribute__((unused))
static accounts_accounts_register_fn _accounts_accounts_accounts_accounts_register_check = accounts_accounts_register_impl;
__attribute__((unused))
static accounts_accounts_exists_fn _accounts_accounts_accounts_accounts_exists_check = accounts_accounts_exists_impl;
__attribute__((unused))
static accounts_accounts_set_balance_fn _accounts_accounts_accounts_accounts_set_balance_check = accounts_accounts_set_balance_impl;
__attribute__((unused))
static accounts_accounts_balance_fn _accounts_accounts_accounts_accounts_balance_check = accounts_accounts_balance_impl;
__attribute__((unused))
static accounts_accounts_count_fn _accounts_accounts_accounts_accounts_count_check = accounts_accounts_count_impl;
__attribute__((unused))
static accounts_accounts_set_groups_fn _accounts_accounts_accounts_accounts_set_groups_check = accounts_accounts_set_groups_impl;
__attribute__((unused))
static accounts_accounts_groups_fn _accounts_accounts_accounts_accounts_groups_check = accounts_accounts_groups_impl;
__attribute__((unused))
static accounts_accounts_list_fn _accounts_accounts_accounts_accounts_list_check = accounts_accounts_list_impl;
__attribute__((unused))
static accounts_accounts_list_all_fn _accounts_accounts_accounts_accounts_list_all_check = accounts_accounts_list_all_impl;

struct class_ptr_result accounts_accounts_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return PICOMESH_OK(class_ptr, cls);
    ydebug("registering class=accounts_accounts");

    static const struct class_descriptor desc = {
        .name = "accounts_accounts",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct accounts_accounts_data),
    };
    static const struct op ops[] = {
        {"accounts", "accounts_register", (method_id_t)accounts_accounts_register, (impl_t)accounts_accounts_register_impl},
        {"accounts", "accounts_exists", (method_id_t)accounts_accounts_exists, (impl_t)accounts_accounts_exists_impl},
        {"accounts", "accounts_set_balance", (method_id_t)accounts_accounts_set_balance, (impl_t)accounts_accounts_set_balance_impl},
        {"accounts", "accounts_balance", (method_id_t)accounts_accounts_balance, (impl_t)accounts_accounts_balance_impl},
        {"accounts", "accounts_count", (method_id_t)accounts_accounts_count, (impl_t)accounts_accounts_count_impl},
        {"accounts", "accounts_set_groups", (method_id_t)accounts_accounts_set_groups, (impl_t)accounts_accounts_set_groups_impl},
        {"accounts", "accounts_groups", (method_id_t)accounts_accounts_groups, (impl_t)accounts_accounts_groups_impl},
        {"accounts", "accounts_list", (method_id_t)accounts_accounts_list, (impl_t)accounts_accounts_list_impl},
        {"accounts", "accounts_list_all", (method_id_t)accounts_accounts_list_all, (impl_t)accounts_accounts_list_all_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (PICOMESH_IS_ERR(_r))
        return PICOMESH_ERR(class_ptr, "accounts_accounts_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
