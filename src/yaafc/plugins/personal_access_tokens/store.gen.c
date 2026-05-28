/* GENERATED — do not edit. */
#include "personal_access_tokens.internal.h"

__attribute__((unused))
static personal_access_tokens_store_mint_fn _personal_access_tokens_store_personal_access_tokens_store_mint_check = personal_access_tokens_store_mint_impl;
__attribute__((unused))
static personal_access_tokens_store_lookup_fn _personal_access_tokens_store_personal_access_tokens_store_lookup_check = personal_access_tokens_store_lookup_impl;
__attribute__((unused))
static personal_access_tokens_store_revoke_fn _personal_access_tokens_store_personal_access_tokens_store_revoke_check = personal_access_tokens_store_revoke_impl;
__attribute__((unused))
static personal_access_tokens_store_list_for_user_fn _personal_access_tokens_store_personal_access_tokens_store_list_for_user_check = personal_access_tokens_store_list_for_user_impl;
__attribute__((unused))
static personal_access_tokens_store_count_active_fn _personal_access_tokens_store_personal_access_tokens_store_count_active_check = personal_access_tokens_store_count_active_impl;

struct class_ptr_result personal_access_tokens_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=personal_access_tokens_store");

    static const struct class_descriptor desc = {
        .name = "personal_access_tokens_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct personal_access_tokens_store_data),
    };
    static const struct op ops[] = {
        {"personal_access_tokens", "store_mint", (method_id_t)personal_access_tokens_store_mint, (impl_t)personal_access_tokens_store_mint_impl},
        {"personal_access_tokens", "store_lookup", (method_id_t)personal_access_tokens_store_lookup, (impl_t)personal_access_tokens_store_lookup_impl},
        {"personal_access_tokens", "store_revoke", (method_id_t)personal_access_tokens_store_revoke, (impl_t)personal_access_tokens_store_revoke_impl},
        {"personal_access_tokens", "store_list_for_user", (method_id_t)personal_access_tokens_store_list_for_user, (impl_t)personal_access_tokens_store_list_for_user_impl},
        {"personal_access_tokens", "store_count_active", (method_id_t)personal_access_tokens_store_count_active, (impl_t)personal_access_tokens_store_count_active_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "personal_access_tokens_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
