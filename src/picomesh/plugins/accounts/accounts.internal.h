/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `accounts`.
 * NEVER include this from outside src/picomesh/plugins/accounts/. */
#ifndef PICOMESH_ACCOUNTS_INTERNAL_H
#define PICOMESH_ACCOUNTS_INTERNAL_H

#include <picomesh/plugin/accounts/accounts.h>

typedef struct picomesh_int_result (*accounts_accounts_claim_username_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *);
typedef struct picomesh_int_result (*accounts_accounts_release_username_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *);
typedef struct picomesh_int64_result (*accounts_accounts_allocate_uid_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_int64_result (*accounts_accounts_uid_for_username_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_int_result (*accounts_accounts_register_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *);
typedef struct picomesh_int_result (*accounts_accounts_exists_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_int_result (*accounts_accounts_set_balance_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, int64_t);
typedef struct picomesh_int64_result (*accounts_accounts_balance_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_size_result (*accounts_accounts_count_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_int_result (*accounts_accounts_set_groups_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *);
typedef struct picomesh_string_result (*accounts_accounts_groups_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_string_result (*accounts_accounts_ns_create_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *, const char *, const char *);
typedef struct picomesh_int_result (*accounts_accounts_ns_add_member_fn)(struct ctx *, struct object *, struct yheaders *, const char *, uint32_t, const char *);
typedef struct picomesh_int64_result (*accounts_accounts_ns_resolve_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_json_result (*accounts_accounts_ns_list_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_json_result (*accounts_accounts_ns_members_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_int_result (*accounts_accounts_ns_remove_member_fn)(struct ctx *, struct object *, struct yheaders *, const char *, uint32_t);
typedef struct picomesh_json_result (*accounts_accounts_ns_subtree_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_int_result (*accounts_accounts_ns_delete_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_json_result (*accounts_accounts_list_fn)(struct ctx *, struct object *, struct yheaders *, int64_t, int64_t);
typedef struct picomesh_json_result (*accounts_accounts_list_all_fn)(struct ctx *, struct object *, struct yheaders *);

#endif
