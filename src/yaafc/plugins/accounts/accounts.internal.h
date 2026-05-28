/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `accounts`.
 * NEVER include this from outside src/yaafc/plugins/accounts/. */
#ifndef YAAFC_ACCOUNTS_INTERNAL_H
#define YAAFC_ACCOUNTS_INTERNAL_H

#include <yaafc/plugin/accounts/accounts.h>

typedef struct yaafc_int_result (*accounts_store_register_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*accounts_store_exists_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*accounts_store_set_balance_fn)(struct ctx *, struct object *, uint32_t, int64_t);
typedef struct yaafc_int64_result (*accounts_store_balance_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*accounts_store_count_fn)(struct ctx *, struct object *);

#endif
