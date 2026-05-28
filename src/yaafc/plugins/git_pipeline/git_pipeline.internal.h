/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `git_pipeline`.
 * NEVER include this from outside src/yaafc/plugins/git_pipeline/. */
#ifndef YAAFC_GIT_PIPELINE_INTERNAL_H
#define YAAFC_GIT_PIPELINE_INTERNAL_H

#include <yaafc/plugin/git_pipeline/git_pipeline.h>

typedef struct yaafc_uint32_result (*git_pipeline_store_enqueue_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_uint32_result (*git_pipeline_store_lease_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*git_pipeline_store_complete_fn)(struct ctx *, struct object *, uint32_t, int32_t);
typedef struct yaafc_size_result (*git_pipeline_store_count_pending_fn)(struct ctx *, struct object *);
typedef struct yaafc_size_result (*git_pipeline_store_count_running_fn)(struct ctx *, struct object *);
typedef struct yaafc_size_result (*git_pipeline_store_count_done_fn)(struct ctx *, struct object *);

#endif
