/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `github_authn`.
 * NEVER include this from outside src/yaafc/plugins/github_authn/. */
#ifndef YAAFC_GITHUB_AUTHN_INTERNAL_H
#define YAAFC_GITHUB_AUTHN_INTERNAL_H

#include <yaafc/plugin/github_authn/github_authn.h>

typedef struct yaafc_int_result (*github_authn_store_set_credentials_fn)(struct ctx *, struct object *, uint32_t, uint32_t);
typedef struct yaafc_int_result (*github_authn_store_register_code_fn)(struct ctx *, struct object *, uint32_t, uint32_t);
typedef struct yaafc_uint32_result (*github_authn_store_resolve_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*github_authn_store_count_codes_fn)(struct ctx *, struct object *);

#endif
