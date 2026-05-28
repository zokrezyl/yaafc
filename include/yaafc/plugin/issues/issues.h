/* GENERATED — do not edit. */
/* Public interface for plugin `issues` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/issues/. */
#ifndef YAAFC_PLUGIN_ISSUES_H
#define YAAFC_PLUGIN_ISSUES_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result issues_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result issues_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_uint32_result issues_store_open(struct ctx * ctx, struct object * obj, uint32_t repo_id, uint32_t author_id);
struct yaafc_int_result issues_store_close(struct ctx * ctx, struct object * obj, uint32_t issue_id);
struct yaafc_int_result issues_store_status(struct ctx * ctx, struct object * obj, uint32_t issue_id);
struct yaafc_size_result issues_store_count_open_in_repo(struct ctx * ctx, struct object * obj, uint32_t repo_id);
struct yaafc_size_result issues_store_count_total(struct ctx * ctx, struct object * obj);

#endif
