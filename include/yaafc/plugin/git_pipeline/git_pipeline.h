/* GENERATED — do not edit. */
/* Public interface for plugin `git_pipeline` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/git_pipeline/. */
#ifndef YAAFC_PLUGIN_GIT_PIPELINE_H
#define YAAFC_PLUGIN_GIT_PIPELINE_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result git_pipeline_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result git_pipeline_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_uint32_result git_pipeline_store_enqueue(struct ctx * ctx, struct object * obj, uint32_t repo_id);
struct yaafc_uint32_result git_pipeline_store_lease(struct ctx * ctx, struct object * obj, uint32_t runner_id);
struct yaafc_int_result git_pipeline_store_complete(struct ctx * ctx, struct object * obj, uint32_t job_id, int32_t status);
struct yaafc_size_result git_pipeline_store_count_pending(struct ctx * ctx, struct object * obj);
struct yaafc_size_result git_pipeline_store_count_running(struct ctx * ctx, struct object * obj);
struct yaafc_size_result git_pipeline_store_count_done(struct ctx * ctx, struct object * obj);

#endif
