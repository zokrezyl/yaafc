/* GENERATED — do not edit. */
#include "personal_access_tokens.internal.h"

__attribute__((unused))
static personal_access_tokens_personal_access_tokens_mint_fn _personal_access_tokens_personal_access_tokens_personal_access_tokens_personal_access_tokens_mint_check = personal_access_tokens_personal_access_tokens_mint_impl;
__attribute__((unused))
static personal_access_tokens_personal_access_tokens_lookup_fn _personal_access_tokens_personal_access_tokens_personal_access_tokens_personal_access_tokens_lookup_check = personal_access_tokens_personal_access_tokens_lookup_impl;
__attribute__((unused))
static personal_access_tokens_personal_access_tokens_revoke_fn _personal_access_tokens_personal_access_tokens_personal_access_tokens_personal_access_tokens_revoke_check = personal_access_tokens_personal_access_tokens_revoke_impl;
__attribute__((unused))
static personal_access_tokens_personal_access_tokens_list_for_user_fn _personal_access_tokens_personal_access_tokens_personal_access_tokens_personal_access_tokens_list_for_user_check = personal_access_tokens_personal_access_tokens_list_for_user_impl;
__attribute__((unused))
static personal_access_tokens_personal_access_tokens_count_active_fn _personal_access_tokens_personal_access_tokens_personal_access_tokens_personal_access_tokens_count_active_check = personal_access_tokens_personal_access_tokens_count_active_impl;
__attribute__((unused))
static personal_access_tokens_personal_access_tokens_list_fn _personal_access_tokens_personal_access_tokens_personal_access_tokens_personal_access_tokens_list_check = personal_access_tokens_personal_access_tokens_list_impl;
__attribute__((unused))
static personal_access_tokens_personal_access_tokens_list_all_fn _personal_access_tokens_personal_access_tokens_personal_access_tokens_personal_access_tokens_list_all_check = personal_access_tokens_personal_access_tokens_list_all_impl;

struct class_ptr_result personal_access_tokens_personal_access_tokens_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return PICOMESH_OK(class_ptr, cls);
    ydebug("registering class=personal_access_tokens_personal_access_tokens");

    static const struct class_descriptor desc = {
        .name = "personal_access_tokens_personal_access_tokens",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct personal_access_tokens_personal_access_tokens_data),
    };
    static const struct op ops[] = {
        {"personal_access_tokens", "personal_access_tokens_mint", (method_id_t)personal_access_tokens_personal_access_tokens_mint, (impl_t)personal_access_tokens_personal_access_tokens_mint_impl},
        {"personal_access_tokens", "personal_access_tokens_lookup", (method_id_t)personal_access_tokens_personal_access_tokens_lookup, (impl_t)personal_access_tokens_personal_access_tokens_lookup_impl},
        {"personal_access_tokens", "personal_access_tokens_revoke", (method_id_t)personal_access_tokens_personal_access_tokens_revoke, (impl_t)personal_access_tokens_personal_access_tokens_revoke_impl},
        {"personal_access_tokens", "personal_access_tokens_list_for_user", (method_id_t)personal_access_tokens_personal_access_tokens_list_for_user, (impl_t)personal_access_tokens_personal_access_tokens_list_for_user_impl},
        {"personal_access_tokens", "personal_access_tokens_count_active", (method_id_t)personal_access_tokens_personal_access_tokens_count_active, (impl_t)personal_access_tokens_personal_access_tokens_count_active_impl},
        {"personal_access_tokens", "personal_access_tokens_list", (method_id_t)personal_access_tokens_personal_access_tokens_list, (impl_t)personal_access_tokens_personal_access_tokens_list_impl},
        {"personal_access_tokens", "personal_access_tokens_list_all", (method_id_t)personal_access_tokens_personal_access_tokens_list_all, (impl_t)personal_access_tokens_personal_access_tokens_list_all_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (PICOMESH_IS_ERR(_r))
        return PICOMESH_ERR(class_ptr, "personal_access_tokens_personal_access_tokens_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
