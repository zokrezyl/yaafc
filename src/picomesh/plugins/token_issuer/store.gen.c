/* GENERATED — do not edit. */
#include "token_issuer.internal.h"

__attribute__((unused))
static token_issuer_token_issuer_login_fn _token_issuer_token_issuer_token_issuer_token_issuer_login_check = token_issuer_token_issuer_login_impl;
__attribute__((unused))
static token_issuer_token_issuer_refresh_fn _token_issuer_token_issuer_token_issuer_token_issuer_refresh_check = token_issuer_token_issuer_refresh_impl;
__attribute__((unused))
static token_issuer_token_issuer_mint_fn _token_issuer_token_issuer_token_issuer_token_issuer_mint_check = token_issuer_token_issuer_mint_impl;
__attribute__((unused))
static token_issuer_token_issuer_count_active_fn _token_issuer_token_issuer_token_issuer_token_issuer_count_active_check = token_issuer_token_issuer_count_active_impl;
__attribute__((unused))
static token_issuer_token_issuer_list_fn _token_issuer_token_issuer_token_issuer_token_issuer_list_check = token_issuer_token_issuer_list_impl;
__attribute__((unused))
static token_issuer_token_issuer_list_all_fn _token_issuer_token_issuer_token_issuer_token_issuer_list_all_check = token_issuer_token_issuer_list_all_impl;

struct class_ptr_result token_issuer_token_issuer_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return PICOMESH_OK(class_ptr, cls);
    ydebug("registering class=token_issuer_token_issuer");

    static const struct class_descriptor desc = {
        .name = "token_issuer_token_issuer",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct token_issuer_token_issuer_data),
    };
    static const struct op ops[] = {
        {"token_issuer", "token_issuer_login", (method_id_t)token_issuer_token_issuer_login, (impl_t)token_issuer_token_issuer_login_impl},
        {"token_issuer", "token_issuer_refresh", (method_id_t)token_issuer_token_issuer_refresh, (impl_t)token_issuer_token_issuer_refresh_impl},
        {"token_issuer", "token_issuer_mint", (method_id_t)token_issuer_token_issuer_mint, (impl_t)token_issuer_token_issuer_mint_impl},
        {"token_issuer", "token_issuer_count_active", (method_id_t)token_issuer_token_issuer_count_active, (impl_t)token_issuer_token_issuer_count_active_impl},
        {"token_issuer", "token_issuer_list", (method_id_t)token_issuer_token_issuer_list, (impl_t)token_issuer_token_issuer_list_impl},
        {"token_issuer", "token_issuer_list_all", (method_id_t)token_issuer_token_issuer_list_all, (impl_t)token_issuer_token_issuer_list_all_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (PICOMESH_IS_ERR(_r))
        return PICOMESH_ERR(class_ptr, "token_issuer_token_issuer_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
