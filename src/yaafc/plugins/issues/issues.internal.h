/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `issues`.
 * NEVER include this from outside src/yaafc/plugins/issues/. */
#ifndef YAAFC_ISSUES_INTERNAL_H
#define YAAFC_ISSUES_INTERNAL_H

#include <yaafc/plugin/issues/issues.h>

typedef struct yaafc_uint32_result (*issues_store_open_fn)(struct ctx *, struct object *, uint32_t, uint32_t);
typedef struct yaafc_int_result (*issues_store_close_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*issues_store_status_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*issues_store_count_open_in_repo_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*issues_store_count_total_fn)(struct ctx *, struct object *);

#endif
