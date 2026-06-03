/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `git_pipeline`.
 * NEVER include this from outside src/picomesh/plugins/git_pipeline/. */
#ifndef PICOMESH_GIT_PIPELINE_INTERNAL_H
#define PICOMESH_GIT_PIPELINE_INTERNAL_H

#include <picomesh/plugin/git_pipeline/git_pipeline.h>

typedef struct picomesh_uint32_result (*git_pipeline_git_pipeline_enqueue_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_uint32_result (*git_pipeline_git_pipeline_enqueue_job_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *, const char *, int64_t);
typedef struct picomesh_uint32_result (*git_pipeline_git_pipeline_lease_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_json_result (*git_pipeline_git_pipeline_lease_job_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *);
typedef struct picomesh_json_result (*git_pipeline_git_pipeline_job_descriptor_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_int64_result (*git_pipeline_git_pipeline_append_log_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, int64_t, const char *);
typedef struct picomesh_string_result (*git_pipeline_git_pipeline_read_log_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_int_result (*git_pipeline_git_pipeline_complete_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, int32_t);
typedef struct picomesh_int_result (*git_pipeline_git_pipeline_complete_job_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, int32_t, const char *);
typedef struct picomesh_size_result (*git_pipeline_git_pipeline_requeue_expired_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_size_result (*git_pipeline_git_pipeline_count_pending_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_size_result (*git_pipeline_git_pipeline_count_running_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_size_result (*git_pipeline_git_pipeline_count_done_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_json_result (*git_pipeline_git_pipeline_list_fn)(struct ctx *, struct object *, struct yheaders *, int64_t, int64_t);
typedef struct picomesh_json_result (*git_pipeline_git_pipeline_list_all_fn)(struct ctx *, struct object *, struct yheaders *);

#endif
