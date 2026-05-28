/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `token_issuer`.
 * NEVER include this from outside src/yaafc/plugins/token_issuer/. */
#ifndef YAAFC_TOKEN_ISSUER_INTERNAL_H
#define YAAFC_TOKEN_ISSUER_INTERNAL_H

#include <yaafc/plugin/token_issuer/token_issuer.h>

typedef struct yaafc_uint32_result (*token_issuer_store_login_fn)(struct ctx *, struct object *, uint32_t, uint32_t);
typedef struct yaafc_uint32_result (*token_issuer_store_validate_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_uint32_result (*token_issuer_store_refresh_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*token_issuer_store_revoke_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*token_issuer_store_count_active_fn)(struct ctx *, struct object *);

#endif
