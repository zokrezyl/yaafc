/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `git_repo`.
 * NEVER include this from outside src/yaafc/plugins/git_repo/. */
#ifndef YAAFC_GIT_REPO_INTERNAL_H
#define YAAFC_GIT_REPO_INTERNAL_H

#include <yaafc/plugin/git_repo/git_repo.h>

typedef struct yaafc_uint32_result (*git_repo_store_make_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*git_repo_store_delete_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_uint32_result (*git_repo_store_owner_of_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*git_repo_store_count_for_owner_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*git_repo_store_count_total_fn)(struct ctx *, struct object *);

#endif
