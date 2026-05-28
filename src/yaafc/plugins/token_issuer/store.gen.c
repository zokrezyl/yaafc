/* GENERATED — do not edit. */
#include "token_issuer.internal.h"

__attribute__((unused))
static token_issuer_store_login_fn _token_issuer_store_token_issuer_store_login_check = token_issuer_store_login_impl;
__attribute__((unused))
static token_issuer_store_validate_fn _token_issuer_store_token_issuer_store_validate_check = token_issuer_store_validate_impl;
__attribute__((unused))
static token_issuer_store_refresh_fn _token_issuer_store_token_issuer_store_refresh_check = token_issuer_store_refresh_impl;
__attribute__((unused))
static token_issuer_store_revoke_fn _token_issuer_store_token_issuer_store_revoke_check = token_issuer_store_revoke_impl;
__attribute__((unused))
static token_issuer_store_count_active_fn _token_issuer_store_token_issuer_store_count_active_check = token_issuer_store_count_active_impl;

struct class_ptr_result token_issuer_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=token_issuer_store");

    static const struct class_descriptor desc = {
        .name = "token_issuer_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct token_issuer_store_data),
    };
    static const struct op ops[] = {
        {"token_issuer", "store_login", (method_id_t)token_issuer_store_login, (impl_t)token_issuer_store_login_impl},
        {"token_issuer", "store_validate", (method_id_t)token_issuer_store_validate, (impl_t)token_issuer_store_validate_impl},
        {"token_issuer", "store_refresh", (method_id_t)token_issuer_store_refresh, (impl_t)token_issuer_store_refresh_impl},
        {"token_issuer", "store_revoke", (method_id_t)token_issuer_store_revoke, (impl_t)token_issuer_store_revoke_impl},
        {"token_issuer", "store_count_active", (method_id_t)token_issuer_store_count_active, (impl_t)token_issuer_store_count_active_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "token_issuer_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
