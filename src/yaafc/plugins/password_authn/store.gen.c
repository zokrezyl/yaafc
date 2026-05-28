/* GENERATED — do not edit. */
#include "password_authn.internal.h"

__attribute__((unused))
static password_authn_store_register_fn _password_authn_store_password_authn_store_register_check = password_authn_store_register_impl;
__attribute__((unused))
static password_authn_store_authenticate_fn _password_authn_store_password_authn_store_authenticate_check = password_authn_store_authenticate_impl;
__attribute__((unused))
static password_authn_store_change_password_fn _password_authn_store_password_authn_store_change_password_check = password_authn_store_change_password_impl;
__attribute__((unused))
static password_authn_store_count_registered_fn _password_authn_store_password_authn_store_count_registered_check = password_authn_store_count_registered_impl;

struct class_ptr_result password_authn_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=password_authn_store");

    static const struct class_descriptor desc = {
        .name = "password_authn_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct password_authn_store_data),
    };
    static const struct op ops[] = {
        {"password_authn", "store_register", (method_id_t)password_authn_store_register, (impl_t)password_authn_store_register_impl},
        {"password_authn", "store_authenticate", (method_id_t)password_authn_store_authenticate, (impl_t)password_authn_store_authenticate_impl},
        {"password_authn", "store_change_password", (method_id_t)password_authn_store_change_password, (impl_t)password_authn_store_change_password_impl},
        {"password_authn", "store_count_registered", (method_id_t)password_authn_store_count_registered, (impl_t)password_authn_store_count_registered_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "password_authn_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
