/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `token_issuer`.
 * NEVER include this from outside src/picomesh/plugins/token_issuer/. */
#ifndef PICOMESH_TOKEN_ISSUER_INTERNAL_H
#define PICOMESH_TOKEN_ISSUER_INTERNAL_H

#include <picomesh/plugin/token_issuer/token_issuer.h>

typedef struct picomesh_json_result (*token_issuer_token_issuer_login_fn)(struct ctx *, struct object *, struct yheaders *, const char *, uint32_t, const char *, int64_t);
typedef struct picomesh_json_result (*token_issuer_token_issuer_refresh_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_string_result (*token_issuer_token_issuer_mint_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *, const char *, int64_t);
typedef struct picomesh_size_result (*token_issuer_token_issuer_count_active_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_json_result (*token_issuer_token_issuer_list_fn)(struct ctx *, struct object *, struct yheaders *, int64_t, int64_t);
typedef struct picomesh_json_result (*token_issuer_token_issuer_list_all_fn)(struct ctx *, struct object *, struct yheaders *);

#endif
