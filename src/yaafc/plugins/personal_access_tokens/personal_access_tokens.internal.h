/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `personal_access_tokens`.
 * NEVER include this from outside src/yaafc/plugins/personal_access_tokens/. */
#ifndef YAAFC_PERSONAL_ACCESS_TOKENS_INTERNAL_H
#define YAAFC_PERSONAL_ACCESS_TOKENS_INTERNAL_H

#include <yaafc/plugin/personal_access_tokens/personal_access_tokens.h>

typedef struct yaafc_uint32_result (*personal_access_tokens_store_mint_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_uint32_result (*personal_access_tokens_store_lookup_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*personal_access_tokens_store_revoke_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*personal_access_tokens_store_list_for_user_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*personal_access_tokens_store_count_active_fn)(struct ctx *, struct object *);

#endif
