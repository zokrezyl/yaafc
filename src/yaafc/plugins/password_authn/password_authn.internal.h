/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `password_authn`.
 * NEVER include this from outside src/yaafc/plugins/password_authn/. */
#ifndef YAAFC_PASSWORD_AUTHN_INTERNAL_H
#define YAAFC_PASSWORD_AUTHN_INTERNAL_H

#include <yaafc/plugin/password_authn/password_authn.h>

typedef struct yaafc_int_result (*password_authn_store_register_fn)(struct ctx *, struct object *, uint32_t, int64_t);
typedef struct yaafc_int_result (*password_authn_store_authenticate_fn)(struct ctx *, struct object *, uint32_t, int64_t);
typedef struct yaafc_int_result (*password_authn_store_change_password_fn)(struct ctx *, struct object *, uint32_t, int64_t);
typedef struct yaafc_size_result (*password_authn_store_count_registered_fn)(struct ctx *, struct object *);

#endif
