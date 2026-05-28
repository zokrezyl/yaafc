/* GENERATED — do not edit. */
/* Public interface for plugin `session` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/session/. */
#ifndef YAAFC_PLUGIN_SESSION_H
#define YAAFC_PLUGIN_SESSION_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result session_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result session_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_uint32_result session_store_start(struct ctx * ctx, struct object * obj, uint32_t user_id, uint32_t provider_id);
struct yaafc_uint32_result session_store_lookup(struct ctx * ctx, struct object * obj, uint32_t sid);
struct yaafc_int_result session_store_destroy(struct ctx * ctx, struct object * obj, uint32_t sid);
struct yaafc_size_result session_store_count_active(struct ctx * ctx, struct object * obj);

#endif
