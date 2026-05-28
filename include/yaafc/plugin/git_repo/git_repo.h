/* GENERATED — do not edit. */
/* Public interface for plugin `git_repo` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/git_repo/. */
#ifndef YAAFC_PLUGIN_GIT_REPO_H
#define YAAFC_PLUGIN_GIT_REPO_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result git_repo_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result git_repo_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_uint32_result git_repo_store_make(struct ctx * ctx, struct object * obj, uint32_t owner_id);
struct yaafc_int_result git_repo_store_delete(struct ctx * ctx, struct object * obj, uint32_t repo_id);
struct yaafc_uint32_result git_repo_store_owner_of(struct ctx * ctx, struct object * obj, uint32_t repo_id);
struct yaafc_size_result git_repo_store_count_for_owner(struct ctx * ctx, struct object * obj, uint32_t owner_id);
struct yaafc_size_result git_repo_store_count_total(struct ctx * ctx, struct object * obj);

#endif
